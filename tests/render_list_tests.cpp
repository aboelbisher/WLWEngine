// Tests for the draw order (rendering/render_list.h): the 64-bit sort keys and the sort. CPU only:
// the renderer builds these keys per mesh and draws in the order this produces.
#include <cmath>
#include <limits>
#include <vector>

#include "test_helpers.h"
#include "wlw/rendering/render_list.h"

TEST_SUITE_BEGIN("render_list");

using namespace wlw::rendering;

namespace {

// Sorts the keys and returns the original indices in draw order.
std::vector<uint32_t> Order(const std::vector<uint64_t>& keys) {
  std::vector<DrawOrder> order;
  for (uint32_t i = 0; i < keys.size(); ++i) order.push_back({keys[i], i});
  SortDrawOrder(order);
  std::vector<uint32_t> items;
  for (const DrawOrder& entry : order) items.push_back(entry.item);
  return items;
}

}  // namespace

TEST_CASE("depth bits") {
  // Monotonic: a farther depth never sorts before a nearer one.
  const float depths[] = {0.0f, 1e-6f, 0.001f, 0.5f, 1.0f, 1.5f, 10.0f, 1000.0f, 1e9f};
  for (size_t i = 1; i < std::size(depths); ++i) CHECK(DepthBits(depths[i - 1]) < DepthBits(depths[i]));
  // Behind the near plane, and NaN: like 0.
  CHECK((DepthBits(-3.0f) == 0 && DepthBits(std::numeric_limits<float>::quiet_NaN()) == 0));
  CHECK(DepthBits(std::numeric_limits<float>::infinity()) <= 0x7FFFFFFFu);
}

TEST_CASE("opaque before transparent") {
  // However near a transparent draw is and however far an opaque one is.
  CHECK(OpaqueSortKey(0xFFFF, 0xFFFF, 1e9f) < TransparentSortKey(0.0f, 0));
  CHECK((!IsTransparentKey(OpaqueSortKey(3, 4, 5.0f)) && IsTransparentKey(TransparentSortKey(5.0f, 3))));
}

TEST_CASE("opaque groups then front to back") {
  const std::vector<uint64_t> keys = {
      OpaqueSortKey(2, 7, 1.0f),  // 0: program 2
      OpaqueSortKey(1, 9, 5.0f),  // 1: program 1, texture 9, far
      OpaqueSortKey(1, 3, 8.0f),  // 2: program 1, texture 3, farther
      OpaqueSortKey(1, 9, 2.0f),  // 3: program 1, texture 9, near
      OpaqueSortKey(1, 3, 0.5f),  // 4: program 1, texture 3, nearest
  };
  // Program 1 first (by program, not by depth), its texture 3 group before 9, near before far
  // inside each group; then program 2.
  CHECK((Order(keys) == std::vector<uint32_t>{4, 2, 3, 1, 0}));
}

TEST_CASE("transparent back to front") {
  const std::vector<uint64_t> keys = {
      TransparentSortKey(2.0f, 1),
      TransparentSortKey(9.0f, 5),
      TransparentSortKey(0.1f, 0),
      TransparentSortKey(9.0f, 2),  // same depth as 1: the program decides, then the list order
  };
  // Farthest first whatever the program: blending needs what's behind drawn first.
  CHECK((Order(keys) == std::vector<uint32_t>{3, 1, 0, 2}));
}

TEST_CASE("ties keep list order") {
  // Equal keys (the same mesh drawn by several nodes at the same depth) keep their list order,
  // the same every frame: no flicker between equal draws.
  const uint64_t key = OpaqueSortKey(1, 1, 3.0f);
  CHECK((Order({key, key, key}) == std::vector<uint32_t>{0, 1, 2}));
}

TEST_CASE("ids use low16 bits") {
  // Ids are grouped by their low 16 bits only; higher bits must not spill into other fields.
  CHECK(OpaqueSortKey(0x10001, 0, 1.0f) == OpaqueSortKey(1, 0, 1.0f));
  CHECK(OpaqueSortKey(0, 0x10002, 1.0f) == OpaqueSortKey(0, 2, 1.0f));
  CHECK(!IsTransparentKey(OpaqueSortKey(0xFFFFFFFFu, 0xFFFFFFFFu, 1e30f)));
}

TEST_SUITE_END();
