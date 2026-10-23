#pragma once

#include <memory>
#include <string>
#include <vector>
#include <array>
#include "wlw/core/math.h"

namespace wlw::utils {

struct RawImage {
  Vec2 size;
  int channels;
  std::vector<unsigned char> pixels;
};

class ImageLoader {
 public:
  static std::unique_ptr<RawImage> LoadImage(const std::string& file_path);
};
}  // namespace wlw::utils