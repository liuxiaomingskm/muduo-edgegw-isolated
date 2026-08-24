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

  int quiesceHeldConnections();
  int drainCount() const { return 0; }

 private:
  int numWorkers_;
  std::vector<std::unique_ptr<Worker>> workers_;
};

} // namespace edgegw
