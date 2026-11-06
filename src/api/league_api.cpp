// League-Soccer League Data API Implementation
// Apache License 2.0 - see LICENSE

#include "league_api.hpp"

#include <cstdlib>
#include <sstream>

#include "utils/database.hpp"

using blunted::Database;

// Forward declaration - defined in main.cpp
Database* GetDB();

namespace api {

namespace {

std::string EscapeJson(const std::string& s) {
  std::string result;
  result.reserve(s.size() + 16);
  for (char c : s) {
    switch (c) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        result += c;
        break;
    }
  }
  return result;
}

bool GetJsonString(const std::string& json, const std::string& key, std::string& out) {
  std::string needle = "\"" + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos)
    return false;
  pos += needle.size();
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
    pos++;
  if (pos >= json.size() || json[pos] != '"')
    return false;
  pos++;
  size_t end = pos;
  while (end < json.size() && json[end] != '"') {
    if (json[end] == '\\' && end + 1 < json.size())
      end++;
    end++;
  }
  out = json.substr(pos, end - pos);
  return true;
}

bool GetJsonInt(const std::string& json, const std::string& key, int& out) {
  std::string needle = "\"" + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos)
    return false;
  pos += needle.size();
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
    pos++;
  out = atoi(json.c_str() + pos);
  return true;
}

}  // namespace

std::string ListLeagues() {
  Database* db = GetDB();
  if (!db) {
    return "{\"type\":\"error\",\"message\":\"Database not available\"}";
  }

  auto result = db->Query("SELECT id, name FROM leagues ORDER BY id");

  std::ostringstream ss;
  ss << "{\"type\":\"leagues\",\"data\":[";

  for (size_t i = 0; i < result->data.size(); i++) {
    if (i > 0)
      ss << ",";
    const auto& row = result->data[i];
    ss << "{\"id\":" << (row.size() > 0 ? row[0] : "0") << ",\"name\":\""
       << EscapeJson(row.size() > 1 ? row[1] : "") << "\"}";
  }

  ss << "]}";
  return ss.str();
}

std::string ListFixtures(int league_id) {
  Database* db = GetDB();
  if (!db) {
    return "{\"type\":\"error\",\"message\":\"Database not available\"}";
  }

  // Check if calendar table exists
  auto tableCheck =
      db->Query("SELECT name FROM sqlite_master WHERE type='table' AND name='calendar'");
  if (tableCheck->data.empty()) {
    // No calendar table - return empty fixtures list
    return "{\"type\":\"fixtures\",\"data\":[]}";
  }

  std::string query =
      "SELECT c.id, c.team1_id, c.team2_id, c.competition_id, "
      "t1.name AS home_name, t2.name AS away_name, "
      "date(c.timestamp) AS match_date, "
      "COALESCE(mr.team1_goals, -1) AS home_goals, "
      "COALESCE(mr.team2_goals, -1) AS away_goals, "
      "COALESCE(mr.played, 0) AS played "
      "FROM calendar c "
      "JOIN teams t1 ON c.team1_id = t1.id "
      "JOIN teams t2 ON c.team2_id = t2.id "
      "LEFT JOIN match_results mr ON mr.calendar_id = c.id ";

  if (league_id >= 0) {
    query += "WHERE c.competition_id = " + std::to_string(league_id) + " ";
  }
  query += "ORDER BY c.timestamp, c.id";

  auto result = db->Query(query);

  std::ostringstream ss;
  ss << "{\"type\":\"fixtures\",\"data\":[";

  for (size_t i = 0; i < result->data.size(); i++) {
    if (i > 0)
      ss << ",";
    const auto& row = result->data[i];
    ss << "{\"id\":" << (row.size() > 0 ? row[0] : "0")
       << ",\"home_team_id\":" << (row.size() > 1 ? row[1] : "0")
       << ",\"away_team_id\":" << (row.size() > 2 ? row[2] : "0")
       << ",\"league_id\":" << (row.size() > 3 ? row[3] : "0") << ",\"home_team\":\""
       << EscapeJson(row.size() > 4 ? row[4] : "") << "\"" << ",\"away_team\":\""
       << EscapeJson(row.size() > 5 ? row[5] : "") << "\"" << ",\"date\":\""
       << (row.size() > 6 ? row[6] : "") << "\""
       << ",\"home_goals\":" << (row.size() > 7 ? row[7] : "-1")
       << ",\"away_goals\":" << (row.size() > 8 ? row[8] : "-1")
       << ",\"played\":" << (row.size() > 9 && row[9] == "1" ? "true" : "false") << "}";
  }

  ss << "]}";
  return ss.str();
}

std::string ListTeams(int league_id) {
  Database* db = GetDB();
  if (!db) {
    return "{\"type\":\"error\",\"message\":\"Database not available\"}";
  }

  std::string query = "SELECT id, name, league_id FROM teams ";
  if (league_id >= 0) {
    query += "WHERE league_id = " + std::to_string(league_id) + " ";
  }
  query += "ORDER BY name";

  auto result = db->Query(query);

  std::ostringstream ss;
  ss << "{\"type\":\"teams\",\"data\":[";

  for (size_t i = 0; i < result->data.size(); i++) {
    if (i > 0)
      ss << ",";
    const auto& row = result->data[i];
    ss << "{\"id\":" << (row.size() > 0 ? row[0] : "0") << ",\"name\":\""
       << EscapeJson(row.size() > 1 ? row[1] : "") << "\""
       << ",\"league_id\":" << (row.size() > 2 ? row[2] : "0") << "}";
  }

  ss << "]}";
  return ss.str();
}

bool CreateLeague(const std::string& json, int& league_id, std::string& error) {
  Database* db = GetDB();
  if (!db) {
    error = "Database not available";
    return false;
  }

  std::string name, country;
  if (!GetJsonString(json, "name", name)) {
    error = "Missing 'name' field";
    return false;
  }
  GetJsonString(json, "country", country);

  std::string query = "INSERT INTO leagues (name) VALUES ('" + EscapeJson(name) + "')";
  db->Query(query);

  // Get the inserted ID
  auto result = db->Query("SELECT last_insert_rowid()");
  if (!result->data.empty() && !result->data[0].empty()) {
    league_id = atoi(result->data[0][0].c_str());
  } else {
    league_id = -1;
  }

  return true;
}

namespace {
bool EnsureCalendarTable(Database* db) {
  // Create calendar table if it doesn't exist
  db->Query(
      "CREATE TABLE IF NOT EXISTS calendar ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
      "timestamp TEXT, "
      "team1_id INTEGER, "
      "team2_id INTEGER, "
      "competition_id INTEGER, "
      "tournament_id INTEGER, "
      "FOREIGN KEY (team1_id) REFERENCES teams(id), "
      "FOREIGN KEY (team2_id) REFERENCES teams(id), "
      "FOREIGN KEY (competition_id) REFERENCES leagues(id)"
      ")");

  // Create match_results table if it doesn't exist
  db->Query(
      "CREATE TABLE IF NOT EXISTS match_results ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
      "calendar_id INTEGER, "
      "team1_id INTEGER, "
      "team2_id INTEGER, "
      "team1_goals INTEGER DEFAULT 0, "
      "team2_goals INTEGER DEFAULT 0, "
      "played INTEGER DEFAULT 0, "
      "competition_id INTEGER, "
      "FOREIGN KEY (calendar_id) REFERENCES calendar(id)"
      ")");

  return true;
}
}  // anonymous namespace

bool CreateFixture(const std::string& json, int& fixture_id, std::string& error) {
  Database* db = GetDB();
  if (!db) {
    error = "Database not available";
    return false;
  }

  // Ensure tables exist
  EnsureCalendarTable(db);

  int league_id = -1, home_team_id = -1, away_team_id = -1;
  std::string date;

  if (!GetJsonInt(json, "league_id", league_id) || league_id < 0) {
    error = "Missing or invalid 'league_id'";
    return false;
  }
  if (!GetJsonInt(json, "home_team_id", home_team_id) || home_team_id < 0) {
    error = "Missing or invalid 'home_team_id'";
    return false;
  }
  if (!GetJsonInt(json, "away_team_id", away_team_id) || away_team_id < 0) {
    error = "Missing or invalid 'away_team_id'";
    return false;
  }
  if (!GetJsonString(json, "date", date)) {
    error = "Missing 'date' field";
    return false;
  }

  std::string query =
      "INSERT INTO calendar (timestamp, team1_id, team2_id, competition_id, tournament_id) "
      "VALUES ('" +
      date + "', " + std::to_string(home_team_id) + ", " + std::to_string(away_team_id) + ", " +
      std::to_string(league_id) + ", NULL)";

  auto insertResult = db->Query(query);

  auto result = db->Query("SELECT last_insert_rowid()");
  if (!result->data.empty() && !result->data[0].empty()) {
    fixture_id = atoi(result->data[0][0].c_str());
    if (fixture_id <= 0) {
      error = "Failed to insert fixture";
      return false;
    }
  } else {
    error = "Failed to get fixture ID";
    return false;
  }

  return true;
}

bool UpdateFixture(const std::string& json, std::string& error) {
  Database* db = GetDB();
  if (!db) {
    error = "Database not available";
    return false;
  }

  int fixture_id = -1;
  if (!GetJsonInt(json, "id", fixture_id) || fixture_id < 0) {
    error = "Missing or invalid 'id'";
    return false;
  }

  // Check what fields to update
  std::string date;
  int home_goals = -1, away_goals = -1;
  bool hasDate = GetJsonString(json, "date", date);
  bool hasHomeGoals = GetJsonInt(json, "home_goals", home_goals);
  bool hasAwayGoals = GetJsonInt(json, "away_goals", away_goals);

  if (hasDate) {
    db->Query("UPDATE calendar SET timestamp = '" + date +
              "' WHERE id = " + std::to_string(fixture_id));
  }

  if (hasHomeGoals || hasAwayGoals) {
    // Check if result exists
    auto result =
        db->Query("SELECT id FROM match_results WHERE calendar_id = " + std::to_string(fixture_id));

    if (result->data.empty()) {
      // Get team IDs from calendar
      auto calResult =
          db->Query("SELECT team1_id, team2_id, competition_id FROM calendar WHERE id = " +
                    std::to_string(fixture_id));

      if (calResult->data.empty()) {
        error = "Fixture not found";
        return false;
      }

      int team1 = atoi(calResult->data[0][0].c_str());
      int team2 = atoi(calResult->data[0][1].c_str());
      int comp = atoi(calResult->data[0][2].c_str());

      db->Query(
          "INSERT INTO match_results (calendar_id, team1_id, team2_id, "
          "team1_goals, team2_goals, played, competition_id) VALUES (" +
          std::to_string(fixture_id) + ", " + std::to_string(team1) + ", " + std::to_string(team2) +
          ", " + std::to_string(home_goals >= 0 ? home_goals : 0) + ", " +
          std::to_string(away_goals >= 0 ? away_goals : 0) + ", 1, " + std::to_string(comp) + ")");
    } else {
      std::string update = "UPDATE match_results SET played = 1";
      if (hasHomeGoals)
        update += ", team1_goals = " + std::to_string(home_goals);
      if (hasAwayGoals)
        update += ", team2_goals = " + std::to_string(away_goals);
      update += " WHERE calendar_id = " + std::to_string(fixture_id);
      db->Query(update);
    }
  }

  return true;
}

bool CreateTeam(const std::string& json, int& team_id, std::string& error) {
  Database* db = GetDB();
  if (!db) {
    error = "Database not available";
    return false;
  }

  std::string name;
  int league_id = -1;

  if (!GetJsonString(json, "name", name)) {
    error = "Missing 'name' field";
    return false;
  }
  GetJsonInt(json, "league_id", league_id);

  std::string query = "INSERT INTO teams (name, league_id) VALUES ('" + EscapeJson(name) + "', " +
                      (league_id >= 0 ? std::to_string(league_id) : "NULL") + ")";
  db->Query(query);

  auto result = db->Query("SELECT last_insert_rowid()");
  if (!result->data.empty() && !result->data[0].empty()) {
    team_id = atoi(result->data[0][0].c_str());
  } else {
    team_id = -1;
  }

  return true;
}

bool ImportLeagueData(const std::string& json, std::string& error) {
  Database* db = GetDB();
  if (!db) {
    error = "Database not available";
    return false;
  }

  // This is a simplified implementation. A full implementation would
  // parse the JSON structure and insert leagues, teams, and fixtures
  // while maintaining referential integrity.

  // For now, we support importing individual fixtures
  // Full batch import would require a JSON parser library

  error = "Batch import requires structured JSON. Use create_fixture for individual fixtures.";
  return false;
}

std::string ExportLeagueData() {
  std::ostringstream ss;
  ss << "{\"type\":\"league_data\"";

  // Add leagues
  std::string leagues = ListLeagues();
  size_t dataPos = leagues.find("\"data\":");
  if (dataPos != std::string::npos) {
    size_t start = leagues.find('[', dataPos);
    size_t end = leagues.rfind(']');
    if (start != std::string::npos && end != std::string::npos) {
      ss << ",\"leagues\":" << leagues.substr(start, end - start + 1);
    }
  }

  // Add teams
  std::string teams = ListTeams(-1);
  dataPos = teams.find("\"data\":");
  if (dataPos != std::string::npos) {
    size_t start = teams.find('[', dataPos);
    size_t end = teams.rfind(']');
    if (start != std::string::npos && end != std::string::npos) {
      ss << ",\"teams\":" << teams.substr(start, end - start + 1);
    }
  }

  // Add fixtures
  std::string fixtures = ListFixtures(-1);
  dataPos = fixtures.find("\"data\":");
  if (dataPos != std::string::npos) {
    size_t start = fixtures.find('[', dataPos);
    size_t end = fixtures.rfind(']');
    if (start != std::string::npos && end != std::string::npos) {
      ss << ",\"fixtures\":" << fixtures.substr(start, end - start + 1);
    }
  }

  ss << "}";
  return ss.str();
}

}  // namespace api
