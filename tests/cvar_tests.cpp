// Tests for wlw/core/cvar.h: types, parsing, ranges, flags, change callbacks, the registry, waiting values,
// config text, the command line / adb list, the order of sources and what's saved.
// Cvars are global: every case uses its own names ("test.<case>.*") and static cvars (registered for good).
#include <cctype>
#include <string>
#include <vector>

#include "test_helpers.h"
#include "wlw/core/cvar.h"
#include "wlw/core/log.h"

TEST_SUITE_BEGIN("cvar");

using namespace wlw;

namespace {

// No console output for the warnings these cases provoke; started state back to "not started".
struct Quiet {
  Quiet() { SetPlatformLogOutput(false); }
  ~Quiet() {
    SetPlatformLogOutput(true);
    CVarBase::SetStarted(false);
    ClearPendingCVars();
  }
};

}  // namespace

TEST_CASE("values parse by type") {
  CHECK(ParseCVarBool("1") == true);
  CHECK(ParseCVarBool("On") == true);
  CHECK(ParseCVarBool(" no ") == false);
  CHECK_FALSE(ParseCVarBool("2").has_value());
  CHECK(ParseCVarInt("-42") == -42);
  CHECK(ParseCVarInt("+7") == 7);
  CHECK_FALSE(ParseCVarInt("4.5").has_value());
  CHECK_FALSE(ParseCVarInt("").has_value());
  CHECK(ParseCVarFloat("0.25") == 0.25f);
  CHECK(ParseCVarFloat("1e-3") == 0.001f);
  CHECK_FALSE(ParseCVarFloat("fast").has_value());
  CHECK(FormatCVarFloat(0.1f) == "0.1");
  CHECK(FormatCVarFloat(2.0f) == "2");
  CHECK(ParseCVarFloat(FormatCVarFloat(1.0f / 3.0f)) == 1.0f / 3.0f);  // reads back the same
}

TEST_CASE("a cvar has a default, a value and text") {
  static CVar<int> count{"test.basic.count", 3, "How many"};
  static CVar<std::string> name{"test.basic.name", "wlw", "A name"};
  CHECK(count.Get() == 3);
  CHECK(count.IsDefault());
  count.Set(5);
  CHECK(count.Get() == 5);
  CHECK(count.ValueText() == "5");
  CHECK(count.DefaultText() == "3");
  CHECK_FALSE(count.IsDefault());
  count.Reset();
  CHECK(count.Get() == 3);
  CHECK(name.SetFromText("\"two words\"", CVarSource::Console) == CVarSetResult::Ok);
  CHECK(name.Get() == "two words");
  CHECK(name.Type() == CVarType::String);
}

TEST_CASE("ranges clamp the default, code and text") {
  Quiet quiet;
  static CVar<float> volume{"test.range.volume", 1.5f, "Volume", CVarFlags::None, CVar<float>::Range{0.0f, 1.0f}};
  CHECK(volume.Get() == 1.0f);  // the default too
  volume.Set(-3.0f);
  CHECK(volume.Get() == 0.0f);
  CHECK(volume.SetFromText("0.5", CVarSource::Console) == CVarSetResult::Ok);
  CHECK(volume.Get() == 0.5f);
  CHECK(volume.SetFromText("9", CVarSource::Console) == CVarSetResult::Ok);
  CHECK(volume.Get() == 1.0f);
  CHECK(volume.RangeText() == "0..1");
}

TEST_CASE("bad text leaves the value alone") {
  static CVar<bool> flag{"test.bad.flag", true, "A flag"};
  CHECK(flag.SetFromText("maybe", CVarSource::Console) == CVarSetResult::BadValue);
  CHECK(flag.Get() == true);
}

TEST_CASE("change callbacks run on changes only") {
  static CVar<int> level{"test.callback.level", 0, "Level"};
  int calls = 0;
  const int id = level.OnChange([&calls] { ++calls; });
  level.Set(1);
  level.Set(1);  // the same value: no change
  CHECK(level.SetFromText("2", CVarSource::Console) == CVarSetResult::Ok);
  CHECK(calls == 2);
  CHECK(level.LastSource() == CVarSource::Console);
  level.RemoveOnChange(id);
  level.Set(3);
  CHECK(calls == 2);
}

TEST_CASE("the registry finds cvars by name, ignoring case") {
  Quiet quiet;
  static CVar<int> found{"test.registry.Found", 1, "Found"};
  CHECK(FindCVar("test.registry.found") == &found);
  CHECK(FindCVar(" TEST.REGISTRY.FOUND ") == &found);
  CHECK(FindCVar("test.registry.missing") == nullptr);
  static CVar<int> twin{"test.registry.found", 2, "Same name"};  // logged and not registered
  CHECK(FindCVar("test.registry.found") == &found);
  const std::vector<CVarBase*> all = AllCVars();
  auto lower = [](std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
  };
  for (size_t i = 1; i < all.size(); ++i) CHECK(lower(all[i - 1]->Name()) < lower(all[i]->Name()));  // sorted
  CHECK(SetCVar("test.registry.found", "7", CVarSource::Console) == CVarSetResult::Ok);
  CHECK(found.Get() == 7);
}

TEST_CASE("a value for an unknown name waits for its cvar") {
  Quiet quiet;
  CHECK(SetCVar("test.later.speed", "2.5", CVarSource::CommandLine) == CVarSetResult::Ok);
  static CVar<float> speed{"test.later.speed", 1.0f, "Speed"};
  CHECK(speed.Get() == 2.5f);
  CHECK(speed.LastSource() == CVarSource::CommandLine);
}

TEST_CASE("read-only cvars change only before the game starts") {
  Quiet quiet;
  static CVar<int> size{"test.readonly.size", 64, "Size", CVarFlags::ReadOnly};
  CHECK(size.SetFromText("128", CVarSource::CommandLine) == CVarSetResult::Ok);
  CVarBase::SetStarted(true);
  CHECK(size.SetFromText("256", CVarSource::Console) == CVarSetResult::ReadOnly);
  CHECK(size.Get() == 128);
  size.Set(512);  // code may
  CHECK(size.Get() == 512);
}

TEST_CASE("cheats change from outside only in debug builds") {
  static CVar<bool> god{"test.cheat.god", false, "Invincible", CVarFlags::Cheat};
#ifdef NDEBUG
  CHECK(god.SetFromText("1", CVarSource::CommandLine) == CVarSetResult::Cheat);
  CHECK(god.Get() == false);
#else
  CHECK(god.SetFromText("1", CVarSource::CommandLine) == CVarSetResult::Ok);
  CHECK(god.Get() == true);
#endif
  god.Set(false);  // code always may
  CHECK(god.Get() == false);
}

TEST_CASE("config text: name = value, name value, comments") {
  Quiet quiet;
  static CVar<int> a{"test.text.a", 0, "A"};
  static CVar<std::string> b{"test.text.b", "", "B"};
  static CVar<bool> c{"test.text.c", false, "C"};
  const int applied = ApplyCVarText(
      "# settings\n"
      "test.text.a = 4\r\n"
      "\n"
      "  test.text.b   hello world  \n"
      "test.text.c=on\n"
      "test.text.a\n"               // no value: skipped
      "test.text.c = sometimes\n",  // bad value: skipped
      CVarSource::ConfigFile, "test.cfg");
  CHECK(applied == 3);
  CHECK(a.Get() == 4);
  CHECK(b.Get() == "hello world");
  CHECK(c.Get() == true);
}

TEST_CASE("later sources win: config file, user file, command line") {
  Quiet quiet;
  static CVar<int> order{"test.order.value", 0, "Order"};
  ApplyCVarText("test.order.value = 1", CVarSource::ConfigFile, "wlw.cfg");
  CHECK(order.Get() == 1);
  ApplyCVarText("test.order.value = 2", CVarSource::UserFile, "cvars.cfg");
  CHECK(order.Get() == 2);
  for (const auto& [name, value] : ParseCVarArguments({"+test.order.value", "3"})) {
    SetCVar(name, value, CVarSource::CommandLine);
  }
  CHECK(order.Get() == 3);
}

TEST_CASE("command-line arguments and the adb list") {
  using Pairs = std::vector<std::pair<std::string, std::string>>;
  CHECK(ParseCVarArguments({"--other", "+r.a", "0", "+r.b=x=y", "+r.c", "+r.d", "plain"}) ==
        Pairs{{"r.a", "0"}, {"r.b", "x=y"}, {"r.c", "1"}, {"r.d", "plain"}});
  CHECK(ParseCVarArguments({"+"}).empty());
  CHECK(ParseCVarList("r.shadows=0; log.filter=Render=Verbose;;bad;=x") ==
        Pairs{{"r.shadows", "0"}, {"log.filter", "Render=Verbose"}});
}

TEST_CASE("saved cvars keep the player's values, not the command line's") {
  Quiet quiet;
  static CVar<float> music{"test.saved.music", 0.8f, "Music", CVarFlags::Saved};
  static CVar<int> plain{"test.saved.plain", 0, "Not saved"};
  ConsumeSavedCVarsChanged();
  CHECK(SavedCVarText().find("test.saved.music") == std::string::npos);  // never changed

  music.Set(0.5f);  // e.g. a settings menu
  plain.Set(9);
  CHECK(ConsumeSavedCVarsChanged());
  CHECK_FALSE(ConsumeSavedCVarsChanged());
  CHECK(SavedCVarText().find("test.saved.music = 0.5\n") != std::string::npos);
  CHECK(SavedCVarText().find("test.saved.plain") == std::string::npos);

  SetCVar("test.saved.music", "0.1", CVarSource::CommandLine);  // a one-off override
  CHECK(music.Get() == 0.1f);
  CHECK_FALSE(ConsumeSavedCVarsChanged());
  CHECK(SavedCVarText().find("test.saved.music = 0.5\n") != std::string::npos);

  ApplyCVarText("test.saved.music = 0.3", CVarSource::UserFile, "cvars.cfg");  // read back: not "changed"
  CHECK_FALSE(ConsumeSavedCVarsChanged());
  CHECK(SavedCVarText().find("test.saved.music = 0.3\n") != std::string::npos);
}

TEST_CASE("a missing config file is not an error") {
  Quiet quiet;
  CHECK_FALSE(ApplyCVarFile("no_such_settings.cfg", CVarSource::ConfigFile));
}

TEST_SUITE_END();
