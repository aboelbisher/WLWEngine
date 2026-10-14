#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

// The order a viewport's meshes are drawn in, decided by ONE 64-bit key per draw and one sort
// (Christer Ericson's "Order your graphics draw calls around!", the scheme the big engines use):
//
//   opaque       0 | program (16) | texture (16) | depth, near first (31)
//   transparent  1 | depth, far first (31) | -  | program (16)
//
// so opaque draws come before transparent ones; opaque ones are grouped by program, then by
// texture (fewer GPU state changes) and go front to back within a group (early-Z skips hidden
// pixels); transparent ones go back to front, as blending needs. No GPU here: tested on the CPU.
namespace wlw::rendering {

// A view depth's position in the order: for depths >= 0 the IEEE-754 bits of a float grow with
// its value, so they sort like it. Negative depths (behind the near plane) and NaN count as 0.
inline uint32_t DepthBits(float depth) {
  if (!(depth > 0.0f)) return 0;
  uint32_t bits = 0;
  std::memcpy(&bits, &depth, sizeof(bits));
  return bits & 0x7FFFFFFFu;  // 31 bits: the sign bit is 0 anyway
}

// `program`, `texture`: ids to group by (only the low 16 bits are used; equal ids group, and a
// collision only costs a state change, never a wrong picture). `depth`: distance in front of the
// camera along its view direction.
inline uint64_t OpaqueSortKey(uint32_t program, uint32_t texture, float depth) {
  return (static_cast<uint64_t>(program & 0xFFFFu) << 47) | (static_cast<uint64_t>(texture & 0xFFFFu) << 31) |
         DepthBits(depth);
}

inline uint64_t TransparentSortKey(float depth, uint32_t program) {
  return (uint64_t{1} << 63) | (static_cast<uint64_t>(0x7FFFFFFFu - DepthBits(depth)) << 32) | (program & 0xFFFFu);
}

inline bool IsTransparentKey(uint64_t key) { return (key >> 63) != 0; }

// A draw's place in the order: its key and its index in the viewport's list of draws.
struct DrawOrder {
  uint64_t key;
  uint32_t item;
};

// Sorts by key; equal keys keep the order they were listed in (the same order every frame).
inline void SortDrawOrder(std::vector<DrawOrder>& order) {
  std::sort(order.begin(), order.end(),
            [](const DrawOrder& a, const DrawOrder& b) { return a.key != b.key ? a.key < b.key : a.item < b.item; });
}

}  // namespace wlw::rendering
