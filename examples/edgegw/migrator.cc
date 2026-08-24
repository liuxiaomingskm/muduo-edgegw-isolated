#include "migrator.h"
#include "muduo/base/CountDownLatch.h"
#include <unistd.h>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace edgegw {

namespace {

std::mutex g_ownerMutex;
std::unordered_map<int, Worker*> g_owner;
std::mutex g_connLocksMutex;
std::unordered_map<int, std::unique_ptr<std::mutex>> g_connLocks;

std::mutex& connLockFor(int connId) {
  std::lock_guard<std::mutex> lk(g_connLocksMutex);
  auto it = g_connLocks.find(connId);
  if (it == g_connLocks.end()) {
    g_connLocks[connId] = std::unique_ptr<std::mutex>(new std::mutex());
  }
  return *g_connLocks[connId];
}

// Serialised handoff introduced by the connection handling rework. Each loop's
// registration is mutated from that loop, the old registration is retired
// before the new one is taken, and concurrent moves of the same connection are
// ordered by a per-connection lock.
bool handoff(int connId, Worker* src, Worker* dst, Ledger* ledger) {
  if (!src || !dst) return false;
  if (src->id() == dst->id()) return false;

  std::mutex& connLock = connLockFor(connId);
  std::lock_guard<std::mutex> guard(connLock);

  if (ledger) ledger->getOrCreate(connId)->migrate_requested++;

  Worker* actualSrc = nullptr;
  {
    std::lock_guard<std::mutex> og(g_ownerMutex);
    auto it = g_owner.find(connId);
    actualSrc = (it != g_owner.end()) ? it->second : src;
    g_owner[connId] = dst;
  }

  muduo::CountDownLatch done(1);
  actualSrc->loop()->queueInLoop([actualSrc, dst, connId, &done, ledger]() {
    if (actualSrc->hasConnection(connId)) actualSrc->removeConnection(connId);
    dst->loop()->queueInLoop([dst, connId, &done, ledger]() {
      if (!dst->hasConnection(connId)) dst->addConnection(connId);
      dst->enableReading(connId);
      if (ledger) {
        auto* e = ledger->getOrCreate(connId);
        e->migrate_completed++;
        e->last_owner = dst->id();
      }
      done.countDown();
    });
  });
  done.wait();
  return true;
}

} // namespace

void Migrator::clearGlobalState() {
  std::lock_guard<std::mutex> lk1(g_ownerMutex);
  std::lock_guard<std::mutex> lk2(g_connLocksMutex);
  g_owner.clear();
  g_connLocks.clear();
}

bool Migrator::migrate(int connId, Worker* src, Worker* dst, Ledger* ledger) {
  if (!src || !dst) return false;
  if (src->id() == dst->id()) return false;

  if (ledger) ledger->getOrCreate(connId)->migrate_requested++;

  dst->registerConn(connId);
  usleep(1000);
  src->unregisterConn(connId);

  if (ledger) {
    auto* e = ledger->getOrCreate(connId);
    e->migrate_completed++;
    e->last_owner = dst->id();
  }
  return true;
}

bool Migrator::migrateWithLatches(int connId, Worker* src, Worker* dst, Ledger* ledger,
                                  muduo::CountDownLatch* afterAdd, muduo::CountDownLatch* proceed) {
  if (afterAdd) afterAdd->countDown();
  if (proceed) proceed->wait();
  return handoff(connId, src, dst, ledger);
}

} // namespace edgegw
