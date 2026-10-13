#include "wlw/rendering/gpu_timer.h"

namespace wlw::rendering {

GpuTimer::~GpuTimer() {
  for (Frame& frame : frames_) {
    for (uint32_t query : frame.queries) queries_->Destroy(query);
  }
}

void GpuTimer::BeginFrame(std::vector<GpuSample>& results) {
  results.clear();
  End();
  current_ = (current_ + 1) % static_cast<int>(frames_.size());
  Frame& frame = frames_[static_cast<size_t>(current_)];  // measured kLatency frames ago

  // Asked every frame (even with nothing to read), so a clock change is always noticed.
  const bool disjoint = queries_->Disjoint();
  bool ready = !frame.sections.empty() && !disjoint;
  for (size_t i = 0; ready && i < frame.sections.size(); ++i) ready = queries_->Available(frame.queries[i]);
  if (ready) {
    for (size_t i = 0; i < frame.sections.size(); ++i) {
      const auto ns = queries_->ElapsedNs(frame.queries[i]);
      results.push_back({frame.sections[i], static_cast<float>(static_cast<double>(ns) / 1.0e6)});
    }
  }
  frame.sections.clear();  // the slot is this frame's now (its queries are reused)
}

void GpuTimer::Begin(const GpuSection& section) {
  End();
  Frame& frame = frames_[static_cast<size_t>(current_)];
  const size_t index = frame.sections.size();
  if (index == frame.queries.size()) frame.queries.push_back(queries_->Create());
  frame.sections.push_back(section);
  queries_->Begin(frame.queries[index]);
  open_ = true;
}

void GpuTimer::End() {
  if (!open_) return;
  queries_->End();
  open_ = false;
}

}  // namespace wlw::rendering
