#include "examples/edgegw/stall_policy.h"

#include <cstdlib>
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

edgegw::StallAction parseAction(const std::string& value) {
  using edgegw::StallAction;
  if (value == "reconcile-accounting") return StallAction::kReconcileAccounting;
  if (value == "dispatch-on-owner") return StallAction::kDispatchOnOwner;
  if (value == "quarantine-slot") return StallAction::kQuarantineSlot;
  if (value == "drain-connection") return StallAction::kDrainConnection;
  return StallAction::kLeaveUnchanged;
}

edgegw::StallPolicyProfile loadProfile(const std::string& document) {
  edgegw::StallPolicyProfile profile;
  profile.edition = jsonStringField(document, "default_edition");
  profile.availableEditions = jsonStringArray(document, "available_editions");
  for (const auto& object : jsonObjectArray(document, "records")) {
    edgegw::StallPolicyRecord record;
    record.id = jsonStringField(object, "id");
    record.stallClass = jsonStringField(object, "stall_class", "*");
    record.evidenceSource = jsonStringField(object, "evidence_source", "*");
    record.effectiveEdition = jsonStringField(object, "effective_edition");
    record.retiredEdition = jsonStringField(object, "retired_edition");
    record.action = parseAction(jsonStringField(object, "action"));
    profile.records.push_back(record);
  }
  return profile;
}

int parseCount(const char* value) {
  return static_cast<int>(std::strtol(value, nullptr, 10));
}

} // namespace

int main(int argc, char* argv[]) {
  edgegw::StallObservation observation;
  observation.evidenceSource = "worker";
  std::string selectedEdition;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--requested" && i + 1 < argc) {
      observation.resumeRequested = parseCount(argv[++i]);
    } else if (argument == "--applied" && i + 1 < argc) {
      observation.resumeApplied = parseCount(argv[++i]);
    } else if (argument == "--events" && i + 1 < argc) {
      observation.eventsAfterResume = parseCount(argv[++i]);
    } else if (argument == "--evidence" && i + 1 < argc) {
      observation.evidenceSource = argv[++i];
    } else if (argument == "--edition" && i + 1 < argc) {
      selectedEdition = argv[++i];
    }
  }

  const std::string document = readFile({
      "examples/edgegw/repro/stall_policy.json",
      "stall_policy.json",
      "/app/examples/edgegw/repro/stall_policy.json"});
  if (document.empty()) {
    std::cerr << "stall policy not found\n";
    return 1;
  }

  edgegw::StallPolicyProfile profile = loadProfile(document);
  if (!selectedEdition.empty()) profile.edition = selectedEdition;
  const edgegw::StallDecision decision =
      edgegw::resolveStallDecision(observation, profile);

  std::cout << "edition=" << profile.edition
            << " requested=" << observation.resumeRequested
            << " applied=" << observation.resumeApplied
            << " events=" << observation.eventsAfterResume
            << " evidence=" << observation.evidenceSource << "\n";
  for (const auto& record : profile.records) {
    std::cout << "record=" << record.id
              << " class=" << record.stallClass
              << " evidence=" << record.evidenceSource
              << " effective=" << record.effectiveEdition
              << " retired=" << record.retiredEdition
              << " action=" << edgegw::stallActionName(record.action) << "\n";
  }
  std::cout << "observed_class=" << decision.stallClass
            << " observed_action=" << edgegw::stallActionName(decision.action)
            << " observed_record=" << decision.recordId << "\n";
  return 0;
}
