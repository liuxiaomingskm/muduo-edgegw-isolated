#include "connection_state.h"
#include "muduo/base/CountDownLatch.h"

namespace edgegw {

namespace {

// Loop-affine re-arm introduced by the connection handling rework: the timer
// on the losing loop is retired from that loop, then interest and the timer
// are re-established on the gaining loop from its own thread.
bool armOnOwner(int connId, Worker* src, Worker* dst) {
  muduo::CountDownLatch done(1);
  src->loop()->queueInLoop([src, dst, connId, &done]() {
    src->clearTimer(connId);
    dst->loop()->queueInLoop([dst, connId, &done]() {
      dst->enableReading(connId);
      dst->setTimer(connId, true);
      done.countDown();
    });
  });
  done.wait();
  return true;
}

} // namespace

bool ConnectionState::transfer(int connId, Worker* src, Worker* dst) {
  if (!src || !dst) return false;
  int buffered = src->getBufferedBytes(connId);
  return transferWithBufferedCheck(connId, src, dst, buffered);
}

bool ConnectionState::transferWithBufferedCheck(int connId, Worker* src, Worker* dst, int bufferedBytes) {
  if (!src || !dst) return false;

  if (bufferedBytes > 0) {
    // Output is still queued on the losing loop; the drain path re-arms once
    // it has flushed.
    dst->setBufferedBytes(connId, 0);
    return false;
  }

  dst->setBufferedBytes(connId, bufferedBytes);
  src->setBufferedBytes(connId, 0);
  return armOnOwner(connId, src, dst);
}

} // namespace edgegw
