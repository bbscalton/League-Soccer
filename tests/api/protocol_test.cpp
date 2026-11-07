// League-Soccer API Protocol Tests
// Apache License 2.0 - see LICENSE

#include <gtest/gtest.h>

#include "api/api_protocol.hpp"

using namespace api;

TEST(ApiProtocolTest, SerializeObservation) {
  Observation obs;
  obs.step = 100;
  obs.match_time_ms = 1000;
  obs.period = 0;
  obs.score_home = 1;
  obs.score_away = 0;
  obs.in_play = true;
  obs.in_set_piece = false;
  obs.goal_scored = false;
  obs.last_touch_team = 0;

  obs.ball.position_x = 0.0f;
  obs.ball.position_y = 5.0f;
  obs.ball.position_z = 0.1f;
  obs.ball.velocity_x = 1.0f;
  obs.ball.velocity_y = -0.5f;
  obs.ball.velocity_z = 0.0f;
  obs.ball.owned_team = 0;

  PlayerObservation player;
  player.player_id = 1;
  player.team_id = 0;
  player.position_x = -20.0f;
  player.position_y = 0.0f;
  player.position_z = 0.0f;
  player.has_ball = true;
  player.is_active = true;
  player.stamina = 0.95f;
  obs.home_players.push_back(player);

  std::string json = SerializeObservation(obs);

  // Check key fields are present
  EXPECT_NE(json.find("\"type\":\"observation\""), std::string::npos);
  EXPECT_NE(json.find("\"step\":100"), std::string::npos);
  EXPECT_NE(json.find("\"match_time_ms\":1000"), std::string::npos);
  EXPECT_NE(json.find("\"score\":[1,0]"), std::string::npos);
  EXPECT_NE(json.find("\"in_play\":true"), std::string::npos);
  EXPECT_NE(json.find("\"ball\":"), std::string::npos);
  EXPECT_NE(json.find("\"home_players\":"), std::string::npos);
}

TEST(ApiProtocolTest, SerializeStepResult) {
  StepResult result;
  result.observation.step = 50;
  result.observation.match_time_ms = 500;
  result.done = false;

  std::string json = SerializeStepResult(result);

  EXPECT_NE(json.find("\"type\":\"step_result\""), std::string::npos);
  EXPECT_NE(json.find("\"done\":false"), std::string::npos);

  result.done = true;
  json = SerializeStepResult(result);
  EXPECT_NE(json.find("\"done\":true"), std::string::npos);
}

TEST(ApiProtocolTest, SerializeError) {
  std::string json = SerializeError("Something went wrong", 42);

  EXPECT_NE(json.find("\"type\":\"error\""), std::string::npos);
  EXPECT_NE(json.find("\"code\":42"), std::string::npos);
  EXPECT_NE(json.find("\"message\":\"Something went wrong\""), std::string::npos);
}

TEST(ApiProtocolTest, SerializeAck) {
  std::string json = SerializeAck("reset");

  EXPECT_NE(json.find("\"type\":\"ack\""), std::string::npos);
  EXPECT_NE(json.find("\"request\":\"reset\""), std::string::npos);
}

TEST(ApiProtocolTest, ParseResetParams) {
  std::string json = R"({
    "type": "reset",
    "home_team_id": 5,
    "away_team_id": 10,
    "seed": 12345,
    "match_duration_minutes": 10.0
  })";

  ResetParams params;
  bool success = ParseResetParams(json, params);

  EXPECT_TRUE(success);
  EXPECT_EQ(params.home_team_id, 5);
  EXPECT_EQ(params.away_team_id, 10);
  EXPECT_EQ(params.seed, 12345);
  EXPECT_FLOAT_EQ(params.match_duration_minutes, 10.0f);
}

TEST(ApiProtocolTest, ParseResetParamsDefaults) {
  std::string json = R"({"type": "reset"})";

  ResetParams params;
  bool success = ParseResetParams(json, params);

  EXPECT_TRUE(success);
  EXPECT_EQ(params.home_team_id, -1);
  EXPECT_EQ(params.away_team_id, -1);
  EXPECT_EQ(params.seed, -1);
  EXPECT_FLOAT_EQ(params.match_duration_minutes, 5.0f);
}

TEST(ApiProtocolTest, ParsePlayerAction) {
  std::string json = R"({
    "type": "action",
    "team": 0,
    "direction_x": 0.5,
    "direction_y": -0.3,
    "short_pass": true,
    "high_pass": false,
    "shot": true,
    "sprint": true,
    "dribble": false
  })";

  PlayerAction action;
  bool success = ParsePlayerAction(json, action);

  EXPECT_TRUE(success);
  EXPECT_FLOAT_EQ(action.direction_x, 0.5f);
  EXPECT_FLOAT_EQ(action.direction_y, -0.3f);
  EXPECT_TRUE(action.short_pass);
  EXPECT_FALSE(action.high_pass);
  EXPECT_TRUE(action.shot);
  EXPECT_TRUE(action.sprint);
  EXPECT_FALSE(action.dribble);
}

TEST(ApiProtocolTest, ParsePlayerActionDefaults) {
  std::string json = R"({"type": "action"})";

  PlayerAction action;
  bool success = ParsePlayerAction(json, action);

  EXPECT_TRUE(success);
  EXPECT_FLOAT_EQ(action.direction_x, 0.0f);
  EXPECT_FLOAT_EQ(action.direction_y, 0.0f);
  EXPECT_FALSE(action.short_pass);
  EXPECT_FALSE(action.shot);
  EXPECT_FALSE(action.sprint);
}
