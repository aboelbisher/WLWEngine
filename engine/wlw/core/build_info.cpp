#include "wlw/core/build_info.h"

#include "wlw/core/git_info.h"
#include "wlw/version.h"

namespace wlw::core {

const BuildInfo& GetBuildInfo() {
  static const BuildInfo info{
      .version = WLW_VERSION_STRING,
      .major = WLW_VERSION_MAJOR,
      .minor = WLW_VERSION_MINOR,
      .patch = WLW_VERSION_PATCH,
      .git_commit = detail::kGitCommit,
      .git_dirty = detail::kGitDirty,
      .build_type = detail::kBuildType,
  };
  return info;
}

std::string BuildInfoText() {
  const BuildInfo& info = GetBuildInfo();
  std::string text = "WLWengine ";
  text += info.version;
  text += " (";
  text += info.git_commit;
  if (info.git_dirty) text += "-dirty";
  text += ", ";
  text += info.build_type;
  text += ")";
  return text;
}

}  // namespace wlw::core
