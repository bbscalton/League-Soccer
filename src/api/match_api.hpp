// League-Soccer Match API Controller
// Apache License 2.0 - see LICENSE
//
// Wraps Match class to provide programmatic step/reset/observe control
// for external programs following the GRF API pattern.

#ifndef _HPP_API_MATCH_API
#define _HPP_API_MATCH_API

#include <cstdint>
#include <memory>
#include <string>

#include "api_protocol.hpp"

class Match;
class MatchData;
class APIGamepad;

namespace api {

class MatchAPIController {
public:
  MatchAPIController();
  ~MatchAPIController();

  // Reset/start a match with the given parameters
  // If seed >= 0, uses deterministic RNG seeding
  bool Reset(const ResetParams& params, std::string& error);

  // Advance simulation by one physics tick (default 10ms)
  // Returns false if no match is active
  bool Step(StepResult& result);

  // Get current observation without stepping
  bool GetObservation(Observation& obs);

  // End the current match
  void Quit();

  // Check if a match is active
  bool IsMatchActive() const { return match != nullptr; }

  // Get the underlying match (for integration with existing game code)
  Match* GetMatch() { return match; }

  // Get API gamepads for teams
  std::shared_ptr<APIGamepad> GetTeamGamepad(int teamId);

  // Set the seed for deterministic matches
  static void SetGlobalSeed(int64_t seed);
  static int64_t GetGlobalSeed();

private:
  void PopulateObservation(Observation& obs);

  Match* match = nullptr;
  MatchData* matchData = nullptr;
  uint64_t stepCount = 0;
  int64_t currentSeed = -1;

  // API gamepads (owned by API server, referenced here)
  std::shared_ptr<APIGamepad> teamGamepads[2];
};

}  // namespace api

#endif
