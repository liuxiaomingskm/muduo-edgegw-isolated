#include "placement.h"
#include <algorithm>
#include <numeric>

namespace edgegw {

PlacementPolicy::PlacementPolicy() {}

std::vector<PlacementPolicy::Move> PlacementPolicy::computeMoves(WorkerPool& pool,
                                                                const std::vector<int>& allConnIds) {
  // Simple skew detection: find overloaded and underloaded workers
  int n = pool.numWorkers();
  std::vector<int> counts(n);
  for (int i = 0; i < n; ++i) {
    counts[i] = static_cast<int>(pool.getWorker(i)->connectionCount());
  }
  int total = std::accumulate(counts.begin(), counts.end(), 0);
  if (total == 0) return {};
  int avg = total / n;
  if (avg == 0) avg = 1;

  std::vector<int> overloaded;
  std::vector<int> underloaded;
  for (int i = 0; i < n; ++i) {
    if (counts[i] > avg + 1) overloaded.push_back(i);
    if (counts[i] < avg) underloaded.push_back(i);
  }
  if (overloaded.empty() || underloaded.empty()) return {};

  // Select subset to move while leaving most where they are
  // For scale condition: need enough workers and connections that rebalance moves subset
  // Small pool (2 workers, few conns) will move 1, large pool moves more but subset
  std::vector<Move> moves;
  // For each overloaded worker, pick some connections to move
  for (int src : overloaded) {
    auto conns = pool.getWorker(src)->getConnections();
    int toMove = 0;
    if (pool.numWorkers() >= 4 && allConnIds.size() >= 40) {
      // Fleet shape: move subset (e.g., 30% of overloaded worker's conns)
      toMove = std::max(1, static_cast<int>(conns.size() / 3));
    } else {
      // Small shape: move 1
      toMove = std::min(1, static_cast<int>(conns.size()));
    }
    int moved = 0;
    for (int connId : conns) {
      if (moved >= toMove) break;
      // Pick underloaded dst round-robin
      int dst = underloaded[moved % underloaded.size()];
      if (dst == src) continue;
      moves.push_back({connId, src, dst});
      moved++;
    }
  }
  return moves;
}

void PlacementPolicy::scheduleMoves(const std::vector<Move>& moves) {
  std::lock_guard<std::mutex> lock(mutex_);
  // ---- Class A defect ----
  // The structure tracking which connections need to move loses some of them under overlapping passes:
  // a connection selected by pass 1 is dropped when pass 2 begins before pass 1 finishes, so no migration ever requested.
  // Buggy: clears pending without preserving undispached from previous pass
  // Correct should merge or preserve until dispatched.
  pendingMoves_.clear();
  pendingMoves_ = moves;
}

std::vector<PlacementPolicy::Move> PlacementPolicy::getPendingMoves() {
  std::lock_guard<std::mutex> lock(mutex_);
  return pendingMoves_;
}

void PlacementPolicy::markDispatched(int n) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (n >= static_cast<int>(pendingMoves_.size())) {
    pendingMoves_.clear();
  } else {
    pendingMoves_.erase(pendingMoves_.begin(), pendingMoves_.begin() + n);
  }
}

size_t PlacementPolicy::pendingSize() {
  std::lock_guard<std::mutex> lock(mutex_);
  return pendingMoves_.size();
}

void PlacementPolicy::parkConnectionsForMove(WorkerPool& pool, const std::vector<Move>& moves) {
  for (auto& m : moves) {
    Worker* src = pool.getWorker(m.src);
    if (!src) continue;
    int cid = m.connId;
    src->loop()->queueInLoop([src, cid]() {
      src->disableReading(cid);
    });
  }
}

} // namespace edgegw
