#pragma once
#include "worker_pool.h"

namespace edgegw {

// Loop-affine state that must travel with connection: interest set, buffered output, timer.
// Class C defect lives here.

class ConnectionState {
 public:
  // Transfer state from src to dst for connId. Returns true if armed.
  static bool transfer(int connId, Worker* src, Worker* dst);

  // Variant that checks buffered condition
  static bool transferWithBufferedCheck(int connId, Worker* src, Worker* dst, int bufferedBytes);
};

} // namespace edgegw
