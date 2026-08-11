#pragma once

#include "crayon/browser_engine/types.h"

namespace crayon::browser_engine {

class EngineEventSink {
 public:
  virtual ~EngineEventSink() = default;

  virtual void OnProfileEvent(const ProfileEvent& event) = 0;
  virtual void OnTabEvent(const TabEvent& event) = 0;
  virtual void OnNavigationEvent(const NavigationEvent& event) = 0;
  virtual void OnPermissionRequest(const PermissionRequest& request) = 0;
  virtual void OnTrustedInput(const TrustedInputFact& fact) = 0;
  virtual void OnObservation(const ObservationEvent& event) = 0;
};

}  // namespace crayon::browser_engine
