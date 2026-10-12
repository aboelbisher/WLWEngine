#include "wlw/rendering/shader_library.h"

#include <map>
#include <memory>

namespace wlw::rendering {

namespace {

// name -> text. The engine's are copied in once; std::map nodes never move, so pointers handed
// out stay valid (a replaced game file keeps its node and gets new text).
std::map<std::string, std::string, std::less<>>& Files() {
  static std::map<std::string, std::string, std::less<>> files = [] {
    std::map<std::string, std::string, std::less<>> engine;
    for (const EmbeddedShaderFile& file : EmbeddedShaderFiles()) engine.emplace(file.name, file.source);
    return engine;
  }();
  return files;
}

}  // namespace

const std::string* FindShaderFile(std::string_view name) {
  const auto& files = Files();
  const auto it = files.find(name);
  return it == files.end() ? nullptr : &it->second;
}

void AddShaderFile(std::string name, std::string source) { Files()[std::move(name)] = std::move(source); }

}  // namespace wlw::rendering
