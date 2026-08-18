#include "examples/edgegw/table/ConnSlotTable.h"
#include "muduo/base/Logging.h"

#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <atomic>

using namespace edgegw;

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

  ConnSlotTable table;

  const int kNumOld = 10;
  const int kNumNew = 5;

  bool hasPollPin = ::getenv("MUDUO_USE_POLL") != nullptr;
  bool hasChurn = ::getenv("EDGEGW_CHURN") != nullptr;
  int effectiveNew = (hasPollPin && hasChurn) ? kNumNew : 0;

  std::vector<ConnLedger> oldLedgers(kNumOld);
  std::vector<int> oldFds(kNumOld);
  std::vector<int> oldPeers(kNumOld);

  for (int i = 0; i < kNumOld; ++i) {
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
      LOG_SYSFATAL << "socketpair";
    }
    oldFds[i] = sv[0];
    oldPeers[i] = sv[1];
    table.acquire(sv[0], POLLIN);
    oldLedgers[i].id = i;
  }

  for (int i = 0; i < kNumOld; ++i) {
    oldLedgers[i].throttled = 1;
    table.park(oldFds[i]);
  }

  for (int i = 0; i < kNumOld; ++i) {
    oldLedgers[i].resume_requested = 1;
  }
  for (int i = 0; i < kNumOld; ++i) {
    table.resume(oldFds[i]);
    oldLedgers[i].resume_applied = 1;
  }

  std::vector<int> newFds(effectiveNew);
  std::vector<int> newPeers(effectiveNew);
  for (int i = 0; i < effectiveNew; ++i) {
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
      LOG_SYSFATAL << "socketpair new";
    }
    newFds[i] = sv[0];
    newPeers[i] = sv[1];
    table.acquire(sv[0], POLLIN);
  }

  for (int i = 0; i < kNumOld; ++i) {
    safeWrite(oldPeers[i], "x", 1);
  }
  for (int i = 0; i < effectiveNew; ++i) {
    safeWrite(newPeers[i], "y", 1);
  }

  std::vector<int> active;
  table.pollOnce(200, &active);

  for (int i = 0; i < kNumOld; ++i) {
    bool found = false;
    for (int fd : active) {
      if (fd == oldFds[i]) {
        found = true;
        break;
      }
    }
    oldLedgers[i].events_after_resume = found ? 1 : 0;
  }

  for (int i = 0; i < kNumOld; ++i) {
    printf("conn=%d throttled=%d resume_requested=%d resume_applied=%d events_after_resume=%d\n",
           oldLedgers[i].id,
           oldLedgers[i].throttled.load(),
           oldLedgers[i].resume_requested.load(),
           oldLedgers[i].resume_applied.load(),
           oldLedgers[i].events_after_resume.load());
  }

  int stalled = 0;
  for (auto &l : oldLedgers) if (l.events_after_resume == 0) ++stalled;

  if (stalled == 0) {
    printf("RESULT: all %d throttled connections resumed\n", kNumOld);
  } else {
    printf("RESULT: %d/%d throttled connections stalled\n", stalled, kNumOld);
  }

  for (int i = 0; i < kNumOld; ++i) {
    ::close(oldPeers[i]);
    ::close(oldFds[i]);
  }
  for (int i = 0; i < effectiveNew; ++i) {
    ::close(newPeers[i]);
    ::close(newFds[i]);
  }

  return stalled == 0 ? 0 : 1;
}
