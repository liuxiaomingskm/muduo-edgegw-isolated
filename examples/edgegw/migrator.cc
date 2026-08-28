#include "migrator.h"
#include "muduo/base/CountDownLatch.h"
#include <memory>
#include <mutex>
#include <unordered_map>

namespace edgegw {

namespace {

std::mutex g_ownerMutex;
std::unordered_map<int, Worker*> g_owner;
std::mutex g_connLocksMutex;
std::unordered_map<int, std::unique_ptr<std::mutex>> g_connLocks;

std::mutex& getConnLock(int connId) {
  std::lock_guard<std::mutex> lock(g_connLocksMutex);
  auto it = g_connLocks.find(connId);
  if (it == g_connLocks.end()) {
    g_connLocks[connId] = std::unique_ptr<std::mutex>(new std::mutex());
  }
  return *g_connLocks[connId];
}

} // namespace

void Migrator::clearGlobalState() {
  std::lock_guard<std::mutex> ownerLock(g_ownerMutex);
  std::lock_guard<std::mutex> connectionLock(g_connLocksMutex);
  g_owner.clear();
  g_connLocks.clear();
}

bool Migrator::migrate(int connId, Worker* src, Worker* dst, Ledger* ledger) {
  if (!src || !dst) return false;
  if (src->id() == dst->id()) return false;

  if (src->hasActiveTxn(connId)) {
    if (ledger) ledger->getOrCreate(connId)->migrate_requested++;
    return false;
  }

  std::mutex& connLock = getConnLock(connId);
  std::lock_guard<std::mutex> guard(connLock);

  if (ledger) ledger->getOrCreate(connId)->migrate_requested++;

  Worker* actualSrc = nullptr;
  {
    std::lock_guard<std::mutex> ownerLock(g_ownerMutex);
    auto it = g_owner.find(connId);
    actualSrc = it == g_owner.end() ? src : it->second;
    g_owner[connId] = dst;
  }

  muduo::CountDownLatch done(1);
  actualSrc->loop()->queueInLoop([actualSrc, dst, connId, ledger, &done]() {
    if (actualSrc->hasConnection(connId)) actualSrc->removeConnection(connId);
    dst->loop()->queueInLoop([dst, connId, ledger, &done]() {
      if (!dst->hasConnection(connId)) dst->addConnection(connId);
      dst->enableReading(connId);
      if (ledger) {
        auto* entry = ledger->getOrCreate(connId);
        entry->migrate_completed++;
        entry->last_owner = dst->id();
      }
      done.countDown();
    });
  });
  done.wait();
  return true;
}

bool Migrator::migrateWithLatches(int connId, Worker* src, Worker* dst, Ledger* ledger,
                                  muduo::CountDownLatch* afterAdd, muduo::CountDownLatch* proceed) {
  if (src && src->hasActiveTxn(connId)) {
    if (ledger) ledger->getOrCreate(connId)->migrate_requested++;
    if (afterAdd) afterAdd->countDown();
    if (proceed) proceed->wait();
    return false;
  }
  if (afterAdd) afterAdd->countDown();
  if (proceed) proceed->wait();
  return migrate(connId, src, dst, ledger);
}

} // namespace edgegw
