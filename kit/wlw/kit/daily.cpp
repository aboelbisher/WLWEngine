#include "wlw/kit/daily.h"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace wlw::kit {

// Howard Hinnant's days_from_civil / civil_from_days: exact for the proleptic Gregorian calendar.
// The trick is to start the year in March, so the leap day is the last day of the "year".
int64_t Date::DayNumber() const {
  const int64_t y = year - (month <= 2 ? 1 : 0);
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t year_of_era = y - era * 400;                                         // [0, 399]
  const int64_t day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;  // [0, 365]
  const int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + day_of_era - 719468;
}

Date Date::FromDayNumber(int64_t days) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const int64_t day_of_era = days - era * 146097;
  const int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
  const int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const int64_t mp = (5 * day_of_year + 2) / 153;
  const int day = static_cast<int>(day_of_year - (153 * mp + 2) / 5 + 1);
  const int month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  const int year = static_cast<int>(year_of_era + era * 400 + (month <= 2 ? 1 : 0));
  return {year, month, day};
}

Date Today() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  return {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday};
}

int64_t NowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

bool DailyLog::Mark(int date) { return days_.insert(date).second; }

int DailyLog::CurrentStreak(int today) const {
  Date day = Date::FromNumber(today);
  if (!Marked(day.ToNumber())) day = day.AddDays(-1);
  int streak = 0;
  while (Marked(day.ToNumber())) {
    ++streak;
    day = day.AddDays(-1);
  }
  return streak;
}

int DailyLog::BestStreak() const {
  int best = 0, run = 0;
  int64_t previous = 0;
  for (const int date : days_) {  // sorted: yyyymmdd orders like the calendar
    const int64_t day = Date::FromNumber(date).DayNumber();
    run = (run > 0 && day == previous + 1) ? run + 1 : 1;
    best = std::max(best, run);
    previous = day;
  }
  return best;
}

DailyLog DailyLog::FromJson(const Json& json) {
  DailyLog log;
  if (!json.is_array()) return log;
  for (const Json& date : json) {
    if (date.is_number_integer()) log.Mark(date.get<int>());
  }
  return log;
}

}  // namespace wlw::kit
