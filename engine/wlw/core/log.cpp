#include "wlw/core/log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <unordered_map>

#include "wlw/platform/platform.h"

namespace wlw {
namespace {

const auto g_start = std::chrono::steady_clock::now();

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

// "LogRender" -> "render" (filters match names without the prefix, any case).
std::string FilterKey(std::string_view name) {
  if (name.size() > 3 && name.substr(0, 3) == "Log") name.remove_prefix(3);
  return Lower(name);
}

// ---- Categories and filters ----
struct Categories {
  std::mutex mutex;
  std::vector<LogCategory*> all;
  LogLevel default_level = LogLevel::Info;
  std::unordered_map<std::string, LogLevel> pending;  // filters for names not registered (yet)
};
Categories& GetCategories() {
  static Categories categories;  // first use may be another file's static initialisation
  return categories;
}

// ---- Sinks, history, file ----
struct Output {
  std::mutex mutex;
  std::vector<std::pair<int, LogSink>> sinks;
  int next_sink = 0;
  bool platform = true;
  std::deque<LogRecord> history;
  FILE* file = nullptr;
};
Output& GetOutput() {
  static Output output;
  return output;
}

thread_local bool t_in_log = false;  // a sink that logs would deadlock: its records are dropped

std::string FormatV(const char* format, va_list args) {
  char buffer[1024];
  va_list copy;
  va_copy(copy, args);
  const int length = std::vsnprintf(buffer, sizeof(buffer), format, copy);
  va_end(copy);
  if (length < 0) return format;
  if (static_cast<size_t>(length) < sizeof(buffer)) return std::string(buffer, static_cast<size_t>(length));
  std::string long_text(static_cast<size_t>(length) + 1, '\0');
  std::vsnprintf(long_text.data(), long_text.size(), format, args);
  long_text.resize(static_cast<size_t>(length));
  return long_text;
}

void Write(const LogCategory& category, LogLevel level, const char* file, int line, std::string message) {
  if (t_in_log) return;
  t_in_log = true;
  LogRecord record;
  record.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
  record.thread = LogThreadId();
  record.level = level;
  record.category = category.Name();
  record.message = std::move(message);
  record.file = file;
  record.line = line;
  {
    Output& output = GetOutput();
    std::lock_guard lock(output.mutex);
    if (output.platform) platform::WriteLog(record);
    if (output.file) {
      std::fprintf(output.file, "%s\n", FormatLogRecord(record).c_str());
      std::fflush(output.file);  // every line: a crash must not lose the last ones
    }
    for (const auto& [_, sink] : output.sinks) sink(record);
    if (output.history.size() == kLogHistorySize) output.history.pop_front();
    output.history.push_back(std::move(record));
  }
  t_in_log = false;
  if (level == LogLevel::Fatal) {
    // Everything is written and flushed above: stop here (in the debugger first, if there is one).
    if (platform::IsDebuggerAttached()) platform::BreakIntoDebugger();
    std::abort();
  }
}

}  // namespace

const char* LogLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Verbose: return "verbose";
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warning: return "warning";
    case LogLevel::Error: return "error";
    case LogLevel::Fatal: return "fatal";
    case LogLevel::Off: return "off";
  }
  return "?";
}

std::optional<LogLevel> ParseLogLevel(std::string_view text) {
  const std::string lower = Lower(text);
  if (lower == "verbose" || lower == "all") return LogLevel::Verbose;
  if (lower == "debug") return LogLevel::Debug;
  if (lower == "info" || lower == "log") return LogLevel::Info;
  if (lower == "warning" || lower == "warn") return LogLevel::Warning;
  if (lower == "error") return LogLevel::Error;
  if (lower == "fatal") return LogLevel::Fatal;
  if (lower == "off" || lower == "none") return LogLevel::Off;
  return std::nullopt;
}

LogCategory::LogCategory(const char* name) : name_(name), level_(static_cast<uint8_t>(LogLevel::Info)) {
  if (std::string_view(name_).substr(0, 3) == "Log" && name_[3] != '\0') name_ += 3;  // shown without "Log"
  Categories& categories = GetCategories();
  std::lock_guard lock(categories.mutex);
  const auto pending = categories.pending.find(FilterKey(name_));
  SetLevel(pending != categories.pending.end() ? pending->second : categories.default_level);
  categories.all.push_back(this);
}

std::string FormatLogRecord(const LogRecord& record) {
  char head[96];
  std::snprintf(head, sizeof(head), "[%.3f][T%d][%s]", record.seconds, record.thread, record.category);
  std::string line = head;
  if (record.level != LogLevel::Info) {
    line += '[';
    line += LogLevelName(record.level);
    line += ']';
  }
  line += ' ';
  line += record.message;
  return line;
}

bool SetLogFilter(std::string_view filter) {
  bool ok = true;
  std::vector<std::pair<std::string, LogLevel>> parts;  // (key, level); key "*" = every category
  while (!filter.empty()) {
    const size_t comma = filter.find(',');
    std::string_view part = filter.substr(0, comma);
    filter = comma == std::string_view::npos ? std::string_view{} : filter.substr(comma + 1);
    part = Trim(part);
    if (part.empty()) continue;
    const size_t equals = part.find('=');
    const std::string_view name = equals == std::string_view::npos ? "*" : Trim(part.substr(0, equals));
    const std::string_view level_text = equals == std::string_view::npos ? part : Trim(part.substr(equals + 1));
    const std::optional<LogLevel> level = ParseLogLevel(level_text);
    if (!level || name.empty()) {
      ok = false;
      continue;
    }
    parts.emplace_back(name == "*" ? "*" : FilterKey(name), *level);
  }
  // Every-category parts first, so "Render=Verbose,*=Warning" keeps Render verbose.
  std::stable_partition(parts.begin(), parts.end(), [](const auto& part) { return part.first == "*"; });
  Categories& categories = GetCategories();
  std::lock_guard lock(categories.mutex);
  for (const auto& [key, level] : parts) {
    if (key == "*") {
      categories.default_level = level;
      categories.pending.clear();
      for (LogCategory* category : categories.all) category->SetLevel(level);
      continue;
    }
    categories.pending[key] = level;
    for (LogCategory* category : categories.all) {
      if (FilterKey(category->Name()) == key) category->SetLevel(level);
    }
  }
  return ok;
}

void SetLogLevel(LogLevel level) { SetLogFilter(LogLevelName(level)); }

std::vector<std::string> LogCategoryNames() {
  Categories& categories = GetCategories();
  std::lock_guard lock(categories.mutex);
  std::vector<std::string> names;
  for (const LogCategory* category : categories.all) names.emplace_back(category->Name());
  return names;
}

int AddLogSink(LogSink sink) {
  Output& output = GetOutput();
  std::lock_guard lock(output.mutex);
  output.sinks.emplace_back(output.next_sink, std::move(sink));
  return output.next_sink++;
}

void RemoveLogSink(int id) {
  Output& output = GetOutput();
  std::lock_guard lock(output.mutex);
  std::erase_if(output.sinks, [id](const auto& sink) { return sink.first == id; });
}

void SetPlatformLogOutput(bool on) {
  Output& output = GetOutput();
  std::lock_guard lock(output.mutex);
  output.platform = on;
}

std::vector<LogRecord> RecentLogs() {
  Output& output = GetOutput();
  std::lock_guard lock(output.mutex);
  return {output.history.begin(), output.history.end()};
}

void ClearRecentLogs() {
  Output& output = GetOutput();
  std::lock_guard lock(output.mutex);
  output.history.clear();
}

bool StartLogFile(const std::string& path) {
  StopLogFile();
  std::error_code error;
  if (std::filesystem::exists(path, error)) std::filesystem::rename(path, path + ".prev", error);
  FILE* file = std::fopen(path.c_str(), "w");
  if (file == nullptr) {
    WLW_LOG(LogCore, Warning, "Can't open the log file %s", path.c_str());
    return false;
  }
  Output& output = GetOutput();
  std::lock_guard lock(output.mutex);
  output.file = file;
  return true;
}

void StopLogFile() {
  Output& output = GetOutput();
  std::lock_guard lock(output.mutex);
  if (output.file) std::fclose(output.file);
  output.file = nullptr;
}

int LogThreadId() {
  static std::atomic<int> next{0};
  thread_local const int id = next.fetch_add(1);
  return id;
}

namespace detail {
void LogWrite(const LogCategory& category, LogLevel level, const char* file, int line, const char* format, ...) {
  va_list args;
  va_start(args, format);
  std::string message = FormatV(format, args);
  va_end(args);
  Write(category, level, file, line, std::move(message));
}
}  // namespace detail

void Log(LogLevel level, const char* format, ...) {
  if (!LogGeneral.Enabled(level)) return;
  va_list args;
  va_start(args, format);
  std::string message = FormatV(format, args);
  va_end(args);
  Write(LogGeneral, level, "", 0, std::move(message));
}

}  // namespace wlw
