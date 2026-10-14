#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Console variables (cvars), like Unreal's IConsoleVariable / Source's ConVar: a named, typed setting
// with a default and a help line, settable from code, config files, the command line, adb and the
// in-game console, without a rebuild.
//
//   inline wlw::CVar<bool> cv_shadows{"r.shadows", true, "Draw shadow maps"};          // in a header
//   inline wlw::CVar<float> cv_music{"audio.music", 0.8f, "Music volume", wlw::CVarFlags::Saved, {0.0f, 1.0f}};
//   if (cv_shadows.Get()) ...;   cv_music.Set(0.5f);   cv_shadows.OnChange([] { ... });
//
// Names: dotted lowercase, the system first ("r." rendering, "audio.", "log.", "quality.", a game's own
// prefix). Lookups ignore case.
//
// Where values come from, later ones winning (the Application applies them at startup, in this order):
//   1. the code default
//   2. "wlw.cfg" in the game's assets (shipped settings)
//   3. <save dir>/cvars.cfg: the player's Saved cvars, written back when they change
//   4. the command line: game.exe +r.shadows 0 +log.filter Render=Verbose
//      Android: adb shell am start -n <package>/<activity> --es wlw "r.shadows=0;log.filter=Render=Verbose"
//   5. the console, while the game runs
// A value for a name nobody registered (yet) waits, and applies when that cvar is created.
//
// Threads: cvars belong to the game thread (read, set and registered there; OnChange runs there).
namespace wlw {

enum class CVarFlags : uint32_t {
  None = 0,
  Saved = 1 << 0,     // kept in <save dir>/cvars.cfg when changed from code or the console
  ReadOnly = 1 << 1,  // set only before the game starts (config files, command line), not later
  Cheat = 1 << 2,     // only changeable from outside the code in debug builds (or with WLW_ENABLE_CHEATS)
};
constexpr CVarFlags operator|(CVarFlags a, CVarFlags b) {
  return static_cast<CVarFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr bool HasFlag(CVarFlags flags, CVarFlags flag) {
  return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

// Who set a value: decides what's allowed (flags) and what's saved.
enum class CVarSource : uint8_t { Code, ConfigFile, UserFile, CommandLine, Console };

enum class CVarType : uint8_t { Bool, Int, Float, String };

enum class CVarSetResult : uint8_t { Ok, BadValue, ReadOnly, Cheat };
const char* CVarSetResultText(CVarSetResult result);

class CVarBase {
 public:
  CVarBase(const CVarBase&) = delete;
  CVarBase& operator=(const CVarBase&) = delete;
  virtual ~CVarBase();

  const std::string& Name() const { return name_; }
  const std::string& Help() const { return help_; }
  CVarFlags Flags() const { return flags_; }
  CVarSource LastSource() const { return last_source_; }

  virtual CVarType Type() const = 0;
  virtual std::string ValueText() const = 0;
  virtual std::string DefaultText() const = 0;
  virtual bool IsDefault() const = 0;
  // A numeric cvar's range as text ("0..1"), "" if it has none.
  virtual std::string RangeText() const { return {}; }
  // Parses and sets (clamped to the range); checks the flags for `source`.
  CVarSetResult SetFromText(std::string_view text, CVarSource source);
  virtual void Reset() = 0;

  // Called after every change of value (not when a Set leaves it the same). Returns an id for
  // RemoveOnChange.
  int OnChange(std::function<void()> callback);
  void RemoveOnChange(int id);

  // Once the game has started, ReadOnly cvars refuse changes (the Application calls it after startup).
  static void SetStarted(bool started);

 protected:
  CVarBase(std::string name, std::string help, CVarFlags flags);
  void Register();  // by the typed constructor, once its value exists (applies a waiting value)
  CVarSetResult Check(CVarSource source) const;
  virtual bool ParseAndSet(std::string_view text, CVarSource source) = 0;  // false: not a value of this type
  void Changed(CVarSource source);

  std::string name_;
  std::string help_;
  CVarFlags flags_;
  CVarSource last_source_ = CVarSource::Code;

 private:
  friend std::string SavedCVarText();
  std::optional<std::string> player_value_;  // Saved cvars: the last value from code, the console or the user file
  std::vector<std::pair<int, std::function<void()>>> callbacks_;
  int next_callback_ = 0;
};

// Parsing used by every cvar (and the tests): bools take 1/0, true/false, on/off, yes/no.
std::optional<bool> ParseCVarBool(std::string_view text);
std::optional<int> ParseCVarInt(std::string_view text);
std::optional<float> ParseCVarFloat(std::string_view text);
std::string FormatCVarFloat(float value);  // shortest text that reads back the same

template <typename T>
class CVar final : public CVarBase {
  static_assert(std::is_same_v<T, bool> || std::is_same_v<T, int> || std::is_same_v<T, float> ||
                    std::is_same_v<T, std::string>,
                "CVar<T>: bool, int, float or std::string");

 public:
  struct Range {
    T min;
    T max;
  };

  CVar(std::string name, T default_value, std::string help, CVarFlags flags = CVarFlags::None,
       std::optional<Range> range = std::nullopt)
      : CVarBase(std::move(name), std::move(help), flags),
        default_(default_value),
        value_(default_value),
        range_(range) {
    if constexpr (kNumeric) {
      if (range_) value_ = default_ = Clamp(default_value);
    }
    Register();
  }

  const T& Get() const { return value_; }
  const T& Default() const { return default_; }
  const std::optional<Range>& GetRange() const { return range_; }
  // From code: always allowed (flags limit outside sources). Clamped to the range.
  void Set(T value, CVarSource source = CVarSource::Code) {
    if constexpr (kNumeric) value = Clamp(value);
    if (value == value_) return;
    value_ = std::move(value);
    Changed(source);
  }
  void Reset() override { Set(default_); }

  CVarType Type() const override {
    if constexpr (std::is_same_v<T, bool>) return CVarType::Bool;
    else if constexpr (std::is_same_v<T, int>) return CVarType::Int;
    else if constexpr (std::is_same_v<T, float>) return CVarType::Float;
    else return CVarType::String;
  }
  std::string ValueText() const override { return Text(value_); }
  std::string DefaultText() const override { return Text(default_); }
  bool IsDefault() const override { return value_ == default_; }
  std::string RangeText() const override {
    if constexpr (kNumeric) {
      if (range_) return Text(range_->min) + ".." + Text(range_->max);
    }
    return {};
  }

 private:
  static constexpr bool kNumeric = std::is_same_v<T, int> || std::is_same_v<T, float>;

  T Clamp(T value) const {
    if (!range_) return value;
    return value < range_->min ? range_->min : (range_->max < value ? range_->max : value);
  }
  static std::string Text(const T& value) {
    if constexpr (std::is_same_v<T, bool>) return value ? "1" : "0";
    else if constexpr (std::is_same_v<T, int>) return std::to_string(value);
    else if constexpr (std::is_same_v<T, float>) return FormatCVarFloat(value);
    else return value;
  }
  bool ParseAndSet(std::string_view text, CVarSource source) override {
    if constexpr (std::is_same_v<T, bool>) {
      const auto parsed = ParseCVarBool(text);
      if (!parsed) return false;
      Set(*parsed, source);
    } else if constexpr (std::is_same_v<T, int>) {
      const auto parsed = ParseCVarInt(text);
      if (!parsed) return false;
      Set(*parsed, source);
    } else if constexpr (std::is_same_v<T, float>) {
      const auto parsed = ParseCVarFloat(text);
      if (!parsed) return false;
      Set(*parsed, source);
    } else {
      Set(std::string(text), source);
    }
    return true;
  }

  T default_;
  T value_;
  std::optional<Range> range_;
};

// ---- The registry ----
CVarBase* FindCVar(std::string_view name);  // ignores case; null if none
// Every cvar, sorted by name.
std::vector<CVarBase*> AllCVars();
// Sets `name` from text; a name nobody registered yet keeps the value for when it is (Ok then).
CVarSetResult SetCVar(std::string_view name, std::string_view value, CVarSource source);
// Drops values waiting for cvars that were never registered (tests).
void ClearPendingCVars();

// ---- Config text and the command line ----
// One setting per line: "name = value" or "name value"; blank lines and # comments are skipped.
// Returns how many lines were applied; problems are logged (with `origin`, e.g. the file name).
int ApplyCVarText(std::string_view text, CVarSource source, std::string_view origin);
// Reads a file through platform::ReadFile (assets on Android); false if it doesn't exist.
bool ApplyCVarFile(const std::string& path, CVarSource source);

// "+r.shadows 0 +log.filter=Render=Verbose" as program arguments -> (name, value) pairs. A "+name" with
// no "=" takes the next argument as its value (unless that's another "+name": then it's "1").
// Arguments without "+" are ignored (they belong to someone else).
std::vector<std::pair<std::string, std::string>> ParseCVarArguments(const std::vector<std::string>& args);
// Android's intent extra "r.shadows=0;log.filter=Render=Verbose" -> the same pairs.
std::vector<std::pair<std::string, std::string>> ParseCVarList(std::string_view list);

// The Saved cvars that differ from their defaults (and weren't set by the command line), as config text.
std::string SavedCVarText();
// True once a Saved cvar changed since the last call (the Application saves then).
bool ConsumeSavedCVarsChanged();

}  // namespace wlw
