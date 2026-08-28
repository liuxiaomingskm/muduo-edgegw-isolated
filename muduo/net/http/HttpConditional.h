#ifndef MUDUO_NET_HTTP_HTTPCONDITIONAL_H
#define MUDUO_NET_HTTP_HTTPCONDITIONAL_H

#include "muduo/net/http/HttpRequest.h"
#include <string>
#include <map>
#include <vector>

namespace muduo {
namespace net {
namespace http {

enum class ConditionalOutcome {
  kNotFound,
  kFullRepresentation,
  kPreconditionFailed,
  kNotModified,
  kNotModifiedSuppressed,
  kPartialContent,
  kRangeNotSatisfiable,
  kRangeIgnored,
};

enum class ConditionalRoute {
  kProtocolDefault,
  kAssetIntegrity,
  kRepresentationReuse,
};

struct ConditionalPolicyRecord {
  std::string id;
  std::string assetClass = "*";
  std::string method = "*";
  ConditionalOutcome outcome = ConditionalOutcome::kFullRepresentation;
  std::string effectiveProfile;
  std::string retiredProfile;
  int status = 200;
  ConditionalRoute route = ConditionalRoute::kProtocolDefault;
};

struct Resource {
  std::string etag;
  std::string lastModified;
  long contentLength = 1024;
  bool exists = true;
  std::string assetClass = "static";
  std::string profileVersion = "2024-09";
  std::vector<std::string> availableProfileVersions;
  std::vector<ConditionalPolicyRecord> conditionalPolicies;
};

struct ConditionalDecision {
  int status;
  ConditionalOutcome outcome;
  ConditionalRoute route;
};

const char* conditionalOutcomeName(ConditionalOutcome outcome);
const char* conditionalRouteName(ConditionalRoute route);
std::string conditionalProfileIdentity(const Resource& resource);

ConditionalDecision evaluateConditionalDecision(const HttpRequest& request,
                                                 const Resource& resource);
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
