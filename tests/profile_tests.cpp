// Tests for core/profile.h (the profiler macros) and core/type_name.h (the names system zones use). The unit
// tests build without WLW_PROFILE: the macros must vanish with their arguments. A build with it (the *-profile
// presets) runs the same cases with Tracy compiled in and no viewer connected.
#include <memory>
#include <string>

#include "test_helpers.h"
#include "wlw/app/system.h"
#include "wlw/core/log.h"
#include "wlw/core/profile.h"
#include "wlw/core/type_name.h"

TEST_SUITE_BEGIN("profile");

namespace {

int g_evaluated = 0;
[[maybe_unused]] const char* Counted(const char* text) {
  ++g_evaluated;
  return text;
}
[[maybe_unused]] int CountedValue() {
  ++g_evaluated;
  return 7;
}

struct PlainStruct {};
class PlainClass {};
enum class PlainEnum { A };
namespace inner {
class Nested {};
}  // namespace inner
template <typename T>
struct Box {};

class NamedSystem : public wlw::app::System {};

}  // namespace

TEST_CASE("profile macros: arguments are evaluated only in profile builds") {
  g_evaluated = 0;
  {
    WLW_PROFILE_SCOPE("Test zone");
    WLW_PROFILE_TEXT(Counted("text"), 4);
    WLW_PROFILE_PLOT("Test plot", CountedValue());
    WLW_PROFILE_MESSAGE(Counted("message"));
  }
  {
    WLW_PROFILE_SCOPE_DYNAMIC(Counted("Dynamic zone"));
  }
  {
    WLW_PROFILE_FUNCTION();
  }
  WLW_PROFILE_FRAME();
  // Profile builds evaluate each argument once (a dynamic name too: it's held in a variable).
  CHECK(g_evaluated == (wlw::profile::kEnabled ? 4 : 0));
}

TEST_CASE("profile: no viewer is connected in tests") {
  CHECK_FALSE(wlw::profile::Connected());
  wlw::profile::Start();  // twice is harmless (one log sink)
  wlw::profile::Start();
  WLW_LOG(wlw::LogCore, Info, "a log record while the profiler's sink is on");
  wlw::profile::Stop();
  wlw::profile::Stop();
}

TEST_CASE("type name: classes, structs, enums and namespaces without keywords") {
  CHECK(wlw::core::TypeName<int>() == "int");
  CHECK(wlw::core::TypeName<wlw::app::System>() == "wlw::app::System");
  CHECK(wlw::core::TypeName<PlainStruct>().find("PlainStruct") != std::string_view::npos);
  CHECK(wlw::core::TypeName<PlainClass>().find("class") == std::string_view::npos);
  CHECK(wlw::core::TypeName<PlainStruct>().find("struct") == std::string_view::npos);
  CHECK(wlw::core::TypeName<PlainEnum>().find("enum") == std::string_view::npos);
  const std::string_view nested = wlw::core::TypeName<inner::Nested>();
  CHECK(nested.substr(nested.size() - std::string_view("inner::Nested").size()) == "inner::Nested");
  CHECK(wlw::core::TypeName<Box<int>>().find("Box<int>") != std::string_view::npos);
}

TEST_CASE("type name: known at compile time") {
  constexpr std::string_view name = wlw::core::TypeName<wlw::app::SystemList>();
  static_assert(!name.empty());
  CHECK(name == "wlw::app::SystemList");
}

TEST_CASE("systems: named by the caller, \"System\" by default") {
  wlw::app::SystemList list;
  const wlw::app::System& unnamed = list.Add(std::make_unique<NamedSystem>(), nullptr);
  const wlw::app::System& named =
      list.Add(std::make_unique<NamedSystem>(), nullptr, std::string(wlw::core::TypeName<NamedSystem>()));
  CHECK(unnamed.Name() == "System");
  CHECK(named.Name().find("NamedSystem") != std::string::npos);
}

TEST_SUITE_END();
