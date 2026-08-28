#include "worker_pool.h"
#include "muduo/base/CountDownLatch.h"

namespace edgegw {

WorkerPool::WorkerPool(int numWorkers) : numWorkers_(numWorkers) {
  for (int i = 0; i < numWorkers_; ++i) {
    workers_.emplace_back(new Worker(i));
  }
}

WorkerPool::~WorkerPool() { stop(); }

void WorkerPool::start() {
  for (auto& w : workers_) w->start();
}

void WorkerPool::stop() {
  for (auto& w : workers_) w->stop();
}

Worker* WorkerPool::getWorker(int id) {
  if (id < 0 || id >= numWorkers_) return nullptr;
  return workers_[id].get();
}

std::vector<Worker*> WorkerPool::allWorkers() {
  std::vector<Worker*> out;
  for (auto& w : workers_) out.push_back(w.get());
  return out;
}

int WorkerPool::loopsRegistered(int connId) const {
  int cnt = 0;
  for (auto& w : workers_) {
    if (w->hasConnection(connId)) cnt++;
  }
  return cnt;
}

int WorkerPool::ownerOf(int connId) const {
  for (auto& w : workers_) {
    if (w->hasConnection(connId)) return w->id();
  }
  return -1;
}

bool WorkerPool::isReadingEnabledSomewhere(int connId) const {
  for (auto& w : workers_) {
    if (w->hasConnection(connId) && w->isReadingEnabled(connId)) return true;
  }
  return false;
}

size_t WorkerPool::totalConnections() const {
  size_t total = 0;
  for (auto& w : workers_) total += w->connectionCount();
  return total;
}

bool WorkerPool::hasBufferedAndTimer(int connId) const {
  for (auto& w : workers_) {
    if (w->hasConnection(connId)) {
      return w->hasTimer(connId) && w->getBufferedBytes(connId) > 0;
    }
  }
  return false;
}

bool WorkerPool::isStalledForRecovery(int connId) const {
  return hasBufferedAndTimer(connId);
}

int WorkerPool::countStalled() const {
  int cnt = 0;
  for (auto& w : workers_) {
    for (int cid : w->getConnections()) {
      if (w->hasTimer(cid) && w->getBufferedBytes(cid) > 0) cnt++;
    }
  }
  return cnt;
}

bool WorkerPool::checkExactlyOneOwner(int connId) const {
  return loopsRegistered(connId) == 1;
}

int WorkerPool::quiesceHeldConnections() {
  int drained = 0;
  for (const auto& worker : workers_) {
    Worker* current = worker.get();
    for (int connId : current->getConnections()) {
      if (!current->hasActiveTxn(connId) || current->isDraining(connId)) {
        continue;
      }

      muduo::CountDownLatch done(1);
      current->loop()->queueInLoop([current, connId, &done]() {
        current->setBufferedBytes(connId, 0);
        current->clearActiveTxn(connId);
        current->clearTimer(connId);
        current->setDraining(connId, true);
        current->removeConnection(connId);
        done.countDown();
      });
      done.wait();
      ++drained;
      ++drainCount_;
    }
  }
  return drained;
}

} // namespace edgegw
