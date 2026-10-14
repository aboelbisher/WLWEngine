// Tests for wlw/version.h (generated from project(VERSION)), core/build_info.h and core/deprecated.h.
// cmake/CheckDeprecations.cmake has its own CTest tests ("deprecations: ...", tests/CMakeLists.txt).
#include <cstdio>
#include <string>

#include "test_helpers.h"
#include "wlw/core/build_info.h"
#include "wlw/core/deprecated.h"
#include "wlw/version.h"

TEST_SUITE_BEGIN("version");

namespace {

int NewAnswer() { return 42; }

// A shim the way engine code writes one (the version here never ages: tests/ isn't checked for old shims).
#if !WLW_NO_DEPRECATED
WLW_DEPRECATED(0.1, "Use NewAnswer()") int OldAnswer() { return NewAnswer(); }
#endif

}  // namespace

TEST_CASE("the version string matches its numbers") {
  char text[32];
  std::snprintf(text, sizeof(text), "%d.%d.%d", WLW_VERSION_MAJOR, WLW_VERSION_MINOR, WLW_VERSION_PATCH);
  CHECK(std::string(text) == WLW_VERSION_STRING);
}

TEST_CASE("WLW_VERSION packs major, minor and patch into one comparable number") {
  static_assert(WLW_MAKE_VERSION(0, 3, 1) == 301);
  static_assert(WLW_MAKE_VERSION(1, 0, 0) > WLW_MAKE_VERSION(0, 99, 99));
  static_assert(WLW_VERSION == WLW_MAKE_VERSION(WLW_VERSION_MAJOR, WLW_VERSION_MINOR, WLW_VERSION_PATCH));
  static_assert(WLW_VERSION_MINOR < 100 && WLW_VERSION_PATCH < 100);
  CHECK(WLW_VERSION >= WLW_MAKE_VERSION(0, 1, 0));
}

TEST_CASE("WLW_VERSION_AT_LEAST compares with the engine's version") {
  static_assert(WLW_VERSION_AT_LEAST(WLW_VERSION_MAJOR, WLW_VERSION_MINOR, WLW_VERSION_PATCH));
  static_assert(WLW_VERSION_AT_LEAST(0, 0, 0));
  static_assert(!WLW_VERSION_AT_LEAST(WLW_VERSION_MAJOR, WLW_VERSION_MINOR, WLW_VERSION_PATCH + 1));
  static_assert(!WLW_VERSION_AT_LEAST(WLW_VERSION_MAJOR, WLW_VERSION_MINOR + 1, 0));
  static_assert(!WLW_VERSION_AT_LEAST(WLW_VERSION_MAJOR + 1, 0, 0));
  CHECK(true);
}

TEST_CASE("build info carries the compiled-in version") {
  const wlw::core::BuildInfo& info = wlw::core::GetBuildInfo();
  CHECK(info.version == WLW_VERSION_STRING);
  CHECK((info.major == WLW_VERSION_MAJOR && info.minor == WLW_VERSION_MINOR && info.patch == WLW_VERSION_PATCH));
  CHECK(&info == &wlw::core::GetBuildInfo());
}

TEST_CASE("build info names a commit and a build type") {
  const wlw::core::BuildInfo& info = wlw::core::GetBuildInfo();
  // A hex hash from git, or "unknown" when built outside a checkout.
  CHECK(!info.git_commit.empty());
  CHECK((info.git_commit == "unknown" || info.git_commit.find_first_not_of("0123456789abcdef") == std::string::npos));
  CHECK(!info.build_type.empty());
}

TEST_CASE("BuildInfoText is one line with the version, commit and build type") {
  const wlw::core::BuildInfo& info = wlw::core::GetBuildInfo();
  const std::string text = wlw::core::BuildInfoText();
  CHECK(text.rfind("WLWengine " + std::string(info.version) + " (", 0) == 0);
  CHECK(text.find(info.git_commit) != std::string::npos);
  CHECK(text.find(info.build_type) != std::string::npos);
  CHECK((text.find("-dirty") != std::string::npos) == info.git_dirty);
  CHECK(text.find('\n') == std::string::npos);
}

#if !WLW_NO_DEPRECATED
TEST_CASE("a deprecated shim still works inside the suppression macros") {
  WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN
  CHECK(OldAnswer() == NewAnswer());
  WLW_DISABLE_DEPRECATION_WARNINGS_END
}
#endif

TEST_SUITE_END();
