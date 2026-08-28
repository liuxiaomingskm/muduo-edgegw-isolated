#pragma once

#include <string>
#include <vector>

namespace edgegw {

enum class RecoveryAction {
  kLeaveUnchanged,
  kRequeuePending,
  kReconcileOwnership,
  kRestoreState,
  kQuiesce,
  kMigrate,
};

struct RecoveryObservation {
  std::string condition;
  std::string evidenceSource;
};

struct RecoveryPolicyRecord {
  std::string id;
  std::string condition = "*";
  std::string evidenceSource = "*";
  std::string effectiveProfile;
  std::string retiredProfile;
  RecoveryAction action = RecoveryAction::kLeaveUnchanged;
};

struct RecoveryProfile {
  std::string version;
  std::vector<std::string> availableVersions;
  std::vector<RecoveryPolicyRecord> records;
};

struct RecoveryDecision {
  RecoveryObservation observation;
  RecoveryAction action;
  std::string recordId;
};

const char* recoveryActionName(RecoveryAction action);
RecoveryDecision resolveRecoveryDecision(const RecoveryObservation& observation,
                                         const RecoveryProfile& profile);

} // namespace edgegw
