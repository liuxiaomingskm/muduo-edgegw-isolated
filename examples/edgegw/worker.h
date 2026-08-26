#pragma once
#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThread.h"
#include "muduo/base/Mutex.h"

#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <string>

namespace edgegw {

class Worker {
 public:
  explicit Worker(int id);
  ~Worker();

  void start();
  void stop();

  muduo::net::EventLoop* loop() { return loop_; }
  int id() const { return id_; }
  pid_t threadId() const { return threadId_; }

  /**
   * These four methods mutate the registration table that belongs to this
   * worker's own EventLoop. The mutation must occur on the loop that owns
   * the table. A caller running on a different thread is expected to use
   * loop()->queueInLoop(...) to run the operation on the owning loop.
   */
  void addConnection(int connId);
  void removeConnection(int connId);
  void enableReading(int connId);
  void disableReading(int connId);

  void unsafeAdd(int connId);
  void unsafeRemove(int connId);
  void unsafeSetReading(int connId, bool enabled);

  bool hasConnection(int connId) const;
  bool isReadingEnabled(int connId) const;
  size_t connectionCount() const;
  std::unordered_set<int> getConnections() const;

  int getBufferedBytes(int connId) const;
  void setBufferedBytes(int connId, int bytes);
  bool hasTimer(int connId) const;
  void setTimer(int connId, bool has);
  void clearTimer(int connId);

  bool hasActiveTxn(int connId) const;
  void setActiveTxn(int connId, bool has);
  void clearActiveTxn(int connId);

  bool isDraining(int connId) const;
  void setDraining(int connId, bool has);

  /**
   * Flag raised when a registration-table mutation occurs outside the
   * owning loop thread. Used for detecting thread-affinity violations.
   */
  std::atomic<bool> wrongThreadMutation{false};
  std::atomic<int> mutationCount{0};

 private:
  void checkThread(const char* op);

  int id_;
  muduo::net::EventLoopThread* thread_;
  muduo::net::EventLoop* loop_;
  pid_t threadId_{0};

  mutable muduo::MutexLock mutex_;
  std::unordered_map<int, bool> connections_ GUARDED_BY(mutex_);
  std::unordered_map<int, int> buffered_ GUARDED_BY(mutex_);
  std::unordered_set<int> timers_ GUARDED_BY(mutex_);
  std::unordered_set<int> activeTxn_ GUARDED_BY(mutex_);
  std::unordered_set<int> draining_ GUARDED_BY(mutex_);
};

} // namespace edgegw
