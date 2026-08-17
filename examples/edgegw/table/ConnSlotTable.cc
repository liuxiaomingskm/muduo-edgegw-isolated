#include "examples/edgegw/table/ConnSlotTable.h"
#include <unistd.h>
#include <cassert>
#include <poll.h>

namespace edgegw {

int ConnSlotTable::acquire(int fd, short events) {
  int idx;
  if (!free_.empty()) {
    idx = free_.back();
    free_.pop_back();
    slots_[idx].fd = fd;
    slots_[idx].events = events;
    slots_[idx].revents = 0;
  } else {
    idx = static_cast<int>(slots_.size());
    slots_.push_back(Slot(fd, events));
  }
  owner_[fd] = idx;
  return idx;
}

void ConnSlotTable::recycle(int idx) {
  assert(idx >= 0 && idx < static_cast<int>(slots_.size()));
  slots_[idx].fd = -1;
  slots_[idx].events = 0;
  slots_[idx].revents = 0;
  free_.push_back(idx);
  // BUG: never erases owner_ entries, so stale fd->idx remains
}

void ConnSlotTable::park(int fd) {
  auto it = owner_.find(fd);
  if (it == owner_.end()) return;
  int idx = it->second;
  if (idx < 0 || idx >= static_cast<int>(slots_.size())) return;
  parked_[fd] = slots_[idx].events;
  slots_[idx].fd = -1;
  slots_[idx].events = 0;
  slots_[idx].revents = 0;
  free_.push_back(idx);
  // BUG: leaves owner_[fd] stale; resume will use fast path that re-arms a slot still on free list
}

void ConnSlotTable::resume(int fd) {
  auto pit = parked_.find(fd);
  if (pit == parked_.end()) return;
  short saved = pit->second;
  parked_.erase(pit);
  auto oit = owner_.find(fd);
  if (oit != owner_.end()) {
    int idx = oit->second;
    if (idx >= 0 && idx < static_cast<int>(slots_.size())) {
      // fast path: reuse cached slot (which is currently on free list)
      slots_[idx].fd = fd;
      slots_[idx].events = saved;
      slots_[idx].revents = 0;
      return;
    }
  }
  acquire(fd, saved);
}

void ConnSlotTable::release(int fd) {
  parked_.erase(fd);
  auto it = owner_.find(fd);
  if (it != owner_.end()) {
    int idx = it->second;
    if (idx >= 0 && idx < static_cast<int>(slots_.size())) {
      slots_[idx].fd = -1;
      slots_[idx].events = 0;
      slots_[idx].revents = 0;
      free_.push_back(idx);
    }
    owner_.erase(it);
  }
}

int ConnSlotTable::pollOnce(int timeoutMs, std::vector<int>* active) {
  std::vector<struct pollfd> pfds;
  std::vector<int> fdMap;
  pfds.reserve(slots_.size());
  fdMap.reserve(slots_.size());
  for (size_t i = 0; i < slots_.size(); ++i) {
    const auto& s = slots_[i];
    if (s.fd >= 0 && s.events != 0) {
      struct pollfd pfd;
      pfd.fd = s.fd;
      pfd.events = s.events;
      pfd.revents = 0;
      pfds.push_back(pfd);
      fdMap.push_back(s.fd);
    }
  }
  int n = ::poll(pfds.data(), pfds.size(), timeoutMs);
  if (n > 0 && active) {
    for (size_t i = 0; i < pfds.size(); ++i) {
      if (pfds[i].revents & (POLLIN | POLLERR | POLLHUP)) {
        active->push_back(fdMap[i]);
      }
    }
  }
  return n;
}

} // namespace edgegw
