add_library(edgegw_lib
  worker.cc
  worker_pool.cc
  placement.cc
  migrator.cc
  connection_state.cc
  recovery_policy.cc
  ledger.cc
)
target_link_libraries(edgegw_lib muduo_net)

add_executable(edgegw edgegw.cc)
target_link_libraries(edgegw edgegw_lib muduo_net)

add_subdirectory(repro)
