#pragma once

#include <array>
#include <bit>
#include <cstdint>

// A grid of W x H yes/no cells packed into 64-bit words: a "bitboard", as chess engines call it.
//
// Cell (x, y) is bit y * W + x. Questions that would loop over cells become a few word-wide
// instructions: "does this piece fit here?" is (piece & occupied).None(), "how many blocks?" is a
// popcount. Searching thousands of positions per frame (game AI, puzzle generators, cellular
// automata) becomes cheap.
//
//   using Board = BitGrid<8, 8>;          // one uint64_t
//   Board l = Board::Cell(0, 0) | Board::Cell(0, 1) | Board::Cell(1, 1);
//   bool fits = (l.Shifted(3, 2) & occupied).None();
namespace wlw::math {

template <int W, int H>
class BitGrid {
 public:
  static_assert(W > 0 && H > 0, "BitGrid needs at least one cell");
  static constexpr int kWidth = W;
  static constexpr int kHeight = H;
  static constexpr int kCells = W * H;
  static constexpr int kWords = (kCells + 63) / 64;

  constexpr BitGrid() = default;

  static constexpr BitGrid Cell(int x, int y) {
    BitGrid grid;
    grid.Set(x, y);
    return grid;
  }
  static constexpr BitGrid Full() { return ~BitGrid(); }
  static constexpr BitGrid Row(int y, int length = W) { return Rect(0, y, length, 1); }
  static constexpr BitGrid Column(int x, int length = H) { return Rect(x, 0, 1, length); }
  static constexpr BitGrid Rect(int x, int y, int width, int height) {
    BitGrid grid;
    for (int j = y; j < y + height; ++j) {
      for (int i = x; i < x + width; ++i) grid.Set(i, j);
    }
    return grid;
  }

  static constexpr bool InBounds(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }
  constexpr bool Test(int x, int y) const {
    if (!InBounds(x, y)) return false;
    const int bit = y * W + x;
    return (words_[bit / 64] >> (bit % 64)) & 1u;
  }
  constexpr void Set(int x, int y, bool value = true) {
    if (!InBounds(x, y)) return;
    const int bit = y * W + x;
    const uint64_t mask = uint64_t{1} << (bit % 64);
    words_[bit / 64] = value ? (words_[bit / 64] | mask) : (words_[bit / 64] & ~mask);
  }

  constexpr int Count() const {
    int count = 0;
    for (uint64_t word : words_) count += std::popcount(word);
    return count;
  }
  constexpr bool Any() const {
    for (uint64_t word : words_) {
      if (word != 0) return true;
    }
    return false;
  }
  constexpr bool None() const { return !Any(); }
  // Every cell of `other` is set here.
  constexpr bool Contains(const BitGrid& other) const { return (*this & other) == other; }

  // Moved by (dx, dy) cells. Bits pushed past the right or left edge do NOT wrap into the next
  // row: they're dropped, like everything pushed off the top or bottom. Cell by cell: meant for
  // precomputing masks (e.g. a piece at every position), not for inner loops.
  constexpr BitGrid Shifted(int dx, int dy) const {
    BitGrid result;
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) {
        if (Test(x, y)) result.Set(x + dx, y + dy);
      }
    }
    return result;
  }

  constexpr BitGrid operator&(const BitGrid& o) const {
    return Combine(o, [](uint64_t a, uint64_t b) { return a & b; });
  }
  constexpr BitGrid operator|(const BitGrid& o) const {
    return Combine(o, [](uint64_t a, uint64_t b) { return a | b; });
  }
  constexpr BitGrid operator^(const BitGrid& o) const {
    return Combine(o, [](uint64_t a, uint64_t b) { return a ^ b; });
  }
  constexpr BitGrid operator~() const {
    BitGrid result;
    for (int i = 0; i < kWords; ++i) result.words_[i] = ~words_[i];
    result.ClearPadding();
    return result;
  }
  constexpr BitGrid& operator&=(const BitGrid& o) { return *this = *this & o; }
  constexpr BitGrid& operator|=(const BitGrid& o) { return *this = *this | o; }
  constexpr BitGrid& operator^=(const BitGrid& o) { return *this = *this ^ o; }
  constexpr bool operator==(const BitGrid&) const = default;

  // For hashing (e.g. a transposition table) and saving.
  constexpr const std::array<uint64_t, kWords>& Words() const { return words_; }

 private:
  template <typename Op>
  constexpr BitGrid Combine(const BitGrid& o, Op op) const {
    BitGrid result;
    for (int i = 0; i < kWords; ++i) result.words_[i] = op(words_[i], o.words_[i]);
    return result;
  }
  // The bits past the last cell in the last word stay 0, so Count/Any/== only see real cells.
  constexpr void ClearPadding() {
    constexpr int used = kCells % 64;
    if constexpr (used != 0) words_[kWords - 1] &= (uint64_t{1} << used) - 1;
  }

  std::array<uint64_t, kWords> words_{};
};

}  // namespace wlw::math
