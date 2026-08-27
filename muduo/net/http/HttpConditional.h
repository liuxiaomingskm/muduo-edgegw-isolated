#ifndef MUDUO_NET_HTTP_HTTPCONDITIONAL_H
#define MUDUO_NET_HTTP_HTTPCONDITIONAL_H

#include "muduo/net/http/HttpRequest.h"
#include <string>
#include <map>

namespace muduo {
namespace net {
namespace http {

struct Resource {
  std::string etag;
  std::string lastModified;
  long contentLength = 1024;
  bool exists = true;
  std::string assetClass = "static";
};

int evaluateConditionalRequest(const HttpRequest& request, const Resource& resource);

bool strongCompare(const std::string& etagList, const std::string& currentEtag);
bool weakCompare(const std::string& etagList, const std::string& currentEtag);
bool isDateModifiedAfter(const std::string& headerDate, const std::string& resourceDate);
bool isDateNotModifiedSince(const std::string& headerDate, const std::string& resourceDate);
bool ifRangeMatches(const std::string& ifRangeValue, const Resource& resource);
bool isRangeSatisfiable(const std::string& rangeHeader, const Resource& resource);

std::string trim(const std::string& s);
std::string stripWeakPrefix(const std::string& etag);
std::string normalizeEtag(const std::string& etag);

} // namespace http
} // namespace net
} // namespace muduo

#endif
