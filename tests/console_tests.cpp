// Tests for wlw/core/console.h: splitting lines, running commands and cvars, the built-in commands,
// completion, and the console model's history and lines.
#include <string>
#include <vector>

#include "test_helpers.h"
#include "wlw/core/console.h"

TEST_SUITE_BEGIN("console");

using namespace wlw;

namespace {

// Collects what the console logs, without console output.
struct Output {
  std::vector<LogRecord> records;
  int sink = AddLogSink([this](const LogRecord& record) { records.push_back(record); });
  Output() { SetPlatformLogOutput(false); }
  ~Output() {
    RemoveLogSink(sink);
    SetPlatformLogOutput(true);
  }
  bool Contains(const std::string& text) const {
    for (const LogRecord& record : records) {
      if (record.message.find(text) != std::string::npos) return true;
    }
    return false;
  }
};

}  // namespace

TEST_CASE("lines split into words, quotes kept together") {
  CHECK(SplitConsoleLine("  set  r.a   1 ") == std::vector<std::string>{"set", "r.a", "1"});
  CHECK(SplitConsoleLine("say \"hello there\" now") == std::vector<std::string>{"say", "hello there", "now"});
  CHECK(SplitConsoleLine("").empty());
}

TEST_CASE("a line sets or shows a cvar") {
  Output output;
  static CVar<int> lives{"test.console.lives", 3, "Lives left"};
  CHECK(ExecuteConsoleLine("test.console.lives 5"));
  CHECK(lives.Get() == 5);
  CHECK(ExecuteConsoleLine("test.console.lives = 6"));
  CHECK(lives.Get() == 6);
  CHECK(ExecuteConsoleLine("TEST.CONSOLE.LIVES=7"));
  CHECK(lives.Get() == 7);
  CHECK(lives.LastSource() == CVarSource::Console);
  CHECK(ExecuteConsoleLine("test.console.lives"));  // shows it
  CHECK(output.Contains("test.console.lives = 7"));
  CHECK(output.Contains("Lives left"));
  CHECK_FALSE(ExecuteConsoleLine("test.console.lives many"));
  CHECK(lives.Get() == 7);
  CHECK_FALSE(ExecuteConsoleLine("no.such.thing"));
  CHECK(ExecuteConsoleLine("   "));  // nothing to do
}

TEST_CASE("string cvars take the rest of the line") {
  Output output;
  static CVar<std::string> greeting{"test.console.greeting", "", "Greeting"};
  CHECK(ExecuteConsoleLine("test.console.greeting hello big world"));
  CHECK(greeting.Get() == "hello big world");
}

TEST_CASE("commands run with their arguments and unregister when destroyed") {
  Output output;
  std::vector<std::string> got;
  {
    ConsoleCommand command{"test.echo", "Repeats its words", [&got](const ConsoleArgs& args) { got = args; }};
    CHECK(FindConsoleCommand("TEST.ECHO") == &command);
    CHECK(ExecuteConsoleLine("test.echo a \"b c\""));
    CHECK(got == std::vector<std::string>{"a", "b c"});
  }
  CHECK(FindConsoleCommand("test.echo") == nullptr);
  CHECK_FALSE(ExecuteConsoleLine("test.echo again"));
}

TEST_CASE("built-in commands: set, reset, help, find") {
  Output output;
  static CVar<float> zoom{"test.builtin.zoom", 1.0f, "Camera zoom"};
  CHECK(ExecuteConsoleLine("set test.builtin.zoom 2"));
  CHECK(zoom.Get() == 2.0f);
  CHECK(ExecuteConsoleLine("reset test.builtin.zoom"));
  CHECK(zoom.Get() == 1.0f);
  CHECK(ExecuteConsoleLine("help"));
  CHECK(output.Contains("find"));
  CHECK(ExecuteConsoleLine("help test.builtin.zoom"));
  CHECK(output.Contains("Camera zoom"));
  output.records.clear();
  CHECK(ExecuteConsoleLine("find camera"));
  CHECK(output.Contains("test.builtin.zoom"));
}

TEST_CASE("names complete from any case") {
  static CVar<int> alpha{"test.complete.alpha", 0, "A"};
  static CVar<int> also{"test.complete.also", 0, "A too"};
  static CVar<int> beta{"test.complete.beta", 0, "B"};
  CHECK(CompleteConsoleName("TEST.COMPLETE.A") ==
        std::vector<std::string>{"test.complete.alpha", "test.complete.also"});
  ConsoleModel model;
  ConsoleModel::Completion one = model.Complete("test.complete.b");
  CHECK(one.text == "test.complete.beta ");
  CHECK(one.candidates.empty());
  ConsoleModel::Completion two = model.Complete("test.complete.a");
  CHECK(two.text == "test.complete.al");  // as far as both agree
  CHECK(two.candidates.size() == 2);
  CHECK(model.Complete("test.complete.zzz").text == "test.complete.zzz");
  CHECK(model.Complete("set test.complete.a").text == "set test.complete.a");  // only the first word
}

TEST_CASE("the history walks back and forth") {
  Output output;
  ConsoleModel model;
  model.Submit("help");
  model.Submit("find a");
  model.Submit("find a");  // the same twice: kept once
  model.Submit("   ");     // empty: not kept
  CHECK(model.History() == std::vector<std::string>{"help", "find a"});
  CHECK(model.HistoryOlder() == "find a");
  CHECK(model.HistoryOlder() == "help");
  CHECK_FALSE(model.HistoryOlder().has_value());  // nothing older
  CHECK(model.HistoryNewer() == "find a");
  CHECK(model.HistoryNewer() == "");  // back to an empty line
  CHECK_FALSE(model.HistoryNewer().has_value());
}

TEST_CASE("the model keeps the log's lines up to its capacity") {
  Output output;
  ConsoleModel model(3);
  model.Clear();
  for (int i = 0; i < 5; ++i) WLW_LOG_INFO("line %d", i);
  std::vector<std::string> lines;
  model.ForEachLine([&lines](const LogRecord& record) { lines.push_back(record.message); });
  CHECK(lines == std::vector<std::string>{"line 2", "line 3", "line 4"});
  model.Submit("help");
  CHECK(output.Contains("> help"));  // what was typed is echoed
}

TEST_SUITE_END();
