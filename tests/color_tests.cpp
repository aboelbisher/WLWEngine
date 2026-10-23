// Tests for wlw/core/color.h (Roadmap 2.1): Color's constructors, hex and text round trips, the exact sRGB curve
// (every byte value survives decode + encode), alpha and shading helpers, operators with Lerp and Tween<Color>,
// Color32 bytes, and the deprecated core::Color / PackColor shims.
#include <cstdint>
#include <string>
#include <type_traits>

#include "test_helpers.h"
#include "wlw/anim/tween.h"
#include "wlw/core/color.h"
#include "wlw/core/deprecated.h"
#include "wlw/rendering/instance_buffer.h"

TEST_SUITE_BEGIN("color");

using namespace wlw;

namespace {

bool SameColor(const Color& a, const Color& b, float tolerance = 1e-6f) {
  return Near(a.r, b.r, tolerance) && Near(a.g, b.g, tolerance) && Near(a.b, b.b, tolerance) &&
         Near(a.a, b.a, tolerance);
}

}  // namespace

TEST_CASE("constructors") {
  CHECK(Color() == Color(0.0f, 0.0f, 0.0f, 0.0f));  // like 0.2's Color{}
  CHECK(Color(0.1f, 0.2f, 0.3f).a == 1.0f);         // opaque unless told otherwise
  CHECK(Color(Vec4(1, 2, 3, 4)) == Color(1, 2, 3, 4));
  CHECK(Color(0.1f, 0.2f, 0.3f, 0.4f).ToVec4() == Vec4(0.1f, 0.2f, 0.3f, 0.4f));
  CHECK(Color(0.1f, 0.2f, 0.3f, 0.4f).Rgb() == Vec3(0.1f, 0.2f, 0.3f));
  static_assert(Color::Hex(0xFF8000) == Color(1.0f, 128.0f / 255.0f, 0.0f));  // usable in constant tables
  static_assert(colors::kWhite == Color(1, 1, 1, 1) && colors::kTransparent == Color());
  static_assert(std::is_trivially_copyable_v<Color> && std::is_standard_layout_v<Color>);
}

TEST_CASE("hex codes") {
  CHECK(Color::Hex(0x336699) == Color(0x33 / 255.0f, 0x66 / 255.0f, 0x99 / 255.0f, 1.0f));
  CHECK(Color::Hex(0x336699, 0.5f).a == 0.5f);
  CHECK(Color::HexRgba(0x33669980) == Color(0x33 / 255.0f, 0x66 / 255.0f, 0x99 / 255.0f, 0x80 / 255.0f));
  CHECK(Color::Hex(0xFFFFFF) == colors::kWhite);
}

TEST_CASE("parse and format text") {
  CHECK(Color::Parse("#336699") == Color::Hex(0x336699));
  CHECK(Color::Parse("336699") == Color::Hex(0x336699));
  CHECK(Color::Parse("#3A6b9C") == Color::Hex(0x3A6B9C));
  CHECK(Color::Parse("#369") == Color::Hex(0x336699));
  CHECK(Color::Parse("#3698") == Color::HexRgba(0x33669988));
  CHECK(Color::Parse("#33669980") == Color::HexRgba(0x33669980));
  for (const char* bad : {"", "#", "#12", "#12345", "#1234567", "#123456789", "#ggg", "red", "#12 456"}) {
    CHECK_MESSAGE(!Color::Parse(bad), bad);
  }
  CHECK(Color::Hex(0x336699).ToHex() == "#336699");
  CHECK(Color::HexRgba(0x33669980).ToHex() == "#33669980");
  CHECK(Color(2.0f, -1.0f, 0.5f).ToHex() == "#ff0080");  // clamped and rounded
  for (uint32_t v : {0x000000u, 0xFFFFFFu, 0x12AB9Fu, 0x808080u}) {
    CHECK(Color::Parse(Color::Hex(v).ToHex()) == Color::Hex(v));
  }
}

TEST_CASE("srgb curve survives every byte value") {
  for (int byte = 0; byte < 256; ++byte) {
    const float encoded = static_cast<float>(byte) / 255.0f;
    const float linear = SrgbToLinear(encoded);
    CHECK_MESSAGE(UnitToByte(LinearToSrgb(linear)) == byte, byte);
    if (byte > 0) CHECK(linear > SrgbToLinear(static_cast<float>(byte - 1) / 255.0f));  // strictly increasing
  }
  CHECK(SrgbToLinear(0.0f) == 0.0f);
  CHECK(Near(SrgbToLinear(1.0f), 1.0f, 1e-6f));
  CHECK(Near(SrgbToLinear(0.5f), 0.21404f, 1e-4f));               // the reference value
  CHECK(Near(LinearToSrgb(0.18f), 0.46135f, 1e-4f));              // 18% grey
  CHECK(Near(SrgbToLinear(0.04045f), 0.04045f / 12.92f, 1e-7f));  // the linear toe
  const Color c(0.5f, 0.25f, 1.0f, 0.3f);
  CHECK(c.ToLinear().a == 0.3f);  // alpha is never encoded
  CHECK(SameColor(Color::FromLinear(c.ToLinear()), c, 1e-5f));
}

TEST_CASE("alpha and shading") {
  const Color c(0.2f, 0.4f, 0.8f, 0.5f);
  CHECK(c.WithAlpha(0.9f) == Color(0.2f, 0.4f, 0.8f, 0.9f));  // replaces
  CHECK(c.Faded(0.5f) == Color(0.2f, 0.4f, 0.8f, 0.25f));     // multiplies
  CHECK(SameColor(c.Darkened(0.5f), Color(0.1f, 0.2f, 0.4f, 0.5f)));
  CHECK(c.Darkened(1.0f) == Color(0, 0, 0, 0.5f));
  CHECK(SameColor(c.Lightened(0.5f), Color(0.6f, 0.7f, 0.9f, 0.5f)));
  CHECK(c.Lightened(1.0f) == Color(1, 1, 1, 0.5f));
  CHECK(c.Darkened(0.0f) == c);
}

TEST_CASE("operators, lerp and tweens") {
  const Color a(0.2f, 0.4f, 0.6f, 1.0f), b(0.6f, 0.4f, 0.2f, 0.0f);
  CHECK(SameColor(a + b, Color(0.8f, 0.8f, 0.8f, 1.0f)));
  CHECK(SameColor(a - b, Color(-0.4f, 0.0f, 0.4f, 1.0f)));
  CHECK(SameColor(a * 2.0f, Color(0.4f, 0.8f, 1.2f, 2.0f)));
  CHECK(SameColor(2.0f * a, a * 2.0f));
  CHECK(SameColor(a / 2.0f, Color(0.1f, 0.2f, 0.3f, 0.5f)));
  CHECK(SameColor(a * b, Color(0.12f, 0.16f, 0.12f, 0.0f)));  // tint
  Color c = a;
  c += b;
  c *= 0.5f;
  CHECK(SameColor(c, Color(0.4f, 0.4f, 0.4f, 0.5f)));
  CHECK(a != b);

  CHECK(SameColor(Lerp(a, b, 0.5f), Color(0.4f, 0.4f, 0.4f, 0.5f)));
  anim::Tween<Color> tween(a, b, 1.0f, anim::ease::Linear);
  tween.Update(0.25f);
  CHECK(SameColor(tween.Value(), Lerp(a, b, 0.25f)));
}

TEST_CASE("color32 bytes") {
  static_assert(sizeof(Color32) == 4);
  const Color32 bytes(Color(1.0f, 0.0f, 0.5f, 0.25f));
  CHECK((bytes.r == 255 && bytes.g == 0 && bytes.b == 128 && bytes.a == 64));
  CHECK(bytes.Packed() == 0x408000FFu);  // r in the low byte
  CHECK(Color32::FromPacked(0x408000FFu) == bytes);
  CHECK(Color32(Color(2.0f, -1.0f, 0.0f, 0.0f)).Packed() == 0xFFu);  // clamped
  CHECK(Color32(10, 20, 30).a == 255);
  for (int v = 0; v < 256; ++v) {  // every byte survives ToColor and back
    const auto b8 = static_cast<uint8_t>(v);
    CHECK(Color32(Color32(b8, b8, b8, b8).ToColor()) == Color32(b8, b8, b8, b8));
  }
}

#if !WLW_NO_DEPRECATED
WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN
TEST_CASE("deprecated color shims") {
  static_assert(std::is_same_v<core::Color, Color>);
  const core::Color old{0.25f, 0.5f, 0.75f, 1.0f};
  CHECK(rendering::PackColor(old) == Color32(old).Packed());
  CHECK(rendering::UnpackColor(0x408000FFu) == Color32::FromPacked(0x408000FFu).ToColor());
  CHECK(rendering::PackUnitByte(0.5f) == 128u);
}
WLW_DISABLE_DEPRECATION_WARNINGS_END
#endif

TEST_SUITE_END();
