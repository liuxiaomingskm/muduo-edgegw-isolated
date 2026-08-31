#pragma once

#include <string>
#include <vector>

namespace edgegw {

enum class StallAction {
  kLeaveUnchanged,
  kReconcileAccounting,
  kDispatchOnOwner,
  kQuarantineSlot,
  kDrainConnection,
};

struct StallObservation {
  int resumeRequested = 0;
  int resumeApplied = 0;
  int eventsAfterResume = 0;
  std::string evidenceSource;
};

struct StallPolicyRecord {
  std::string id;
  std::string stallClass = "*";
  std::string evidenceSource = "*";
  std::string effectiveEdition;
  std::string retiredEdition;
  StallAction action = StallAction::kLeaveUnchanged;
};

struct StallPolicyProfile {
  std::string edition;
  std::vector<std::string> availableEditions;
  std::vector<StallPolicyRecord> records;
};

struct StallDecision {
  StallObservation observation;
  std::string stallClass;
  StallAction action;
  std::string recordId;
};

const char* stallActionName(StallAction action);
StallDecision resolveStallDecision(const StallObservation& observation,
                                   const StallPolicyProfile& profile);

} // namespace edgegw
