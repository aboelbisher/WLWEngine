#pragma once

#include <string>
#include <string_view>

namespace wlw::core {

// What this binary was built from: for logs, bug reports and the engine panel. A tester's log.txt starts with it.
// The version numbers come from wlw/version.h (compile time); the git commit is read at build time
// (cmake/GitInfo.cmake rewrites a generated source only when it changes, so commits don't rebuild the engine).
struct BuildInfo {
  std::string_view version;  // "0.1.0"
  int major = 0;
  int minor = 0;
  int patch = 0;
  std::string_view git_commit;  // short hash of the engine's checkout, "unknown" outside a git checkout
  bool git_dirty = false;       // tracked files had uncommitted changes when it was built
  std::string_view build_type;  // "Debug", "Release", "RelWithDebInfo"... ("unknown" if CMake had none)
};

// The engine's build info (any thread: it never changes).
const BuildInfo& GetBuildInfo();

// One line: "WLWengine 0.1.0 (3f2a9c1d0e-dirty, Debug)".
std::string BuildInfoText();

}  // namespace wlw::core
