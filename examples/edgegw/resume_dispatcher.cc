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

} // namespace edgegw
