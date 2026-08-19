#pragma once
#include "worker_pool.h"
#include "ledger.h"

namespace edgegw {

class Migrator {
 public:
  static bool migrate(int connId, Worker* src, Worker* dst, Ledger* ledger);
  static bool migrateWithLatches(int connId, Worker* src, Worker* dst, Ledger* ledger,
                                 muduo::CountDownLatch* afterAdd, muduo::CountDownLatch* proceed);
  static void clearGlobalState();
};

} // namespace edgegw
