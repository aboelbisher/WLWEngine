// Tests for GPU timing (rendering/gpu_timer.h): results arrive kLatency frames late, frames whose
// results aren't ready or crossed a GPU clock change are dropped, queries are reused. A fake GPU
// stands in for the timer queries.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "test_helpers.h"
#include "wlw/rendering/gpu_timer.h"

TEST_SUITE_BEGIN("gpu_timer");

using namespace wlw;
using rendering::GpuSample;
using rendering::GpuSection;
using rendering::GpuTimer;

namespace {

// A GPU whose query n measures (n + 1) ms; it can be told some results aren't ready yet or that
// its clock changed.
struct FakeGpu {
  int created = 0;
  int destroyed = 0;
  int open = 0;  // queries begun and not ended (timer queries can't nest: must stay <= 1)
  int max_open = 0;
  bool ready = true;
  bool disjoint = false;
};

class FakeQueries : public rendering::GpuQueries {
 public:
  explicit FakeQueries(FakeGpu& gpu) : gpu_(gpu) {}
  uint32_t Create() override { return static_cast<uint32_t>(gpu_.created++); }
  void Destroy(uint32_t) override { ++gpu_.destroyed; }
  void Begin(uint32_t) override { gpu_.max_open = std::max(gpu_.max_open, ++gpu_.open); }
  void End() override { --gpu_.open; }
  bool Available(uint32_t) override { return gpu_.ready; }
  uint64_t ElapsedNs(uint32_t query) override { return (query + 1) * 1'000'000ull; }
  bool Disjoint() override {
    const bool was = gpu_.disjoint;
    gpu_.disjoint = false;
    return was;
  }

 private:
  FakeGpu& gpu_;
};

// One frame: the screen's 3D, then its canvas.
void MeasureFrame(GpuTimer& timer, std::vector<GpuSample>& results) {
  timer.BeginFrame(results);
  timer.Begin({1, GpuSection::Kind::Scene});
  timer.Begin({1, GpuSection::Kind::Canvas});  // ends the 3D section first
  timer.End();
}

}  // namespace

TEST_CASE("latency") {
  FakeGpu gpu;
  std::vector<GpuSample> results;
  {
    GpuTimer timer(std::make_unique<FakeQueries>(gpu));
    // The first frames have nothing to read yet.
    for (int frame = 0; frame < GpuTimer::kLatency + 1; ++frame) {
      MeasureFrame(timer, results);
      CHECK(results.empty());
    }
    // Then each frame gets the one kLatency frames before it, in order.
    timer.BeginFrame(results);
    CHECK(results.size() == 2);
    CHECK((results[0].section.kind == GpuSection::Kind::Scene && results[0].section.viewport == 1));
    CHECK(results[1].section.kind == GpuSection::Kind::Canvas);
    CHECK((Near(results[0].ms, 1.0f) && Near(results[1].ms, 2.0f)));  // the first frame's two queries

    // Sections never overlap (Begin ends the open one).
    CHECK((gpu.max_open == 1 && gpu.open == 0));
    // Each slot reuses its queries: 4 slots x 2 sections, however many frames.
    for (int frame = 0; frame < 20; ++frame) MeasureFrame(timer, results);
    CHECK(gpu.created == 2 * (GpuTimer::kLatency + 1));
  }
  CHECK(gpu.destroyed == gpu.created);
}

TEST_CASE("dropped frames") {
  FakeGpu gpu;
  GpuTimer timer(std::make_unique<FakeQueries>(gpu));
  std::vector<GpuSample> results;
  for (int frame = 0; frame < 10; ++frame) MeasureFrame(timer, results);
  CHECK(results.size() == 2);

  // Results not ready yet: that frame is skipped rather than waited for (no stall).
  gpu.ready = false;
  MeasureFrame(timer, results);
  CHECK(results.empty());
  gpu.ready = true;

  // The GPU clock changed: what is read now can't be trusted.
  gpu.disjoint = true;
  MeasureFrame(timer, results);
  CHECK(results.empty());
  MeasureFrame(timer, results);
  CHECK(results.size() == 2);

  // The last measured frame is read kLatency + 1 frames on; after that, frames with nothing
  // measured give nothing.
  for (int frame = 0; frame < GpuTimer::kLatency + 1; ++frame) timer.BeginFrame(results);
  CHECK(results.size() == 2);
  timer.BeginFrame(results);
  CHECK(results.empty());
}

TEST_CASE("smoothing") {
  CHECK(rendering::SmoothMs(-1.0f, 4.0f) == 4.0f);  // the first reading is taken as is
  float shown = 2.0f;
  for (int frame = 0; frame < 120; ++frame) shown = rendering::SmoothMs(shown, 4.0f);
  CHECK(Near(shown, 4.0f, 0.01f));  // follows a steady value within ~2 s
  const float after_spike = rendering::SmoothMs(4.0f, 40.0f);
  CHECK((after_spike > 4.0f && after_spike < 7.0f));  // one slow frame doesn't jump the display
}

TEST_SUITE_END();
