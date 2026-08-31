#ifndef EXAMPLES_EDGEGW_CONN_BOOKKEEPING_H
#define EXAMPLES_EDGEGW_CONN_BOOKKEEPING_H

#include <cstdint>

namespace edgegw {

class ConnBookkeeping {
 public:
  ConnBookkeeping(int64_t highWatermark, int64_t lowWatermark)
      : kHighWatermark_(highWatermark),
        kLowWatermark_(lowWatermark),
        pendingBytes_(0),
        paused_(false),
        pauseCount_(0),
        resumeCount_(0) {}

  void onDataQueued(int64_t n);
  void onWriteComplete(int64_t n, bool syncComplete, bool error);

  bool shouldPause() const { return pendingBytes_ > kHighWatermark_ && !paused_; }
  bool shouldResume() const { return pendingBytes_ < kLowWatermark_ && paused_; }

  int64_t pendingBytes() const { return pendingBytes_; }
  bool paused() const { return paused_; }
  int64_t pauseCount() const { return pauseCount_; }
  int64_t resumeCount() const { return resumeCount_; }

 private:
  const int64_t kHighWatermark_;
  const int64_t kLowWatermark_;
  int64_t pendingBytes_;
  bool paused_;
  int64_t pauseCount_;
  int64_t resumeCount_;

  void pause();
  void resume();
};

} // namespace edgegw

#endif
