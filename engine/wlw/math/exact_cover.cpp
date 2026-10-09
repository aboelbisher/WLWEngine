#include "wlw/math/exact_cover.h"

#include "wlw/core/random.h"

#include <algorithm>
#include <numeric>

namespace wlw::math {

namespace {

// The dancing-links structure for one search. Nodes are indices into parallel arrays:
//   0              the root (its left/right ring holds the columns not yet covered)
//   1..columns     column headers (their up/down ring holds that column's remaining rows)
//   columns+1..    one node per 1 in the table, in a left/right ring per row
struct Links {
  std::vector<int> left, right, up, down, column, row;
  std::vector<int> size;  // per column: how many rows can still cover it

  int AddNode(int col, int row_id) {
    const int n = static_cast<int>(left.size());
    left.push_back(n);
    right.push_back(n);
    // Append at the bottom of the column's ring.
    up.push_back(up[col]);
    down.push_back(col);
    down[up[col]] = n;
    up[col] = n;
    column.push_back(col);
    row.push_back(row_id);
    ++size[col];
    return n;
  }

  // Take column c out of play: unlink its header, and every row that has a 1 in c from all the
  // *other* columns it touches (those rows clash with whichever row covers c).
  void Cover(int c) {
    right[left[c]] = right[c];
    left[right[c]] = left[c];
    for (int i = down[c]; i != c; i = down[i]) {
      for (int j = right[i]; j != i; j = right[j]) {
        down[up[j]] = down[j];
        up[down[j]] = up[j];
        --size[column[j]];
      }
    }
  }

  // The exact reverse, in reverse order. Each unlinked node kept its own up/down/left/right, so
  // pointing its neighbours back at it is all it takes: the links "dance" back in.
  void Uncover(int c) {
    for (int i = up[c]; i != c; i = up[i]) {
      for (int j = left[i]; j != i; j = left[j]) {
        ++size[column[j]];
        down[up[j]] = j;
        up[down[j]] = j;
      }
    }
    right[left[c]] = c;
    left[right[c]] = c;
  }
};

struct Search {
  Links& links;
  int64_t max_solutions;
  int64_t node_limit;
  ExactCover::Result& result;
  std::vector<int> partial;  // rows chosen so far

  bool Done() const {
    return (max_solutions > 0 && result.count >= max_solutions) || (node_limit > 0 && result.nodes >= node_limit);
  }

  void Run() {
    if (links.right[0] == 0) {  // no columns left uncovered: a solution
      result.solutions.push_back(partial);
      ++result.count;
      return;
    }
    ++result.nodes;
    if (node_limit > 0 && result.nodes >= node_limit) {
      result.complete = false;
      return;
    }
    // Knuth's heuristic: branch on the column with the fewest remaining rows. If one has none,
    // this branch is dead; if one has a single row, that row is forced.
    int best = links.right[0];
    for (int c = links.right[best]; c != 0; c = links.right[c]) {
      if (links.size[c] < links.size[best]) best = c;
    }
    if (links.size[best] == 0) return;

    links.Cover(best);
    for (int r = links.down[best]; r != best && !Done(); r = links.down[r]) {
      partial.push_back(links.row[r]);
      for (int j = links.right[r]; j != r; j = links.right[j]) links.Cover(links.column[j]);
      Run();
      for (int j = links.left[r]; j != r; j = links.left[j]) links.Uncover(links.column[j]);
      partial.pop_back();
    }
    links.Uncover(best);
  }
};

}  // namespace

ExactCover::ExactCover(int columns) : columns_(std::max(columns, 0)) {}

int ExactCover::AddRow(const std::vector<int>& columns) {
  std::vector<int> row;
  for (int c : columns) {
    if (c >= 0 && c < columns_) row.push_back(c);
  }
  std::sort(row.begin(), row.end());
  row.erase(std::unique(row.begin(), row.end()), row.end());
  rows_.push_back(std::move(row));
  return static_cast<int>(rows_.size()) - 1;
}

ExactCover::Result ExactCover::Solve(int64_t max_solutions, uint32_t shuffle_seed, const std::vector<int>& chosen,
                                     int64_t node_limit) const {
  Result result;
  // Build the links: root + column headers in one ring, then each row.
  Links links;
  const int header_count = columns_ + 1;
  links.left.resize(header_count);
  links.right.resize(header_count);
  links.up.resize(header_count);
  links.down.resize(header_count);
  links.column.resize(header_count);
  links.row.assign(header_count, -1);
  links.size.assign(header_count, 0);
  for (int i = 0; i < header_count; ++i) {
    links.left[i] = (i + header_count - 1) % header_count;
    links.right[i] = (i + 1) % header_count;
    links.up[i] = links.down[i] = links.column[i] = i;
  }

  // Row order decides which solution is found first. Shuffled with the engine's deterministic
  // generator, so the same seed gives the same order on every platform.
  std::vector<int> order(rows_.size());
  std::iota(order.begin(), order.end(), 0);
  if (shuffle_seed != 0) {
    core::Random random(shuffle_seed);
    for (size_t i = order.size(); i > 1; --i) {  // Fisher-Yates
      std::swap(order[i - 1], order[random.Below(static_cast<int>(i))]);
    }
  }
  std::vector<int> first_node(rows_.size(), -1);
  for (int r : order) {
    int first = -1;
    for (int c : rows_[r]) {
      const int n = links.AddNode(c + 1, r);  // column c's header is node c + 1
      if (first < 0) {
        first = n;
      } else {  // insert at the end of the row's left/right ring
        links.left[n] = links.left[first];
        links.right[n] = first;
        links.right[links.left[first]] = n;
        links.left[first] = n;
      }
    }
    first_node[r] = first;
  }

  // Rows given in advance: cover their columns as if the search had chosen them. Two of them
  // covering the same column (or one covering an already covered column) means no solution.
  std::vector<bool> covered(columns_, false);
  Search search{links, max_solutions, node_limit, result, {}};
  for (int r : chosen) {
    if (r < 0 || r >= Rows() || first_node[r] < 0) return result;
    for (int c : rows_[r]) {
      if (covered[c]) return result;
      covered[c] = true;
    }
    for (int c : rows_[r]) links.Cover(c + 1);
    search.partial.push_back(r);
  }
  search.Run();
  if (max_solutions > 0 && result.count >= max_solutions) result.complete = false;  // there may be more
  return result;
}

}  // namespace wlw::math
