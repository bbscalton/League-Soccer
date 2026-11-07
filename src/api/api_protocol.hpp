// League-Soccer External Control API Protocol Definitions
// Apache License 2.0 - see LICENSE
//
// JSON-lines protocol for external programmatic control of match simulation,
// following a GRF-style reset/step/observe/act pattern.

#ifndef _HPP_API_PROTOCOL
#define _HPP_API_PROTOCOL

#include <cstdint>
#include <string>
#include <vector>

namespace api {

// Player action (mirrors game button functions)
struct PlayerAction {
  float direction_x = 0.0f;   // [-1, 1] left stick X
  float direction_y = 0.0f;   // [-1, 1] left stick Y
  bool short_pass = false;    // A button
  bool high_pass = false;     // B button
  bool long_pass = false;     // Y button
  bool shot = false;          // X button
  bool sprint = false;        // RB button
  bool dribble = false;       // RT trigger
  bool switch_player = false; // LB button
  bool special = false;       // RS press
};

// Per-player observation data
struct PlayerObservation {
  int player_id = 0;
  int team_id = 0;
  float position_x = 0.0f;
  float position_y = 0.0f;
  float position_z = 0.0f;
  float velocity_x = 0.0f;
  float velocity_y = 0.0f;
  float direction = 0.0f;     // radians
  bool has_ball = false;
  bool is_active = false;     // controlled by human/API
  float stamina = 1.0f;
};

// Ball observation
struct BallObservation {
  float position_x = 0.0f;
  float position_y = 0.0f;
  float position_z = 0.0f;
  float velocity_x = 0.0f;
  float velocity_y = 0.0f;
  float velocity_z = 0.0f;
  int owned_team = -1;        // -1 = contested
};

// Full game state observation
struct Observation {
  uint64_t step = 0;
  uint64_t match_time_ms = 0;
  int period = 0;             // 0=1st half, 1=2nd half, etc.
  int score_home = 0;
  int score_away = 0;
  bool in_play = false;
  bool in_set_piece = false;
  bool goal_scored = false;
  int last_touch_team = -1;
  BallObservation ball;
  std::vector<PlayerObservation> home_players;
  std::vector<PlayerObservation> away_players;
};

// Reset request parameters
struct ResetParams {
  int home_team_id = -1;      // -1 = use current/default
  int away_team_id = -1;
  int64_t seed = -1;          // -1 = random seed
  float match_duration_minutes = 5.0f;
};

// Step result (observation + metadata)
struct StepResult {
  Observation observation;
  bool done = false;          // match ended
};

// Protocol constants
constexpr int kDefaultPort = 50051;
constexpr const char* kDefaultHost = "127.0.0.1";
constexpr size_t kMaxMessageSize = 1024 * 1024;  // 1 MB

// JSON serialization helpers (implemented in api_protocol.cpp)
std::string SerializeObservation(const Observation& obs);
std::string SerializeStepResult(const StepResult& result);
std::string SerializeError(const std::string& message, int code = -1);
std::string SerializeAck(const std::string& request_type);

bool ParseResetParams(const std::string& json, ResetParams& out);
bool ParsePlayerAction(const std::string& json, PlayerAction& out);

}  // namespace api

#endif
