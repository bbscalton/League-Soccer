#!/usr/bin/env python3
"""
League-Soccer Seed Automation Client

A Python client for automated match cycles with seed control.
Connects to League-Soccer's TCP API to:
- Monitor match state (observe)
- Reset matches with specific seeds and team configurations
- Run automated match cycles for data collection

The game runs CPU-vs-CPU automatically; this client only needs to
observe and optionally reset when matches complete.

Usage:
    python seed_automation.py --host 127.0.0.1 --port 50051

Apache License 2.0 - see LICENSE
"""

import argparse
import json
import socket
import sys
import time
from dataclasses import dataclass
from typing import Optional, Dict, Any, List


@dataclass
class MatchState:
    """Current match state from observe/step."""
    step: int = 0
    match_time_ms: int = 0
    period: int = 0
    score_home: int = 0
    score_away: int = 0
    in_play: bool = False
    done: bool = False
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "MatchState":
        return cls(
            step=data.get("step", 0),
            match_time_ms=data.get("match_time_ms", 0),
            period=data.get("period", 0),
            score_home=data.get("score_home", data.get("score", [0, 0])[0] if isinstance(data.get("score"), list) else 0),
            score_away=data.get("score_away", data.get("score", [0, 0])[1] if isinstance(data.get("score"), list) else 0),
            in_play=data.get("in_play", False),
            done=data.get("done", False),
        )


class SeedAutomationClient:
    """
    Automation client for League-Soccer match cycles.
    
    The game runs CPU-vs-CPU automatically. This client:
    - Observes match state
    - Detects match completion
    - Resets with new seeds/teams as needed
    """
    
    DEFAULT_HOST = "127.0.0.1"
    DEFAULT_PORT = 50051
    
    def __init__(self, host: str = None, port: int = None, verbose: bool = True):
        self.host = host or self.DEFAULT_HOST
        self.port = port or self.DEFAULT_PORT
        self.verbose = verbose
        self.socket: Optional[socket.socket] = None
        self._buffer = ""
        self.match_count = 0
    
    def log(self, msg: str):
        if self.verbose:
            print(f"[SeedAuto] {msg}")
    
    def connect(self) -> bool:
        """Connect to the League-Soccer API server."""
        if self.socket:
            return True
        
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(10.0)
            self.socket.connect((self.host, self.port))
            self.log(f"Connected to {self.host}:{self.port}")
            return True
        except Exception as e:
            self.log(f"Connection failed: {e}")
            self.socket = None
            return False
    
    def close(self):
        """Close the connection."""
        if self.socket:
            try:
                self._send({"type": "quit"})
            except:
                pass
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
            self.log("Disconnected")
    
    def _send(self, message: Dict[str, Any]) -> Dict[str, Any]:
        """Send a message and receive the response."""
        if not self.socket:
            if not self.connect():
                raise ConnectionError("Not connected to server")
        
        line = json.dumps(message) + "\n"
        self.socket.sendall(line.encode("utf-8"))
        
        while "\n" not in self._buffer:
            data = self.socket.recv(4096)
            if not data:
                raise ConnectionError("Server closed connection")
            self._buffer += data.decode("utf-8")
        
        line, self._buffer = self._buffer.split("\n", 1)
        return json.loads(line)
    
    def observe(self) -> MatchState:
        """Get current match state without stepping."""
        response = self._send({"type": "observe"})
        if response.get("type") == "error":
            raise RuntimeError(response.get("message", "Observe failed"))
        return MatchState.from_dict(response)
    
    def step(self) -> MatchState:
        """Advance simulation and get state."""
        response = self._send({"type": "step"})
        if response.get("type") == "error":
            raise RuntimeError(response.get("message", "Step failed"))
        return MatchState.from_dict(response)
    
    def reset(
        self,
        home_team_id: int = -1,
        away_team_id: int = -1,
        seed: int = -1,
        match_duration_minutes: float = 5.0,
    ) -> MatchState:
        """
        Reset/start a new match.
        
        Args:
            home_team_id: Home team database ID (-1 for default Barcelona)
            away_team_id: Away team database ID (-1 for default Real Madrid)
            seed: RNG seed for deterministic replay (-1 for random)
            match_duration_minutes: Match duration (default 5.0)
        
        Returns:
            Initial match state
        """
        response = self._send({
            "type": "reset",
            "home_team_id": home_team_id,
            "away_team_id": away_team_id,
            "seed": seed,
            "match_duration_minutes": match_duration_minutes,
        })
        
        if response.get("type") == "error":
            raise RuntimeError(response.get("message", "Reset failed"))
        
        self.match_count += 1
        return MatchState.from_dict(response)
    
    def list_teams(self, league_id: int = -1) -> List[Dict[str, Any]]:
        """Get list of available teams."""
        response = self._send({"type": "list_teams", "league_id": league_id})
        if response.get("type") == "error":
            raise RuntimeError(response.get("message"))
        return response.get("data", [])
    
    def wait_for_ready(self, timeout_seconds: float = 30.0) -> bool:
        """
        Wait for the game to be ready (match active).
        
        Returns True if ready, False if timeout.
        """
        start = time.time()
        while time.time() - start < timeout_seconds:
            try:
                state = self.observe()
                # Match is ready when we get a valid observation
                self.log(f"Game ready: period={state.period}, in_play={state.in_play}, time={state.match_time_ms}ms")
                return True
            except Exception as e:
                self.log(f"Waiting for game... ({e})")
                time.sleep(1.0)
        return False
    
    def run_match_cycle(
        self,
        seed: int = -1,
        home_team_id: int = -1,
        away_team_id: int = -1,
        poll_interval: float = 1.0,
    ) -> Dict[str, Any]:
        """
        Run a single match cycle and return the result.
        
        The game plays CPU-vs-CPU automatically; this just monitors
        and returns the final score when done.
        
        Args:
            seed: RNG seed (-1 for random)
            home_team_id: Home team (-1 for default)
            away_team_id: Away team (-1 for default)
            poll_interval: Seconds between state checks
        
        Returns:
            Dict with match result (score, duration, etc.)
        """
        self.log(f"Starting match cycle (seed={seed}, home={home_team_id}, away={away_team_id})")
        
        # Reset to start a new match
        try:
            state = self.reset(
                home_team_id=home_team_id,
                away_team_id=away_team_id,
                seed=seed,
            )
            self.log(f"Match reset: period={state.period}, in_play={state.in_play}")
        except Exception as e:
            self.log(f"Reset failed: {e}")
            # Game may already be running a match, just observe
            state = self.observe()
        
        # Monitor until match completes
        last_score = (state.score_home, state.score_away)
        while not state.done:
            time.sleep(poll_interval)
            state = self.observe()
            
            # Log score changes
            new_score = (state.score_home, state.score_away)
            if new_score != last_score:
                self.log(f"GOAL! Score: {new_score[0]} - {new_score[1]}")
                last_score = new_score
        
        result = {
            "seed": seed,
            "home_team_id": home_team_id,
            "away_team_id": away_team_id,
            "score_home": state.score_home,
            "score_away": state.score_away,
            "match_time_ms": state.match_time_ms,
            "period": state.period,
        }
        self.log(f"Match complete: {result['score_home']} - {result['score_away']}")
        return result
    
    def run_automation(
        self,
        num_matches: int = 0,
        seeds: List[int] = None,
        poll_interval: float = 1.0,
    ) -> List[Dict[str, Any]]:
        """
        Run automated match cycles.
        
        Args:
            num_matches: Number of matches to run (0 = infinite)
            seeds: List of seeds to use (cycles through if shorter than num_matches)
            poll_interval: Seconds between state checks
        
        Returns:
            List of match results
        """
        results = []
        match_num = 0
        
        self.log(f"Starting automation: {num_matches if num_matches > 0 else 'infinite'} matches")
        
        try:
            while num_matches == 0 or match_num < num_matches:
                # Determine seed for this match
                if seeds:
                    seed = seeds[match_num % len(seeds)]
                else:
                    seed = -1  # Random
                
                result = self.run_match_cycle(seed=seed, poll_interval=poll_interval)
                result["match_number"] = match_num + 1
                results.append(result)
                
                match_num += 1
                self.log(f"Completed {match_num} matches")
                
        except KeyboardInterrupt:
            self.log("Automation interrupted by user")
        
        return results


def main():
    parser = argparse.ArgumentParser(
        description="League-Soccer Seed Automation Client",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Monitor current match
  python seed_automation.py --observe
  
  # Reset with specific seed
  python seed_automation.py --reset --seed 42
  
  # Run 10 automated matches
  python seed_automation.py --run 10
  
  # Run infinite matches with specific seeds
  python seed_automation.py --run 0 --seeds 1,2,3,4,5
"""
    )
    parser.add_argument("--host", default="127.0.0.1", help="Server host")
    parser.add_argument("--port", type=int, default=50051, help="Server port")
    parser.add_argument("--observe", action="store_true", help="Just observe current state")
    parser.add_argument("--reset", action="store_true", help="Reset match with given params")
    parser.add_argument("--seed", type=int, default=-1, help="RNG seed for reset")
    parser.add_argument("--home", type=int, default=-1, help="Home team ID")
    parser.add_argument("--away", type=int, default=-1, help="Away team ID")
    parser.add_argument("--run", type=int, default=None, metavar="N", 
                        help="Run N automated matches (0 = infinite)")
    parser.add_argument("--seeds", type=str, default=None,
                        help="Comma-separated list of seeds for automation")
    parser.add_argument("--poll", type=float, default=1.0, help="Poll interval in seconds")
    parser.add_argument("--list-teams", action="store_true", help="List available teams")
    parser.add_argument("--quiet", action="store_true", help="Less verbose output")
    args = parser.parse_args()
    
    client = SeedAutomationClient(
        host=args.host, 
        port=args.port,
        verbose=not args.quiet,
    )
    
    try:
        if not client.connect():
            print("ERROR: Could not connect to League-Soccer API server")
            print(f"Make sure the game is running with api_mode enabled on port {args.port}")
            sys.exit(1)
        
        if args.list_teams:
            teams = client.list_teams()
            print("Available teams:")
            for team in teams:
                print(f"  ID {team.get('id', '?')}: {team.get('name', 'Unknown')}")
            return
        
        if args.observe:
            state = client.observe()
            print(f"Match State:")
            print(f"  Period: {state.period}")
            print(f"  Time: {state.match_time_ms}ms")
            print(f"  Score: {state.score_home} - {state.score_away}")
            print(f"  In Play: {state.in_play}")
            print(f"  Done: {state.done}")
            return
        
        if args.reset:
            state = client.reset(
                home_team_id=args.home,
                away_team_id=args.away,
                seed=args.seed,
            )
            print(f"Match reset with seed={args.seed}")
            print(f"  Period: {state.period}, In Play: {state.in_play}")
            return
        
        if args.run is not None:
            seeds = None
            if args.seeds:
                seeds = [int(s.strip()) for s in args.seeds.split(",")]
            
            results = client.run_automation(
                num_matches=args.run,
                seeds=seeds,
                poll_interval=args.poll,
            )
            
            print(f"\nAutomation complete: {len(results)} matches")
            for r in results:
                print(f"  Match {r['match_number']}: {r['score_home']}-{r['score_away']} (seed={r['seed']})")
            return
        
        # Default: wait for ready and show state
        if client.wait_for_ready():
            state = client.observe()
            print(f"Game is running!")
            print(f"  Score: {state.score_home} - {state.score_away}")
            print(f"  Time: {state.match_time_ms}ms, Period: {state.period}")
            print(f"  In Play: {state.in_play}")
        else:
            print("ERROR: Game not ready within timeout")
            sys.exit(1)
            
    except KeyboardInterrupt:
        print("\nInterrupted")
    except Exception as e:
        print(f"ERROR: {e}")
        sys.exit(1)
    finally:
        client.close()


if __name__ == "__main__":
    main()
