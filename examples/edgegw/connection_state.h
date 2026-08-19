#pragma once
#include "worker_pool.h"

namespace edgegw {

class ConnectionState {
 public:
  static bool transfer(int connId, Worker* src, Worker* dst);
  static bool transferWithBufferedCheck(int connId, Worker* src, Worker* dst, int bufferedBytes);
};

} // namespace edgegw
