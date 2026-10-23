// Tests for wlw/core/log.h and wlw/core/assert.h: levels, categories, filters, sinks, history, the log
// file, thread ids, compile-time stripping, WLW_VERIFY / WLW_ASSERT / WLW_UNREACHABLE.
// Log state is global: every case restores what it changed (Quiet), and makes its categories static
// (registered categories must outlive the program's logging).
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "test_helpers.h"
#include "wlw/core/assert.h"
#include "wlw/core/log.h"

TEST_SUITE_BEGIN("log");

using namespace wlw;

namespace {

// For one case: no console output, records collected, every level back to Info afterwards.
struct Quiet {
  std::vector<LogRecord> records;
  core::Connection sink = AddLogSink([this](const LogRecord& record) { records.push_back(record); });
  Quiet() { SetPlatformLogOutput(false); }
  ~Quiet() {
    sink.Disconnect();
    SetLogLevel(LogLevel::Info);
    SetPlatformLogOutput(true);
  }
};

std::string ReadText(const std::string& path) {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("log levels parse by name") {
  CHECK(ParseLogLevel("verbose") == LogLevel::Verbose);
  CHECK(ParseLogLevel("Warning") == LogLevel::Warning);
  CHECK(ParseLogLevel("WARN") == LogLevel::Warning);
  CHECK(ParseLogLevel("error") == LogLevel::Error);
  CHECK(ParseLogLevel("off") == LogLevel::Off);
  CHECK_FALSE(ParseLogLevel("loud").has_value());
  CHECK(std::string(LogLevelName(LogLevel::Debug)) == "debug");
}

TEST_CASE("categories drop the Log prefix and start at Info") {
  static LogCategory LogTestStart{"LogTestStart"};
  static LogCategory Plain{"Plain"};
  CHECK(std::string(LogTestStart.Name()) == "TestStart");
  CHECK(std::string(Plain.Name()) == "Plain");
  CHECK(LogTestStart.Level() == LogLevel::Info);
  CHECK(LogTestStart.Enabled(LogLevel::Warning));
  CHECK_FALSE(LogTestStart.Enabled(LogLevel::Debug));
  const std::vector<std::string> names = LogCategoryNames();
  CHECK(std::find(names.begin(), names.end(), "Render") != names.end());  // the engine's own
}

TEST_CASE("filters set levels per category and for all") {
  Quiet quiet;
  CHECK(SetLogFilter("Render=Verbose, audio = error"));
  CHECK(LogRender.Level() == LogLevel::Verbose);
  CHECK(LogAudio.Level() == LogLevel::Error);  // names match without "Log", any case
  CHECK(LogUI.Level() == LogLevel::Info);

  // "*" applies first, whatever the order written.
  CHECK(SetLogFilter("Shader=Debug,*=Warning"));
  CHECK(LogShader.Level() == LogLevel::Debug);
  CHECK(LogRender.Level() == LogLevel::Warning);

  CHECK(SetLogFilter("Error"));  // a bare level = every category
  CHECK(LogShader.Level() == LogLevel::Error);
}

TEST_CASE("bad filter parts are reported and the rest still applies") {
  Quiet quiet;
  CHECK_FALSE(SetLogFilter("Render=Loud,Audio=Debug,=Info"));
  CHECK(LogRender.Level() == LogLevel::Info);
  CHECK(LogAudio.Level() == LogLevel::Debug);
}

TEST_CASE("a filter applies to a category registered later") {
  Quiet quiet;
  SetLogFilter("LateTest=Verbose");
  static LogCategory LogLateTest{"LogLateTest"};
  CHECK(LogLateTest.Level() == LogLevel::Verbose);
}

TEST_CASE("sinks get the records the filters let through") {
  Quiet quiet;
  static LogCategory LogSinkTest{"LogSinkTest"};
  WLW_LOG(LogSinkTest, Info, "hello %d", 42);
  WLW_LOG(LogSinkTest, Debug, "filtered out at Info");
  const int line = __LINE__ - 2;
  REQUIRE(quiet.records.size() == 1);
  const LogRecord& record = quiet.records[0];
  CHECK(record.message == "hello 42");
  CHECK(std::string(record.category) == "SinkTest");
  CHECK(record.level == LogLevel::Info);
  CHECK(record.line == line);
  CHECK(std::string(record.file).find("log_tests.cpp") != std::string::npos);
  CHECK(record.thread == LogThreadId());

  SetLogFilter("SinkTest=Off");
  WLW_LOG(LogSinkTest, Error, "off means off");
  CHECK(quiet.records.size() == 1);
}

TEST_CASE("a removed sink gets nothing") {
  Quiet quiet;
  int calls = 0;
  core::Connection connection = AddLogSink([&calls](const LogRecord&) { ++calls; });
  WLW_LOG_INFO("one");
  CHECK(connection.Connected());
  connection.Disconnect();
  CHECK(!connection.Connected());
  WLW_LOG_INFO("two");
  connection.Disconnect();  // twice: nothing happens
  CHECK(calls == 1);
  CHECK(quiet.records.size() == 2);
}

TEST_CASE("levels below the compile level aren't evaluated") {
  Quiet quiet;
  SetLogLevel(LogLevel::Verbose);  // on at runtime: only the compile level can stop it
  int evaluated = 0;
  auto count = [&evaluated] { return ++evaluated; };
  WLW_LOG(LogCore, Verbose, "%d", count());  // WLW_LOG_COMPILE_LEVEL is Debug or Info
  CHECK(evaluated == 0);
  CHECK(quiet.records.empty());
  WLW_LOG(LogCore, Warning, "%d", count());
  CHECK(evaluated == 1);
}

TEST_CASE("records format with time, thread, category and level") {
  LogRecord record;
  record.seconds = 1.5;
  record.thread = 2;
  record.category = "Render";
  record.level = LogLevel::Warning;
  record.message = "slow";
  CHECK(FormatLogRecord(record) == "[1.500][T2][Render][warning] slow");
  record.level = LogLevel::Info;
  CHECK(FormatLogRecord(record) == "[1.500][T2][Render] slow");
}

TEST_CASE("long messages are kept whole") {
  Quiet quiet;
  const std::string text(5000, 'x');
  WLW_LOG_INFO("%s!", text.c_str());
  REQUIRE(quiet.records.size() == 1);
  CHECK(quiet.records[0].message.size() == 5001);
}

TEST_CASE("the history keeps the latest records") {
  Quiet quiet;
  ClearRecentLogs();
  for (size_t i = 0; i < kLogHistorySize + 10; ++i) WLW_LOG_INFO("record %zu", i);
  const std::vector<LogRecord> recent = RecentLogs();
  REQUIRE(recent.size() == kLogHistorySize);
  CHECK(recent.front().message == "record 10");
  CHECK(recent.back().message == "record " + std::to_string(kLogHistorySize + 9));
}

TEST_CASE("threads get their own ids") {
  Quiet quiet;
  int other = -1;
  std::thread worker([&other] {
    other = LogThreadId();
    WLW_LOG_INFO("from a worker");
  });
  worker.join();
  CHECK(other != LogThreadId());
  REQUIRE(quiet.records.size() == 1);
  CHECK(quiet.records[0].thread == other);
}

TEST_CASE("the log file keeps the run before") {
  Quiet quiet;
  const std::string path = "log_tests_file.txt";  // only this case uses it
  std::filesystem::remove(path);
  std::filesystem::remove(path + ".prev");
  REQUIRE(StartLogFile(path));
  WLW_LOG(LogCore, Warning, "first run");
  StopLogFile();
  CHECK(ReadText(path).find("[Core][warning] first run") != std::string::npos);

  REQUIRE(StartLogFile(path));
  WLW_LOG_INFO("second run");
  StopLogFile();
  CHECK(ReadText(path).find("second run") != std::string::npos);
  CHECK(ReadText(path).find("first run") == std::string::npos);
  CHECK(ReadText(path + ".prev").find("first run") != std::string::npos);
  std::filesystem::remove(path);
  std::filesystem::remove(path + ".prev");
}

TEST_CASE("WLW_VERIFY returns the condition and logs failures") {
  Quiet quiet;
  int evaluated = 0;
  CHECK(WLW_VERIFY(++evaluated == 1, "not logged"));
  CHECK(evaluated == 1);  // evaluated once
  CHECK(quiet.records.empty());

  const char* name = "missing.png";
  CHECK_FALSE(WLW_VERIFY(evaluated == 2, "can't read %s", name));
  REQUIRE(quiet.records.size() == 1);
  CHECK(quiet.records[0].level == LogLevel::Error);
  CHECK(std::string(quiet.records[0].category) == "Core");
  CHECK(quiet.records[0].message.find("can't read missing.png") != std::string::npos);
  CHECK(quiet.records[0].message.find("WLW_VERIFY(evaluated == 2)") != std::string::npos);

  CHECK_FALSE(WLW_VERIFY(false));  // no message
  CHECK(quiet.records.size() == 2);
}

TEST_CASE("asserts go to the handler") {
  Quiet quiet;
  std::vector<AssertFailure> failures;
  const AssertHandler previous = SetAssertHandler([&failures](const AssertFailure& failure) {
    failures.push_back(failure);
    throw std::runtime_error("assert");
  });
  int evaluated = 0;
#if WLW_ASSERTS_ENABLED
  CHECK_THROWS([&] { WLW_ASSERT(++evaluated == 0, "count is %d", evaluated); }());
  REQUIRE(failures.size() == 1);
  CHECK(std::string(failures[0].kind) == "WLW_ASSERT");
  CHECK(std::string(failures[0].expression) == "++evaluated == 0");
  CHECK(failures[0].message == "count is 1");
  WLW_ASSERT(evaluated == 1);  // passes: nothing happens
  CHECK(failures.size() == 1);
#else
  WLW_ASSERT(++evaluated == 0);  // compiled out: not even evaluated
  CHECK(evaluated == 0);
#endif
  CHECK_THROWS([] { WLW_UNREACHABLE("mode %d", 7); }());
  CHECK(std::string(failures.back().kind) == "WLW_UNREACHABLE");
  CHECK(failures.back().message == "mode 7");
  SetAssertHandler(previous);
}

TEST_SUITE_END();
