#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "wlw/kit/save_store.h"

// Player progress that can live on several devices (a phone and a tablet, a reinstall, later a
// cloud copy) and be combined without losing anything.
//
// Every type here has Merge(other): the result is what you'd get if one player had done both
// sets of things. Merging is commutative, associative and idempotent (a.Merge(b) == b.Merge(a),
// merging twice changes nothing), so it doesn't matter which copy syncs first or how often.
// That's the idea behind CRDTs ("conflict-free replicated data types"): instead of asking the
// player "keep this device or that one?", pick data shapes whose merge is obvious:
//   a best value   -> max          a set of things done -> union
//   a counter      -> one count per device, total = sum (a plain max or sum would be wrong)
// Per-device things (settings, the game in progress) are NOT merged: keep them in another store.
namespace wlw::kit {

// A random id for this installation, created on first use and kept in `device_store`
// (a per-device store, never synced). Counters and score entries remember which device made them.
std::string DeviceId(SaveStore& device_store);

// Named counters that merge correctly (a "grow-only counter" per name).
// Each device only ever increases its own count, so merging takes the max per device, and the
// total is the sum over devices. Two devices that each played 5 games offline merge to 10.
class Counters {
 public:
  void Add(const std::string& device, const std::string& name, int64_t amount = 1);
  int64_t Get(const std::string& name) const;  // total over all devices
  void Merge(const Counters& other);

  Json ToJson() const;
  static Counters FromJson(const Json& json);
  bool operator==(const Counters&) const = default;

 private:
  std::map<std::string, std::map<std::string, int64_t>> counts_;  // name -> device -> count
};

// A local leaderboard: the best `capacity` scores, with when and where they were made.
class Scores {
 public:
  struct Entry {
    int64_t score = 0;
    int date = 0;      // local date, yyyymmdd (for showing)
    int64_t time = 0;  // when the game started, seconds since 1970 (UTC): with `device`, the game's id
    std::string device;
    bool operator==(const Entry&) const = default;
  };

  explicit Scores(int capacity = 10) : capacity_(capacity) {}

  // Records a finished game. Returns its place in the table (0 = new best), or -1 if it didn't
  // make it. Submitting the same game (time + device) again keeps only its best score.
  int Submit(const Entry& entry);
  int64_t Best() const { return entries_.empty() ? 0 : entries_.front().score; }
  const std::vector<Entry>& Entries() const { return entries_; }  // best first
  void Merge(const Scores& other);                                // union, best `capacity` kept

  Json ToJson() const;
  static Scores FromJson(const Json& json, int capacity = 10);
  bool operator==(const Scores&) const = default;

 private:
  int Insert(const Entry& entry);  // keeps entries_ sorted and unique

  int capacity_;
  std::vector<Entry> entries_;
};

// Stars per level (0-3 or whatever the game uses); a level is completed once it has an entry.
// Which levels are unlocked is the game's rule (e.g. "the next one after each completed level"),
// computed from this, so it never needs merging itself.
class LevelProgress {
 public:
  // Records a completion; keeps the best stars ever. Returns true if that improved anything.
  bool Complete(const std::string& level, int stars);
  bool Completed(const std::string& level) const { return stars_.count(level) != 0; }
  int Stars(const std::string& level) const;  // 0 if never completed
  int TotalStars() const;
  int CompletedCount() const { return static_cast<int>(stars_.size()); }
  void Merge(const LevelProgress& other);  // max per level

  Json ToJson() const;
  static LevelProgress FromJson(const Json& json);
  bool operator==(const LevelProgress&) const = default;

 private:
  std::map<std::string, int> stars_;
};

}  // namespace wlw::kit
