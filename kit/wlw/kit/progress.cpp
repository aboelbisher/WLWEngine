#include "wlw/kit/progress.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

#include "wlw/core/random.h"

namespace wlw::kit {

std::string DeviceId(SaveStore& device_store) {
  Json& data = device_store.Data();
  if (data.contains("device_id") && data["device_id"].is_string()) return data["device_id"].get<std::string>();
  // 64 random bits as hex: random_device where it works, mixed with the clock where it doesn't.
  const uint64_t seed = (static_cast<uint64_t>(std::random_device{}()) << 32) ^
                        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
                        static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
  core::Random random(seed);
  char id[17];
  std::snprintf(id, sizeof(id), "%08x%08x", random.Next(), random.Next());
  data["device_id"] = id;
  device_store.MarkDirty();
  return id;
}

// ---- Counters ----

void Counters::Add(const std::string& device, const std::string& name, int64_t amount) {
  if (amount > 0) counts_[name][device] += amount;  // grow-only: a decrease couldn't be merged
}

int64_t Counters::Get(const std::string& name) const {
  const auto it = counts_.find(name);
  if (it == counts_.end()) return 0;
  int64_t total = 0;
  for (const auto& [device, count] : it->second) total += count;
  return total;
}

void Counters::Merge(const Counters& other) {
  for (const auto& [name, devices] : other.counts_) {
    for (const auto& [device, count] : devices) {
      int64_t& mine = counts_[name][device];
      mine = std::max(mine, count);
    }
  }
}

Json Counters::ToJson() const {
  Json json = Json::object();
  for (const auto& [name, devices] : counts_) json[name] = devices;
  return json;
}

Counters Counters::FromJson(const Json& json) {
  Counters counters;
  if (!json.is_object()) return counters;
  for (const auto& [name, devices] : json.items()) {
    if (!devices.is_object()) continue;
    for (const auto& [device, count] : devices.items()) {
      if (count.is_number_integer() && count.get<int64_t>() > 0) counters.counts_[name][device] = count.get<int64_t>();
    }
  }
  return counters;
}

// ---- Scores ----

namespace {
// Best first; ties by older game first (the first to reach a score keeps the higher place),
// then by device so the order is the same on every device.
bool Better(const Scores::Entry& a, const Scores::Entry& b) {
  if (a.score != b.score) return a.score > b.score;
  if (a.time != b.time) return a.time < b.time;
  return a.device < b.device;
}
}  // namespace

int Scores::Insert(const Entry& entry) {
  // The same game (time + device) is listed once, with its higher score: it may come back from a
  // copy of this save, or be submitted again after a "continue".
  const auto same = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const Entry& e) { return e.time == entry.time && e.device == entry.device; });
  if (same != entries_.end()) {
    if (same->score >= entry.score) return -1;
    entries_.erase(same);
  }
  const auto at = std::lower_bound(entries_.begin(), entries_.end(), entry, Better);
  const int place = static_cast<int>(at - entries_.begin());
  if (place >= capacity_) return -1;
  entries_.insert(at, entry);
  if (static_cast<int>(entries_.size()) > capacity_) entries_.resize(static_cast<size_t>(capacity_));
  return place;
}

int Scores::Submit(const Entry& entry) { return entry.score > 0 ? Insert(entry) : -1; }

void Scores::Merge(const Scores& other) {
  for (const Entry& entry : other.entries_) Insert(entry);
}

Json Scores::ToJson() const {
  Json list = Json::array();
  for (const Entry& entry : entries_) {
    list.push_back({{"score", entry.score}, {"date", entry.date}, {"time", entry.time}, {"device", entry.device}});
  }
  return list;
}

Scores Scores::FromJson(const Json& json, int capacity) {
  Scores scores(capacity);
  if (!json.is_array()) return scores;
  for (const Json& item : json) {
    if (!item.is_object()) continue;
    scores.Insert({item.value("score", int64_t{0}), item.value("date", 0), item.value("time", int64_t{0}),
                   item.value("device", std::string())});
  }
  return scores;
}

// ---- LevelProgress ----

bool LevelProgress::Complete(const std::string& level, int stars) {
  stars = std::max(stars, 0);
  const auto it = stars_.find(level);
  if (it == stars_.end()) {
    stars_[level] = stars;
    return true;
  }
  if (stars <= it->second) return false;
  it->second = stars;
  return true;
}

int LevelProgress::Stars(const std::string& level) const {
  const auto it = stars_.find(level);
  return it == stars_.end() ? 0 : it->second;
}

int LevelProgress::TotalStars() const {
  int total = 0;
  for (const auto& [level, stars] : stars_) total += stars;
  return total;
}

void LevelProgress::Merge(const LevelProgress& other) {
  for (const auto& [level, stars] : other.stars_) Complete(level, stars);
}

Json LevelProgress::ToJson() const { return Json(stars_); }

LevelProgress LevelProgress::FromJson(const Json& json) {
  LevelProgress progress;
  if (!json.is_object()) return progress;
  for (const auto& [level, stars] : json.items()) {
    if (stars.is_number_integer()) progress.Complete(level, stars.get<int>());
  }
  return progress;
}

}  // namespace wlw::kit
