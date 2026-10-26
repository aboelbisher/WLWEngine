#include "wlw/audio/mixer.h"

#include <algorithm>
#include <cmath>

#include "miniaudio.h"

namespace wlw::audio {

std::optional<SoundData> Decode(const uint8_t* bytes, size_t size, uint32_t sample_rate) {
  // Ask the decoder for exactly what the mixer wants: float, stereo, the device's rate. miniaudio
  // converts channels and resamples on the way.
  ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, sample_rate);
  ma_decoder decoder;
  if (ma_decoder_init_memory(bytes, size, &config, &decoder) != MA_SUCCESS) return std::nullopt;
  SoundData data;
  float chunk[4096 * 2];
  for (;;) {
    ma_uint64 read = 0;
    if (ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &read) != MA_SUCCESS || read == 0) break;
    data.samples.insert(data.samples.end(), chunk, chunk + read * 2);
  }
  ma_decoder_uninit(&decoder);
  data.frames = static_cast<uint32_t>(data.samples.size() / 2);
  if (data.frames == 0) return std::nullopt;
  return data;
}

Mixer::Mixer(uint32_t sample_rate, int max_voices) : sample_rate_(sample_rate), voices_(std::max(max_voices, 1)) {
  for (auto& volume : bus_volume_) volume.store(1.0f);
  stream_scratch_.resize(kStreamChunk * 2);  // here, so the audio thread never allocates
}

Mixer::SoundId Mixer::AddSound(SoundData data) {
  sounds_.push_back(std::make_unique<SoundData>(std::move(data)));
  return static_cast<SoundId>(sounds_.size() - 1);
}

void Mixer::Play(SoundId sound, const PlayParams& params) {
  if (sound < 0 || sound >= static_cast<SoundId>(sounds_.size())) return;
  Command command;
  command.sound = sounds_[sound].get();  // the pointer stays valid: the SoundData never moves
  command.params = params;
  Push(command);
}

void Mixer::PlayStream(StreamSource* source, float fade_seconds, float volume) {
  Command command;
  command.kind = Command::Kind::PlayStream;
  command.stream = source;
  command.fade_seconds = fade_seconds;
  command.stream_volume = volume;
  if (source) source->queued_.fetch_add(1);  // in use from now on, before the audio thread can see it
  if (!Push(command) && source) source->queued_.fetch_sub(1);
}

void Mixer::StopAll() {
  Command command;
  command.kind = Command::Kind::StopAll;
  Push(command);
}

bool Mixer::Push(const Command& command) {
  const size_t head = head_.load(std::memory_order_relaxed);
  const size_t next = (head + 1) % kQueueSize;
  if (next == tail_.load(std::memory_order_acquire)) return false;  // full
  queue_[head] = command;
  head_.store(next, std::memory_order_release);  // publishes the command to the audio thread
  return true;
}

void Mixer::StartVoice(const Command& command) {
  // Pick a voice: a copy of the same sound if there are already too many of it, else a free
  // one, else steal the oldest voice of all.
  Voice* chosen = nullptr;
  int copies = 0;
  Voice* oldest_copy = nullptr;
  Voice* oldest = nullptr;
  for (Voice& voice : voices_) {
    if (!voice.sound) {
      if (!chosen) chosen = &voice;
      continue;
    }
    if (!oldest || voice.started < oldest->started) oldest = &voice;
    if (voice.sound == command.sound) {
      ++copies;
      if (!oldest_copy || voice.started < oldest_copy->started) oldest_copy = &voice;
    }
  }
  if (copies >= max_instances_per_sound && oldest_copy) chosen = oldest_copy;
  if (!chosen) chosen = oldest;

  const PlayParams& p = command.params;
  const float pan = std::clamp(p.pan, -1.0f, 1.0f);
  Voice& voice = *chosen;
  voice.sound = command.sound;
  voice.position = 0.0;
  voice.step = std::max(p.pitch, 0.01f);
  // Balance panning: the centre is full volume on both sides; panning turns the other side down.
  voice.gain_left = p.volume * std::min(1.0f, 1.0f - pan);
  voice.gain_right = p.volume * std::min(1.0f, 1.0f + pan);
  voice.bus = p.bus;
  voice.loop = p.loop;
  voice.started = ++play_counter_;
}

void Mixer::Mix(float* out, uint32_t frames) {
  // 1. Take the commands the game queued since the last block.
  size_t tail = tail_.load(std::memory_order_relaxed);
  while (tail != head_.load(std::memory_order_acquire)) {
    const Command& command = queue_[tail];
    if (command.kind == Command::Kind::PlayStream) {
      StartStream(command);                                      // marks the source as in a slot first...
      if (command.stream) command.stream->queued_.fetch_sub(1);  // ...so InUse() never blinks false
    } else if (command.kind == Command::Kind::StopAll) {
      for (Voice& voice : voices_) voice.sound = nullptr;
    } else {
      StartVoice(command);
    }
    tail = (tail + 1) % kQueueSize;
    tail_.store(tail, std::memory_order_release);  // frees the slot for the game thread
  }

  // 2. Add every playing voice into the (cleared) output.
  std::fill(out, out + frames * 2, 0.0f);
  const float master = muted_.load() ? 0.0f : master_.load();
  int active = 0;
  for (Voice& voice : voices_) {
    if (!voice.sound) continue;
    const SoundData& sound = *voice.sound;
    const float bus = bus_volume_[static_cast<size_t>(voice.bus)].load() * master;
    const float left = voice.gain_left * bus;
    const float right = voice.gain_right * bus;
    for (uint32_t i = 0; i < frames; ++i) {
      auto index = static_cast<uint32_t>(voice.position);
      if (index + 1 >= sound.frames) {
        if (!voice.loop) {
          voice.sound = nullptr;
          break;
        }
        voice.position -= static_cast<double>(sound.frames - 1);
        index = static_cast<uint32_t>(voice.position);
      }
      // Pitch: read between samples; linear interpolation is good enough for short effects.
      const float t = static_cast<float>(voice.position - index);
      const float* a = &sound.samples[index * 2];
      const float* b = a + 2;
      out[i * 2] += (a[0] + (b[0] - a[0]) * t) * left;
      out[i * 2 + 1] += (a[1] + (b[1] - a[1]) * t) * right;
      voice.position += voice.step;
    }
    if (voice.sound) ++active;
  }
  active_voices_.store(active);

  // 3. Music.
  MixStreams(out, frames, master);

  // 4. Several loud sounds can add up past full scale: clip rather than wrap around.
  for (uint32_t i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

void Mixer::StartStream(const Command& command) {
  const float step = 1.0f / (std::max(command.fade_seconds, 0.001f) * static_cast<float>(sample_rate_));
  StreamSource* source = command.stream;
  StreamSlot& current = streams_[0];
  if (source && current.source == source) {  // already playing: a new volume (and it keeps fading in)
    current.volume = command.stream_volume;
    current.step = step;
    return;
  }
  // The current stream fades out from where it is, in a free slot; if every slot is fading, the
  // quietest is dropped (by then it's nearly silent).
  if (current.source) {
    StreamSlot* into = nullptr;
    for (size_t i = 1; i < kStreamSlots && !into; ++i) {
      if (!streams_[i].source) into = &streams_[i];
    }
    if (!into) {
      into = &streams_[1];
      for (size_t i = 2; i < kStreamSlots; ++i) {
        if (streams_[i].Gain() < into->Gain()) into = &streams_[i];
      }
      FreeSlot(*into);
    }
    *into = current;
    into->step = -step;
    current = {};
  }
  if (!source) return;  // just a fade-out
  current = {source, 0.0f, step, command.stream_volume};
  for (size_t i = 1; i < kStreamSlots; ++i) {
    if (streams_[i].source != source) continue;
    current.t = streams_[i].t;  // still fading out: it turns round, without a cut
    streams_[i] = {};
  }
  source->in_slot_.store(true);
}

float Mixer::StreamSlot::Gain() const { return volume * std::sin(t * 1.57079633f); }

void Mixer::FreeSlot(StreamSlot& slot) {
  if (slot.source) slot.source->in_slot_.store(false);  // its owner may free it now: not touched again
  slot = {};
}

void Mixer::MixStreams(float* out, uint32_t frames, float master) {
  const float bus = bus_volume_[static_cast<size_t>(Bus::Music)].load() * master;
  for (StreamSlot& slot : streams_) {
    if (!slot.source) continue;
    uint32_t done = 0;
    while (done < frames && slot.source) {
      const uint32_t want = std::min(kStreamChunk, frames - done);
      const uint32_t got = slot.source->Read(stream_scratch_.data(), want);
      for (uint32_t i = 0; i < got; ++i) {
        slot.t = std::clamp(slot.t + slot.step, 0.0f, 1.0f);
        const float g = slot.Gain() * bus;
        out[(done + i) * 2] += stream_scratch_[i * 2] * g;
        out[(done + i) * 2 + 1] += stream_scratch_[i * 2 + 1] * g;
      }
      done += got;
      // Ended, or faded all the way out: the slot is free.
      if (got < want || (slot.step < 0.0f && slot.t <= 0.0f)) FreeSlot(slot);
    }
  }
}

}  // namespace wlw::audio
