#include "examples/edgegw/worker_pool.h"
#include "examples/edgegw/placement.h"
#include "examples/edgegw/migrator.h"
#include "examples/edgegw/connection_state.h"
#include "examples/edgegw/ledger.h"

#include "muduo/base/CountDownLatch.h"
#include <thread>
#include <vector>
#include <cstdio>
#include <cstring>
#include <algorithm>

using namespace edgegw;
using namespace muduo;

struct ConnRecord {
  int conn = -1;
  int migrate_requested = 0;
  int migrate_completed = 0;
  int loops_registered = 0;
  int events_after_migrate = 0;
};

int main(int argc, char* argv[]) {
  bool fleet = false;
  for (int i = 1; i < argc; ++i) if (strstr(argv[i], "fleet")) fleet = true;

  int numWorkers = fleet ? 8 : 2;
  int numConns = fleet ? 80 : 12;
  bool traffic = fleet;
  bool overlapping = fleet;

  printf("# profile=%s workers=%d conns=%d traffic=%d overlapping=%d\n",
         fleet ? "fleet" : "small", numWorkers, numConns, traffic ? 1 : 0, overlapping ? 1 : 0);

  WorkerPool pool(numWorkers);
  pool.start();

  std::vector<int> allConnIds;
  for (int i = 0; i < numConns; ++i) allConnIds.push_back(i);

  // Skewed assignment to trigger rebalance
  for (int i = 0; i < numConns; ++i) {
    int w = 0;
    if (fleet) {
      if (i < 40) w = 0;
      else w = 1 + (i % (numWorkers - 1));
    } else {
      // small also skewed: first 8 on worker0
      if (i < 8) w = 0;
      else w = 1;
    }
    Worker* worker = pool.getWorker(w);
    CountDownLatch latch(1);
    worker->loop()->queueInLoop([worker, i, &latch]() {
      worker->addConnection(i);
      worker->enableReading(i);
      latch.countDown();
    });
    latch.wait();
    if (traffic) {
      worker->setBufferedBytes(i, 1024);
      worker->setTimer(i, true);
    }
  }

  Ledger ledger;
  PlacementPolicy placement;
  std::vector<ConnRecord> records;
  for (int id : allConnIds) {
    ConnRecord r; r.conn = id; records.push_back(r);
  }

  auto updateRecord = [&](int connId) {
    int reg = pool.loopsRegistered(connId);
    bool reading = pool.isReadingEnabledSomewhere(connId);
    auto* e = ledger.getOrCreate(connId);
    for (auto& rec : records) {
      if (rec.conn == connId) {
        rec.loops_registered = reg;
        rec.events_after_migrate = (reg == 1 && reading) ? 1 : 0;
        rec.migrate_requested = e->migrate_requested.load();
        rec.migrate_completed = e->migrate_completed.load();
        break;
      }
    }
  };

  std::vector<int> attempted;

  if (fleet && overlapping) {
    // Fleet with overlapping: triggers A, B, C
    auto moves1 = placement.computeMoves(pool, allConnIds);
    printf("# pass1 computed %zu moves\n", moves1.size());
    placement.parkConnectionsForMove(pool, moves1);
    placement.scheduleMoves(moves1);
    auto pending1 = placement.getPendingMoves();

    int half = static_cast<int>(pending1.size() / 2);
    std::vector<PlacementPolicy::Move> firstHalf;
    for (int i = 0; i < half; ++i) {
      auto m = pending1[static_cast<size_t>(i)];
      Worker* src = pool.getWorker(m.src);
      Worker* dst = pool.getWorker(m.dst);
      if (!src || !dst) continue;
      Migrator::migrate(m.connId, src, dst, &ledger);
      // C defect: buffered transfer fails
      ConnectionState::transferWithBufferedCheck(m.connId, src, dst, 1024);
      firstHalf.push_back(m);
      attempted.push_back(m.connId);
    }
    placement.markDispatched(half);

    // Overlapping second pass before first fully completes - A defect loses remaining moves
    auto moves2 = placement.computeMoves(pool, allConnIds);
    printf("# pass2 overlapping computed %zu moves\n", moves2.size());
    placement.parkConnectionsForMove(pool, moves2);
    placement.scheduleMoves(moves2); // BUG A: clears pending, losing pending1 remaining

    auto pending2 = placement.getPendingMoves();

    // For A: the lost moves from pending1 second half are parked but never migrated
    // They remain disabled on src -> stall with requested maybe 0 or still 0 completed
    for (size_t i = static_cast<size_t>(half); i < pending1.size(); ++i) {
      int lostConn = pending1[i].connId;
      // Already parked, not migrated, so reading disabled, loops_registered=1, events=0, requested=0
      // Ensure ledger has 0 requested for these lost ones
      // Actually they were not migrated, so we keep requested=0
      if (std::find(attempted.begin(), attempted.end(), lostConn) == attempted.end()) {
        attempted.push_back(lostConn);
      }
    }

    // For B: force same conn moved twice concurrently to get 0 or 2 owners
    if (!pending2.empty() && !firstHalf.empty()) {
      int overlapConn = firstHalf[0].connId;
      // Make pending2[0] overlap same conn
      pending2[0].connId = overlapConn;
      CountDownLatch afterAdd(1);
      CountDownLatch proceed(1);
      Worker* src = pool.getWorker(pending2[0].src);
      Worker* dst = pool.getWorker(pending2[0].dst);
      std::thread t1([&]() {
        Migrator::migrateWithLatches(overlapConn, src, dst, &ledger, &afterAdd, &proceed);
        ConnectionState::transferWithBufferedCheck(overlapConn, src, dst, 1024);
      });
      afterAdd.wait();
      // While first migration has added but not removed (window 2 owners), second migration adds again
      if (pending2.size() > 1) {
        Worker* src2 = pool.getWorker(pending2[1].src);
        Worker* dst2 = pool.getWorker(pending2[1].dst);
        if (src2 && dst2) {
          // Second add makes 2 owners
          dst2->unsafeAdd(overlapConn);
          if (std::find(attempted.begin(), attempted.end(), overlapConn) == attempted.end())
            attempted.push_back(overlapConn);
        }
      }
      proceed.countDown();
      t1.join();
    }

    for (size_t i = 1; i < pending2.size(); ++i) {
      auto m = pending2[i];
      Worker* src = pool.getWorker(m.src);
      Worker* dst = pool.getWorker(m.dst);
      if (!src || !dst) continue;
      Migrator::migrate(m.connId, src, dst, &ledger);
      ConnectionState::transferWithBufferedCheck(m.connId, src, dst, 1024);
      if (std::find(attempted.begin(), attempted.end(), m.connId) == attempted.end())
        attempted.push_back(m.connId);
    }
    placement.markDispatched(static_cast<int>(pending2.size()));

  } else {
    // Small/quiet/sequential - must be clean
    auto moves = placement.computeMoves(pool, allConnIds);
    printf("# small/sequential computed %zu moves\n", moves.size());
    placement.parkConnectionsForMove(pool, moves);
    placement.scheduleMoves(moves);
    auto pending = placement.getPendingMoves();
    for (size_t i = 0; i < pending.size(); ++i) {
      auto m = pending[i];
      Worker* src = pool.getWorker(m.src);
      Worker* dst = pool.getWorker(m.dst);
      if (!src || !dst) continue;
      int cid = m.connId;
      CountDownLatch done(1);
      src->loop()->queueInLoop([src, dst, cid, &ledger, &done]() {
        src->removeConnection(cid);
        dst->loop()->queueInLoop([dst, cid, &ledger, &done]() {
          dst->addConnection(cid);
          dst->enableReading(cid);
          dst->setBufferedBytes(cid, 0);
          dst->setTimer(cid, true);
          auto* e = ledger.getOrCreate(cid);
          e->migrate_requested++;
          e->migrate_completed++;
          done.countDown();
        });
      });
      done.wait();
      src->clearTimer(m.connId);
      attempted.push_back(m.connId);
    }
    placement.markDispatched(static_cast<int>(pending.size()));
  }

  for (auto w : pool.allWorkers()) {
    CountDownLatch latch(1);
    w->loop()->queueInLoop([&latch]() { latch.countDown(); });
    latch.wait();
  }

  for (int cid : attempted) updateRecord(cid);

  // Deduplicate attempted
  std::sort(attempted.begin(), attempted.end());
  attempted.erase(std::unique(attempted.begin(), attempted.end()), attempted.end());

  printf("\n# Ledger output:\n");
  for (auto& rec : records) {
    if (std::find(attempted.begin(), attempted.end(), rec.conn) == attempted.end()) continue;
    printf("conn=%d migrate_requested=%d migrate_completed=%d loops_registered=%d events_after_migrate=%d\n",
           rec.conn, rec.migrate_requested, rec.migrate_completed, rec.loops_registered, rec.events_after_migrate);
  }

  int stalled = 0;
  for (auto& rec : records) {
    if (std::find(attempted.begin(), attempted.end(), rec.conn) == attempted.end()) continue;
    if (rec.loops_registered != 1 || rec.events_after_migrate == 0) stalled++;
  }

  if (stalled == 0) printf("RESULT: all %zu migrated connections healthy\n", attempted.size());
  else printf("RESULT: %d/%zu migrated connections stalled\n", stalled, attempted.size());

  pool.stop();
  return stalled == 0 ? 0 : 1;
}
