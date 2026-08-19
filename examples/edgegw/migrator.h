#pragma once
#include "worker_pool.h"
#include "ledger.h"

namespace edgegw {

// Ownership transfer: registration on new loop and deregistration from old.
// Class B defect lives here.

class Migrator {
 public:
  // Migrate connId from src to dst. Returns true if completed.
  // ledger may be nullptr for tests.
  static bool migrate(int connId, Worker* src, Worker* dst, Ledger* ledger);

  // Variant with latches to force interleaving for deterministic tests
  static bool migrateWithLatches(int connId, Worker* src, Worker* dst, Ledger* ledger,
                                 muduo::CountDownLatch* afterAdd, muduo::CountDownLatch* proceed);
};

} // namespace edgegw
