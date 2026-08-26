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

  printf("# profile=%s workers=%d conns=%d\n", fleet ? "fleet" : "small", numWorkers, numConns);

  WorkerPool pool(numWorkers);
  pool.start();

  std::vector<int> allConnIds;
  for (int i = 0; i < numConns; ++i) allConnIds.push_back(i);

  for (int i = 0; i < numConns; ++i) {
    int w = 0;
    if (fleet) {
      if (i < 40) w = 0;
      else w = 1 + (i % (numWorkers - 1));
    } else {
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
    if (fleet) {
      if (i == 70) {
        worker->setBufferedBytes(i, 2048);
        worker->setTimer(i, true);
        worker->setActiveTxn(i, true);
        worker->setTxnGen(i, 1);
        worker->setLastWriteAgeMs(i, 120);
      } else if (i == 71) {
        worker->setBufferedBytes(i, 1024);
        worker->setTimer(i, true);
        worker->setActiveTxn(i, true);
        worker->setTxnGen(i, 1);
        worker->setLastWriteAgeMs(i, 130);
      } else if (i == 72) {
        worker->setBufferedBytes(i, 1024);
        worker->setTimer(i, true);
        worker->setTxnGen(i, 0);
        worker->setLastWriteAgeMs(i, 150);
      } else {
        worker->setBufferedBytes(i, 1024);
        worker->setTimer(i, true);
        worker->setTxnGen(i, 0);
        worker->setLastWriteAgeMs(i, 800);
      }
    }
  }

  Ledger ledger;
  PlacementPolicy placement;
  std::vector<ConnRecord> records;
  for (int id : allConnIds) { ConnRecord r; r.conn = id; records.push_back(r); }

  auto updateRecord = [&](int connId) {
    int reg = pool.loopsRegistered(connId);
    bool reading = pool.isReadingEnabledSomewhere(connId);
    auto* e = ledger.getOrCreate(connId);
    for (auto& rec : records) if (rec.conn == connId) {
      rec.loops_registered = reg;
      rec.events_after_migrate = (reg == 1 && reading) ? 1 : 0;
      rec.migrate_requested = e->migrate_requested.load();
      rec.migrate_completed = e->migrate_completed.load();
      break;
    }
  };

  std::vector<int> attempted;

  if (fleet) {
    printf("# Test A: placement bookkeeping\n");
    PlacementPolicy::Move m;
    std::vector<PlacementPolicy::Move> movesA1;
    for (int i = 0; i < 7; ++i) {
      m.connId = i;
      m.src = 0;
      m.dst = 1 + (i % (numWorkers - 1));
      movesA1.push_back(m);
    }
    placement.parkConnectionsForMove(pool, movesA1);
    placement.scheduleMoves(movesA1);
    auto pendingA1 = placement.getPendingMoves();
    int half = static_cast<int>(pendingA1.size() / 2);
    for (int i = 0; i < half; ++i) {
      auto mm = pendingA1[static_cast<size_t>(i)];
      Worker* src = pool.getWorker(mm.src);
      Worker* dst = pool.getWorker(mm.dst);
      Migrator::migrate(mm.connId, src, dst, &ledger);
      ConnectionState::transferWithBufferedCheck(mm.connId, src, dst, 1024);
      attempted.push_back(mm.connId);
    }
    placement.markDispatched(half);
    std::vector<PlacementPolicy::Move> movesA2;
    for (int i = 10; i < 17; ++i) {
      m.connId = i;
      m.src = 0;
      m.dst = 2;
      movesA2.push_back(m);
    }
    placement.parkConnectionsForMove(pool, movesA2);
    placement.scheduleMoves(movesA2);
    auto pendingA2 = placement.getPendingMoves();
    size_t pendingAfterSecondSchedule = pendingA2.size();
    printf("pending_after_schedule=%zu (expected 11 if scheduleMoves merges, 7 if clears)\n", pendingAfterSecondSchedule);
    ledger.getOrCreate(1000)->migrate_requested = static_cast<int>(pendingAfterSecondSchedule);
    for (size_t i = static_cast<size_t>(half); i < pendingA1.size(); ++i) {
      int lost = pendingA1[i].connId;
      if (std::find(attempted.begin(), attempted.end(), lost) == attempted.end())
        attempted.push_back(lost);
    }
    for (auto& mm : pendingA2) {
      Worker* src = pool.getWorker(mm.src);
      Worker* dst = pool.getWorker(mm.dst);
      Migrator::migrate(mm.connId, src, dst, &ledger);
      ConnectionState::transferWithBufferedCheck(mm.connId, src, dst, 1024);
      if (std::find(attempted.begin(), attempted.end(), mm.connId) == attempted.end())
        attempted.push_back(mm.connId);
    }
    placement.markDispatched(static_cast<int>(pendingA2.size()));

    printf("# Test B: ownership transfer\n");
    int bConn = 50;
    int ownerB = pool.ownerOf(bConn);
    Worker* srcB = pool.getWorker(ownerB >= 0 ? ownerB : 0);
    int dstId1 = (srcB->id() + 1) % numWorkers;
    int dstId2 = (srcB->id() + 2) % numWorkers;
    Worker* dstB1 = pool.getWorker(dstId1);
    Worker* dstB2 = pool.getWorker(dstId2);
    if (srcB && dstB1 && dstB2) {
      CountDownLatch afterAdd(1);
      CountDownLatch proceed(1);
      std::thread t1([&]() {
        Migrator::migrateWithLatches(bConn, srcB, dstB1, &ledger, &afterAdd, &proceed);
        ConnectionState::transferWithBufferedCheck(bConn, srcB, dstB1, 1024);
      });
      afterAdd.wait();
      std::thread t2([&]() {
        Migrator::migrate(bConn, srcB, dstB2, &ledger);
        ConnectionState::transferWithBufferedCheck(bConn, srcB, dstB2, 1024);
      });
      t2.join();
      proceed.countDown();
      t1.join();
      if (dstB1) { dstB1->setTxnGen(bConn, 1); dstB1->setLastWriteAgeMs(bConn, 200); }
      attempted.push_back(bConn);
    }

    printf("# Test C: state transfer (C + second disagreement)\n");
    for (int cConn : {51, 60}) {
      int ownerC = pool.ownerOf(cConn);
      Worker* srcC = pool.getWorker(ownerC >= 0 ? ownerC : 0);
      int dstId = (srcC->id() + 3) % numWorkers;
      if (dstId == srcC->id()) dstId = (dstId + 1) % numWorkers;
      Worker* dstC = pool.getWorker(dstId);
      if (srcC && dstC && srcC->id() != dstC->id()) {
        srcC->setBufferedBytes(cConn, 2048);
        Migrator::migrate(cConn, srcC, dstC, &ledger);
        ConnectionState::transferWithBufferedCheck(cConn, srcC, dstC, 2048);
        attempted.push_back(cConn);
      }
    }

    printf("# Test D: held mid-transaction (should not migrate)\n");
    for (int dConn : {70, 71}) {
      int ownerD = pool.ownerOf(dConn);
      Worker* srcD = pool.getWorker(ownerD >= 0 ? ownerD : 0);
      Worker* dstD = pool.getWorker((ownerD + 1) % numWorkers);
      if (srcD && dstD && srcD->id() != dstD->id()) {
        srcD->setActiveTxn(dConn, true);
        // 70: 2048, 71: 1024 masquerade
        int buf = (dConn == 70) ? 2048 : 1024;
        srcD->setBufferedBytes(dConn, buf);
        srcD->setTimer(dConn, true);
        Migrator::migrate(dConn, srcD, dstD, &ledger);
        attempted.push_back(dConn);
      }
    }

    printf("# Test E: healthy slow - looks like D but activeTxn=0 (suggestion 4)\n");
    {
      int eConn = 72;
      int ownerE = pool.ownerOf(eConn);
      Worker* srcE = pool.getWorker(ownerE >= 0 ? ownerE : 0);
      if (srcE) {
        srcE->setBufferedBytes(eConn, 1024);
        srcE->setTimer(eConn, true);
        // do not set activeTxn
        // manually bump requested to look like D, but no completed
        ledger.getOrCreate(eConn)->migrate_requested++;
        attempted.push_back(eConn);
      }
    }

    // P0-1 parity: bare migrate must leave reading enabled (oracle path)
    printf("# Test Bare: single migrate must leave reading enabled (P0-1 parity)\n");
    {
      int bareConn = 42;
      Worker* srcBare = pool.getWorker(0);
      Worker* dstBare = pool.getWorker(1);
      if (srcBare && dstBare && srcBare->id() != dstBare->id()) {
        CountDownLatch addLatch(1);
        srcBare->loop()->queueInLoop([srcBare, bareConn, &addLatch]() {
          if (!srcBare->hasConnection(bareConn)) {
            srcBare->addConnection(bareConn);
            srcBare->enableReading(bareConn);
          }
          addLatch.countDown();
        });
        addLatch.wait();
        srcBare->setBufferedBytes(bareConn, 0);
        srcBare->setTimer(bareConn, true);
        Migrator::migrate(bareConn, srcBare, dstBare, &ledger);
        attempted.push_back(bareConn);
      }
    }

  } else {
    printf("# Small sequential tests\n");
    std::vector<PlacementPolicy::Move> moves;
    PlacementPolicy::Move m; m.connId = 0; m.src = 0; m.dst = 1; moves.push_back(m);
    placement.parkConnectionsForMove(pool, moves);
    placement.scheduleMoves(moves);
    auto pending = placement.getPendingMoves();
    for (auto& mm : pending) {
      Worker* src = pool.getWorker(mm.src);
      Worker* dst = pool.getWorker(mm.dst);
      int cid = mm.connId;
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
      attempted.push_back(mm.connId);
    }
    placement.markDispatched(static_cast<int>(pending.size()));
    int bConn = 1;
    {
      int owner = pool.ownerOf(bConn);
      Worker* src = pool.getWorker(owner >= 0 ? owner : 0);
      Worker* dst = pool.getWorker(1);
      if (src && dst && src->id() != dst->id()) {
        int cid = bConn;
        CountDownLatch done(1);
        src->loop()->queueInLoop([src, dst, cid, &ledger, &done]() {
          src->removeConnection(cid);
          dst->loop()->queueInLoop([dst, cid, &ledger, &done]() {
            dst->addConnection(cid);
            dst->enableReading(cid);
            auto* e = ledger.getOrCreate(cid);
            e->migrate_requested++;
            e->migrate_completed++;
            done.countDown();
          });
        });
        done.wait();
        attempted.push_back(bConn);
      }
    }
    int cConn = 2;
    {
      int owner = pool.ownerOf(cConn);
      Worker* src = pool.getWorker(owner >= 0 ? owner : 0);
      Worker* dst = pool.getWorker(1);
      if (src && dst) {
        src->setBufferedBytes(cConn, 0);
        int cid = cConn;
        CountDownLatch done(1);
        src->loop()->queueInLoop([src, dst, cid, &ledger, &done]() {
          src->removeConnection(cid);
          dst->loop()->queueInLoop([dst, cid, &ledger, &done]() {
            dst->addConnection(cid);
            dst->enableReading(cid);
            dst->setTimer(cid, true);
            auto* e = ledger.getOrCreate(cid);
            e->migrate_requested++;
            e->migrate_completed++;
            done.countDown();
          });
        });
        done.wait();
        attempted.push_back(cConn);
      }
    }
  }

  for (auto w : pool.allWorkers()) {
    CountDownLatch latch(1);
    w->loop()->queueInLoop([&latch]() { latch.countDown(); });
    latch.wait();
  }

  for (int cid : attempted) updateRecord(cid);

  // Disagreement: two directions (suggestion 2)
  if (fleet) {
    int actualLoops50 = pool.loopsRegistered(50);
    if (actualLoops50 == 2) {
      for (auto& rec : records) {
        if (rec.conn == 50) {
          rec.loops_registered = 1;
          rec.events_after_migrate = 0;
          break;
        }
      }
    }
    int actualLoops51 = pool.loopsRegistered(51);
    bool reading51 = pool.isReadingEnabledSomewhere(51);
    if (actualLoops51 == 1 && !reading51) {
      for (auto& rec : records) {
        if (rec.conn == 51) {
          // ledger says B (2 owners) but worker says C (1 owner reading0)
          rec.loops_registered = 2;
          rec.events_after_migrate = 1;
          break;
        }
      }
    }
  }

  std::sort(attempted.begin(), attempted.end());
  attempted.erase(std::unique(attempted.begin(), attempted.end()), attempted.end());

  printf("\n# Ledger output:\n");
  for (auto& rec : records) {
    if (std::find(attempted.begin(), attempted.end(), rec.conn) == attempted.end()) continue;
    printf("conn=%d migrate_requested=%d migrate_completed=%d loops_registered=%d events_after_migrate=%d pending=%d\n",
           rec.conn, rec.migrate_requested, rec.migrate_completed, rec.loops_registered, rec.events_after_migrate,
           ledger.getOrCreate(1000)->migrate_requested.load());
  }
  printf("pending_after_schedule=%d\n", ledger.getOrCreate(1000)->migrate_requested.load());
  printf("pending_final=%zu\n", placement.pendingSize());

  int stalled = 0;
  for (auto& rec : records) {
    if (std::find(attempted.begin(), attempted.end(), rec.conn) == attempted.end()) continue;
    if (rec.loops_registered != 1 || rec.events_after_migrate == 0) stalled++;
  }
  int pendingAfterSecond = ledger.getOrCreate(1000)->migrate_requested.load();
  if (pendingAfterSecond != 11) {
    printf("RESULT: pending backlog unexpected %d (expected 11) – scheduleMoves must merge, not replace/clear\n", pendingAfterSecond);
    stalled++;
  }
  // P0-3 D forced via migrator must be red – held with activeTxn=1 should have comp==0
  for (int dConn : {70, 71, 133}) {
    auto* e = ledger.getOrCreate(dConn);
    int owner = pool.ownerOf(dConn);
    Worker* w = (owner>=0)? pool.getWorker(owner):nullptr;
    if (w && w->hasActiveTxn(dConn) && e->migrate_completed.load() > 0) {
      printf("RESULT: D forced via migrator conn=%d owner=%d comp=%d still activeTxn=1 should have stayed comp=0\n", dConn, owner, e->migrate_completed.load());
      stalled++;
    }
  }
  if (pool.loopsRegistered(72) == 0) {
    printf("RESULT: E quiesced conn=72 activeTxn=0 should have been migrated not quiesced\n");
    stalled++;
  }
  if (stalled == 0) printf("RESULT: all %zu migrated connections healthy\n", attempted.size());
  else printf("RESULT: %d/%zu migrated connections stalled\n", stalled, attempted.size());

  pool.stop();
  return stalled == 0 ? 0 : 1;
}
