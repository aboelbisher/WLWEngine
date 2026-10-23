#include "wlw/core/color.h"

#include <cmath>

namespace wlw {

float SrgbToLinear(float encoded) {
  return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float linear) {
  return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

namespace {

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

std::optional<Color> Color::Parse(std::string_view text) {
  if (!text.empty() && text.front() == '#') text.remove_prefix(1);
  const size_t n = text.size();
  if (n != 3 && n != 4 && n != 6 && n != 8) return std::nullopt;
  uint32_t value = 0;
  for (char c : text) {
    const int digit = HexDigit(c);
    if (digit < 0) return std::nullopt;
    value = (value << 4) | static_cast<uint32_t>(digit);
  }
  if (n <= 4) {  // one digit per channel: #rgb(a) = #rrggbb(aa)
    uint32_t wide = 0;
    for (size_t i = 0; i < n; ++i) {
      const uint32_t digit = (value >> (4 * (n - 1 - i))) & 0xFu;
      wide = (wide << 8) | (digit * 0x11u);
    }
    value = wide;
  }
  return n == 3 || n == 6 ? Hex(value) : HexRgba(value);
}

std::string Color::ToHex() const {
  const Color32 bytes(*this);
  constexpr char kDigits[] = "0123456789abcdef";
  std::string out = "#";
  auto put = [&](uint8_t v) {
    out += kDigits[v >> 4];
    out += kDigits[v & 0xF];
  };
  put(bytes.r);
  put(bytes.g);
  put(bytes.b);
  if (bytes.a != 255) put(bytes.a);
  return out;
}

}  // namespace wlw
