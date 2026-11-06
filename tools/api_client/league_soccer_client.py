#!/usr/bin/env python3
"""
League-Soccer External Control API Client

A Python client for programmatic control of League-Soccer matches,
following the Google Research Football (GRF) API pattern:
reset/step/observe/act.

Usage:
    from league_soccer_client import LeagueSoccerEnv

    env = LeagueSoccerEnv()
    obs = env.reset(seed=42)
    
    while True:
        action = {"direction_x": 0.5, "direction_y": 0.0, "short_pass": True}
        obs, done = env.step(action)
        if done:
            break
    
    env.close()

Apache License 2.0 - see LICENSE
"""

import json
import socket
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Any


@dataclass
class PlayerObs:
    """Observation for a single player."""
    id: int = 0
    team: int = 0
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    vx: float = 0.0
    vy: float = 0.0
    direction: float = 0.0
    has_ball: bool = False
    active: bool = False
    stamina: float = 1.0


@dataclass
class BallObs:
    """Observation for the ball."""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    vx: float = 0.0
    vy: float = 0.0
    vz: float = 0.0
    owned_team: int = -1


@dataclass
class Observation:
    """Full game state observation."""
    step: int = 0
    match_time_ms: int = 0
    period: int = 0
    score: List[int] = field(default_factory=lambda: [0, 0])
    in_play: bool = False
    in_set_piece: bool = False
    goal_scored: bool = False
    last_touch_team: int = -1
    ball: BallObs = field(default_factory=BallObs)
    home_players: List[PlayerObs] = field(default_factory=list)
    away_players: List[PlayerObs] = field(default_factory=list)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Observation":
        """Create Observation from JSON dict."""
        obs = cls()
        obs.step = data.get("step", 0)
        obs.match_time_ms = data.get("match_time_ms", 0)
        obs.period = data.get("period", 0)
        score_data = data.get("score", [0, 0])
        if isinstance(score_data, list) and len(score_data) >= 2:
            obs.score = score_data
        else:
            obs.score = [data.get("score_home", 0), data.get("score_away", 0)]
        obs.in_play = data.get("in_play", False)
        obs.in_set_piece = data.get("in_set_piece", False)
        obs.goal_scored = data.get("goal_scored", False)
        obs.last_touch_team = data.get("last_touch_team", -1)
        
        ball_data = data.get("ball", {})
        obs.ball = BallObs(
            x=ball_data.get("x", 0.0),
            y=ball_data.get("y", 0.0),
            z=ball_data.get("z", 0.0),
            vx=ball_data.get("vx", 0.0),
            vy=ball_data.get("vy", 0.0),
            vz=ball_data.get("vz", 0.0),
            owned_team=ball_data.get("owned_team", -1),
        )
        
        for p in data.get("home_players", []):
            obs.home_players.append(PlayerObs(
                id=p.get("id", 0),
                team=p.get("team", 0),
                x=p.get("x", 0.0),
                y=p.get("y", 0.0),
                z=p.get("z", 0.0),
                vx=p.get("vx", 0.0),
                vy=p.get("vy", 0.0),
                direction=p.get("dir", 0.0),
                has_ball=p.get("has_ball", False),
                active=p.get("active", False),
                stamina=p.get("stamina", 1.0),
            ))
        for p in data.get("away_players", []):
            obs.away_players.append(PlayerObs(
                id=p.get("id", 0),
                team=p.get("team", 0),
                x=p.get("x", 0.0),
                y=p.get("y", 0.0),
                z=p.get("z", 0.0),
                vx=p.get("vx", 0.0),
                vy=p.get("vy", 0.0),
                direction=p.get("dir", 0.0),
                has_ball=p.get("has_ball", False),
                active=p.get("active", False),
                stamina=p.get("stamina", 1.0),
            ))
        
        return obs


class LeagueSoccerEnv:
    """
    GRF-style environment interface for League-Soccer.
    
    Provides reset/step/observe/act methods for external control
    of match simulation via TCP JSON-lines protocol.
    """

    DEFAULT_HOST = "127.0.0.1"
    DEFAULT_PORT = 50051

    def __init__(self, host: str = None, port: int = None):
        """
        Initialize the environment.
        
        Args:
            host: Server hostname (default: 127.0.0.1)
            port: Server port (default: 50051)
        """
        self.host = host or self.DEFAULT_HOST
        self.port = port or self.DEFAULT_PORT
        self.socket: Optional[socket.socket] = None
        self._buffer = ""

    def connect(self) -> bool:
        """Connect to the League-Soccer API server."""
        if self.socket:
            return True
        
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.host, self.port))
            self.socket.settimeout(30.0)
            return True
        except Exception as e:
            print(f"Connection failed: {e}")
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

    def _send(self, message: Dict[str, Any]) -> Dict[str, Any]:
        """Send a message and receive the response."""
        if not self.socket:
            if not self.connect():
                raise ConnectionError("Not connected to server")
        
        # Send message
        line = json.dumps(message) + "\n"
        self.socket.sendall(line.encode("utf-8"))
        
        # Receive response (JSON line)
        while "\n" not in self._buffer:
            data = self.socket.recv(4096)
            if not data:
                raise ConnectionError("Server closed connection")
            self._buffer += data.decode("utf-8")
        
        line, self._buffer = self._buffer.split("\n", 1)
        return json.loads(line)

    def reset(
        self,
        home_team_id: int = -1,
        away_team_id: int = -1,
        seed: int = -1,
        match_duration_minutes: float = 5.0,
    ) -> Observation:
        """
        Reset/start a new match.
        
        Args:
            home_team_id: Home team database ID (-1 for default)
            away_team_id: Away team database ID (-1 for default)
            seed: RNG seed for deterministic matches (-1 for random)
            match_duration_minutes: Match duration in minutes
        
        Returns:
            Initial observation
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
        
        return Observation.from_dict(response)

    def step(
        self,
        action: Dict[str, Any] = None,
        team: int = 0,
    ) -> tuple[Observation, bool]:
        """
        Advance simulation by one physics tick.
        
        Args:
            action: Player action dict (optional)
            team: Team ID for the action (0=home, 1=away)
        
        Returns:
            Tuple of (observation, done)
        """
        # Send action if provided
        if action:
            action_msg = {"type": "action", "team": team}
            action_msg.update(action)
            self._send(action_msg)
        
        # Step simulation
        response = self._send({"type": "step"})
        
        if response.get("type") == "error":
            raise RuntimeError(response.get("message", "Step failed"))
        
        obs = Observation.from_dict(response)
        done = response.get("done", False)
        
        return obs, done

    def observe(self) -> Observation:
        """Get current observation without stepping."""
        response = self._send({"type": "observe"})
        
        if response.get("type") == "error":
            raise RuntimeError(response.get("message", "Observe failed"))
        
        return Observation.from_dict(response)

    def set_action(self, action: Dict[str, Any], team: int = 0):
        """
        Set action for next step(s).
        
        Args:
            action: Action dict with keys:
                - direction_x: float [-1, 1]
                - direction_y: float [-1, 1]
                - short_pass: bool
                - high_pass: bool
                - long_pass: bool
                - shot: bool
                - sprint: bool
                - dribble: bool
                - switch_player: bool
                - special: bool
            team: Team ID (0=home, 1=away)
        """
        msg = {"type": "action", "team": team}
        msg.update(action)
        self._send(msg)

    # League/fixture data methods

    def list_leagues(self) -> List[Dict[str, Any]]:
        """Get list of all leagues."""
        response = self._send({"type": "list_leagues"})
        if response.get("type") == "error":
            raise RuntimeError(response.get("message"))
        return response.get("data", [])

    def list_fixtures(self, league_id: int = -1) -> List[Dict[str, Any]]:
        """Get list of fixtures (optionally filtered by league)."""
        response = self._send({"type": "list_fixtures", "league_id": league_id})
        if response.get("type") == "error":
            raise RuntimeError(response.get("message"))
        return response.get("data", [])

    def create_fixture(
        self,
        league_id: int,
        home_team_id: int,
        away_team_id: int,
        date: str,
    ) -> bool:
        """
        Create a new fixture.
        
        Args:
            league_id: League database ID
            home_team_id: Home team database ID
            away_team_id: Away team database ID
            date: Match date (YYYY-MM-DD)
        
        Returns:
            True on success
        """
        response = self._send({
            "type": "create_fixture",
            "league_id": league_id,
            "home_team_id": home_team_id,
            "away_team_id": away_team_id,
            "date": date,
        })
        return response.get("type") == "ack"

    def export_league_data(self) -> Dict[str, Any]:
        """Export all league data (leagues, teams, fixtures)."""
        response = self._send({"type": "export_league_data"})
        if response.get("type") == "error":
            raise RuntimeError(response.get("message"))
        return response


def main():
    """Example usage demonstrating the API."""
    import argparse
    
    parser = argparse.ArgumentParser(description="League-Soccer API Client Example")
    parser.add_argument("--host", default="127.0.0.1", help="Server host")
    parser.add_argument("--port", type=int, default=50051, help="Server port")
    parser.add_argument("--seed", type=int, default=-1, help="RNG seed")
    parser.add_argument("--steps", type=int, default=100, help="Steps to run")
    args = parser.parse_args()

    print(f"Connecting to {args.host}:{args.port}...")
    env = LeagueSoccerEnv(host=args.host, port=args.port)
    
    try:
        print(f"Resetting with seed={args.seed}...")
        obs = env.reset(seed=args.seed)
        print(f"Initial observation: step={obs.step}, score={obs.score}")
        
        for i in range(args.steps):
            # Example: random-ish action
            action = {
                "direction_x": 0.5 if i % 2 == 0 else -0.5,
                "direction_y": 0.0,
                "short_pass": (i % 10 == 0),
                "sprint": True,
            }
            
            obs, done = env.step(action, team=0)
            
            if i % 10 == 0:
                print(f"Step {obs.step}: time={obs.match_time_ms}ms, "
                      f"score={obs.score}, in_play={obs.in_play}")
            
            if done:
                print(f"Match ended at step {obs.step}")
                break
        
        print(f"Final score: {obs.score}")
        
    except Exception as e:
        print(f"Error: {e}")
    finally:
        env.close()
        print("Disconnected.")


if __name__ == "__main__":
    main()
