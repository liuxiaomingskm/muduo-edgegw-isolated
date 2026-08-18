#include "examples/edgegw/conn_bookkeeping.h"
#include "examples/edgegw/resume_dispatcher.h"

#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpServer.h"
#include "muduo/net/InetAddress.h"

using namespace muduo;
using namespace muduo::net;

int main() {
  LOG_INFO << "edgegw example";
  EventLoop loop;
  InetAddress addr(9988);
  TcpServer server(&loop, addr, "edgegw");
  edgegw::ConnBookkeeping bookkeeping(256*1024, 64*1024);
  edgegw::ResumeDispatcher dispatcher;
  (void)bookkeeping;
  (void)dispatcher;
  loop.loop();
}
