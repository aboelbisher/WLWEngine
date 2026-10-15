// The main() of the golden-image tests (wlw_gpu_tests): doctest plus one flag of our own,
// --update, which rewrites the reference pictures instead of comparing with them (golden.h), and
// --dump-shaders <dir>, which writes every shader stage the compile tests build (shader_compile_tests.cpp).
//   wlw_gpu_tests                      compare every scene
//   wlw_gpu_tests -tc="shadows"        one scene
//   wlw_gpu_tests --update             new references from this GPU (use llvmpipe: see golden.h)
//   wlw_gpu_tests -ts=shader_compile --dump-shaders out   the final GLSL / GLSL ES sources, e.g. for malioc
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <cstring>
#include <vector>

#include "golden.h"

int main(int argc, char** argv) {
  std::vector<char*> args;
  for (int i = 0; i < argc; ++i) {
    if (std::strcmp(argv[i], "--update") == 0) {
      wlw::gpu_tests::SetUpdateReferences(true);
    } else if (std::strcmp(argv[i], "--dump-shaders") == 0 && i + 1 < argc) {
      wlw::gpu_tests::SetShaderDumpDirectory(argv[++i]);
    } else {
      args.push_back(argv[i]);
    }
  }
  doctest::Context context(static_cast<int>(args.size()), args.data());
  return context.run();
}
