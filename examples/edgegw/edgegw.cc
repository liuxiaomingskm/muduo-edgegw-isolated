#include "worker_pool.h"
#include "placement.h"
#include "migrator.h"

#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/TcpServer.h"

using namespace muduo;
using namespace muduo::net;

int main() {
  LOG_INFO << "edgegw example with rebalance";
  EventLoop loop;
  edgegw::WorkerPool pool(4);
  pool.start();
  edgegw::PlacementPolicy policy;
  (void)policy;
  loop.loop();
}
