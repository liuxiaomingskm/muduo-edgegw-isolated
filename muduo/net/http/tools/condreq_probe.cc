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
  if (!profileContent.empty()) {
    auto vPos=profileContent.find("\"version\"");
    if (vPos!=std::string::npos) {
      auto colon=profileContent.find(':', vPos);
      auto q1=findUnescapedQuote(profileContent, colon+1);
      auto q2=findUnescapedQuote(profileContent, q1+1);
      if (q1!=std::string::npos && q2!=std::string::npos) profileVersion=profileContent.substr(q1+1, q2-q1-1);
    }
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
    std::cout<<"  }\n";
    std::cout<<"}\n";
  } else {
    std::cout<<"Request "<<reqId<<" method="<<method<<" asset_class="<<assetClass<<" profile="<<profileVersion<<" has_range="<<hasRange<<" etag="<<etag<<"\n";
    for (auto& kv: headers) std::cout<<"  "<<kv.first<<": "<<kv.second<<"\n";
  }
  return 0;
}
