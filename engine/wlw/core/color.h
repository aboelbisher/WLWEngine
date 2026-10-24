#pragma once

// Colours: Color (4 floats, for maths and the engine's APIs) and Color32 (4 bytes, for GPU data and files).
//
// What a Color holds: the values as authored, like Unity's and Godot's Color: a picker's or a hex code's
// numbers, which are sRGB-encoded. The renderer shades in gamma space today, so they are used as they are;
// once it shades in linear space (Roadmap 13.1, D8) it converts them when they reach the GPU. Maths that must
// be physically right (adding light, averaging colours exactly) goes through ToLinear() / FromLinear().
// Channels are not clamped: light colours times intensity and HDR values go past 1.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "wlw/core/math.h"

namespace wlw {

// The exact sRGB transfer functions (IEC 61966-2-1), one channel in 0..1.
float SrgbToLinear(float encoded);
float LinearToSrgb(float linear);

struct Color {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 0.0f;

  constexpr Color() = default;  // transparent black (0, 0, 0, 0)
  constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
  constexpr explicit Color(const Vec4& v) : r(v.x), g(v.y), b(v.z), a(v.w) {}

  // 0xRRGGBB, the way colours are written in design tools and CSS (each byte / 255, no conversion).
  static constexpr Color Hex(uint32_t rgb, float alpha = 1.0f) {
    return {Byte(rgb >> 16), Byte(rgb >> 8), Byte(rgb), alpha};
  }
  // 0xRRGGBBAA.
  static constexpr Color HexRgba(uint32_t rgba) {
    return {Byte(rgba >> 24), Byte(rgba >> 16), Byte(rgba >> 8), Byte(rgba)};
  }
  // "#rgb", "#rgba", "#rrggbb" or "#rrggbbaa" (the '#' is optional; either case); nothing for anything else.
  static std::optional<Color> Parse(std::string_view text);
  // "#rrggbb", or "#rrggbbaa" when alpha isn't 1 (lower case; each channel clamped and rounded to a byte).
  std::string ToHex() const;

  // The same colour with alpha `alpha` (replaced).
  constexpr Color WithAlpha(float alpha) const { return {r, g, b, alpha}; }
  // Alpha multiplied by `k` (fading out: k = 1 unchanged, 0 invisible).
  constexpr Color Faded(float k) const { return {r, g, b, a * k}; }
  // Towards black by `k` (0 unchanged, 1 black); alpha kept.
  constexpr Color Darkened(float k) const { return {r * (1.0f - k), g * (1.0f - k), b * (1.0f - k), a}; }
  // Towards white by `k` (0 unchanged, 1 white); alpha kept.
  constexpr Color Lightened(float k) const { return {r + (1.0f - r) * k, g + (1.0f - g) * k, b + (1.0f - b) * k, a}; }

  // This colour's sRGB values decoded to linear light (alpha unchanged), and back.
  Color ToLinear() const { return {SrgbToLinear(r), SrgbToLinear(g), SrgbToLinear(b), a}; }
  static Color FromLinear(const Color& linear) {
    return {LinearToSrgb(linear.r), LinearToSrgb(linear.g), LinearToSrgb(linear.b), linear.a};
  }

  constexpr Vec4 ToVec4() const { return {r, g, b, a}; }
  constexpr Vec3 Rgb() const { return {r, g, b}; }

  // Channel by channel (alpha too), so Lerp, Tween<Color> and ExpDecay work on colours.
  constexpr Color operator+(const Color& o) const { return {r + o.r, g + o.g, b + o.b, a + o.a}; }
  constexpr Color operator-(const Color& o) const { return {r - o.r, g - o.g, b - o.b, a - o.a}; }
  constexpr Color operator*(const Color& o) const { return {r * o.r, g * o.g, b * o.b, a * o.a}; }  // tinting
  constexpr Color operator*(float k) const { return {r * k, g * k, b * k, a * k}; }
  constexpr Color operator/(float k) const { return {r / k, g / k, b / k, a / k}; }
  constexpr Color& operator+=(const Color& o) { return *this = *this + o; }
  constexpr Color& operator-=(const Color& o) { return *this = *this - o; }
  constexpr Color& operator*=(const Color& o) { return *this = *this * o; }
  constexpr Color& operator*=(float k) { return *this = *this * k; }
  constexpr bool operator==(const Color&) const = default;

 private:
  static constexpr float Byte(uint32_t v) { return static_cast<float>(v & 0xFFu) / 255.0f; }
};
constexpr Color operator*(float k, const Color& c) { return c * k; }
static_assert(sizeof(Color) == 16);

namespace colors {
inline constexpr Color kTransparent{0.0f, 0.0f, 0.0f, 0.0f};
inline constexpr Color kBlack{0.0f, 0.0f, 0.0f};
inline constexpr Color kWhite{1.0f, 1.0f, 1.0f};
inline constexpr Color kGray{0.5f, 0.5f, 0.5f};
inline constexpr Color kRed{1.0f, 0.0f, 0.0f};
inline constexpr Color kGreen{0.0f, 1.0f, 0.0f};
inline constexpr Color kBlue{0.0f, 0.0f, 1.0f};
inline constexpr Color kYellow{1.0f, 1.0f, 0.0f};
inline constexpr Color kCyan{0.0f, 1.0f, 1.0f};
inline constexpr Color kMagenta{1.0f, 0.0f, 1.0f};
}  // namespace colors

// One channel 0..1 as a byte: clamped, rounded to the nearest (halves up), NaN -> 0. Not std::lround: the
// same bytes for every float in 0..1 (checked exhaustively) at a fraction of the cost (it's in the particle
// loop).
inline uint8_t UnitToByte(float v) {
  const float x = (v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f) * 255.0f;
  const auto whole = static_cast<uint32_t>(x);
  return static_cast<uint8_t>(whole + (x - static_cast<float>(whole) >= 0.5f ? 1u : 0u));
}

// A colour as 4 bytes r, g, b, a in that order in memory (RGBA8): what the GPU reads for instance tints and
// sprite colours (Unity's Color32, Unreal's FColor).
struct Color32 {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 0;

  constexpr Color32() = default;
  constexpr Color32(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255) : r(r_), g(g_), b(b_), a(a_) {}
  explicit Color32(const Color& c) : r(UnitToByte(c.r)), g(UnitToByte(c.g)), b(UnitToByte(c.b)), a(UnitToByte(c.a)) {}

  // The 4 bytes as one little-endian word: r in the low byte (the layout of rendering::Instance::color).
  constexpr uint32_t Packed() const {
    return uint32_t{r} | (uint32_t{g} << 8) | (uint32_t{b} << 16) | (uint32_t{a} << 24);
  }
  static constexpr Color32 FromPacked(uint32_t packed) {
    return {static_cast<uint8_t>(packed), static_cast<uint8_t>(packed >> 8), static_cast<uint8_t>(packed >> 16),
            static_cast<uint8_t>(packed >> 24)};
  }
  constexpr Color ToColor() const {
    return {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f,
            static_cast<float>(a) / 255.0f};
  }
  constexpr bool operator==(const Color32&) const = default;
};
static_assert(sizeof(Color32) == 4);

}  // namespace wlw
