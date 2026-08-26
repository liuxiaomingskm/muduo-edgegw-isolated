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
      if (i == 70 || i == 71) {
        worker->setBufferedBytes(i, 2048);
        worker->setTimer(i, true);
        worker->setActiveTxn(i, true);
        worker->setTxnGen(i, 1);
        worker->setLastWriteAgeMs(i, 120);
      } else if (i == 72) {
        worker->setBufferedBytes(i, 1024);
        worker->setTimer(i, true);
        worker->setTxnGen(i, 0);
        worker->setLastWriteAgeMs(i, 120);
      } else if (i == 50) {
        worker->setBufferedBytes(i, 1024);
        worker->setTimer(i, true);
        worker->setTxnGen(i, 1);
        worker->setLastWriteAgeMs(i, 120);
      } else if ((i >= 0 && i < 7) || (i >= 10 && i < 17)) {
        worker->setBufferedBytes(i, 1024);
        worker->setTimer(i, false);
        worker->setTxnGen(i, 0);
        worker->setLastWriteAgeMs(i, 800);
      } else if (i == 51 || i == 60) {
        worker->setBufferedBytes(i, 2048);
        worker->setTimer(i, true);
        worker->setTxnGen(i, 0);
        worker->setLastWriteAgeMs(i, 800);
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

  // Monitoring uses uniform stalled classification
  int stalled = pool.countStalled();
  printf("# stalled_for_recovery=%d (hasBufferedAndTimer count)\n", stalled);

  if (fleet) {
    // Same sequence as rebalance_repro to produce same registry state
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
    for (auto& mm : pendingA2) {
      Worker* src = pool.getWorker(mm.src);
      Worker* dst = pool.getWorker(mm.dst);
      Migrator::migrate(mm.connId, src, dst, &ledger);
      ConnectionState::transferWithBufferedCheck(mm.connId, src, dst, 1024);
    }
    placement.markDispatched(static_cast<int>(pendingA2.size()));

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
      if (dstB1) { dstB1->setTxnGen(bConn, 1); dstB1->setLastWriteAgeMs(bConn, 120); }
      if (dstB2) { dstB2->setTxnGen(bConn, 1); dstB2->setLastWriteAgeMs(bConn, 120); }
    }

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
      }
    }

    for (int dConn : {70, 71}) {
      int ownerD = pool.ownerOf(dConn);
      Worker* srcD = pool.getWorker(ownerD >= 0 ? ownerD : 0);
      Worker* dstD = pool.getWorker((ownerD + 1) % numWorkers);
      if (srcD && dstD && srcD->id() != dstD->id()) {
        srcD->setActiveTxn(dConn, true);
        int buf = (dConn == 70) ? 2048 : 1024;
        srcD->setBufferedBytes(dConn, buf);
        srcD->setTimer(dConn, true);
        Migrator::migrate(dConn, srcD, dstD, &ledger);
      }
    }

    {
      int eConn = 72;
      int ownerE = pool.ownerOf(eConn);
      Worker* srcE = pool.getWorker(ownerE >= 0 ? ownerE : 0);
      if (srcE) {
        srcE->setBufferedBytes(eConn, 1024);
        srcE->setTimer(eConn, true);
        ledger.getOrCreate(eConn)->migrate_requested++;
      }
    }

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
      }
    }
  }

  for (auto w : pool.allWorkers()) {
    CountDownLatch latch(1);
    w->loop()->queueInLoop([&latch]() { latch.countDown(); });
    latch.wait();
  }

  printf("\n# Worker dump output (authoritative):\n");
  for (auto w : pool.allWorkers()) {
    auto conns = w->getConnections();
    for (int cid : conns) {
      int reading = w->isReadingEnabled(cid) ? 1 : 0;
      int buffered = w->getBufferedBytes(cid);
      int timer = w->hasTimer(cid) ? 1 : 0;
      int txnGen = w->getTxnGen(cid);
      int writeAge = w->getLastWriteAgeMs(cid);
      int draining = w->isDraining(cid) ? 1 : 0;
      printf("worker=%d conn=%d reading=%d buffered=%d timer=%d txnGen=%d writeAge=%d draining=%d\n",
             w->id(), cid, reading, buffered, timer, txnGen, writeAge, draining);
    }
  }

  int totalOwners = 0;
  for (int cid : allConnIds) totalOwners += pool.loopsRegistered(cid);
  printf("RESULT: total owner entries %d\n", totalOwners);

  pool.stop();
  return 0;
}
