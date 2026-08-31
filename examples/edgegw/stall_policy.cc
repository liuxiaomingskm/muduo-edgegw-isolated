#include "stall_policy.h"

#include <algorithm>

namespace edgegw {

const char* stallActionName(StallAction action) {
  switch (action) {
    case StallAction::kLeaveUnchanged:
      return "leave-unchanged";
    case StallAction::kReconcileAccounting:
      return "reconcile-accounting";
    case StallAction::kDispatchOnOwner:
      return "dispatch-on-owner";
    case StallAction::kQuarantineSlot:
      return "quarantine-slot";
    case StallAction::kDrainConnection:
      return "drain-connection";
  }
  return "unknown";
}

namespace {

bool containsEdition(const std::vector<std::string>& editions,
                     const std::string& edition) {
  return std::find(editions.begin(), editions.end(), edition) != editions.end();
}

std::string classifyStall(const StallObservation& observation) {
  if (observation.eventsAfterResume == 0) return "readiness-missing";
  if (observation.resumeApplied == 0) return "dispatch-missing";
  if (observation.resumeRequested == 0) return "request-missing";
  return "healthy";
}

bool selectorMatches(const std::string& selector, const std::string& value) {
  return selector == "*" || selector == value;
}

bool recordIsActive(const StallPolicyRecord& record,
                    const std::string& edition) {
  return !record.effectiveEdition.empty() &&
         edition > record.effectiveEdition &&
         !record.retiredEdition.empty() &&
         edition < record.retiredEdition;
}

const StallPolicyRecord* selectRecord(const std::string& stallClass,
                                      const StallObservation& observation,
                                      const StallPolicyProfile& profile) {
  if (!containsEdition(profile.availableEditions, profile.edition)) {
    return nullptr;
  }

  for (const auto& record : profile.records) {
    if (!selectorMatches(record.stallClass, stallClass) ||
        !selectorMatches(record.evidenceSource, observation.evidenceSource) ||
        !recordIsActive(record, profile.edition)) {
      continue;
    }
    return &record;
  }
  return nullptr;
}

} // namespace

StallDecision resolveStallDecision(const StallObservation& observation,
                                   const StallPolicyProfile& profile) {
  StallDecision decision{
      observation, classifyStall(observation), StallAction::kLeaveUnchanged, ""};
  const StallPolicyRecord* record =
      selectRecord(decision.stallClass, observation, profile);
  if (record != nullptr) {
    decision.action = record->action;
    decision.recordId = record->id;
  }
  return decision;
}

} // namespace edgegw
