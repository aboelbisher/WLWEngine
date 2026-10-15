#include "wlw/rendering/glsl_header.h"

#include <regex>

namespace wlw::rendering {

std::string GlslHeader(GlslDialect dialect, ShaderStage /*stage*/) {
  if (dialect == GlslDialect::Glsl330) return "#version 330 core\n";
  return "#version 300 es\n"
         "precision highp float;\n"
         "precision highp int;\n"
         "precision mediump sampler2D;\n"
         "precision mediump sampler3D;\n"
         "precision mediump samplerCube;\n"
         "precision mediump sampler2DArray;\n"
         "precision highp sampler2DShadow;\n"
         "precision highp samplerCubeShadow;\n"
         "precision highp sampler2DArrayShadow;\n"
         "precision highp isampler2D;\n"
         "precision highp isampler3D;\n"
         "precision highp isamplerCube;\n"
         "precision highp isampler2DArray;\n"
         "precision highp usampler2D;\n"
         "precision highp usampler3D;\n"
         "precision highp usamplerCube;\n"
         "precision highp usampler2DArray;\n";
}

std::string ApplyMediumpSetting(std::string code, bool allow_mediump) {
  if (allow_mediump) return code;
  static const std::regex opt_in(R"(\bprecision\s+mediump\s+float\s*;)");
  return std::regex_replace(code, opt_in, "precision highp float;");
}

}  // namespace wlw::rendering
