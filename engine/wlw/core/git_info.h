#pragma once

// Engine-internal: the values cmake/GitInfo.cmake writes into the generated wlw_git_info.cpp at build time.
// Read them through core/build_info.h.

namespace wlw::core::detail {

extern const char kGitCommit[];  // short hash, or "unknown"
extern const bool kGitDirty;
extern const char kBuildType[];  // CMake's configuration, or "unknown"

}  // namespace wlw::core::detail
