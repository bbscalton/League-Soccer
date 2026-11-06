# League-Soccer External Control API

This document describes the external programmatic control API for League-Soccer,
enabling outside programs to drive matches in a GRF (Google Research Football)
style reset/step/observe/act pattern.

## Overview

The API provides:

1. **Live command interface** via TCP socket with JSON-lines protocol
2. **Step-and-reset loop** for programmatic match control
3. **Deterministic seeding** for reproducible matches
4. **League/fixture data access** for creating custom leagues and fixtures
5. **Frame capture** via existing PNG dump mechanism

## Quick Start

### Starting the API Server

Launch League-Soccer with API mode enabled via `football.config`:

```
"api_mode" "true"
"api_port" "50051"
```

Then launch the game with the config file as the first argument:

```bash
# Linux/macOS
./gameplayfootball football.config

# Windows
gameplayfootball.exe football.config
```

**Note:** Command-line flags like `--api-mode` are NOT supported. The game expects
the first argument to be a config file path. Set all options in `football.config`.

### Cold Start Behavior

When `api_mode` is enabled, the game:

1. **Skips all user prompts** - No intro screen, no "Press any key to continue",
   no main menu, no controller selection
2. **Starts a default match immediately** - Barcelona vs Real Madrid loads automatically
3. **Listens for API commands** - The TCP server starts on the configured port
4. **Renders normally** - The match is fully rendered for frame capture

The game boots directly into a playable match with zero human input required.
Use the `reset` command to start a new match with custom teams and settings.

### Python Client Example

```python
from tools.api_client.league_soccer_client import LeagueSoccerEnv

env = LeagueSoccerEnv(host="127.0.0.1", port=50051)

# Reset with deterministic seed
obs = env.reset(seed=42, match_duration_minutes=5.0)

# Game loop
while True:
    # Set player action
    action = {
        "direction_x": 0.5,
        "direction_y": 0.0,
        "short_pass": True,
        "sprint": True
    }
    
    # Step simulation
    obs, done = env.step(action, team=0)
    
    if done:
        break

print(f"Final score: {obs.score}")
env.close()
```

## Protocol Specification

### Transport

- **Protocol**: TCP with JSON-lines (one JSON object per line, newline-delimited)
- **Default host**: `127.0.0.1` (localhost only for security)
- **Default port**: `50051`
- **Max message size**: 1 MB

### Message Format

All messages are JSON objects with a `type` field:

```json
{"type": "message_type", ...additional fields...}
```

Responses include a `type` field indicating success or error.

### Client Messages

#### reset

Start or restart a match with optional parameters.

```json
{
  "type": "reset",
  "home_team_id": -1,
  "away_team_id": -1,
  "seed": 42,
  "match_duration_minutes": 5.0
}
```

**Parameters:**
- `home_team_id`: Home team database ID (-1 for default: Barcelona = 3)
- `away_team_id`: Away team database ID (-1 for default: Real Madrid = 8)
- `seed`: RNG seed for deterministic replay (-1 for random)
- `match_duration_minutes`: Match duration (default 5.0)

**Response:** Initial observation (see below)

**Cold Start Note:** In API mode, the game automatically loads a default match
(Barcelona vs Real Madrid) on startup. The `reset` command stops any running
match and starts a new one with the specified parameters. You can call `observe`
immediately after the game starts to get the initial match state, or call `reset`
to configure a custom match.

#### step

Advance simulation by one physics tick (default 10ms).

```json
{"type": "step"}
```

**Response:** Step result with observation and done flag

```json
{
  "type": "step_result",
  "step": 1234,
  "match_time_ms": 12340,
  "period": 0,
  "score": [1, 0],
  "in_play": true,
  "done": false,
  ...
}
```

#### observe

Get current game state without stepping.

```json
{"type": "observe"}
```

**Response:** Current observation

#### action

Set player actions for the next step(s).

```json
{
  "type": "action",
  "team": 0,
  "direction_x": 0.5,
  "direction_y": -0.3,
  "short_pass": true,
  "high_pass": false,
  "long_pass": false,
  "shot": false,
  "sprint": true,
  "dribble": false,
  "switch_player": false,
  "special": false
}
```

**Parameters:**
- `team`: Team ID (0 = home, 1 = away)
- `direction_x`: Left stick X axis [-1.0, 1.0]
- `direction_y`: Left stick Y axis [-1.0, 1.0]
- Button states (all boolean)

**Response:** Acknowledgment

#### quit

Disconnect and optionally end the match.

```json
{"type": "quit"}
```

### League/Fixture Messages

#### list_leagues

Get all leagues.

```json
{"type": "list_leagues"}
```

**Response:**
```json
{
  "type": "leagues",
  "data": [
    {"id": 1, "name": "Premier League"},
    {"id": 2, "name": "La Liga"}
  ]
}
```

#### list_fixtures

Get fixtures, optionally filtered by league.

```json
{"type": "list_fixtures", "league_id": 1}
```

**Response:**
```json
{
  "type": "fixtures",
  "data": [
    {
      "id": 123,
      "home_team_id": 1,
      "away_team_id": 2,
      "league_id": 1,
      "home_team": "Manchester United",
      "away_team": "Liverpool",
      "date": "2024-01-15",
      "home_goals": 2,
      "away_goals": 1,
      "played": true
    }
  ]
}
```

#### create_fixture

Create a new fixture.

```json
{
  "type": "create_fixture",
  "league_id": 1,
  "home_team_id": 1,
  "away_team_id": 2,
  "date": "2024-02-01"
}
```

**Response:** Acknowledgment or error

#### export_league_data

Export all league data.

```json
{"type": "export_league_data"}
```

**Response:**
```json
{
  "type": "league_data",
  "leagues": [...],
  "teams": [...],
  "fixtures": [...]
}
```

### Observation Format

```json
{
  "type": "observation",
  "step": 1234,
  "match_time_ms": 12340,
  "period": 0,
  "score": [1, 0],
  "in_play": true,
  "in_set_piece": false,
  "goal_scored": false,
  "last_touch_team": 0,
  "ball": {
    "x": 0.0,
    "y": 5.2,
    "z": 0.1,
    "vx": 2.5,
    "vy": -1.0,
    "vz": 0.0,
    "owned_team": 0
  },
  "home_players": [
    {
      "id": 1,
      "team": 0,
      "x": -20.0,
      "y": 0.0,
      "z": 0.0,
      "vx": 1.0,
      "vy": 0.0,
      "dir": 0.0,
      "has_ball": true,
      "active": true,
      "stamina": 0.95
    }
  ],
  "away_players": [...]
}
```

**Field descriptions:**
- `step`: Physics tick count
- `match_time_ms`: In-game match time in milliseconds
- `period`: Match period (0=1st half, 1=2nd half, 2+=extra time)
- `score`: [home_goals, away_goals]
- `in_play`: Ball is in play
- `in_set_piece`: Set piece in progress
- `goal_scored`: Goal was just scored
- `last_touch_team`: Last team to touch ball (-1 if none)
- `ball`: Ball state
- `home_players`, `away_players`: Player states

## Frame Capture

The existing PNG frame dump remains functional and can write to any directory:

### Environment Variables

- `GF_CAPTURE_DIRECTORY`: Output directory for frame PNGs
  - Example: `C:\Users\HP Pavilion x360\LeagueSoccer\frames` (Windows)
  - Example: `/home/user/LeagueSoccer/frames` (Linux/macOS)
- `GF_CAPTURE_INTERVAL_MS`: Minimum interval between captures (default: 2000ms)
- `GF_CAPTURE_LIMIT`: Maximum frames to capture (default: 60)

### Windows Example

```powershell
$env:GF_CAPTURE_DIRECTORY = "C:\Users\HP Pavilion x360\LeagueSoccer\frames"
$env:GF_CAPTURE_INTERVAL_MS = "500"
$env:GF_CAPTURE_LIMIT = "200"
.\gameplayfootball.exe --api-mode
```

### Linux/macOS Example

```bash
GF_CAPTURE_DIRECTORY=/home/user/LeagueSoccer/frames \
GF_CAPTURE_INTERVAL_MS=500 \
GF_CAPTURE_LIMIT=200 \
./gameplayfootball --api-mode
```

The frames are captured from the OpenGL framebuffer after rendering,
including the full game view with HUD elements.

**Note:** This is the League Soccer frame capture system. Do not use
paths associated with Google Research Football (`grf_frames`) as that
pipeline is being disconnected.

## Deterministic Seeding

When a seed is provided via the `reset` message, the match becomes
deterministic:

```json
{"type": "reset", "seed": 12345}
```

This seeds:
- The C standard library `rand()` used by AI decisions
- The Boost random generators used by physics

To replay a match identically, use the same seed and provide
the same action sequence.

## Building with API Support

The API is included in the standard build:

### Windows (MSVC + vcpkg)

```powershell
.\build.ps1
```

The API server links against Winsock2 (`ws2_32.lib`) automatically.

### Linux

```bash
./build.sh
```

### CMake Options

The API is built when `GAMEPLAYFOOTBALL_BUILD_GAME=ON` (default).

## SQLite Database Schema

League and fixture data is stored in SQLite. Key tables:

### leagues
```sql
CREATE TABLE leagues (
  id INTEGER PRIMARY KEY,
  name VARCHAR(128)
);
```

### teams
```sql
CREATE TABLE teams (
  id INTEGER PRIMARY KEY,
  name VARCHAR(128),
  league_id INTEGER,
  logo_url VARCHAR(256),
  kit_url VARCHAR(256)
);
```

### calendar (fixtures)
```sql
CREATE TABLE calendar (
  id INTEGER PRIMARY KEY,
  timestamp DATETIME,
  team1_id INTEGER,  -- home team
  team2_id INTEGER,  -- away team
  competition_id INTEGER,
  tournament_id INTEGER
);
```

### match_results
```sql
CREATE TABLE match_results (
  id INTEGER PRIMARY KEY,
  calendar_id INTEGER,
  team1_id INTEGER,
  team2_id INTEGER,
  team1_goals INTEGER,
  team2_goals INTEGER,
  played INTEGER,
  competition_id INTEGER
);
```

## Licensing Notes

### Code License

League-Soccer code is licensed under **Apache License 2.0**.

### Media Assets

The `data/` directory contains media assets (textures, sounds, 3D models,
player databases) inherited from the original GameplayFootball project
and Google Research Football. These assets may have different licensing
terms:

- **Player databases**: Team names, player names, and likeness data may
  have trademark or publicity rights considerations for commercial use.
- **Sound effects and music**: Original license terms not explicitly
  documented; treat as potentially restricted for commercial distribution.
- **3D models and textures**: Created for the open-source project but
  original authorship/license terms not explicitly stated.

For commercial use, consider:
1. Replacing player/team data with fictional or licensed content
2. Replacing audio assets with licensed or original content
3. Consulting legal counsel regarding trademark and publicity rights

The API code itself (src/api/) is fully Apache 2.0 licensed.

## Error Handling

Errors are returned as:

```json
{
  "type": "error",
  "code": -1,
  "message": "Description of the error"
}
```

Common errors:
- "No match active" - Call reset before step/observe
- "Database not available" - League data operations require database
- "Missing required field" - Check message format
