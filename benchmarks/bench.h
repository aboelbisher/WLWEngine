#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nanobench.h>

// Engine microbenchmarks (wlw_benchmarks): nanobench measures one piece of engine work in a loop
// under fixed conditions (fixed seeds, no window, no GPU), so a number can be compared between
// commits and machines. One "op" = one frame's worth of that work (e.g. one Update of 20k dots).
//
// A file adds a group of benchmarks with WLW_BENCHMARK(group) { ... }: it builds its data once,
// then calls ctx.Run("name", [&] { work; }) per benchmark. Names are "<group>/<what> <size>" and
// stable: baselines (benchmarks/baselines/*.json) are matched by name.
namespace wlw::bench {

// One benchmark's result.
struct Measured {
  std::string name;
  double us = 0.0;         // median time per op, microseconds
  double error_pct = 0.0;  // median absolute percentage error of the epochs (noise)
};

class Context {
 public:
  Context(ankerl::nanobench::Bench& bench, std::string filter, bool smoke, std::string data_dir)
      : bench_(bench), filter_(std::move(filter)), smoke_(smoke), data_dir_(std::move(data_dir)) {}

  // Measures `op` as benchmark `name` unless the filter skips it. `op` must leave the state ready
  // for the next call (it runs thousands of times).
  void Run(const std::string& name, const std::function<void()>& op);
  // Keeps the compiler from deleting work whose result nobody reads.
  template <typename T>
  static void Keep(const T& value) {
    ankerl::nanobench::doNotOptimizeAway(value);
  }

  bool Wants(const std::string& name) const { return filter_.empty() || name.find(filter_) != std::string::npos; }
  bool Smoke() const { return smoke_; }
  const std::string& DataDir() const { return data_dir_; }  // fonts and other inputs
  const std::vector<Measured>& Results() const { return results_; }

 private:
  ankerl::nanobench::Bench& bench_;
  std::string filter_;
  bool smoke_;
  std::string data_dir_;
  std::vector<Measured> results_;
};

using GroupFn = void (*)(Context&);
struct Group {
  const char* name;
  GroupFn run;
};
std::vector<Group>& Groups();

struct Registration {
  Registration(const char* name, GroupFn run) { Groups().push_back({name, run}); }
};

}  // namespace wlw::bench

#define WLW_BENCH_CONCAT_(a, b) a##b
#define WLW_BENCH_CONCAT(a, b) WLW_BENCH_CONCAT_(a, b)
#define WLW_BENCHMARK(group)                                                                         \
  static void WLW_BENCH_CONCAT(BenchGroup_, __LINE__)(::wlw::bench::Context & ctx);                  \
  static const ::wlw::bench::Registration WLW_BENCH_CONCAT(bench_registration_, __LINE__)(          \
      group, &WLW_BENCH_CONCAT(BenchGroup_, __LINE__));                                              \
  static void WLW_BENCH_CONCAT(BenchGroup_, __LINE__)(::wlw::bench::Context & ctx)
