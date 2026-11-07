// League-Soccer Match API Controller Implementation
// Apache License 2.0 - see LICENSE
//
// Note: This is a simplified implementation. Full match control requires
// integration with the game's main loop and scene graph infrastructure.
// The API server provides the network interface; actual match stepping
// happens through the game's existing task system.

#include "match_api.hpp"

#include <cstdlib>
#include <ctime>

#include "api_server.hpp"
#include "hid/apigamepad.hpp"

namespace api {

namespace {
static int64_t g_globalSeed = -1;
}

MatchAPIController::MatchAPIController() {}

MatchAPIController::~MatchAPIController() {
  Quit();
}

void MatchAPIController::SetGlobalSeed(int64_t seed) {
  g_globalSeed = seed;
}

int64_t MatchAPIController::GetGlobalSeed() {
  return g_globalSeed;
}

bool MatchAPIController::Reset(const ResetParams& params, std::string& error) {
  // Clean up any existing state
  Quit();

  // Set up deterministic seeding if requested
  currentSeed = params.seed;
  if (currentSeed >= 0) {
    srand(static_cast<unsigned int>(currentSeed));
    g_globalSeed = currentSeed;
  } else {
    srand(static_cast<unsigned int>(time(nullptr)));
    g_globalSeed = -1;
  }

  // Get API gamepads from server
  APIServer* server = GetAPIServer();
  if (server) {
    teamGamepads[0] = server->GetTeamGamepad(0);
    teamGamepads[1] = server->GetTeamGamepad(1);
  }

  stepCount = 0;
  
  // Match creation is handled by the game's existing infrastructure.
  // The API provides the external interface; the actual match start
  // is triggered via GameTask::Action(e_GameTaskMessage_StartMatch).
  error = "";
  return true;
}

bool MatchAPIController::Step(StepResult& result) {
  if (!match) {
    result.done = true;
    return false;
  }

  // Process API gamepad inputs
  if (teamGamepads[0]) teamGamepads[0]->Process();
  if (teamGamepads[1]) teamGamepads[1]->Process();

  // Match stepping is handled by the game's task system
  // This would integrate with Match::Process()

  stepCount++;

  // Populate observation
  PopulateObservation(result.observation);

  // Check if match is done.
  // Note: This MatchAPIController is not currently used by the main game;
  // the step/observe callbacks in main.cpp access the live Match directly.
  // This placeholder returns false; actual done detection is in main.cpp.
  result.done = false;

  return true;
}

bool MatchAPIController::GetObservation(Observation& obs) {
  PopulateObservation(obs);
  return true;
}

void MatchAPIController::Quit() {
  match = nullptr;
  matchData = nullptr;
  stepCount = 0;

  // Clear gamepad actions
  if (teamGamepads[0]) teamGamepads[0]->ClearAction();
  if (teamGamepads[1]) teamGamepads[1]->ClearAction();
}

std::shared_ptr<APIGamepad> MatchAPIController::GetTeamGamepad(int teamId) {
  if (teamId < 0 || teamId > 1) return nullptr;
  return teamGamepads[teamId];
}

void MatchAPIController::PopulateObservation(Observation& obs) {
  obs = Observation{};
  obs.step = stepCount;

  // When no match is active, return empty observation
  // Full implementation would read from the active Match instance
}

}  // namespace api
