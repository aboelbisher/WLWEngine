// The main() of the golden-image tests (wlw_gpu_tests): doctest plus one flag of our own,
// --update, which rewrites the reference pictures instead of comparing with them (golden.h).
//   wlw_gpu_tests                      compare every scene
//   wlw_gpu_tests -tc="shadows"        one scene
//   wlw_gpu_tests --update             new references from this GPU (use llvmpipe: see golden.h)
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
    } else {
      args.push_back(argv[i]);
    }
  }
  doctest::Context context(static_cast<int>(args.size()), args.data());
  return context.run();
}
