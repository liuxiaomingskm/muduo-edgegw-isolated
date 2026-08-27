#include "muduo/net/http/HttpConditional.h"
#include <cctype>
#include <algorithm>
#include <vector>
#include <sstream>

namespace muduo {
namespace net {
namespace http {

std::string trim(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end-1]))) --end;
  return s.substr(start, end-start);
}

std::string stripWeakPrefix(const std::string& etag) {
  std::string t = trim(etag);
  if (t.size() >= 2 && t[0] == 'W' && t[1] == '/') {
    return trim(t.substr(2));
  }
  return t;
}

std::string normalizeEtag(const std::string& etag) {
  return stripWeakPrefix(etag);
}

static std::vector<std::string> parseEtagList(const std::string& list) {
  std::vector<std::string> result;
  std::string cur;
  bool inQuotes = false;
  for (size_t i=0;i<list.size();++i) {
    char c = list[i];
    if (c == '"') inQuotes = !inQuotes;
    if (c == ',' && !inQuotes) {
      result.push_back(trim(cur));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) result.push_back(trim(cur));
  return result;
}

bool strongCompare(const std::string& etagList, const std::string& currentEtag) {
  std::string cur = trim(currentEtag);
  if (cur.empty()) return false;
  if (cur.size()>=2 && cur[0]=='W' && cur[1]=='/') return false;
  auto list = parseEtagList(etagList);
  for (auto& item : list) {
    std::string t = trim(item);
    if (t == "*") return true;
    if (t.size()>=2 && t[0]=='W' && t[1]=='/') continue;
    if (t == cur) return true;
  }
  return false;
}

bool weakCompare(const std::string& etagList, const std::string& currentEtag) {
  std::string cur = normalizeEtag(currentEtag);
  if (cur.empty()) return false;
  auto list = parseEtagList(etagList);
  for (auto& item : list) {
    std::string t = trim(item);
    if (t == "*") return true;
    std::string norm = normalizeEtag(t);
    if (norm == cur) return true;
  }
  return false;
}

bool isDateModifiedAfter(const std::string& headerDate, const std::string& resourceDate) {
  if (headerDate.empty() || resourceDate.empty()) return false;
  return trim(headerDate) != trim(resourceDate) && trim(resourceDate) > trim(headerDate);
}

bool isDateNotModifiedSince(const std::string& headerDate, const std::string& resourceDate) {
  if (headerDate.empty() || resourceDate.empty()) return false;
  return trim(resourceDate) <= trim(headerDate);
}

bool ifRangeMatches(const std::string& ifRangeValue, const Resource& resource) {
  std::string v = trim(ifRangeValue);
  if (v.empty()) return false;
  if (v[0] == '"' || (v.size()>=2 && v[0]=='W' && v[1]=='/')) {
    return strongCompare(v, resource.etag);
  } else {
    return isDateNotModifiedSince(v, resource.lastModified);
  }
}

bool isRangeSatisfiable(const std::string& rangeHeader, const Resource& resource) {
  std::string r = trim(rangeHeader);
  if (r.empty()) return false;
  auto pos = r.find('=');
  if (pos == std::string::npos) return false;
  std::string rangePart = r.substr(pos+1);
  auto dash = rangePart.find('-');
  if (dash == std::string::npos) return false;
  std::string startStr = trim(rangePart.substr(0, dash));
  std::string endStr = trim(rangePart.substr(dash+1));
  try {
    long start = std::stol(startStr);
    if (start >= resource.contentLength) return false;
    if (!endStr.empty()) {
      long end = std::stol(endStr);
      if (end >= resource.contentLength) return false;
      if (start > end) return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

int evaluateConditionalRequest(const HttpRequest& request, const Resource& resource) {
  if (!resource.exists) {
    return 404;
  }

  std::string ifMatch = request.getHeader("If-Match");
  std::string ifNoneMatch = request.getHeader("If-None-Match");
  std::string ifModifiedSince = request.getHeader("If-Modified-Since");
  std::string ifUnmodifiedSince = request.getHeader("If-Unmodified-Since");
  std::string ifRange = request.getHeader("If-Range");
  std::string range = request.getHeader("Range");

  bool hasIfMatch = !trim(ifMatch).empty();
  bool hasIfNoneMatch = !trim(ifNoneMatch).empty();
  bool hasIfModifiedSince = !trim(ifModifiedSince).empty();
  bool hasIfUnmodifiedSince = !trim(ifUnmodifiedSince).empty();
  bool hasIfRange = !trim(ifRange).empty();
  bool hasRange = !trim(range).empty();

  if (hasIfMatch) {
    if (!strongCompare(ifMatch, resource.etag)) {
      return 412;
    }
  } else if (hasIfUnmodifiedSince) {
    if (isDateModifiedAfter(ifUnmodifiedSince, resource.lastModified)) {
      return 412;
    }
  }

  if (hasIfNoneMatch) {
    if (weakCompare(ifNoneMatch, resource.etag)) {
      if (request.method() == HttpRequest::kGet || request.method() == HttpRequest::kHead) {
        return 304;
      } else {
        return 412;
      }
    }
  } else if (hasIfModifiedSince) {
    if (request.method() == HttpRequest::kGet || request.method() == HttpRequest::kHead) {
      if (isDateNotModifiedSince(ifModifiedSince, resource.lastModified)) {
        return 304;
      }
    }
  }

  if (hasRange) {
    if (hasIfRange) {
      std::string v = trim(ifRange);
      bool matches = false;
      if (!v.empty() && (v[0] == '"' || (v.size()>=2 && v[0]=='W' && v[1]=='/'))) {
        matches = strongCompare(v, resource.etag);
      } else {
        matches = isDateNotModifiedSince(v, resource.lastModified);
      }
      if (matches) {
        if (isRangeSatisfiable(range, resource)) return 206;
        else return 416;
      } else {
        return 200;
      }
    } else {
      if (isRangeSatisfiable(range, resource)) return 206;
      else return 416;
    }
  }

  return 200;
}

} // namespace http
} // namespace net
} // namespace muduo
