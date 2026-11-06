// League-Soccer External Control API Protocol Implementation
// Apache License 2.0 - see LICENSE

#include "api_protocol.hpp"

#include <cstdio>
#include <sstream>

namespace api {

namespace {

std::string EscapeJson(const std::string& s) {
  std::string result;
  result.reserve(s.size() + 16);
  for (char c : s) {
    switch (c) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += c; break;
    }
  }
  return result;
}

bool GetJsonString(const std::string& json, const std::string& key, std::string& out) {
  std::string needle = "\"" + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  pos += needle.size();
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  if (pos >= json.size() || json[pos] != '"') return false;
  pos++;
  size_t end = pos;
  while (end < json.size() && json[end] != '"') {
    if (json[end] == '\\' && end + 1 < json.size()) end++;
    end++;
  }
  out = json.substr(pos, end - pos);
  return true;
}

bool GetJsonInt(const std::string& json, const std::string& key, int& out) {
  std::string needle = "\"" + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  pos += needle.size();
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  out = atoi(json.c_str() + pos);
  return true;
}

bool GetJsonInt64(const std::string& json, const std::string& key, int64_t& out) {
  std::string needle = "\"" + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  pos += needle.size();
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  out = strtoll(json.c_str() + pos, nullptr, 10);
  return true;
}

bool GetJsonFloat(const std::string& json, const std::string& key, float& out) {
  std::string needle = "\"" + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  pos += needle.size();
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  out = static_cast<float>(atof(json.c_str() + pos));
  return true;
}

bool GetJsonBool(const std::string& json, const std::string& key, bool& out) {
  std::string needle = "\"" + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  pos += needle.size();
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  out = (json.substr(pos, 4) == "true");
  return true;
}

std::string SerializePlayerObs(const PlayerObservation& p) {
  char buf[512];
  snprintf(buf, sizeof(buf),
    "{\"id\":%d,\"team\":%d,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,"
    "\"vx\":%.3f,\"vy\":%.3f,\"dir\":%.4f,\"has_ball\":%s,"
    "\"active\":%s,\"stamina\":%.3f}",
    p.player_id, p.team_id, p.position_x, p.position_y, p.position_z,
    p.velocity_x, p.velocity_y, p.direction,
    p.has_ball ? "true" : "false",
    p.is_active ? "true" : "false",
    p.stamina);
  return buf;
}

std::string SerializeBallObs(const BallObservation& b) {
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"vx\":%.3f,\"vy\":%.3f,\"vz\":%.3f,\"owned_team\":%d}",
    b.position_x, b.position_y, b.position_z,
    b.velocity_x, b.velocity_y, b.velocity_z, b.owned_team);
  return buf;
}

}  // namespace

std::string SerializeObservation(const Observation& obs) {
  std::ostringstream ss;
  ss << "{\"type\":\"observation\",\"step\":" << obs.step
     << ",\"match_time_ms\":" << obs.match_time_ms
     << ",\"period\":" << obs.period
     << ",\"score\":[" << obs.score_home << "," << obs.score_away << "]"
     << ",\"in_play\":" << (obs.in_play ? "true" : "false")
     << ",\"in_set_piece\":" << (obs.in_set_piece ? "true" : "false")
     << ",\"goal_scored\":" << (obs.goal_scored ? "true" : "false")
     << ",\"last_touch_team\":" << obs.last_touch_team
     << ",\"ball\":" << SerializeBallObs(obs.ball)
     << ",\"home_players\":[";
  for (size_t i = 0; i < obs.home_players.size(); i++) {
    if (i > 0) ss << ",";
    ss << SerializePlayerObs(obs.home_players[i]);
  }
  ss << "],\"away_players\":[";
  for (size_t i = 0; i < obs.away_players.size(); i++) {
    if (i > 0) ss << ",";
    ss << SerializePlayerObs(obs.away_players[i]);
  }
  ss << "]}";
  return ss.str();
}

std::string SerializeStepResult(const StepResult& result) {
  std::string obs = SerializeObservation(result.observation);
  // Replace closing brace with done field
  obs.pop_back();
  obs += ",\"done\":" + std::string(result.done ? "true" : "false") + "}";
  // Change type
  size_t pos = obs.find("\"type\":\"observation\"");
  if (pos != std::string::npos) {
    obs.replace(pos, 20, "\"type\":\"step_result\"");
  }
  return obs;
}

std::string SerializeError(const std::string& message, int code) {
  return "{\"type\":\"error\",\"code\":" + std::to_string(code) +
         ",\"message\":\"" + EscapeJson(message) + "\"}";
}

std::string SerializeAck(const std::string& request_type) {
  return "{\"type\":\"ack\",\"request\":\"" + EscapeJson(request_type) + "\"}";
}

bool ParseResetParams(const std::string& json, ResetParams& out) {
  out = ResetParams{};
  GetJsonInt(json, "home_team_id", out.home_team_id);
  GetJsonInt(json, "away_team_id", out.away_team_id);
  GetJsonInt64(json, "seed", out.seed);
  GetJsonFloat(json, "match_duration_minutes", out.match_duration_minutes);
  return true;
}

bool ParsePlayerAction(const std::string& json, PlayerAction& out) {
  out = PlayerAction{};
  GetJsonFloat(json, "direction_x", out.direction_x);
  GetJsonFloat(json, "direction_y", out.direction_y);
  GetJsonBool(json, "short_pass", out.short_pass);
  GetJsonBool(json, "high_pass", out.high_pass);
  GetJsonBool(json, "long_pass", out.long_pass);
  GetJsonBool(json, "shot", out.shot);
  GetJsonBool(json, "sprint", out.sprint);
  GetJsonBool(json, "dribble", out.dribble);
  GetJsonBool(json, "switch_player", out.switch_player);
  GetJsonBool(json, "special", out.special);
  return true;
}

}  // namespace api
