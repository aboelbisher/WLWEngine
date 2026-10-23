#include "wlw/core/console.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace wlw {
namespace {

std::string Lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::map<std::string, ConsoleCommand*>& Commands() {
  static std::map<std::string, ConsoleCommand*> commands;  // by lowercase name
  return commands;
}

void PrintCVar(const CVarBase& cvar) {
  std::string details = "default " + cvar.DefaultText();
  if (const std::string range = cvar.RangeText(); !range.empty()) details += ", " + range;
  if (HasFlag(cvar.Flags(), CVarFlags::Saved)) details += ", saved";
  if (HasFlag(cvar.Flags(), CVarFlags::ReadOnly)) details += ", read-only";
  if (HasFlag(cvar.Flags(), CVarFlags::Cheat)) details += ", cheat";
  WLW_LOG(LogConsole, Info, "%s = %s  (%s)  %s", cvar.Name().c_str(), cvar.ValueText().c_str(), details.c_str(),
          cvar.Help().c_str());
}

bool SetAndReport(CVarBase& cvar, std::string_view value, CVarSource source) {
  const CVarSetResult result = cvar.SetFromText(value, source);
  if (result != CVarSetResult::Ok) {
    WLW_LOG(LogConsole, Warning, "%s: \"%.*s\" %s", cvar.Name().c_str(), static_cast<int>(value.size()), value.data(),
            CVarSetResultText(result));
    return false;
  }
  WLW_LOG(LogConsole, Info, "%s = %s", cvar.Name().c_str(), cvar.ValueText().c_str());
  return true;
}

// ---- Built-in commands ----

const ConsoleCommand kHelp{"help", "Lists the commands; help <name> describes a command or cvar",
                           [](const ConsoleArgs& args) {
                             if (!args.empty()) {
                               if (const CVarBase* cvar = FindCVar(args[0])) return PrintCVar(*cvar);
                               if (const ConsoleCommand* command = FindConsoleCommand(args[0])) {
                                 WLW_LOG(LogConsole, Info, "%s: %s", command->Name().c_str(), command->Help().c_str());
                                 return;
                               }
                               WLW_LOG(LogConsole, Warning, "help: nothing called %s", args[0].c_str());
                               return;
                             }
                             WLW_LOG(LogConsole, Info, "Commands (find <text> searches cvars too; Tab completes):");
                             for (const ConsoleCommand* command : AllConsoleCommands()) {
                               WLW_LOG(LogConsole, Info, "  %s: %s", command->Name().c_str(), command->Help().c_str());
                             }
                           }};

const ConsoleCommand kFind{
    "find", "Lists the cvars and commands whose name or help contains <text>", [](const ConsoleArgs& args) {
      const std::string text = args.empty() ? "" : Lower(args[0]);
      int found = 0;
      for (const CVarBase* cvar : AllCVars()) {
        if (Lower(cvar->Name() + " " + cvar->Help()).find(text) == std::string::npos) continue;
        PrintCVar(*cvar);
        ++found;
      }
      for (const ConsoleCommand* command : AllConsoleCommands()) {
        if (Lower(command->Name() + " " + command->Help()).find(text) == std::string::npos) continue;
        WLW_LOG(LogConsole, Info, "%s (command): %s", command->Name().c_str(), command->Help().c_str());
        ++found;
      }
      if (found == 0) WLW_LOG(LogConsole, Info, "find: nothing matches \"%s\"", text.c_str());
    }};

const ConsoleCommand kSet{"set", "set <cvar> <value>", [](const ConsoleArgs& args) {
                            if (args.size() < 2) {
                              WLW_LOG(LogConsole, Warning, "usage: set <cvar> <value>");
                              return;
                            }
                            CVarBase* cvar = FindCVar(args[0]);
                            if (!cvar) {
                              WLW_LOG(LogConsole, Warning, "set: no cvar %s", args[0].c_str());
                              return;
                            }
                            SetAndReport(*cvar, args[1], CVarSource::Console);
                          }};

const ConsoleCommand kReset{"reset", "reset <cvar>: back to its default; reset all: every cvar",
                            [](const ConsoleArgs& args) {
                              if (args.empty()) {
                                WLW_LOG(LogConsole, Warning, "usage: reset <cvar> | all");
                                return;
                              }
                              if (Lower(args[0]) == "all") {
                                for (CVarBase* cvar : AllCVars()) cvar->Reset();
                                {
                                  WLW_LOG(LogConsole, Info, "every cvar is back to its default");
                                  return;
                                }
                              }
                              CVarBase* cvar = FindCVar(args[0]);
                              if (!cvar) {
                                WLW_LOG(LogConsole, Warning, "reset: no cvar %s", args[0].c_str());
                                return;
                              }
                              cvar->Reset();
                              PrintCVar(*cvar);
                            }};

const ConsoleCommand kExec{"exec", "exec <file>: runs a config file's settings (assets or a path)",
                           [](const ConsoleArgs& args) {
                             if (args.empty()) {
                               WLW_LOG(LogConsole, Warning, "usage: exec <file>");
                               return;
                             }
                             if (!ApplyCVarFile(args[0], CVarSource::Console)) {
                               WLW_LOG(LogConsole, Warning, "exec: can't read %s", args[0].c_str());
                             }
                           }};

}  // namespace

ConsoleCommand::ConsoleCommand(std::string name, std::string help, std::function<void(const ConsoleArgs&)> run)
    : name_(std::move(name)), help_(std::move(help)), run_(std::move(run)) {
  auto& commands = Commands();
  const std::string key = Lower(name_);
  if (commands.count(key) != 0) {
    WLW_LOG(LogConsole, Error, "Two console commands are called %s: the second one isn't registered", name_.c_str());
    return;
  }
  commands[key] = this;
}

ConsoleCommand::~ConsoleCommand() {
  auto& commands = Commands();
  const auto it = commands.find(Lower(name_));
  if (it != commands.end() && it->second == this) commands.erase(it);
}

ConsoleCommand* FindConsoleCommand(std::string_view name) {
  const auto it = Commands().find(Lower(name));
  return it == Commands().end() ? nullptr : it->second;
}

std::vector<ConsoleCommand*> AllConsoleCommands() {
  std::vector<ConsoleCommand*> all;
  for (const auto& [_, command] : Commands()) all.push_back(command);
  return all;
}

std::vector<std::string> SplitConsoleLine(std::string_view line) {
  std::vector<std::string> words;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    if (i >= line.size()) break;
    std::string word;
    if (line[i] == '"') {
      ++i;
      while (i < line.size() && line[i] != '"') word += line[i++];
      ++i;  // the closing quote (if any)
    } else {
      while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) word += line[i++];
    }
    words.push_back(std::move(word));
  }
  return words;
}

bool ExecuteConsoleLine(std::string_view line, CVarSource source) {
  std::vector<std::string> words = SplitConsoleLine(line);
  if (words.empty()) return true;
  // "name=value" written without spaces
  if (const size_t equals = words[0].find('='); equals != std::string::npos && equals > 0) {
    const std::string value = words[0].substr(equals + 1);
    words[0].resize(equals);
    words.insert(words.begin() + 1, value);
  }
  const std::string name = words[0];
  words.erase(words.begin());
  if (const ConsoleCommand* command = FindConsoleCommand(name)) {
    command->Run(words);
    return true;
  }
  if (CVarBase* cvar = FindCVar(name)) {
    if (!words.empty() && words[0] == "=") words.erase(words.begin());
    if (words.empty()) {
      PrintCVar(*cvar);
      return true;
    }
    std::string value = words[0];
    for (size_t i = 1; i < words.size(); ++i) value += " " + words[i];  // strings with spaces
    return SetAndReport(*cvar, value, source);
  }
  WLW_LOG(LogConsole, Warning, "Unknown command or cvar: %s (try help, or find <text>)", name.c_str());
  return false;
}

std::vector<std::string> CompleteConsoleName(std::string_view prefix) {
  const std::string lower = Lower(prefix);
  std::vector<std::string> names;
  for (const ConsoleCommand* command : AllConsoleCommands()) {
    if (Lower(command->Name()).rfind(lower, 0) == 0) names.push_back(command->Name());
  }
  for (const CVarBase* cvar : AllCVars()) {
    if (Lower(cvar->Name()).rfind(lower, 0) == 0) names.push_back(cvar->Name());
  }
  std::sort(names.begin(), names.end());
  return names;
}

// ---- ConsoleModel ----

ConsoleModel::ConsoleModel(size_t capacity) : capacity_(std::max<size_t>(capacity, 1)) {
  for (LogRecord& record : RecentLogs()) lines_.push_back(std::move(record));
  while (lines_.size() > capacity_) lines_.pop_front();
  sink_ = AddLogSink([this](const LogRecord& record) {
    std::lock_guard lock(mutex_);
    if (lines_.size() == capacity_) lines_.pop_front();
    lines_.push_back(record);
  });
}

ConsoleModel::~ConsoleModel() { sink_.Disconnect(); }

void ConsoleModel::ForEachLine(const std::function<void(const LogRecord&)>& visit) const {
  std::lock_guard lock(mutex_);
  for (const LogRecord& record : lines_) visit(record);
}

size_t ConsoleModel::LineCount() const {
  std::lock_guard lock(mutex_);
  return lines_.size();
}

void ConsoleModel::Clear() {
  std::lock_guard lock(mutex_);
  lines_.clear();
}

void ConsoleModel::Submit(std::string_view line) {
  std::string text(line);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
  history_position_ = history_.size();
  if (text.empty()) return;
  WLW_LOG(LogConsole, Info, "> %s", text.c_str());
  if (history_.empty() || history_.back() != text) history_.push_back(text);
  history_position_ = history_.size();
  ExecuteConsoleLine(text, CVarSource::Console);
}

std::optional<std::string> ConsoleModel::HistoryOlder() {
  if (history_position_ == 0) return std::nullopt;
  return history_[--history_position_];
}

std::optional<std::string> ConsoleModel::HistoryNewer() {
  if (history_position_ >= history_.size()) return std::nullopt;
  ++history_position_;
  return history_position_ == history_.size() ? std::string() : history_[history_position_];
}

ConsoleModel::Completion ConsoleModel::Complete(std::string_view text) const {
  Completion completion{std::string(text), {}};
  if (text.find(' ') != std::string_view::npos) return completion;  // only the first word completes
  std::vector<std::string> names = CompleteConsoleName(text);
  if (names.empty()) return completion;
  if (names.size() == 1) {
    completion.text = names[0] + " ";
    return completion;
  }
  // As far as they all agree (ignoring case; written as the first one spells it).
  size_t common = names[0].size();
  for (const std::string& name : names) {
    size_t i = 0;
    while (i < common && i < name.size() &&
           std::tolower(static_cast<unsigned char>(name[i])) == std::tolower(static_cast<unsigned char>(names[0][i]))) {
      ++i;
    }
    common = i;
  }
  if (common > text.size()) completion.text = names[0].substr(0, common);
  completion.candidates = std::move(names);
  return completion;
}

}  // namespace wlw
