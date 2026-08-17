#ifndef EXAMPLES_EDGEGW_RESUME_DISPATCHER_H
#define EXAMPLES_EDGEGW_RESUME_DISPATCHER_H

#include "muduo/net/EventLoop.h"
#include "muduo/net/Channel.h"

#include <atomic>

namespace edgegw {

class ResumeDispatcher {
 public:
  explicit ResumeDispatcher() : resume_requested_(0), resume_applied_(0) {}

  void requestResume(muduo::net::Channel* ch, muduo::net::EventLoop* loop);

  int64_t resumeRequested() const { return resume_requested_.load(); }
  int64_t resumeApplied() const { return resume_applied_.load(); }

 private:
  std::atomic<int64_t> resume_requested_;
  std::atomic<int64_t> resume_applied_;
};

} // namespace edgegw

#endif
