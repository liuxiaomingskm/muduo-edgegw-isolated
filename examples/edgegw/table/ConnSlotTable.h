#pragma once
#include <vector>
#include <poll.h>
#include <unordered_map>
#include <cstddef>

namespace edgegw {

class ConnSlotTable {
 public:
  struct Slot {
    int fd = -1;
    short events = 0;
    short revents = 0;
    Slot(): fd(-1), events(0), revents(0) {}
    Slot(int f, short ev): fd(f), events(ev), revents(0) {}
  };

  ConnSlotTable() = default;

  int acquire(int fd, short events = POLLIN);
  void recycle(int idx);
  void park(int fd);
  void resume(int fd);
  void release(int fd);
  void addFd(int fd, short events = POLLIN) { acquire(fd, events); }

  int pollOnce(int timeoutMs, std::vector<int>* active);

  size_t slotCount() const { return slots_.size(); }
  size_t freeCount() const { return free_.size(); }
  const Slot& slotAt(int idx) const { return slots_[idx]; }
  Slot& slotAt(int idx) { return slots_[idx]; }

  bool hasOwner(int fd) const {
    auto it = owner_.find(fd);
    return it != owner_.end();
  }
  bool isParked(int fd) const {
    auto it = parked_.find(fd);
    return it != parked_.end();
  }

  
  int indexOf(int fd) const;
  void rearm(int idx, int fd, short events = POLLIN);
  void removeAndCompact(int fd);

 private:
  std::vector<Slot> slots_;
  std::vector<int> free_;
  std::unordered_map<int,int> owner_;
  std::unordered_map<int,short> parked_;
  std::unordered_map<int,int> parkedIdx_; // quarantine: fd -> idx while parked
};

} // namespace edgegw
