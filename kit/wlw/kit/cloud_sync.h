#pragma once

#include <functional>
#include <optional>
#include <string>

// A place on a server where a save can be kept, so progress follows the player to a new phone.
//
// Only the interface for now. Implementations come with the native SDK bridge: Google Play Games
// "Saved Games" on Android, iCloud / Game Center on iOS, and a fake (a local folder) on desktop.
//
// How a game uses it: keep the synced data in its own SaveStore, made of mergeable types
// (progress.h, daily.h). When signed in, Download -> merge the remote copy into the local one ->
// Upload the result. Because merging can't lose anything, there is never a "which copy do you
// want to keep?" question, and it doesn't matter if both devices sync at the same time.
// Calls are asynchronous (network); callbacks run on the game thread, during a later frame.
namespace wlw::kit {

class CloudSync {
 public:
  virtual ~CloudSync() = default;

  // Signed in and online enough to try. Games show a "sync" button or status from this.
  virtual bool Available() const = 0;

  // The save named `slot` (e.g. "progress"): its JSON text, or nothing if there is none yet or
  // it couldn't be fetched (`error` says which).
  using DownloadDone = std::function<void(std::optional<std::string> json, bool error)>;
  virtual void Download(const std::string& slot, DownloadDone done) = 0;

  using UploadDone = std::function<void(bool ok)>;
  virtual void Upload(const std::string& slot, const std::string& json, UploadDone done) = 0;
};

}  // namespace wlw::kit
