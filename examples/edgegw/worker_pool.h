#pragma once
#include "worker.h"
#include <vector>
#include <memory>

namespace edgegw {

class WorkerPool {
 public:
  explicit WorkerPool(int numWorkers);
  ~WorkerPool();

  void start();
  void stop();

  Worker* getWorker(int id);
  std::vector<Worker*> allWorkers();
  int numWorkers() const { return numWorkers_; }

  int loopsRegistered(int connId) const;
  int ownerOf(int connId) const; // returns worker id or -1
  bool isReadingEnabledSomewhere(int connId) const;
  size_t totalConnections() const;

  // For testing: check invariants
  bool checkExactlyOneOwner(int connId) const;

  /**
   * Drains connections that are mid-transaction and should not be migrated.
   * A connection qualifies if it has an active transaction (hasActiveTxn)
   * and has not yet been marked as draining.
   * For each qualifying connection, the state change must happen on that
   * worker's own loop: clear buffered bytes, clear its timer, clear the
   * active-transaction mark, mark the connection as draining, and finally
   * remove it from the worker's connection table. The removal is required;
   * merely marking draining is not sufficient.
   * Returns how many connections were drained in this call.
   * Cumulative drained count is available via drainCount(), so
   * totalConnections() will decrease after a successful quiesce.
   */
  int quiesceHeldConnections();

  /**
   * Returns cumulative number of connections drained via
   * quiesceHeldConnections().
   */
  int drainCount() const { return drainCount_; }

 private:
  int numWorkers_;
  std::vector<std::unique_ptr<Worker>> workers_;
  int drainCount_{0};
};

} // namespace edgegw
