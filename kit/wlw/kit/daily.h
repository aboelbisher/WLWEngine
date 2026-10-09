#pragma once

#include <cstdint>
#include <set>

#include "wlw/kit/save_store.h"

// Calendar dates and "daily" features (a daily puzzle, a login streak).
//
// A date is stored as one int, yyyymmdd (20261009): readable in a save file, sortable, and a
// handy seed for "the same puzzle for everyone today". Arithmetic goes through a day number
// (days since 1970-01-01), so month ends, leap years and new year just work.
namespace wlw::kit {

struct Date {
  int year = 1970;
  int month = 1;  // 1-12
  int day = 1;    // 1-31

  int ToNumber() const { return year * 10000 + month * 100 + day; }
  static Date FromNumber(int yyyymmdd) { return {yyyymmdd / 10000, yyyymmdd / 100 % 100, yyyymmdd % 100}; }

  int64_t DayNumber() const;                // days since 1970-01-01 (negative before)
  static Date FromDayNumber(int64_t days);  // the reverse
  Date AddDays(int64_t days) const { return FromDayNumber(DayNumber() + days); }
  bool operator==(const Date&) const = default;
};

// The device's local date: a "daily" thing changes at the player's midnight, not at UTC's.
Date Today();
// Seconds since 1970 (UTC), for timestamps.
int64_t NowSeconds();

// Which days something was done (daily puzzle solved, game played...), with streaks.
// Mergeable: the union of the days (see progress.h).
class DailyLog {
 public:
  bool Mark(int date);  // yyyymmdd; true if it wasn't marked yet
  bool Marked(int date) const { return days_.count(date) != 0; }
  int Count() const { return static_cast<int>(days_.size()); }

  // Consecutive marked days ending today, or ending yesterday (today isn't over: the streak is
  // still alive until midnight). 0 if neither is marked.
  int CurrentStreak(int today) const;
  int BestStreak() const;
  void Merge(const DailyLog& other) { days_.insert(other.days_.begin(), other.days_.end()); }

  Json ToJson() const { return Json(days_); }
  static DailyLog FromJson(const Json& json);
  bool operator==(const DailyLog&) const = default;

 private:
  std::set<int> days_;  // yyyymmdd
};

}  // namespace wlw::kit
