#include "image_compare.h"

#include <algorithm>
#include <cstdlib>

#include <stb_image.h>
#include <stb_image_write.h>

namespace wlw::testing {

bool LoadPng(const std::string& path, Image& image) {
  int width = 0, height = 0, channels = 0;
  stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
  if (data == nullptr) return false;
  image.width = width;
  image.height = height;
  image.rgba.assign(data, data + static_cast<size_t>(width) * height * 4);
  stbi_image_free(data);
  return true;
}

bool SavePng(const std::string& path, const Image& image) {
  if (image.Empty()) return false;
  return stbi_write_png(path.c_str(), image.width, image.height, 4, image.rgba.data(), image.width * 4) != 0;
}

CompareResult CompareImages(const Image& actual, const Image& reference, const CompareSettings& settings) {
  CompareResult result;
  result.same_size = actual.width == reference.width && actual.height == reference.height && !actual.Empty();
  if (!result.same_size) {
    result.bad_fraction = 1.0;
    return result;
  }
  result.diff.width = reference.width;
  result.diff.height = reference.height;
  result.diff.rgba.resize(reference.rgba.size());
  const size_t pixels = static_cast<size_t>(reference.width) * reference.height;
  for (size_t i = 0; i < pixels; ++i) {
    const uint8_t* a = &actual.rgba[i * 4];
    const uint8_t* r = &reference.rgba[i * 4];
    int difference = 0;
    for (int c = 0; c < 4; ++c) difference = std::max(difference, std::abs(a[c] - r[c]));
    result.max_difference = std::max(result.max_difference, difference);
    uint8_t* d = &result.diff.rgba[i * 4];
    if (difference > settings.channel_tolerance) {
      ++result.bad_pixels;
      d[0] = 255, d[1] = 0, d[2] = 0;
    } else {
      const uint8_t grey = static_cast<uint8_t>(64 + (r[0] * 3 + r[1] * 6 + r[2]) / 40);  // faded luma
      d[0] = d[1] = d[2] = grey;
    }
    d[3] = 255;
  }
  result.bad_fraction = static_cast<double>(result.bad_pixels) / static_cast<double>(pixels);
  result.passed = result.bad_fraction <= settings.max_bad_fraction;
  return result;
}

}  // namespace wlw::testing
