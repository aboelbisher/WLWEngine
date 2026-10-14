// Tests for the golden-image comparison the GPU tests use (tests/support/image_compare.h).
#include <cstdio>
#include <string>

#include "support/image_compare.h"
#include "test_helpers.h"

TEST_SUITE_BEGIN("image compare");

using namespace wlw::testing;

namespace {

Image Solid(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  Image image{width, height, {}};
  for (int i = 0; i < width * height; ++i) image.rgba.insert(image.rgba.end(), {r, g, b, a});
  return image;
}

void SetPixel(Image& image, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  uint8_t* p = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
  p[0] = r, p[1] = g, p[2] = b;
}

}  // namespace

TEST_CASE("identical images match") {
  const Image image = Solid(8, 4, 10, 20, 30);
  const CompareResult result = CompareImages(image, image, {});
  CHECK(result.same_size);
  CHECK(result.passed);
  CHECK(result.bad_pixels == 0);
  CHECK(result.max_difference == 0);
  CHECK((result.diff.width == 8 && result.diff.height == 4));
}

TEST_CASE("differences within the channel tolerance are not counted") {
  Image actual = Solid(4, 4, 100, 100, 100);
  SetPixel(actual, 1, 1, 102, 98, 100);  // 2 off: at the tolerance
  const CompareResult result = CompareImages(actual, Solid(4, 4, 100, 100, 100), {.channel_tolerance = 2});
  CHECK(result.bad_pixels == 0);
  CHECK(result.max_difference == 2);
  CHECK(result.passed);
}

TEST_CASE("alpha counts like the colour channels") {
  const CompareResult result = CompareImages(Solid(2, 2, 0, 0, 0, 0), Solid(2, 2, 0, 0, 0, 255), {});
  CHECK(result.bad_pixels == 4);
  CHECK_FALSE(result.passed);
}

TEST_CASE("a few bad pixels pass up to the allowed fraction") {
  const Image reference = Solid(10, 10, 0, 0, 0);
  Image actual = reference;
  SetPixel(actual, 3, 7, 255, 0, 0);  // 1 of 100 pixels
  CHECK(CompareImages(actual, reference, {.channel_tolerance = 2, .max_bad_fraction = 0.01}).passed);
  const CompareResult strict = CompareImages(actual, reference, {.channel_tolerance = 2, .max_bad_fraction = 0.005});
  CHECK_FALSE(strict.passed);
  CHECK(Near(static_cast<float>(strict.bad_fraction), 0.01f));
  CHECK(strict.max_difference == 255);
}

TEST_CASE("the diff image marks bad pixels red and fades the rest") {
  const Image reference = Solid(3, 1, 255, 255, 255);
  Image actual = reference;
  SetPixel(actual, 2, 0, 0, 0, 0);
  const CompareResult result = CompareImages(actual, reference, {});
  const uint8_t* bad = result.diff.Pixel(2, 0);
  CHECK((bad[0] == 255 && bad[1] == 0 && bad[2] == 0 && bad[3] == 255));
  const uint8_t* good = result.diff.Pixel(0, 0);
  CHECK((good[0] == good[1] && good[1] == good[2] && good[0] < 200));  // grey, darker than the white reference
}

TEST_CASE("different sizes never match") {
  const CompareResult result = CompareImages(Solid(4, 4, 0, 0, 0), Solid(4, 5, 0, 0, 0), {.max_bad_fraction = 1.0});
  CHECK_FALSE(result.same_size);
  CHECK_FALSE(result.passed);
  CHECK(result.diff.Empty());
  CHECK_FALSE(CompareImages(Image{}, Image{}, {}).passed);  // nothing drawn is never a match
}

TEST_CASE("png files round trip") {
  Image image = Solid(5, 3, 1, 2, 3, 4);
  SetPixel(image, 4, 2, 200, 100, 50);
  const std::string path = "image_compare_round_trip.png";  // only this case uses it
  REQUIRE(SavePng(path, image));
  Image loaded;
  REQUIRE(LoadPng(path, loaded));
  std::remove(path.c_str());
  CHECK((loaded.width == 5 && loaded.height == 3));
  CHECK(loaded.rgba == image.rgba);
  CHECK_FALSE(LoadPng("no_such_file.png", loaded));
}

TEST_SUITE_END();
