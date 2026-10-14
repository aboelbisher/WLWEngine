// wlw_benchmarks: runs the engine's microbenchmarks (see bench.h), saves the results as JSON and
// compares them with a baseline. Same tool on desktop and on a phone (adb shell).
//
//   wlw_benchmarks [filter] [--out run.json] [--baseline base.json] [--markdown summary.md]
//                  [--threshold 25] [--data <dir>] [--smoke] [--list]
//
//   filter       only benchmarks whose name contains it ("fx/", "10k")
//   --out        write the results (name -> us per op) as JSON: commit it as a baseline
//   --baseline   compare with an earlier run: a table of baseline, now and change
//   --markdown   append that table as Markdown (CI: $GITHUB_STEP_SUMMARY)
//   --threshold  % slower that counts as a regression (default 25): a warning, never a failure;
//                on GitHub Actions also a ::warning:: annotation
//   --data       folder with the test font (default: the source tree's tests/data)
//   --smoke      run each benchmark a few times without measuring (CTest: keeps them working)
#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "bench.h"
#include "json.hpp"

namespace wlw::bench {

std::vector<Group>& Groups() {
  static std::vector<Group> groups;
  return groups;
}

void Context::Run(const std::string& name, const std::function<void()>& op) {
  if (!Wants(name)) return;
  if (smoke_) {
    for (int i = 0; i < 3; ++i) op();
    std::printf("ok  %s\n", name.c_str());
    results_.push_back({name, 0.0, 0.0});
    return;
  }
  bench_.run(name, op);
  const ankerl::nanobench::Result& result = bench_.results().back();
  using Measure = ankerl::nanobench::Result::Measure;
  results_.push_back(
      {name, result.median(Measure::elapsed) * 1e6, result.medianAbsolutePercentError(Measure::elapsed) * 100.0});
}

}  // namespace wlw::bench

namespace {

using wlw::bench::Measured;

const char* BuildType() {
#ifdef WLW_BENCH_BUILD_TYPE
  return WLW_BENCH_BUILD_TYPE;
#else
  return "unknown";
#endif
}

std::string Compiler() {
#if defined(__clang__)
  return std::string("clang ") + __clang_version__;
#elif defined(_MSC_VER)
  return "msvc " + std::to_string(_MSC_FULL_VER);
#elif defined(__GNUC__)
  return "gcc " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
  return "unknown";
#endif
}

const char* Platform() {
#if defined(__ANDROID__)
  return "android";
#elif defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#else
  return "linux";
#endif
}

bool WriteResults(const std::string& path, const std::vector<Measured>& results) {
  nlohmann::ordered_json json;
  json["platform"] = Platform();
  json["build"] = BuildType();
  json["compiler"] = Compiler();
  nlohmann::ordered_json list = nlohmann::ordered_json::object();
  for (const Measured& m : results) {
    // Rounded: diffs of committed baselines stay readable.
    list[m.name] = {{"us", std::round(m.us * 1000.0) / 1000.0}, {"error_pct", std::round(m.error_pct * 10.0) / 10.0}};
  }
  json["results"] = list;
  std::ofstream file(path, std::ios::binary);
  file << json.dump(2) << "\n";
  return static_cast<bool>(file);
}

std::map<std::string, double> ReadBaseline(const std::string& path, std::string* description) {
  std::map<std::string, double> out;
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::fprintf(stderr, "Baseline not found: %s\n", path.c_str());
    return out;
  }
  const nlohmann::json json = nlohmann::json::parse(file, nullptr, /*allow_exceptions=*/false);
  if (json.is_discarded() || !json.contains("results")) {
    std::fprintf(stderr, "Not a benchmark file: %s\n", path.c_str());
    return out;
  }
  *description = json.value("platform", "?") + " " + json.value("build", "?") + ", " + json.value("compiler", "?");
  for (const auto& [name, value] : json["results"].items()) out[name] = value.value("us", 0.0);
  return out;
}

// The comparison as text (stdout) and Markdown (CI job summary); regressions past `threshold` %.
void Compare(const std::vector<Measured>& results, const std::string& baseline_path, const std::string& markdown_path,
             double threshold) {
  std::string description;
  const std::map<std::string, double> baseline = ReadBaseline(baseline_path, &description);
  if (baseline.empty()) return;
  const bool github = std::getenv("GITHUB_ACTIONS") != nullptr;
  std::ostringstream md;
  md << "### Benchmarks vs `" << baseline_path << "`\n\nBaseline: " << description << ". Now: " << Platform() << " "
     << BuildType() << ", " << Compiler() << ". Regression threshold: +" << threshold << "%.\n\n"
     << "| Benchmark | Baseline (us) | Now (us) | Change |\n|---|---:|---:|---:|\n";
  std::printf("\n%-40s %14s %14s %9s\n", "benchmark (vs baseline)", "baseline us", "now us", "change");
  int regressions = 0;
  for (const Measured& m : results) {
    const auto it = baseline.find(m.name);
    if (it == baseline.end() || it->second <= 0.0) {
      std::printf("%-40s %14s %14.3f %9s\n", m.name.c_str(), "-", m.us, "new");
      md << "| " << m.name << " | - | " << m.us << " | new |\n";
      continue;
    }
    const double change = (m.us / it->second - 1.0) * 100.0;
    const bool slower = change > threshold;
    regressions += slower ? 1 : 0;
    std::printf("%-40s %14.3f %14.3f %+8.1f%%%s\n", m.name.c_str(), it->second, m.us, change, slower ? "  SLOWER" : "");
    char cell[64];
    std::snprintf(cell, sizeof(cell), "%+.1f%%%s", change, slower ? " :warning:" : "");
    md << "| " << m.name << " | " << it->second << " | " << m.us << " | " << cell << " |\n";
    if (slower && github) {
      std::printf("::warning title=Benchmark regression::%s: %.3f us -> %.3f us (%+.1f%%)\n", m.name.c_str(),
                  it->second, m.us, change);
    }
  }
  std::printf("%d benchmark(s) more than %.0f%% slower than the baseline\n", regressions, threshold);
  if (!markdown_path.empty()) {
    std::ofstream file(markdown_path, std::ios::app);
    file << md.str() << "\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string filter, out_path, baseline_path, markdown_path;
#ifdef WLW_BENCH_DATA_DIR
  std::string data_dir = WLW_BENCH_DATA_DIR;
#else
  std::string data_dir = ".";
#endif
  double threshold = 25.0;
  bool smoke = false, list = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&](std::string& target) {
      if (i + 1 < argc) target = argv[++i];
    };
    if (arg == "--out") {
      value(out_path);
    } else if (arg == "--baseline") {
      value(baseline_path);
    } else if (arg == "--markdown") {
      value(markdown_path);
    } else if (arg == "--data") {
      value(data_dir);
    } else if (arg == "--threshold" && i + 1 < argc) {
      threshold = std::atof(argv[++i]);
    } else if (arg == "--smoke") {
      smoke = true;
    } else if (arg == "--list") {
      list = true;
    } else if (!arg.empty() && arg[0] == '-') {
      std::fprintf(stderr, "Unknown option %s (see the top of benchmarks/bench_main.cpp)\n", arg.c_str());
      return 2;
    } else {
      filter = arg;
    }
  }
  // Registration order depends on the linker: run groups by name, the same everywhere.
  std::sort(wlw::bench::Groups().begin(), wlw::bench::Groups().end(),
            [](const wlw::bench::Group& a, const wlw::bench::Group& b) { return std::string(a.name) < b.name; });
  if (list) {
    for (const wlw::bench::Group& group : wlw::bench::Groups()) std::printf("%s\n", group.name);
    return 0;
  }
#ifndef NDEBUG
  if (!smoke) std::printf("WARNING: a debug build: these numbers mean nothing (use a Release or profile build)\n");
#endif

  ankerl::nanobench::Bench bench;
  bench.title("WLWengine").unit("op").warmup(3).minEpochTime(std::chrono::milliseconds(20));
  wlw::bench::Context ctx(bench, filter, smoke, data_dir);
  for (const wlw::bench::Group& group : wlw::bench::Groups()) group.run(ctx);
  if (ctx.Results().empty()) {
    std::fprintf(stderr, "No benchmark matches \"%s\"\n", filter.c_str());
    return 1;
  }
  if (smoke) return 0;

  if (!out_path.empty()) {
    if (!WriteResults(out_path, ctx.Results())) {
      std::fprintf(stderr, "Could not write %s\n", out_path.c_str());
      return 1;
    }
    std::printf("Results written to %s\n", out_path.c_str());
  }
  if (!baseline_path.empty()) Compare(ctx.Results(), baseline_path, markdown_path, threshold);
  return 0;
}
