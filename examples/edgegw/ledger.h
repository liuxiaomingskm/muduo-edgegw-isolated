#pragma once
#include <atomic>
#include <unordered_map>
#include <mutex>

namespace edgegw {

struct LedgerEntry {
  int connId = -1;
  std::atomic<int> migrate_requested{0};
  std::atomic<int> migrate_completed{0};
  std::atomic<int> loops_registered{0};
  std::atomic<int> events_after_migrate{0};
  std::atomic<int> last_owner{-1};
};

class Ledger {
 public:
  LedgerEntry* getOrCreate(int connId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(connId);
    if (it == entries_.end()) {
      auto* e = new LedgerEntry();
      e->connId = connId;
      entries_[connId] = e;
      return e;
    }
    return it->second;
  }
  std::unordered_map<int, LedgerEntry*> snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
  }
  ~Ledger() {
    for (auto& kv : entries_) delete kv.second;
  }
 private:
  std::mutex mutex_;
  std::unordered_map<int, LedgerEntry*> entries_;
};

} // namespace edgegw
