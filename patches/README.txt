League-Soccer CPU AI Fix + Maximum Aggression Patch
===================================================

Date: Nov 6, 2026
Source baseline: 355cd11
Difficulty: 1.0 (Top Player maximum) - PRESERVED
Build validation: PASSED (cmake + compile, warnings only from pre-existing code)

================================================================================
CRITICAL FIX #1: GPU Warmup Phase Bug (Root Cause of "Not AI vs AI")
================================================================================

SYMPTOM: Players stand still waiting for kickoff. observe shows:
  period=1, match_time_ms=0, in_play=false

ROOT CAUSE: GPU warmup counter only incremented during e_MatchPhase_PreMatch,
but the phase transitions to 1stHalf at prepareTime (before startTime).
If warmup didn't complete before prepareTime, IsWarmedUp() never returns true
and the referee never blows the whistle.

FIX in src/onthepitch/match.cpp (Put() function):
  - Changed: warmup counter now increments regardless of match phase
  - Was: if (matchPhase == e_MatchPhase_PreMatch && warmupFramesRendered < ...)
  - Now: if (warmupFramesRendered < kWarmupFramesRequired)

ALSO FIXED: seed_automation.py --run mode
  - observe() does not return 'done' field (only step() does)
  - Changed run_match_cycle() to use step() instead of observe() for monitoring

================================================================================
CRITICAL FIX #2: Reset Crash (Thread Safety)
================================================================================

SYMPTOM: Game crashes on API reset command during active match.
  - Minidump written during "Caching animation positions"
  - WinError 10054 on client (connection forcibly closed)

ROOT CAUSE: API server callbacks run on network threads (per-client).
  Reset callback directly called gameTask->Action() to stop/start match,
  which destroys and creates Match objects. These operations race with the
  render thread (Put phase) and physics thread accessing match resources.

FIX: Thread-safe deferred reset mechanism:
  - Added PendingResetRequest struct in gametask.hpp with completion signaling
  - API callback now calls gameTask->RequestReset() which:
    1. Queues reset parameters (with mutex protection)
    2. Blocks waiting for completion (condition variable, 10s timeout)
  - GameTask::ProcessPhase() (main thread) checks for pending reset via
    ProcessPendingReset() and executes it safely
  - All match stop/start operations now occur on main game thread

Files modified:
  - src/gametask.hpp: Added PendingResetRequest struct, RequestReset() method
  - src/gametask.cpp: Implemented RequestReset() and ProcessPendingReset()
  - src/main.cpp: Reset callback now uses RequestReset() instead of direct Action()

================================================================================
FIX #3: CPU-Only Tackle AI (More Sliding and Standing Tackles)
================================================================================

SYMPTOM: CPU teams rarely/never perform sliding tackles despite max skill stats.
  Sliding tackles only attempted under very restrictive conditions.

ROOT CAUSE: Tackle decision thresholds in _SlidingCommand and _InterfereCommand
  were conservative, requiring near-perfect approach angles and narrow distances.
  The skill stats (technical_slidingtackle, technical_standingtackle) only affect
  SUCCESS probability in duels, NOT the frequency of tackle attempts.

FIX: CPU-only relaxed thresholds (human controls unchanged):

  _SlidingCommand (src/onthepitch/player/controller/playercontroller.cpp):
    - CouldWinABallDuelLikeliness threshold: 0.7 -> 0.45 (allows worse angles)
    - Ball distance window: 0.7-1.6m -> 0.5-2.2m (wider range)
    - Opponent time threshold: 260ms -> 180ms (more contested situations)
    - Shot-block distance: 0.6-1.8m -> 0.5-2.0m

  _InterfereCommand (standing tackle):
    - CouldWinABallDuelLikeliness threshold: 0.2 -> 0.1 for CPU-only teams
    - Human teams retain 0.2 threshold

EFFECT: CPU teams will:
  - Attempt sliding tackles more frequently during defensive play
  - Attempt standing tackles from slightly worse approach angles
  - Create more contested situations (potential fouls depend on referee logic)
  - NOT force constant tackles - still requires possession/distance conditions

Files modified:
  - src/onthepitch/player/controller/playercontroller.cpp: _SlidingCommand, _InterfereCommand

================================================================================
FIX #4: Reliable Match Cycling (No Quit Crash During Loading)
================================================================================

SYMPTOM: Game crashed at fulltime when client sent quit during next match loading.
  - Client detected done=true, sent {type:quit}
  - Game was already loading next match (internal auto-rematch triggered)
  - Quit handler ran on API thread during loading -> access violation crash

ROOT CAUSE:
  1. Auto-rematch started internally before client could observe done
  2. Client step() got "no match active" error, treated as fatal, sent quit
  3. Quit handler called SignalQuit() directly on API network thread
  4. Teardown raced with active loading/rendering -> crash

FIX - Multiple coordinated changes:

  1. External client controls match cycling when connected:
     - gameover.cpp: Check HasAPIClients() - if true, don't auto-rematch
     - main.hpp/cpp: Added HasAPIClients() function
     - api_server.hpp: Added HasConnectedClients() method
     When no client connected, standalone API mode still auto-rematches.

  2. Quit handler scheduled on main thread (like reset):
     - gametask.hpp: Added PendingQuitRequest, RequestQuit()
     - gametask.cpp: Implemented RequestQuit(), ProcessPendingQuit()
     - main.cpp: Quit callback uses RequestQuit() instead of direct SignalQuit()

  3. Client tolerates transient "no match active" errors:
     - seed_automation.py: run_match_cycle() retries on transient errors
     - Up to 30 retries (~30s) for "no match" errors during loading
     - Non-transient errors still fail immediately

  4. Client disconnect doesn't kill game by default:
     - seed_automation.py: close() no longer sends quit
     - Added quit_game() method for explicit game termination
     - Added --quit CLI flag to request game termination after operation

TEST CRITERIA for successful match cycling:
  1. Launch game with api_mode=true
  2. Run: python seed_automation.py --run 2 --seeds 42,43 --poll 1
  3. Expected: Two complete matches, no crashes, no transient errors
  4. Client disconnects cleanly, game stays running (ready for next client)

Files modified:
  - src/menu/ingame/gameover.cpp: HasAPIClients() check before auto-rematch
  - src/api/api_server.hpp: Added HasConnectedClients()
  - src/main.hpp: Added HasAPIClients() declaration
  - src/main.cpp: Implemented HasAPIClients(), quit uses RequestQuit()
  - src/gametask.hpp: Added PendingQuitRequest, RequestQuit()
  - src/gametask.cpp: Implemented RequestQuit(), ProcessPendingQuit()
  - patches/seed_automation.py: Transient error handling, close() vs quit_game()

================================================================================
FIX #5: Draw / Extra Time Phase Transition Bug (REVISED)
================================================================================

SYMPTOM: 0-0 draw at 90:00 causes match to freeze in period 3 (1stExtraTime).
  Match time stuck at 5400000ms, in_play=false, players/ball stationary.
  Client never receives done=true, seed43 never starts.

ROOT CAUSE (Two Issues):

  1. Referee checked CURRENT phase after SetMatchPhase() was already called:
     - Line 128: SetMatchPhase(e_MatchPhase_1stExtraTime)
     - Line 238: if (GetMatchPhase() == e_MatchPhase_1stExtraTime) ← WRONG!
     - This fired when ENTERING 1stExtraTime, not ENDING it

  2. Step callback didn't use gameOver flag:
     - result.done used broken phase logic, never detected regulation draws
     - GameOver() sets gameOver=true, but step callback ignored it

FIX (Proper Phase Lifecycle):

  1. Added buffer.endingPhase to RefereeBuffer (referee.hpp):
     - Stores the phase BEFORE SetMatchPhase() is called
     - Game-over checks use endingPhase, not current phase

  2. Referee game-over logic uses endingPhase (referee.cpp):
     - endingPhase == 2ndHalf: Check for winner OR (draw + extra_time disabled)
     - endingPhase == 1stExtraTime: Check for winner, else continue
     - endingPhase == 2ndExtraTime: Game over (penalties would need config)
     - endingPhase == Penalties: Game over

  3. Added IsGameOver() accessor (match.hpp)

  4. Step callback uses match->IsGameOver() (main.cpp):
     - result.done = match->IsGameOver();
     - Correctly detects all terminal states

PHASE LIFECYCLE (Exact):

  | Ending Phase    | Score  | extra_time | Result           | done |
  |-----------------|--------|------------|------------------|------|
  | 2ndHalf (90')   | Winner | Any        | GameOver()       | true |
  | 2ndHalf (90')   | Draw   | false      | GameOver()       | true |
  | 2ndHalf (90')   | Draw   | true       | → 1stExtraTime   | false|
  | 1stExtraTime    | Winner | N/A        | GameOver()       | true |
  | 1stExtraTime    | Draw   | N/A        | → 2ndExtraTime   | false|
  | 2ndExtraTime    | Any    | N/A        | GameOver()       | true |
  | Penalties       | Any    | N/A        | GameOver()       | true |

PRESERVED SEMANTICS:
  - Regulation draws valid when match_extra_time=false (default)
  - Extra time enabled via match_extra_time=true config
  - Decisive results end immediately regardless of config
  - No forced winners, no outcome changes

Files modified:
  - src/onthepitch/referee.hpp: Added endingPhase to RefereeBuffer
  - src/onthepitch/referee.cpp: Store endingPhase, use for game-over checks
  - src/onthepitch/match.hpp: Added IsGameOver() accessor
  - src/main.cpp: Step callback uses match->IsGameOver()

================================================================================
ARTIFACTS LOCATION: /opt/cursor/artifacts/
================================================================================

Files:
  cpu_ai_fix.patch      - Git patch for all source changes (161 lines)
  seed_automation.py    - Python client for observe/reset/step
  ai_behavior_notes.txt - Reference notes on AI behavior parameters
  README.txt            - This file
  league_soccer_patch.tar.gz - Tarball of above

================================================================================
CHANGE 1: Default CPU Autoplay with Opt-In Direct Control
================================================================================

Config option: api_direct_player_control (default: false)

When FALSE (default):
  - Both teams default to side=0 (CPU AI)
  - Match plays automatically without external client
  - API observe/reset/step work for monitoring
  - action command has no effect (no HumanGamers)

When TRUE (opt-in):
  - API gamepads control home (side=-1) and away (side=1)
  - action command drives player input directly
  - Original behavior for external player control

To enable direct control, set in config:
  "api_direct_player_control" "1"

Files Changed:
  src/menu/menutask.cpp - Reads api_direct_player_control, sets sides accordingly
  src/main.cpp          - Reset callback respects api_direct_player_control

================================================================================
CHANGE 2: CPU-Only Maximum Tackle Skills (RUNTIME, NO DB MUTATION)
================================================================================

Settings:
  technical_standingtackle: 1.0 (canonical max)
  technical_slidingtackle:  1.0 (canonical max)

Guard: team->GetHumanGamerCount() == 0

Implementation in src/onthepitch/player/player.cpp GetStat():
  if (team->GetHumanGamerCount() == 0) {
    if (strcmp(name, "technical_standingtackle") == 0 ||
        strcmp(name, "technical_slidingtackle") == 0) {
      return 1.0f * multiplier;
    }
  }

Scope:
  - CPU teams (no human gamers): Use 1.0 max
  - Human teams: UNCHANGED (use database values)
  - Mixed teams with human player: UNCHANGED

Canonical max source: src/data/playerdata.cpp:223 clamps to [0.01, 1.0]

================================================================================
CHANGE 3: CPU-Only Maximum Zone Pressure (RUNTIME, NO DB MUTATION)
================================================================================

Setting: team_pressure = 1.0 (supported max)

Guard: team->GetHumanGamerCount() == 0

Implementation in src/onthepitch/teamAIcontroller.cpp (TWO locations):
  const float pressureSetting = (team->GetHumanGamerCount() == 0)
      ? 1.0f
      : team->GetTeamData()->GetTactics().userProperties.GetReal("team_pressure", 0.5f);

Scope:
  - CPU teams (no human gamers): Use 1.0 max pressure
  - Human teams: UNCHANGED (use database tactics)
  - Mixed teams: UNCHANGED

Effect at team_pressure=1.0:
  - Zone pressing from anywhere (territory threshold -0.35)
  - Press range 13m (vs 9m at 0.5)
  - Duration up to 2300ms (vs 1500ms at 0.5)

================================================================================
PRESERVED (UNCHANGED)
================================================================================

  - match_difficulty = 1.0 (Top Player)
  - GPU warmup (30 frames before kickoff)
  - Auto-continue on game-over
  - Goal replay skip in API mode
  - All human player controls and database stats
  - All database records (NO mutation)
  - API action command functionality (when api_direct_player_control=true)

================================================================================
NOT INCLUDED
================================================================================

  - Defense depth/width shape changes (not verified for tackle increase)
  - Any database modifications
  - Any difficulty changes

================================================================================
HOW TO APPLY
================================================================================

1. Copy patch files from /opt/cursor/artifacts/ to your Pavilion checkout

2. Apply the source patch:
   cd /path/to/League-Soccer
   git apply cpu_ai_fix.patch

3. Copy automation client (optional):
   cp seed_automation.py tools/api_client/

4. Rebuild using your cached ls_build.cmd
   (Windows-specific build commands as per your existing setup)

5. Game stays CLOSED until manual test

================================================================================
VALIDATION PERFORMED
================================================================================

1. CMake configuration: SUCCESS
2. Full build: SUCCESS (119s)
3. Warnings: Pre-existing only (singleton, string-plus-int)
4. No new errors introduced

Build command used:
  cmake .. -DCMAKE_BUILD_TYPE=Release
  cmake --build . -j4

================================================================================
SEED AUTOMATION CLIENT
================================================================================

File: seed_automation.py

Reset schema (matches server api_protocol.cpp:160-166):
  - type: "reset"
  - home_team_id: int (-1 for default)
  - away_team_id: int (-1 for default)
  - seed: int (-1 for random)
  - match_duration_minutes: float (default 5.0)

NOTE: This is an automation client, not a "Seed Generator".

================================================================================
TECHNICAL REFERENCE
================================================================================

Tackle skills consumed at:
  src/onthepitch/match.cpp:1911-1917
  Formula: bounceBias += 0.1 + 0.4 * skill

Zone pressure controlled by:
  src/onthepitch/teamAIcontroller.cpp:250, 1205
  src/onthepitch/aitactics.hpp:51-64

CPU team detection:
  team->GetHumanGamerCount() == 0

================================================================================
NO PUSH / NO CI / NO LAUNCH
================================================================================

This package is for local apply and rebuild only.
Do NOT push to remote or trigger CI workflows.
Do NOT launch game - game stays CLOSED.
