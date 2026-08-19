#include "migrator.h"
#include "muduo/base/CountDownLatch.h"
#include <unistd.h>

namespace edgegw {

bool Migrator::migrate(int connId, Worker* src, Worker* dst, Ledger* ledger) {
  if (!src || !dst) return false;
  if (src->id() == dst->id()) return false;

  if (ledger) {
    auto* e = ledger->getOrCreate(connId);
    e->migrate_requested++;
  }

  // ---- Class B defect ----
  // Registration on new loop and deregistration from old loop are not correctly ordered
  // and not both performed from their owning loop's thread.
  // Buggy: add before remove, both from rebalancer thread, no per-conn lock.

  dst->unsafeAdd(connId);
  usleep(1000);
  src->unsafeRemove(connId);

  if (ledger) {
    auto* e = ledger->getOrCreate(connId);
    e->migrate_completed++;
    e->last_owner = dst->id();
  }
  return true;
}

bool Migrator::migrateWithLatches(int connId, Worker* src, Worker* dst, Ledger* ledger,
                                  muduo::CountDownLatch* afterAdd, muduo::CountDownLatch* proceed) {
  if (!src || !dst) return false;
  if (ledger) ledger->getOrCreate(connId)->migrate_requested++;

  dst->unsafeAdd(connId);
  if (afterAdd) afterAdd->countDown();
  if (proceed) proceed->wait();
  src->unsafeRemove(connId);

  if (ledger) {
    ledger->getOrCreate(connId)->migrate_completed++;
    ledger->getOrCreate(connId)->last_owner = dst->id();
  }
  return true;
}

} // namespace edgegw
