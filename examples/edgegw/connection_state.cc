#include "connection_state.h"
#include "muduo/base/CountDownLatch.h"

namespace edgegw {

bool ConnectionState::transfer(int connId, Worker* src, Worker* dst) {
  if (!src || !dst) return false;
  int buffered = src->getBufferedBytes(connId);
  return transferWithBufferedCheck(connId, src, dst, buffered);
}

bool ConnectionState::transferWithBufferedCheck(int connId, Worker* src, Worker* dst, int bufferedBytes) {
  if (src->hasActiveTxn(connId)) {
    return false;
  }

  dst->setBufferedBytes(connId, bufferedBytes);
  src->setBufferedBytes(connId, 0);

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

} // namespace edgegw
