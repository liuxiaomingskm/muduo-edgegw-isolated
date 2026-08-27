set(http_SRCS
  HttpServer.cc
  HttpResponse.cc
  HttpContext.cc
  HttpConditional.cc
  )

add_library(muduo_http ${http_SRCS})
target_link_libraries(muduo_http muduo_net)

install(TARGETS muduo_http DESTINATION lib)
set(HEADERS
  HttpContext.h
  HttpRequest.h
  HttpResponse.h
  HttpServer.h
  HttpConditional.h
  )
install(FILES ${HEADERS} DESTINATION include/muduo/net/http)

if(MUDUO_BUILD_EXAMPLES)
add_executable(httpserver_test tests/HttpServer_test.cc)
target_link_libraries(httpserver_test muduo_http)

if(BOOSTTEST_LIBRARY)
add_executable(httprequest_unittest tests/HttpRequest_unittest.cc)
target_link_libraries(httprequest_unittest muduo_http boost_unit_test_framework)
endif()

endif()

# Tool for evidence-only probe
add_executable(condreq_probe tools/condreq_probe.cc)
target_link_libraries(condreq_probe muduo_http)
