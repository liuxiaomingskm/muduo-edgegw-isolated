add_library(edgegw_lib
  conn_bookkeeping.cc
  resume_dispatcher.cc
  stall_policy.cc
  table/ConnSlotTable.cc
)
target_link_libraries(edgegw_lib muduo_net)

add_executable(edgegw edgegw.cc)
target_link_libraries(edgegw edgegw_lib muduo_net)

add_subdirectory(repro)
