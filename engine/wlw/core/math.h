#pragma once

// Small math functions the engine's hot loops need faster than the standard library gives them.
// (Phase 2.1 grows this into the engine's math header: glm configuration, aliases.)
#include <cmath>

namespace wlw::core {

struct SinCosResult {
  float sin;
  float cos;
};

// sin and cos of one angle (radians) together, faster than std::sin + std::cos: one range
// reduction for both, two short polynomials, no calls (fx/particles instances 5k: see CHANGELOG
// 1.6). Max absolute error 1.2e-7 against double precision over +-60 rad (std::sin's float is
// 6e-8); meant for visuals (particle rotations). Exact reduction up to |radians| ~ 1e5; past
// ~3e9 the quadrant no longer fits an int.
// Like DirectXMath's XMScalarSinCos and Unreal's FMath::SinCos.
//
// How: x = q * pi/2 + r with |r| <= pi/4 (Cody-Waite: pi/2 in 3 parts, the first with few enough
// bits that q * part is exact, so r keeps its precision), then Cephes' minimax polynomials for sin
// and cos on [-pi/4, pi/4], swapped and negated by the quadrant q mod 4.
inline SinCosResult SinCos(float radians) {
  constexpr float kTwoOverPi = 0.636619772367581343f;
  constexpr float kPiOver2A = 1.5703125f;  // 8 significant bits: q * it is exact for |q| < 2^16
  constexpr float kPiOver2B = 4.837512969970703125e-4f;
  constexpr float kPiOver2C = 7.54978995489188216e-8f;

  // The nearest quadrant: floor(x + 0.5). Not a branch on the sign (random angles mispredict it: 40%
  // slower on MSVC), not truncation fixed up below zero (int <-> float round trips: 45% slower), not
  // the (x + 1.5 * 2^23) - 1.5 * 2^23 trick (fast-math folds it away). One instruction on ARM64.
  const float qf = std::floor(radians * kTwoOverPi + 0.5f);
  const auto q = static_cast<int>(qf);
  const float r = ((radians - qf * kPiOver2A) - qf * kPiOver2B) - qf * kPiOver2C;
  const float z = r * r;

  const float s = ((-1.9515295891e-4f * z + 8.3321608736e-3f) * z - 1.6666654611e-1f) * z * r + r;
  const float c =
      ((2.443315711809948e-5f * z - 1.388731625493765e-3f) * z + 4.166664568298827e-2f) * z * z - 0.5f * z + 1.0f;

  // Quadrant q mod 4 (two's complement: right for negative q too): 0 = (s, c), 1 = (c, -s),
  // 2 = (-s, -c), 3 = (-c, s). Arithmetic, not branches or a switch (compilers branch on float
  // selects; random angles mispredict them): the swap is a blend by 0 / 1, exact.
  const auto swap = static_cast<float>(q & 1);
  const float keep = 1.0f - swap;
  const auto sin_sign = static_cast<float>(1 - (q & 2));        // -1 in quadrants 2, 3
  const auto cos_sign = static_cast<float>(1 - ((q + 1) & 2));  // -1 in quadrants 1, 2
  return {(s * keep + c * swap) * sin_sign, (c * keep + s * swap) * cos_sign};
}

}  // namespace wlw::core
