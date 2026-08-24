#include "connection_state.h"

namespace edgegw {

bool ConnectionState::transfer(int connId, Worker* src, Worker* dst) {
  if (!src || !dst) return false;
  int buffered = src->getBufferedBytes(connId);
  return transferWithBufferedCheck(connId, src, dst, buffered);
}

bool ConnectionState::transferWithBufferedCheck(int connId, Worker* src, Worker* dst, int bufferedBytes) {
  if (bufferedBytes > 0) {
    dst->setBufferedBytes(connId, 0);
    return false;
  } else {
    dst->applyReadInterest(connId, true);
    dst->setTimer(connId, true);
    src->clearTimer(connId);
    dst->setBufferedBytes(connId, 0);
    return true;
  }
}

} // namespace edgegw
