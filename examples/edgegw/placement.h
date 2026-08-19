#pragma once
#include "worker_pool.h"
#include <vector>
#include <mutex>

namespace edgegw {

// Placement bookkeeping: decides which connections need to move.
// Class A defect lives here.

class PlacementPolicy {
 public:
  struct Move {
    int connId;
    int src;
    int dst;
  };

  PlacementPolicy();

  // Compute moves based on current pool skew. Returns moves.
  std::vector<Move> computeMoves(WorkerPool& pool, const std::vector<int>& allConnIds);

  // Schedule moves for dispatch. Buggy version clears pending without preserving undispached.
  void scheduleMoves(const std::vector<Move>& moves);

  // Get pending moves (for dispatch)
  std::vector<Move> getPendingMoves();

  // Mark N moves as dispatched (removes from pending)
  void markDispatched(int n);

  // For testing: number of pending
  size_t pendingSize();

  // Disable reading on src in anticipation of move (part of A defect - leaves disabled if move lost)
  void parkConnectionsForMove(WorkerPool& pool, const std::vector<Move>& moves);

 private:
  std::vector<Move> pendingMoves_;
  std::mutex mutex_;
};

} // namespace edgegw
