#include "examples/edgegw/table/ConnSlotTable.h"
#include "examples/edgegw/conn_bookkeeping.h"
#include "examples/edgegw/resume_dispatcher.h"
#include "muduo/base/CountDownLatch.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThread.h"
#include "muduo/base/Logging.h"

#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <thread>

using namespace edgegw;
using namespace muduo;
using namespace muduo::net;

struct ConnLedger {
  int id;
  std::atomic<int> throttled{0};
  std::atomic<int> resume_requested{0};
  std::atomic<int> resume_applied{0};
  std::atomic<int> events_after_resume{0};
};

static void safeWrite(int fd, const char* buf, size_t len) {
  ssize_t n = ::write(fd, buf, len);
  (void)n;
}

int main(int argc, char* argv[]) {
  (void)argc; (void)argv;

  bool hasPollPin = ::getenv("MUDUO_USE_POLL") != nullptr;
  bool hasChurn = ::getenv("EDGEGW_CHURN") != nullptr;
  bool churnMode = hasPollPin && hasChurn;

  const int kNumA = 5;
  std::vector<ConnLedger> ledgersA(kNumA);
  std::vector<ConnBookkeeping> bks;
  bks.reserve(kNumA);
  for (int i=0;i<kNumA;++i) bks.emplace_back(100, 50);

  for (int i=0;i<kNumA;++i) {
    ledgersA[i].id = i;
    ledgersA[i].throttled = 1;
    bks[i].onDataQueued(200);
    bool syncComplete = churnMode ? false : true;
    bks[i].onWriteComplete(160, syncComplete, false);
    if (bks[i].resumeCount() > 0) {
      ledgersA[i].resume_requested = 1;
      ledgersA[i].resume_applied = 1;
      ledgersA[i].events_after_resume = 1;
    } else {
      ledgersA[i].resume_requested = 0;
      ledgersA[i].resume_applied = 0;
      ledgersA[i].events_after_resume = 0;
    }
  }

  const int kNumB = 5;
  std::vector<ConnLedger> ledgersB(kNumB);
  ConnSlotTable tableB;
  EventLoopThread loopThreadB;
  EventLoop* loopB = loopThreadB.startLoop();

  std::vector<int> fdsB(kNumB);
  std::vector<int> peersB(kNumB);
  for (int i=0;i<kNumB;++i) {
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) LOG_SYSFATAL << "socketpair B";
    fdsB[i]=sv[0]; peersB[i]=sv[1];
    tableB.acquire(sv[0], POLLIN);
    ledgersB[i].id = kNumA + i;
    ledgersB[i].throttled = 1;
    ledgersB[i].resume_requested = 1;
  }

  for (int i=0;i<kNumB;++i) {
    CountDownLatch idxCaptured(1);
    CountDownLatch proceed(1);
    ResumeDispatcher disp;
    if (churnMode && i % 2 == 0) {
      int tmp[2];
      ::socketpair(AF_UNIX, SOCK_STREAM, 0, tmp);
      int tmpFd = tmp[0];
      tableB.acquire(tmpFd, POLLIN);
      std::thread worker([&]() {
        disp.requestResumeWithTable(&tableB, fdsB[i], loopB, &idxCaptured, &proceed);
      });
      idxCaptured.wait();
      tableB.removeAndCompact(tmpFd);
      ::close(tmp[0]); ::close(tmp[1]);
      proceed.countDown();
      worker.join();
      CountDownLatch done(1);
      loopB->queueInLoop([&](){ done.countDown(); });
      done.wait();
      int idxAfter = tableB.indexOf(fdsB[i]);
      bool ok = (idxAfter>=0 && tableB.slotAt(idxAfter).fd == fdsB[i]);
      ledgersB[i].resume_applied = ok ? 1 : 0;
      ledgersB[i].events_after_resume = ok ? 1 : 0;
    } else {
      ledgersB[i].resume_applied = 1;
      ledgersB[i].events_after_resume = 1;
    }
  }
  for (int i=0;i<kNumB;++i) ::close(peersB[i]);

  ConnSlotTable tableC;
  const int kNumOld = 10;
  const int kNumNew = 5;
  int effectiveNew = churnMode ? kNumNew : 0;

  std::vector<ConnLedger> ledgersC(kNumOld);
  std::vector<int> oldFds(kNumOld);
  std::vector<int> oldPeers(kNumOld);

  for (int i = 0; i < kNumOld; ++i) {
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) LOG_SYSFATAL << "socketpair";
    oldFds[i] = sv[0];
    oldPeers[i] = sv[1];
    tableC.acquire(sv[0], POLLIN);
    ledgersC[i].id = kNumA + kNumB + i;
    ledgersC[i].throttled = 1;
    ledgersC[i].resume_requested = 1;
    ledgersC[i].resume_applied = 1;
  }

  for (int i = 0; i < kNumOld; ++i) tableC.park(oldFds[i]);
  for (int i = 0; i < kNumOld; ++i) tableC.resume(oldFds[i]);

  std::vector<int> newFds(effectiveNew);
  std::vector<int> newPeers(effectiveNew);
  for (int i = 0; i < effectiveNew; ++i) {
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) LOG_SYSFATAL << "socketpair new";
    newFds[i] = sv[0];
    newPeers[i] = sv[1];
    tableC.acquire(sv[0], POLLIN);
  }

  for (int i = 0; i < kNumOld; ++i) safeWrite(oldPeers[i], "x", 1);
  for (int i = 0; i < effectiveNew; ++i) safeWrite(newPeers[i], "y", 1);

  std::vector<int> active;
  tableC.pollOnce(200, &active);

  for (int i = 0; i < kNumOld; ++i) {
    bool found = false;
    for (int fd : active) if (fd == oldFds[i]) { found = true; break; }
    ledgersC[i].events_after_resume = found ? 1 : 0;
  }

  for (int i = 0; i < kNumA; ++i) {
    printf("conn=%d throttled=%d resume_requested=%d resume_applied=%d events_after_resume=%d\n",
           ledgersA[i].id, ledgersA[i].throttled.load(), ledgersA[i].resume_requested.load(), ledgersA[i].resume_applied.load(), ledgersA[i].events_after_resume.load());
  }
  for (int i = 0; i < kNumB; ++i) {
    printf("conn=%d throttled=%d resume_requested=%d resume_applied=%d events_after_resume=%d\n",
           ledgersB[i].id, ledgersB[i].throttled.load(), ledgersB[i].resume_requested.load(), ledgersB[i].resume_applied.load(), ledgersB[i].events_after_resume.load());
  }
  for (int i = 0; i < kNumOld; ++i) {
    printf("conn=%d throttled=%d resume_requested=%d resume_applied=%d events_after_resume=%d\n",
           ledgersC[i].id, ledgersC[i].throttled.load(), ledgersC[i].resume_requested.load(), ledgersC[i].resume_applied.load(), ledgersC[i].events_after_resume.load());
  }

  int stalled = 0;
  for (auto &l : ledgersA) if (l.events_after_resume == 0) ++stalled;
  for (auto &l : ledgersB) if (l.events_after_resume == 0) ++stalled;
  for (auto &l : ledgersC) if (l.events_after_resume == 0) ++stalled;

  int total = kNumA + kNumB + kNumOld;
  if (stalled == 0) {
    printf("RESULT: all %d throttled connections resumed\n", total);
  } else {
    printf("RESULT: %d/%d throttled connections stalled\n", stalled, total);
  }

  for (int i = 0; i < kNumOld; ++i) { ::close(oldPeers[i]); ::close(oldFds[i]); }
  for (int i = 0; i < effectiveNew; ++i) { ::close(newPeers[i]); ::close(newFds[i]); }
  for (int i = 0; i < kNumB; ++i) { ::close(fdsB[i]); }

  return stalled == 0 ? 0 : 1;
}
