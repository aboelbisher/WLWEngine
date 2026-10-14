#pragma once

#include <span>
#include <string>
#include <string_view>

// Shader files by name: the engine's own (engine/wlw/rendering/shaders/**, embedded in the binary
// at build time by cmake/EmbedShaders.cmake, so nothing is read from disk or the APK), and any a
// game adds for its own shaders to #include.
//
//   engine:  "mesh.vert", "ui.frag", "wlw/frame.glsl", "wlw/lighting.glsl", "wlw/shadows.glsl",
//            "wlw/common.glsl", "wlw/color.glsl", "custom/..." (what wraps a game's shader)
//   game:    AddShaderFile("water/waves.glsl", text) once at start, then #include it
namespace wlw::rendering {

struct EmbeddedShaderFile {
  const char* name;  // relative to engine/wlw/rendering/shaders, '/' separated
  const char* source;
};

// The engine's files (generated source).
std::span<const EmbeddedShaderFile> EmbeddedShaderFiles();

// A shader file by name: a game's (AddShaderFile) first, then the engine's; null if there's none.
// The text lives as long as the program.
const std::string* FindShaderFile(std::string_view name);

// Adds (or replaces) a file a game's shaders can #include. Game thread, before compiling the
// shaders that use it.
void AddShaderFile(std::string name, std::string source);

}  // namespace wlw::rendering
