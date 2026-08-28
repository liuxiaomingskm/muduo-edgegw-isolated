#include "recovery_policy.h"

#include <algorithm>

namespace edgegw {

const char* recoveryActionName(RecoveryAction action) {
  switch (action) {
    case RecoveryAction::kLeaveUnchanged:
      return "leave-unchanged";
    case RecoveryAction::kRequeuePending:
      return "requeue-pending";
    case RecoveryAction::kReconcileOwnership:
      return "reconcile-ownership";
    case RecoveryAction::kRestoreState:
      return "restore-state";
    case RecoveryAction::kQuiesce:
      return "quiesce";
    case RecoveryAction::kMigrate:
      return "migrate";
  }
  return "unknown";
}

namespace {

bool containsProfile(const std::vector<std::string>& profiles,
                     const std::string& profile) {
  return std::find(profiles.begin(), profiles.end(), profile) != profiles.end();
}

bool selectorMatches(const std::string& selector, const std::string& value) {
  return selector == "*" || selector == value;
}

bool recordIsActive(const RecoveryPolicyRecord& record,
                    const std::string& profile) {
  return !record.effectiveProfile.empty() &&
         profile > record.effectiveProfile &&
         !record.retiredProfile.empty() &&
         profile < record.retiredProfile;
}

const RecoveryPolicyRecord* selectRecoveryRecord(
    const RecoveryObservation& observation,
    const RecoveryProfile& profile) {
  if (!containsProfile(profile.availableVersions, profile.version)) {
    return nullptr;
  }

  for (const auto& record : profile.records) {
    if (!selectorMatches(record.condition, observation.condition) ||
        !selectorMatches(record.evidenceSource, observation.evidenceSource) ||
        !recordIsActive(record, profile.version)) {
      continue;
    }
    return &record;
  }
  return nullptr;
}

} // namespace

RecoveryDecision resolveRecoveryDecision(const RecoveryObservation& observation,
                                         const RecoveryProfile& profile) {
  RecoveryDecision decision{observation, RecoveryAction::kLeaveUnchanged, ""};
  const RecoveryPolicyRecord* record = selectRecoveryRecord(observation, profile);
  if (record != nullptr) {
    decision.action = record->action;
    decision.recordId = record->id;
  }
  return decision;
}

} // namespace edgegw
