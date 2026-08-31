#include "examples/edgegw/resume_dispatcher.h"

using namespace muduo::net;

namespace edgegw {

void ResumeDispatcher::requestResume(Channel* ch, EventLoop* loop) {
  ++resume_requested_;
  loop->queueInLoop([this, ch]() {
    ch->enableReading();
    ++resume_applied_;
  });
}

void ResumeDispatcher::requestResumeWithTable(ConnSlotTable* table, int fd, EventLoop* loop, muduo::CountDownLatch* idxCaptured, muduo::CountDownLatch* proceed) {
  ++resume_requested_;
  idxCaptured->countDown();
  proceed->wait();
  loop->queueInLoop([this, table, fd]() {
    int idx = table->indexOf(fd);
    if (idx >= 0) {
      table->rearm(idx, fd);
    }
    ++resume_applied_;
  });
}

} // namespace edgegw
