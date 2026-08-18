#include "examples/edgegw/conn_bookkeeping.h"

namespace edgegw {

void ConnBookkeeping::onDataQueued(int64_t n) {
  pendingBytes_ += n;
  if (pendingBytes_ > kHighWatermark_ && !paused_) {
    pause();
  }
}

void ConnBookkeeping::pause() {
  if (!paused_) {
    paused_ = true;
    ++pauseCount_;
  }
}

void ConnBookkeeping::resume() {
  if (paused_) {
    paused_ = false;
    ++resumeCount_;
  }
}

static inline void accountComplete(int64_t& pending, int64_t n, bool syncComplete, bool error) {
  if (error) return;
  if (syncComplete) {
    pending -= n;
  }
}

void ConnBookkeeping::onWriteComplete(int64_t n, bool syncComplete, bool error) {
  accountComplete(pendingBytes_, n, syncComplete, error);
  if (paused_ && pendingBytes_ < kLowWatermark_) {
    resume();
  }
}

} // namespace edgegw
