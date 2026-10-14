#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Golden-image comparison for the GPU tests (tests/gpu), plain CPU code so its rules are unit-tested
// in wlw_tests (image_compare_tests.cpp).
namespace wlw::testing {

// An RGBA8 picture, top row first.
struct Image {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba;  // width x height x 4

  bool Empty() const { return rgba.empty(); }
  const uint8_t* Pixel(int x, int y) const { return &rgba[(static_cast<size_t>(y) * width + x) * 4]; }
};

// PNG files (stb_image / stb_image_write, compiled in the engine). Load: false if missing or unreadable.
bool LoadPng(const std::string& path, Image& image);
bool SavePng(const std::string& path, const Image& image);

struct CompareSettings {
  int channel_tolerance = 2;        // a pixel differs when any channel differs by more than this (0..255)
  double max_bad_fraction = 0.001;  // the images match while at most this share of pixels differ
};

struct CompareResult {
  bool same_size = false;
  int bad_pixels = 0;         // pixels with a channel beyond the tolerance
  int max_difference = 0;     // the largest channel difference anywhere (0..255)
  double bad_fraction = 0.0;  // bad_pixels / all pixels (1 when the sizes differ)
  bool passed = false;
  // The reference faded to grey with the bad pixels in red, to see where they are (empty when the
  // sizes differ).
  Image diff;
};

CompareResult CompareImages(const Image& actual, const Image& reference, const CompareSettings& settings);

}  // namespace wlw::testing
