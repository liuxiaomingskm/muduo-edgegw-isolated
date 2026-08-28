#include "examples/edgegw/recovery_policy.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string readFile(const std::vector<std::string>& paths) {
  for (const auto& path : paths) {
    std::ifstream input(path);
    if (input.good()) {
      return std::string(std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>());
    }
  }
  return "";
}

size_t findUnescapedQuote(const std::string& text, size_t from) {
  for (size_t i = from; i < text.size(); ++i) {
    if (text[i] != '"') continue;
    size_t slashes = 0;
    size_t cursor = i;
    while (cursor > 0 && text[--cursor] == '\\') ++slashes;
    if (slashes % 2 == 0) return i;
  }
  return std::string::npos;
}

std::string jsonStringField(const std::string& object,
                            const std::string& key,
                            const std::string& fallback = "") {
  const std::string pattern = "\"" + key + "\"";
  const size_t keyPosition = object.find(pattern);
  if (keyPosition == std::string::npos) return fallback;
  const size_t colon = object.find(':', keyPosition + pattern.size());
  if (colon == std::string::npos) return fallback;
  const size_t first = findUnescapedQuote(object, colon + 1);
  if (first == std::string::npos) return fallback;
  const size_t second = findUnescapedQuote(object, first + 1);
  if (second == std::string::npos) return fallback;
  return object.substr(first + 1, second - first - 1);
}

std::vector<std::string> jsonStringArray(const std::string& document,
                                         const std::string& key) {
  std::vector<std::string> values;
  const std::string pattern = "\"" + key + "\"";
  const size_t keyPosition = document.find(pattern);
  if (keyPosition == std::string::npos) return values;
  const size_t open = document.find('[', keyPosition + pattern.size());
  const size_t close = document.find(']', open);
  if (open == std::string::npos || close == std::string::npos) return values;

  size_t position = open + 1;
  while (position < close) {
    const size_t first = findUnescapedQuote(document, position);
    if (first == std::string::npos || first >= close) break;
    const size_t second = findUnescapedQuote(document, first + 1);
    if (second == std::string::npos || second > close) break;
    values.push_back(document.substr(first + 1, second - first - 1));
    position = second + 1;
  }
  return values;
}

std::vector<std::string> jsonObjectArray(const std::string& document,
                                         const std::string& key) {
  std::vector<std::string> objects;
  const std::string pattern = "\"" + key + "\"";
  const size_t keyPosition = document.find(pattern);
  if (keyPosition == std::string::npos) return objects;
  const size_t open = document.find('[', keyPosition + pattern.size());
  if (open == std::string::npos) return objects;

  bool inString = false;
  bool escaped = false;
  int depth = 0;
  size_t objectStart = std::string::npos;
  for (size_t i = open + 1; i < document.size(); ++i) {
    const char current = document[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (current == '\\') escaped = true;
      else if (current == '"') inString = false;
      continue;
    }
    if (current == '"') {
      inString = true;
    } else if (current == '{') {
      if (depth == 0) objectStart = i;
      ++depth;
    } else if (current == '}') {
      --depth;
      if (depth == 0 && objectStart != std::string::npos) {
        objects.push_back(document.substr(objectStart, i - objectStart + 1));
        objectStart = std::string::npos;
      }
    } else if (current == ']' && depth == 0) {
      break;
    }
  }
  return objects;
}

edgegw::RecoveryAction parseAction(const std::string& value) {
  using edgegw::RecoveryAction;
  if (value == "requeue-pending") return RecoveryAction::kRequeuePending;
  if (value == "reconcile-ownership") return RecoveryAction::kReconcileOwnership;
  if (value == "restore-state") return RecoveryAction::kRestoreState;
  if (value == "quiesce") return RecoveryAction::kQuiesce;
  if (value == "migrate") return RecoveryAction::kMigrate;
  return RecoveryAction::kLeaveUnchanged;
}

edgegw::RecoveryProfile loadProfile(const std::string& document) {
  edgegw::RecoveryProfile profile;
  profile.version = jsonStringField(document, "default_version");
  profile.availableVersions = jsonStringArray(document, "available_versions");
  for (const auto& object : jsonObjectArray(document, "records")) {
    edgegw::RecoveryPolicyRecord record;
    record.id = jsonStringField(object, "id");
    record.condition = jsonStringField(object, "condition", "*");
    record.evidenceSource = jsonStringField(object, "evidence_source", "*");
    record.effectiveProfile = jsonStringField(object, "effective_profile");
    record.retiredProfile = jsonStringField(object, "retired_profile");
    record.action = parseAction(jsonStringField(object, "action"));
    profile.records.push_back(record);
  }
  return profile;
}

} // namespace

int main(int argc, char* argv[]) {
  std::string condition = "ownership-conflict";
  std::string evidence = "worker";
  std::string selectedProfile;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--condition" && i + 1 < argc) condition = argv[++i];
    else if (argument == "--evidence" && i + 1 < argc) evidence = argv[++i];
    else if (argument == "--profile" && i + 1 < argc) selectedProfile = argv[++i];
  }

  const std::string document = readFile({
      "examples/edgegw/repro/recovery_profile.json",
      "recovery_profile.json",
      "/app/examples/edgegw/repro/recovery_profile.json"});
  if (document.empty()) {
    std::cerr << "recovery profile not found\n";
    return 1;
  }

  edgegw::RecoveryProfile profile = loadProfile(document);
  if (!selectedProfile.empty()) profile.version = selectedProfile;
  const edgegw::RecoveryObservation observation{condition, evidence};
  const edgegw::RecoveryDecision decision =
      edgegw::resolveRecoveryDecision(observation, profile);

  std::cout << "profile=" << profile.version
            << " condition=" << observation.condition
            << " evidence=" << observation.evidenceSource << "\n";
  for (const auto& record : profile.records) {
    std::cout << "record=" << record.id
              << " condition=" << record.condition
              << " evidence=" << record.evidenceSource
              << " effective=" << record.effectiveProfile
              << " retired=" << record.retiredProfile
              << " action=" << edgegw::recoveryActionName(record.action) << "\n";
  }
  std::cout << "observed_action=" << edgegw::recoveryActionName(decision.action)
            << " observed_record=" << decision.recordId << "\n";
  return 0;
}
