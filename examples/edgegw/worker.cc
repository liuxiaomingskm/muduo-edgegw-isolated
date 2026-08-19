#include "worker.h"
#include "muduo/base/CurrentThread.h"

namespace edgegw {

Worker::Worker(int id) : id_(id), thread_(nullptr), loop_(nullptr) {}

Worker::~Worker() { stop(); delete thread_; }

void Worker::start() {
  thread_ = new muduo::net::EventLoopThread();
  loop_ = thread_->startLoop();
  // capture tid from loop thread
  muduo::MutexLockGuard lock(mutex_);
  // loop thread id is not directly available, we use loop's check via isInLoopThread
  // For tracking, we store current thread id after start via queue
  // We'll set threadId_ via runInLoop
  loop_->runInLoop([this]() { threadId_ = muduo::CurrentThread::tid(); });
  // wait a bit for threadId to be set
  for (int i = 0; i < 100 && threadId_ == 0; ++i) {
    usleep(1000);
  }
}

void Worker::stop() {
  if (loop_) {
    loop_->quit();
    loop_ = nullptr;
  }
}

void Worker::checkThread(const char* op) {
  (void)op;
  if (loop_ && !loop_->isInLoopThread()) {
    wrongThreadMutation = true;
  }
}

void Worker::addConnection(int connId) {
  checkThread("addConnection");
  muduo::MutexLockGuard lock(mutex_);
  connections_[connId] = false; // initially disabled until enabled
  mutationCount++;
}

void Worker::removeConnection(int connId) {
  checkThread("removeConnection");
  muduo::MutexLockGuard lock(mutex_);
  connections_.erase(connId);
  buffered_.erase(connId);
  timers_.erase(connId);
  mutationCount++;
}

void Worker::enableReading(int connId) {
  checkThread("enableReading");
  muduo::MutexLockGuard lock(mutex_);
  auto it = connections_.find(connId);
  if (it != connections_.end()) {
    it->second = true;
  }
  mutationCount++;
}

void Worker::disableReading(int connId) {
  checkThread("disableReading");
  muduo::MutexLockGuard lock(mutex_);
  auto it = connections_.find(connId);
  if (it != connections_.end()) {
    it->second = false;
  }
  mutationCount++;
}

void Worker::unsafeAdd(int connId) {
  // Intentionally not checking thread - buggy path, but we still mark wrongThread
  if (loop_ && !loop_->isInLoopThread()) {
    wrongThreadMutation = true;
  }
  muduo::MutexLockGuard lock(mutex_);
  connections_[connId] = false;
  mutationCount++;
}

void Worker::unsafeRemove(int connId) {
  if (loop_ && !loop_->isInLoopThread()) {
    wrongThreadMutation = true;
  }
  muduo::MutexLockGuard lock(mutex_);
  connections_.erase(connId);
  mutationCount++;
}

void Worker::unsafeSetReading(int connId, bool enabled) {
  if (loop_ && !loop_->isInLoopThread()) {
    wrongThreadMutation = true;
  }
  muduo::MutexLockGuard lock(mutex_);
  auto it = connections_.find(connId);
  if (it != connections_.end()) it->second = enabled;
}

bool Worker::hasConnection(int connId) const {
  muduo::MutexLockGuard lock(mutex_);
  return connections_.find(connId) != connections_.end();
}

bool Worker::isReadingEnabled(int connId) const {
  muduo::MutexLockGuard lock(mutex_);
  auto it = connections_.find(connId);
  return it != connections_.end() && it->second;
}

size_t Worker::connectionCount() const {
  muduo::MutexLockGuard lock(mutex_);
  return connections_.size();
}

std::unordered_set<int> Worker::getConnections() const {
  muduo::MutexLockGuard lock(mutex_);
  std::unordered_set<int> s;
  for (auto& kv : connections_) s.insert(kv.first);
  return s;
}

int Worker::getBufferedBytes(int connId) const {
  muduo::MutexLockGuard lock(mutex_);
  auto it = buffered_.find(connId);
  return it != buffered_.end() ? it->second : 0;
}

void Worker::setBufferedBytes(int connId, int bytes) {
  muduo::MutexLockGuard lock(mutex_);
  buffered_[connId] = bytes;
}

bool Worker::hasTimer(int connId) const {
  muduo::MutexLockGuard lock(mutex_);
  return timers_.find(connId) != timers_.end();
}

void Worker::setTimer(int connId, bool has) {
  muduo::MutexLockGuard lock(mutex_);
  if (has) timers_.insert(connId);
  else timers_.erase(connId);
}

void Worker::clearTimer(int connId) {
  muduo::MutexLockGuard lock(mutex_);
  timers_.erase(connId);
}

} // namespace edgegw
