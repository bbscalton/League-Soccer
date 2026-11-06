// League-Soccer League Data API
// Apache License 2.0 - see LICENSE
//
// Provides JSON import/export of league and fixture data so external
// applications can create their own leagues and feed them into League Soccer.

#ifndef _HPP_API_LEAGUE_API
#define _HPP_API_LEAGUE_API

#include <string>

namespace api {

// List all leagues as JSON array
// Returns: {"type":"leagues","data":[{"id":1,"name":"..."},...]}}
std::string ListLeagues();

// List fixtures for a league (or all fixtures if league_id < 0)
// Returns: {"type":"fixtures","data":[...]}
std::string ListFixtures(int league_id = -1);

// List teams for a league (or all teams if league_id < 0)
// Returns: {"type":"teams","data":[...]}
std::string ListTeams(int league_id = -1);

// Create a new league
// Input JSON: {"name":"League Name","country":"Country"}
// Returns true on success, sets error on failure
bool CreateLeague(const std::string& json, int& league_id, std::string& error);

// Create a new fixture
// Input JSON: {"league_id":1,"home_team_id":1,"away_team_id":2,"date":"2024-01-15"}
bool CreateFixture(const std::string& json, int& fixture_id, std::string& error);

// Update a fixture
// Input JSON: {"id":1,"date":"2024-01-20","home_goals":2,"away_goals":1}
bool UpdateFixture(const std::string& json, std::string& error);

// Create a new team
// Input JSON: {"name":"Team Name","league_id":1}
bool CreateTeam(const std::string& json, int& team_id, std::string& error);

// Import full league data structure (leagues, teams, fixtures)
// Input JSON: {"leagues":[...],"teams":[...],"fixtures":[...]}
bool ImportLeagueData(const std::string& json, std::string& error);

// Export all league data as JSON
// Returns: {"type":"league_data","leagues":[...],"teams":[...],"fixtures":[...]}
std::string ExportLeagueData();

}  // namespace api

#endif
