#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wlw/core/connection.h"

#include "wlw/core/cvar.h"
#include "wlw/core/log.h"

// Console commands and the console's logic (the window that draws it is debug/console_window.h).
//
//   wlw::ConsoleCommand spawn{"game.spawn", "Spawns N enemies: game.spawn <n>",
//                             [](const wlw::ConsoleArgs& args) { ... }};   // unregistered when destroyed
//
// A console line is a command ("help", "find shadow", "exec my.cfg"), a cvar's name (shows it) or a cvar
// and a value ("r.shadows 0", "r.shadows = 0": sets it). What commands print goes to the log, category
// Console, so it shows in the console and in the log file alike.
//
// Threads: the game thread (commands run there).
namespace wlw {

WLW_LOG_CATEGORY(LogConsole);

using ConsoleArgs = std::vector<std::string>;  // the words after the command's name

class ConsoleCommand {
 public:
  ConsoleCommand(std::string name, std::string help, std::function<void(const ConsoleArgs&)> run);
  ~ConsoleCommand();
  ConsoleCommand(const ConsoleCommand&) = delete;
  ConsoleCommand& operator=(const ConsoleCommand&) = delete;

  const std::string& Name() const { return name_; }
  const std::string& Help() const { return help_; }
  void Run(const ConsoleArgs& args) const { run_(args); }

 private:
  std::string name_;
  std::string help_;
  std::function<void(const ConsoleArgs&)> run_;
};

ConsoleCommand* FindConsoleCommand(std::string_view name);  // ignores case
std::vector<ConsoleCommand*> AllConsoleCommands();          // sorted by name

// Words, split at spaces; "quoted text" stays one word.
std::vector<std::string> SplitConsoleLine(std::string_view line);
// Runs one line (see above). False if it named nothing known or a value was refused (the reason is logged).
bool ExecuteConsoleLine(std::string_view line, CVarSource source = CVarSource::Console);
// Command and cvar names starting with `prefix` (any case), sorted.
std::vector<std::string> CompleteConsoleName(std::string_view prefix);

// The console window's state without the drawing: its lines, the input history and Tab completion.
class ConsoleModel {
 public:
  // Starts with the log's recent history and then gets every new record (from any thread).
  explicit ConsoleModel(size_t capacity = 2000);
  ~ConsoleModel();
  ConsoleModel(const ConsoleModel&) = delete;
  ConsoleModel& operator=(const ConsoleModel&) = delete;

  // Calls `visit` for every line, oldest first, while new ones wait (the window draws inside it).
  void ForEachLine(const std::function<void(const LogRecord&)>& visit) const;
  size_t LineCount() const;
  void Clear();

  // Runs what was typed: echoes it ("> line"), keeps it in the history, executes it.
  void Submit(std::string_view line);

  // Up / Down in the input line: the text to show instead (nullopt: nothing older / newer).
  std::optional<std::string> HistoryOlder();
  std::optional<std::string> HistoryNewer();
  const std::vector<std::string>& History() const { return history_; }

  // Tab: the first word completed as far as every candidate agrees (a single match gets a space after
  // it); `candidates` lists them when there's more than one.
  struct Completion {
    std::string text;
    std::vector<std::string> candidates;
  };
  Completion Complete(std::string_view text) const;

 private:
  size_t capacity_;
  mutable std::mutex mutex_;
  std::deque<LogRecord> lines_;
  core::Connection sink_;
  std::vector<std::string> history_;
  size_t history_position_ = 0;  // == history_.size(): not browsing
};

}  // namespace wlw
