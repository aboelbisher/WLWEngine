#pragma once

#include <functional>
#include <map>
#include <string>

#include "json.hpp"  // nlohmann::json (vendored in third_party/tinygltf)

namespace wlw::kit {

using Json = nlohmann::json;

// One JSON document, saved as a file in the app's save folder.
//
// Saving is crash-safe: the previous file is kept as "<name>.json.bak", and the new one is
// written atomically (platform::WriteFileAtomic). Loading falls back to the backup, then to an
// empty document, if a file is missing or unreadable - a bad file never crashes the game.
//
// Every file records the format version. When a game update changes the format, register a
// migration for each old version: Load() runs them in order, so old saves keep working.
//
//   SaveStore store = SaveStore::ForApp("Bluzzle", "progress", 1);
//   store.Load();
//   store.Data()["best"] = 1200;  store.MarkDirty();
//   store.SaveIfDirty();          // e.g. when the app goes to the background
class SaveStore {
 public:
  enum class LoadResult {
    Loaded,      // the file was read
    FromBackup,  // the file was missing or broken; the backup was read
    Fresh,       // nothing usable: an empty document
  };
  using Migration = std::function<void(Json& data)>;  // upgrades the data by one version

  // `folder`: where the files go (games use ForApp; tests use a temporary folder).
  SaveStore(std::string folder, std::string name, int version);
  // In the platform's save folder for `app` (see platform::GetSaveDirectory).
  static SaveStore ForApp(const std::string& app, const std::string& name, int version);

  void AddMigration(int from_version, Migration migration);
  LoadResult Load();

  Json& Data() { return data_; }  // always an object
  const Json& Data() const { return data_; }
  void MarkDirty() { dirty_ = true; }
  bool Dirty() const { return dirty_; }

  bool Save();         // writes now (and clears the dirty flag on success)
  bool SaveIfDirty();  // writes only if something changed since the last save
  void Wipe();         // deletes the files and empties the document (debug tools, "reset progress")

  const std::string& Path() const { return path_; }
  int Version() const { return version_; }

 private:
  std::string path_;
  int version_;
  Json data_ = Json::object();
  bool dirty_ = false;
  std::map<int, Migration> migrations_;
};

}  // namespace wlw::kit
