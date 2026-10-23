#pragma once

namespace wlw::audio {

// How the app's Audio starts (AppConfig::audio).
struct AudioConfig {
  bool enabled = true;  // false: no audio device is opened (silent; sounds still load and "play")
  int max_voices = 24;  // sounds playing at once (Mixer); past it a new sound takes the oldest one's voice
};

}  // namespace wlw::audio
