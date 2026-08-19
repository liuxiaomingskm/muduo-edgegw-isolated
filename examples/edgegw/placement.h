#pragma once
#include "worker_pool.h"
#include <vector>
#include <mutex>

namespace edgegw {

class PlacementPolicy {
 public:
  struct Move {
    int connId;
    int src;
    int dst;
  };

  PlacementPolicy();

  std::vector<Move> computeMoves(WorkerPool& pool, const std::vector<int>& allConnIds);
  void scheduleMoves(const std::vector<Move>& moves);
  std::vector<Move> getPendingMoves();
  void markDispatched(int n);
  size_t pendingSize();
  void parkConnectionsForMove(WorkerPool& pool, const std::vector<Move>& moves);

 private:
  std::vector<Move> pendingMoves_;
  std::mutex mutex_;
};

} // namespace edgegw
