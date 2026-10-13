#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// Measuring the GPU's own time. The CPU only queues commands (glDrawArrays returns at once) and
// the GPU runs them up to a few frames later, so CPU timers can't see what drawing costs. Timer
// queries can: the GPU writes the elapsed time between Begin and End when it actually gets there.
// The results are read kLatency frames later, when the GPU is surely done: reading at once would
// stall the CPU until the GPU caught up (and change what is being measured).
namespace wlw::rendering {

// What a measurement is of.
struct GpuSection {
  enum class Kind : uint8_t { Scene, PostPass, Canvas };
  uint32_t viewport = 0;  // scene::Viewport::Id()
  Kind kind = Kind::Scene;
  int index = 0;          // PostPass: which pass
};

struct GpuSample {
  GpuSection section;
  float ms = 0.0f;
};

// The GPU's timer queries, behind an interface so GpuTimer's logic is testable without a GPU.
class GpuQueries {
 public:
  virtual ~GpuQueries() = default;
  virtual uint32_t Create() = 0;
  virtual void Destroy(uint32_t query) = 0;
  virtual void Begin(uint32_t query) = 0;  // one at a time: timer queries can't nest
  virtual void End() = 0;
  virtual bool Available(uint32_t query) = 0;
  virtual uint64_t ElapsedNs(uint32_t query) = 0;
  // Whether the GPU's clock changed (power saving, a mobile thing) since this was last asked:
  // results read now can't be trusted. Asking resets it.
  virtual bool Disjoint() = 0;
};

// Sections measured one after another (they can't nest), kept in a ring of kLatency + 1 frames;
// each frame reuses that slot's queries (no allocations once warm).
class GpuTimer {
 public:
  static constexpr int kLatency = 3;  // a frame's results are read this many frames later

  explicit GpuTimer(std::unique_ptr<GpuQueries> queries) : queries_(std::move(queries)) {}
  ~GpuTimer();

  // Starts a frame. `results` gets the samples of the frame kLatency frames ago, in the order they
  // were measured; it stays empty if they aren't ready or the GPU clock changed meanwhile.
  void BeginFrame(std::vector<GpuSample>& results);
  // Measures what is drawn until End(). A Begin while one is open ends that one first.
  void Begin(const GpuSection& section);
  void End();

 private:
  struct Frame {
    std::vector<uint32_t> queries;     // pooled: grows to the most sections a frame has had
    std::vector<GpuSection> sections;  // this frame's, one per used query
  };
  std::unique_ptr<GpuQueries> queries_;
  std::array<Frame, kLatency + 1> frames_;
  int current_ = 0;
  bool open_ = false;
};

// Smooths a reading for display (an exponential moving average: ~half a second at 60 fps).
// previous < 0 means none yet.
inline float SmoothMs(float previous, float sample) {
  return previous < 0.0f ? sample : previous + (sample - previous) * 0.06f;
}

}  // namespace wlw::rendering
