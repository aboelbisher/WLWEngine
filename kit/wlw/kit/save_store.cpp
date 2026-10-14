#include "wlw/kit/save_store.h"

#include <filesystem>
#include <optional>

#include "wlw/core/log.h"
#include "wlw/kit/kit_log.h"
#include "wlw/platform/platform.h"

namespace wlw::kit {

namespace {

// Reads and parses a save file: {"version": n, "data": {...}}. Nothing on any problem.
std::optional<Json> ReadDocument(const std::string& path) {
  const std::optional<std::vector<uint8_t>> bytes = platform::ReadLocalFile(path);
  if (!bytes || bytes->empty()) return std::nullopt;
  Json document = Json::parse(bytes->begin(), bytes->end(), nullptr, /*allow_exceptions=*/false);
  if (document.is_discarded() || !document.is_object() || !document.contains("data") || !document["data"].is_object()) {
    return std::nullopt;
  }
  return document;
}

std::vector<uint8_t> ToBytes(const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); }

}  // namespace

SaveStore::SaveStore(std::string folder, std::string name, int version)
    : path_(folder.empty() ? name + ".json" : folder + "/" + name + ".json"), version_(version) {}

SaveStore SaveStore::ForApp(const std::string& app, const std::string& name, int version) {
  return SaveStore(platform::GetSaveDirectory(app), name, version);
}

void SaveStore::AddMigration(int from_version, Migration migration) {
  migrations_[from_version] = std::move(migration);
}

SaveStore::LoadResult SaveStore::Load() {
  LoadResult result = LoadResult::Loaded;
  std::optional<Json> document = ReadDocument(path_);
  if (!document) {
    document = ReadDocument(path_ + ".bak");
    result = document ? LoadResult::FromBackup : LoadResult::Fresh;
  }
  dirty_ = false;
  if (!document) {
    data_ = Json::object();
    return result;
  }
  data_ = std::move((*document)["data"]);
  // Upgrade old formats one version at a time.
  int file_version = document->value("version", 1);
  if (file_version > version_) {
    WLW_LOG(LogKit, Warning, "%s is from a newer version (%d > %d); reading it as is", path_.c_str(), file_version,
            version_);
  }
  for (; file_version < version_; ++file_version) {
    if (auto it = migrations_.find(file_version); it != migrations_.end()) it->second(data_);
    dirty_ = true;  // save the upgraded format
  }
  if (result == LoadResult::FromBackup) dirty_ = true;  // repair the main file at the next save
  return result;
}

bool SaveStore::Save() {
  Json document = Json::object();
  document["version"] = version_;
  document["data"] = data_;
  // Keep the last good file as a backup before replacing it (a failed write leaves it intact).
  if (const std::optional<std::vector<uint8_t>> previous = platform::ReadLocalFile(path_)) {
    platform::WriteFileAtomic(path_ + ".bak", *previous);
  }
  if (!platform::WriteFileAtomic(path_, ToBytes(document.dump(2)))) {
    WLW_LOG(LogKit, Error, "Couldn't save %s", path_.c_str());
    return false;
  }
  dirty_ = false;
  return true;
}

bool SaveStore::SaveIfDirty() { return dirty_ ? Save() : true; }

void SaveStore::Wipe() {
  std::error_code error;
  std::filesystem::remove(std::filesystem::path(std::u8string(path_.begin(), path_.end())), error);
  const std::string backup = path_ + ".bak";
  std::filesystem::remove(std::filesystem::path(std::u8string(backup.begin(), backup.end())), error);
  data_ = Json::object();
  dirty_ = false;
}

}  // namespace wlw::kit
