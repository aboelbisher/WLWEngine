// The program's launch arguments (platform.h): stored by the entry point, read by the Application.
// Shared by every platform (no OS code).
#include "wlw/platform/platform.h"

namespace wlw::platform {
namespace {
std::vector<std::string>& Arguments() {
  static std::vector<std::string> arguments;
  return arguments;
}
}  // namespace

void SetLaunchArguments(std::vector<std::string> arguments) { Arguments() = std::move(arguments); }

const std::vector<std::string>& LaunchArguments() { return Arguments(); }

}  // namespace wlw::platform
