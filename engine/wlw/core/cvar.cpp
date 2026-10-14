#include "wlw/core/cvar.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "wlw/core/log.h"
#include "wlw/platform/platform.h"

namespace wlw {
namespace {

std::string Lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string_view Trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
  return text;
}

// "value" -> value (config files and the console may quote strings).
std::string_view Unquote(std::string_view text) {
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"') return text.substr(1, text.size() - 2);
  return text;
}

struct Pending {
  std::string value;
  CVarSource source;
};

struct Registry {
  std::map<std::string, CVarBase*> cvars;       // by lowercase name: sorted for listing
  std::map<std::string, Pending> pending;       // values for names not registered (yet)
  bool started = false;                         // ReadOnly cvars refuse changes after this
  bool saved_changed = false;
};
Registry& GetRegistry() {
  static Registry registry;  // cvars register from other files' static initialisation
  return registry;
}

#if !defined(NDEBUG) || defined(WLW_ENABLE_CHEATS)
constexpr bool kCheatsAllowed = true;
#else
constexpr bool kCheatsAllowed = false;
#endif

}  // namespace

const char* CVarSetResultText(CVarSetResult result) {
  switch (result) {
    case CVarSetResult::Ok: return "ok";
    case CVarSetResult::BadValue: return "not a valid value";
    case CVarSetResult::ReadOnly: return "read-only once the game has started";
    case CVarSetResult::Cheat: return "a cheat: only changeable in debug builds";
  }
  return "?";
}

std::optional<bool> ParseCVarBool(std::string_view text) {
  const std::string lower = Lower(Trim(text));
  if (lower == "1" || lower == "true" || lower == "on" || lower == "yes") return true;
  if (lower == "0" || lower == "false" || lower == "off" || lower == "no") return false;
  return std::nullopt;
}

std::optional<int> ParseCVarInt(std::string_view text) {
  text = Trim(text);
  if (!text.empty() && text.front() == '+') text.remove_prefix(1);
  int value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size() || text.empty()) return std::nullopt;
  return value;
}

std::optional<float> ParseCVarFloat(std::string_view text) {
  const std::string copy(Trim(text));  // strtof needs a terminated string (from_chars<float> isn't everywhere)
  if (copy.empty()) return std::nullopt;
  char* end = nullptr;
  const float value = std::strtof(copy.c_str(), &end);
  if (end != copy.c_str() + copy.size()) return std::nullopt;
  return value;
}

std::string FormatCVarFloat(float value) {
  char buffer[32];
  for (int precision = 1; precision <= 9; ++precision) {
    std::snprintf(buffer, sizeof(buffer), "%.*g", precision, static_cast<double>(value));
    if (std::strtof(buffer, nullptr) == value) break;
  }
  return buffer;
}

// ---- CVarBase ----

CVarBase::CVarBase(std::string name, std::string help, CVarFlags flags)
    : name_(std::move(name)), help_(std::move(help)), flags_(flags) {}

CVarBase::~CVarBase() {
  Registry& registry = GetRegistry();
  const auto it = registry.cvars.find(Lower(name_));
  if (it != registry.cvars.end() && it->second == this) registry.cvars.erase(it);
}

void CVarBase::Register() {
  Registry& registry = GetRegistry();
  const std::string key = Lower(name_);
  if (registry.cvars.count(key) != 0) {
    WLW_LOG(LogCore, Error, "Two cvars are called %s: the second one isn't registered", name_.c_str());
    return;
  }
  registry.cvars[key] = this;
  if (const auto pending = registry.pending.find(key); pending != registry.pending.end()) {
    const Pending waiting = pending->second;
    registry.pending.erase(pending);
    if (const CVarSetResult result = SetFromText(waiting.value, waiting.source); result != CVarSetResult::Ok) {
      WLW_LOG(LogCore, Warning, "%s = %s: %s", name_.c_str(), waiting.value.c_str(), CVarSetResultText(result));
    }
  }
}

CVarSetResult CVarBase::Check(CVarSource source) const {
  if (source == CVarSource::Code) return CVarSetResult::Ok;
  if (HasFlag(flags_, CVarFlags::Cheat) && !kCheatsAllowed) return CVarSetResult::Cheat;
  if (HasFlag(flags_, CVarFlags::ReadOnly) && GetRegistry().started) return CVarSetResult::ReadOnly;
  return CVarSetResult::Ok;
}

CVarSetResult CVarBase::SetFromText(std::string_view text, CVarSource source) {
  if (const CVarSetResult result = Check(source); result != CVarSetResult::Ok) return result;
  return ParseAndSet(Unquote(Trim(text)), source) ? CVarSetResult::Ok : CVarSetResult::BadValue;
}

void CVarBase::Changed(CVarSource source) {
  last_source_ = source;
  if (HasFlag(flags_, CVarFlags::Saved) &&
      (source == CVarSource::Code || source == CVarSource::Console || source == CVarSource::UserFile)) {
    player_value_ = ValueText();
    if (source != CVarSource::UserFile) GetRegistry().saved_changed = true;
  }
  const auto callbacks = callbacks_;  // a callback may remove itself
  for (const auto& [_, callback] : callbacks) callback();
}

int CVarBase::OnChange(std::function<void()> callback) {
  callbacks_.emplace_back(next_callback_, std::move(callback));
  return next_callback_++;
}

void CVarBase::RemoveOnChange(int id) {
  std::erase_if(callbacks_, [id](const auto& callback) { return callback.first == id; });
}

void CVarBase::SetStarted(bool started) { GetRegistry().started = started; }

// ---- The registry ----

CVarBase* FindCVar(std::string_view name) {
  Registry& registry = GetRegistry();
  const auto it = registry.cvars.find(Lower(Trim(name)));
  return it == registry.cvars.end() ? nullptr : it->second;
}

std::vector<CVarBase*> AllCVars() {
  std::vector<CVarBase*> all;
  for (const auto& [_, cvar] : GetRegistry().cvars) all.push_back(cvar);
  return all;
}

CVarSetResult SetCVar(std::string_view name, std::string_view value, CVarSource source) {
  if (CVarBase* cvar = FindCVar(name)) return cvar->SetFromText(value, source);
  GetRegistry().pending[Lower(Trim(name))] = {std::string(Unquote(Trim(value))), source};
  return CVarSetResult::Ok;
}

void ClearPendingCVars() { GetRegistry().pending.clear(); }

// ---- Config text and the command line ----

int ApplyCVarText(std::string_view text, CVarSource source, std::string_view origin) {
  int applied = 0;
  int line_number = 0;
  while (!text.empty()) {
    const size_t newline = text.find('\n');
    std::string_view line = Trim(text.substr(0, newline));
    text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
    ++line_number;
    if (line.empty() || line.front() == '#') continue;
    // "name = value" or "name value"
    size_t name_end = 0;
    while (name_end < line.size() && !std::isspace(static_cast<unsigned char>(line[name_end])) && line[name_end] != '=') {
      ++name_end;
    }
    const std::string_view name = line.substr(0, name_end);
    std::string_view value = Trim(line.substr(name_end));
    if (!value.empty() && value.front() == '=') value = Trim(value.substr(1));
    if (name.empty() || value.empty()) {
      WLW_LOG(LogCore, Warning, "%.*s:%d: expected \"name = value\"", static_cast<int>(origin.size()), origin.data(),
              line_number);
      continue;
    }
    const CVarSetResult result = SetCVar(name, value, source);
    if (result != CVarSetResult::Ok) {
      WLW_LOG(LogCore, Warning, "%.*s:%d: %.*s: %s", static_cast<int>(origin.size()), origin.data(), line_number,
              static_cast<int>(name.size()), name.data(), CVarSetResultText(result));
      continue;
    }
    ++applied;
  }
  return applied;
}

bool ApplyCVarFile(const std::string& path, CVarSource source) {
  const auto data = platform::ReadFile(path);
  if (!data) return false;
  const int applied = ApplyCVarText({reinterpret_cast<const char*>(data->data()), data->size()}, source, path);
  WLW_LOG(LogCore, Info, "%s: %d setting(s)", path.c_str(), applied);
  return true;
}

std::vector<std::pair<std::string, std::string>> ParseCVarArguments(const std::vector<std::string>& args) {
  std::vector<std::pair<std::string, std::string>> pairs;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg.size() < 2 || arg.front() != '+') continue;
    const size_t equals = arg.find('=');
    if (equals != std::string::npos) {
      pairs.emplace_back(arg.substr(1, equals - 1), arg.substr(equals + 1));
    } else if (i + 1 < args.size() && (args[i + 1].empty() || args[i + 1].front() != '+')) {
      pairs.emplace_back(arg.substr(1), args[i + 1]);
      ++i;
    } else {
      pairs.emplace_back(arg.substr(1), "1");  // "+r.wireframe" alone turns a bool on
    }
  }
  return pairs;
}

std::vector<std::pair<std::string, std::string>> ParseCVarList(std::string_view list) {
  std::vector<std::pair<std::string, std::string>> pairs;
  while (!list.empty()) {
    const size_t semicolon = list.find(';');
    const std::string_view item = Trim(list.substr(0, semicolon));
    list = semicolon == std::string_view::npos ? std::string_view{} : list.substr(semicolon + 1);
    const size_t equals = item.find('=');
    if (item.empty() || equals == std::string_view::npos || equals == 0) continue;
    pairs.emplace_back(std::string(Trim(item.substr(0, equals))), std::string(Trim(item.substr(equals + 1))));
  }
  return pairs;
}

std::string SavedCVarText() {
  std::string text = "# The player's settings (written by the game: Saved cvars)\n";
  for (const auto& [_, cvar] : GetRegistry().cvars) {
    if (!HasFlag(cvar->Flags(), CVarFlags::Saved) || !cvar->player_value_) continue;
    text += cvar->Name() + " = " + *cvar->player_value_ + "\n";
  }
  return text;
}

bool ConsumeSavedCVarsChanged() {
  Registry& registry = GetRegistry();
  return std::exchange(registry.saved_changed, false);
}

}  // namespace wlw
