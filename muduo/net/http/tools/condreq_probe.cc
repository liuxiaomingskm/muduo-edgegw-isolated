#include "muduo/net/http/HttpConditional.h"

#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>

static std::string readFile(const std::vector<std::string>& paths) {
  for (auto& p : paths) {
    std::ifstream f(p);
    if (f.good()) {
      std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      return c;
    }
  }
  return "";
}

static size_t findUnescapedQuote(const std::string& s, size_t from) {
  for (size_t i=from;i<s.size();++i) {
    if (s[i]=='"') {
      size_t bs=0;
      size_t j=i;
      while (j>0 && s[--j]=='\\') ++bs;
      if (bs%2==0) return i;
    }
  }
  return std::string::npos;
}

static std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size()+8);
  for (char c : s) {
    if (c=='\\') out += "\\\\";
    else if (c=='"') out += "\\\"";
    else if (c=='\n') out += "\\n";
    else if (c=='\r') out += "\\r";
    else if (c=='\t') out += "\\t";
    else out.push_back(c);
  }
  return out;
}

static std::string jsonStringField(const std::string& object,
                                   const std::string& key,
                                   const std::string& fallback = "") {
  const std::string pattern = "\"" + key + "\"";
  const size_t keyPos = object.find(pattern);
  if (keyPos == std::string::npos) return fallback;
  const size_t colon = object.find(':', keyPos + pattern.size());
  if (colon == std::string::npos) return fallback;
  const size_t first = findUnescapedQuote(object, colon + 1);
  if (first == std::string::npos) return fallback;
  const size_t second = findUnescapedQuote(object, first + 1);
  if (second == std::string::npos) return fallback;
  return object.substr(first + 1, second - first - 1);
}

static int jsonIntField(const std::string& object,
                        const std::string& key,
                        int fallback) {
  const std::string pattern = "\"" + key + "\"";
  const size_t keyPos = object.find(pattern);
  if (keyPos == std::string::npos) return fallback;
  const size_t colon = object.find(':', keyPos + pattern.size());
  if (colon == std::string::npos) return fallback;
  size_t start = colon + 1;
  while (start < object.size() && std::isspace(static_cast<unsigned char>(object[start]))) {
    ++start;
  }
  size_t end = start;
  while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end]))) {
    ++end;
  }
  if (end == start) return fallback;
  try {
    return std::stoi(object.substr(start, end - start));
  } catch (...) {
    return fallback;
  }
}

static std::vector<std::string> jsonStringArray(const std::string& document,
                                                 const std::string& key) {
  std::vector<std::string> values;
  const std::string pattern = "\"" + key + "\"";
  const size_t keyPos = document.find(pattern);
  if (keyPos == std::string::npos) return values;
  const size_t open = document.find('[', keyPos + pattern.size());
  const size_t close = document.find(']', open);
  if (open == std::string::npos || close == std::string::npos) return values;
  size_t pos = open + 1;
  while (pos < close) {
    const size_t first = findUnescapedQuote(document, pos);
    if (first == std::string::npos || first >= close) break;
    const size_t second = findUnescapedQuote(document, first + 1);
    if (second == std::string::npos || second > close) break;
    values.push_back(document.substr(first + 1, second - first - 1));
    pos = second + 1;
  }
  return values;
}

static std::vector<std::string> jsonObjectArray(const std::string& document,
                                                 const std::string& key) {
  std::vector<std::string> objects;
  const std::string pattern = "\"" + key + "\"";
  const size_t keyPos = document.find(pattern);
  if (keyPos == std::string::npos) return objects;
  const size_t open = document.find('[', keyPos + pattern.size());
  if (open == std::string::npos) return objects;

  bool inString = false;
  bool escaped = false;
  int depth = 0;
  size_t objectStart = std::string::npos;
  for (size_t i = open + 1; i < document.size(); ++i) {
    const char c = document[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') inString = false;
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == '{') {
      if (depth == 0) objectStart = i;
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0 && objectStart != std::string::npos) {
        objects.push_back(document.substr(objectStart, i - objectStart + 1));
        objectStart = std::string::npos;
      }
    } else if (c == ']' && depth == 0) {
      break;
    }
  }
  return objects;
}

static muduo::net::http::ConditionalOutcome parseOutcome(const std::string& value) {
  using muduo::net::http::ConditionalOutcome;
  if (value == "not-found") return ConditionalOutcome::kNotFound;
  if (value == "precondition-failed") return ConditionalOutcome::kPreconditionFailed;
  if (value == "not-modified") return ConditionalOutcome::kNotModified;
  if (value == "not-modified-suppressed") {
    return ConditionalOutcome::kNotModifiedSuppressed;
  }
  if (value == "partial-content") return ConditionalOutcome::kPartialContent;
  if (value == "range-not-satisfiable") {
    return ConditionalOutcome::kRangeNotSatisfiable;
  }
  if (value == "range-ignored") return ConditionalOutcome::kRangeIgnored;
  return ConditionalOutcome::kFullRepresentation;
}

static muduo::net::http::ConditionalRoute parseRoute(const std::string& value) {
  using muduo::net::http::ConditionalRoute;
  if (value == "asset-integrity") return ConditionalRoute::kAssetIntegrity;
  if (value == "representation-reuse") {
    return ConditionalRoute::kRepresentationReuse;
  }
  return ConditionalRoute::kProtocolDefault;
}

static std::vector<muduo::net::http::ConditionalPolicyRecord> parsePolicyRecords(
    const std::string& document) {
  std::vector<muduo::net::http::ConditionalPolicyRecord> records;
  for (const std::string& object : jsonObjectArray(document, "records")) {
    muduo::net::http::ConditionalPolicyRecord record;
    record.id = jsonStringField(object, "id");
    record.assetClass = jsonStringField(object, "asset_class", "*");
    record.method = jsonStringField(object, "method", "*");
    record.outcome = parseOutcome(jsonStringField(object, "outcome"));
    record.effectiveProfile = jsonStringField(object, "effective_profile");
    record.retiredProfile = jsonStringField(object, "retired_profile");
    record.status = jsonIntField(object, "status", 200);
    record.route = parseRoute(jsonStringField(object, "route"));
    records.push_back(record);
  }
  return records;
}

int main(int argc, char* argv[]) {
  std::string reqId;
  bool jsonOutput=false;
  for (int i=1;i<argc;++i) {
    std::string a=argv[i];
    if (a=="--request" && i+1<argc) reqId=argv[++i];
    else if (a=="--json") jsonOutput=true;
  }
  if (reqId.empty()) { std::cerr<<"Usage: condreq_probe --request <id> --json\n"; return 1; }

  std::vector<std::string> reqPaths={
    "muduo/net/http/tests/fixtures/requests.json",
    "tests/fixtures/requests.json",
    "/app/muduo/net/http/tests/fixtures/requests.json"
  };
  std::string reqContent=readFile(reqPaths);

  std::vector<std::string> profilePaths={
    "muduo/net/http/tests/fixtures/conditional_profile.json",
    "tests/fixtures/conditional_profile.json",
    "/app/muduo/net/http/tests/fixtures/conditional_profile.json"
  };
  std::string profileContent=readFile(profilePaths);
  std::string profileVersion="unknown";
  std::vector<std::string> availableProfiles;
  std::vector<muduo::net::http::ConditionalPolicyRecord> policyRecords;
  if (!profileContent.empty()) {
    auto vPos=profileContent.find("\"default_version\"");
    if (vPos!=std::string::npos) {
      auto colon=profileContent.find(':', vPos);
      auto q1=findUnescapedQuote(profileContent, colon+1);
      auto q2=findUnescapedQuote(profileContent, q1+1);
      if (q1!=std::string::npos && q2!=std::string::npos) profileVersion=profileContent.substr(q1+1, q2-q1-1);
    }
    availableProfiles=jsonStringArray(profileContent, "available_versions");
    policyRecords=parsePolicyRecords(profileContent);
  }

  std::string etag="\"abc123\"";
  std::string lastMod="Wed, 21 Oct 2015 07:28:00 GMT";
  long contentLength=1024;
  std::string method="GET";
  std::string assetClass="static";
  std::map<std::string,std::string> headers;
  bool hasRange=false;

  if (!reqContent.empty()) {
    std::string idPattern="\"id\": \""+reqId+"\"";
    auto idPos=reqContent.find(idPattern);
    if (idPos==std::string::npos) idPos=reqContent.find("\"id\":\""+reqId+"\"");
    if (idPos!=std::string::npos) {
      size_t objEnd=reqContent.find("\"id\"", idPos+1);
      if (objEnd==std::string::npos) objEnd=reqContent.size();
      std::string obj=reqContent.substr(idPos, objEnd-idPos);

      auto mPos=obj.find("\"method\"");
      if (mPos!=std::string::npos) {
        auto colon=obj.find(':', mPos);
        auto q1=findUnescapedQuote(obj, colon+1);
        auto q2=findUnescapedQuote(obj, q1+1);
        if (q1!=std::string::npos && q2!=std::string::npos) method=obj.substr(q1+1, q2-q1-1);
      }
      auto acPos=obj.find("\"asset_class\"");
      if (acPos!=std::string::npos) {
        auto colon=obj.find(':', acPos);
        auto q1=findUnescapedQuote(obj, colon+1);
        auto q2=findUnescapedQuote(obj, q1+1);
        if (q1!=std::string::npos && q2!=std::string::npos) assetClass=obj.substr(q1+1, q2-q1-1);
      }
      auto pvPos=obj.find("\"profile_version\"");
      if (pvPos!=std::string::npos) {
        auto colon=obj.find(':', pvPos);
        auto q1=findUnescapedQuote(obj, colon+1);
        auto q2=findUnescapedQuote(obj, q1+1);
        if (q1!=std::string::npos && q2!=std::string::npos) profileVersion=obj.substr(q1+1, q2-q1-1);
      }
      auto hPos=obj.find("\"headers\"");
      if (hPos!=std::string::npos) {
        auto braceOpen=obj.find('{', hPos);
        auto braceClose=obj.find('}', braceOpen);
        if (braceOpen!=std::string::npos && braceClose!=std::string::npos && braceClose>braceOpen) {
          std::string block=obj.substr(braceOpen+1, braceClose-braceOpen-1);
          size_t p=0;
          while (p<block.size()) {
            auto k1=findUnescapedQuote(block, p);
            if (k1==std::string::npos) break;
            auto k2=findUnescapedQuote(block, k1+1);
            if (k2==std::string::npos) break;
            std::string key=block.substr(k1+1, k2-k1-1);
            auto colon=block.find(':', k2);
            if (colon==std::string::npos) break;
            auto v1=findUnescapedQuote(block, colon+1);
            if (v1==std::string::npos) break;
            auto v2=findUnescapedQuote(block, v1+1);
            if (v2==std::string::npos) break;
            std::string value=block.substr(v1+1, v2-v1-1);
            std::string unescaped;
            for (size_t i=0;i<value.size();++i) {
              if (value[i]=='\\' && i+1<value.size() && value[i+1]=='"') { unescaped.push_back('"'); ++i; }
              else if (value[i]=='\\' && i+1<value.size() && value[i+1]=='\\') { unescaped.push_back('\\'); ++i; }
              else unescaped.push_back(value[i]);
            }
            headers[key]=unescaped;
            if (key=="Range") hasRange=true;
            p=v2+1;
          }
        }
      }
    }
  }

  muduo::net::HttpRequest request;
  request.setMethod(method.c_str(), method.c_str() + method.size());
  for (const auto& header : headers) {
    std::string line = header.first + ": " + header.second;
    const char* start = line.c_str();
    request.addHeader(start, start + header.first.size(), start + line.size());
  }

  muduo::net::http::Resource resource;
  resource.etag = etag;
  resource.lastModified = lastMod;
  resource.contentLength = contentLength;
  resource.assetClass = assetClass;
  resource.profileVersion = profileVersion;
  resource.availableProfileVersions = availableProfiles;
  resource.conditionalPolicies = policyRecords;
  muduo::net::http::ConditionalDecision observed =
      muduo::net::http::evaluateConditionalDecision(request, resource);

  if (jsonOutput) {
    std::cout<<"{\n";
    std::cout<<"  \"request_id\": \""<<jsonEscape(reqId)<<"\",\n";
    std::cout<<"  \"method\": \""<<jsonEscape(method)<<"\",\n";
    std::cout<<"  \"asset_class\": \""<<jsonEscape(assetClass)<<"\",\n";
    std::cout<<"  \"profile_version\": \""<<jsonEscape(profileVersion)<<"\",\n";
    std::cout<<"  \"headers\": {\n";
    size_t i=0;
    for (auto& kv: headers) {
      std::cout<<"    \""<<jsonEscape(kv.first)<<"\": \""<<jsonEscape(kv.second)<<"\"";
      if (++i<headers.size()) std::cout<<",";
      std::cout<<"\n";
    }
    std::cout<<"  },\n";
    std::cout<<"  \"has_range\": "<<(hasRange?"true":"false")<<",\n";
    std::cout<<"  \"resource\": {\n";
    std::cout<<"    \"etag\": \""<<jsonEscape(etag)<<"\",\n";
    std::cout<<"    \"last_modified\": \""<<jsonEscape(lastMod)<<"\",\n";
    std::cout<<"    \"content_length\": "<<contentLength<<",\n";
    std::cout<<"    \"asset_class\": \""<<jsonEscape(assetClass)<<"\"\n";
    std::cout<<"  },\n";
    std::cout<<"  \"policy_records\": [\n";
    for (size_t recordIndex=0; recordIndex<policyRecords.size(); ++recordIndex) {
      const auto& record=policyRecords[recordIndex];
      std::cout<<"    {\"id\": \""<<jsonEscape(record.id)
               <<"\", \"asset_class\": \""<<jsonEscape(record.assetClass)
               <<"\", \"method\": \""<<jsonEscape(record.method)
               <<"\", \"outcome\": \""
               <<muduo::net::http::conditionalOutcomeName(record.outcome)
               <<"\", \"effective_profile\": \""
               <<jsonEscape(record.effectiveProfile)
               <<"\", \"retired_profile\": \""
               <<jsonEscape(record.retiredProfile)
               <<"\", \"status\": "<<record.status
               <<", \"route\": \""
               <<muduo::net::http::conditionalRouteName(record.route)<<"\"}";
      if (recordIndex+1<policyRecords.size()) std::cout<<",";
      std::cout<<"\n";
    }
    std::cout<<"  ],\n";
    std::cout<<"  \"observed_decision\": {\n";
    std::cout<<"    \"status\": "<<observed.status<<",\n";
    std::cout<<"    \"rfc_outcome\": \""
             <<muduo::net::http::conditionalOutcomeName(observed.outcome)<<"\",\n";
    std::cout<<"    \"route\": \""
             <<muduo::net::http::conditionalRouteName(observed.route)<<"\",\n";
    std::cout<<"    \"profile_identity\": \""
             <<jsonEscape(muduo::net::http::conditionalProfileIdentity(resource))<<"\"\n";
    std::cout<<"  }\n";
    std::cout<<"}\n";
  } else {
    std::cout<<"Request "<<reqId<<" method="<<method<<" asset_class="<<assetClass<<" profile="<<profileVersion<<" has_range="<<hasRange<<" etag="<<etag
             <<" observed_status="<<observed.status
             <<" observed_outcome="<<muduo::net::http::conditionalOutcomeName(observed.outcome)
             <<" observed_route="<<muduo::net::http::conditionalRouteName(observed.route)<<"\n";
    for (auto& kv: headers) std::cout<<"  "<<kv.first<<": "<<kv.second<<"\n";
  }
  return 0;
}
