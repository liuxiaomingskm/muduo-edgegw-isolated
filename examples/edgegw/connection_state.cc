#include "connection_state.h"

namespace edgegw {

bool ConnectionState::transfer(int connId, Worker* src, Worker* dst) {
  if (!src || !dst) return false;
  int buffered = src->getBufferedBytes(connId);
  return transferWithBufferedCheck(connId, src, dst, buffered);
}

bool ConnectionState::transferWithBufferedCheck(int connId, Worker* src, Worker* dst, int bufferedBytes) {
  // ---- Class C defect ----
  // Connection transfers ownership correctly, but its interest/buffer/timer does not travel.
  // Buggy: when buffered>0 (traffic in flight), forget to arm reading on new loop and strand timer.

  if (bufferedBytes > 0) {
    // Should transfer buffered and arm, but buggy drops
    dst->setBufferedBytes(connId, 0); // buffered output lost
    // reading remains disabled (migrator added with disabled state)
    // timer stranded on old loop - src still has timer
    return false;
  } else {
    // Idle: correctly armed
    dst->unsafeSetReading(connId, true);
    dst->setTimer(connId, true);
    src->clearTimer(connId);
    dst->setBufferedBytes(connId, 0);
    return true;
  }
}

} // namespace edgegw
