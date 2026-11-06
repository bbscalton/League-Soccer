// League-Soccer External Control API Server
// Apache License 2.0 - see LICENSE
//
// TCP server accepting JSON-lines commands for match control.
// Follows GRF-style reset/step/observe/act pattern.

#ifndef _HPP_API_SERVER
#define _HPP_API_SERVER

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "api_protocol.hpp"

class APIGamepad;

namespace api {

// Callback types for match control
using ResetCallback = std::function<bool(const ResetParams&, std::string&)>;
using StepCallback = std::function<bool(StepResult&)>;
using GetObservationCallback = std::function<bool(Observation&)>;
using QuitCallback = std::function<void()>;

// League data callbacks
using ListLeaguesCallback = std::function<std::string()>;
using ListFixturesCallback = std::function<std::string(int league_id)>;
using ListTeamsCallback = std::function<std::string(int league_id)>;
using CreateLeagueCallback =
    std::function<bool(const std::string& json, int& league_id, std::string& error)>;
using CreateTeamCallback =
    std::function<bool(const std::string& json, int& team_id, std::string& error)>;
using CreateFixtureCallback = std::function<bool(const std::string& json, std::string& error)>;
using ImportLeagueDataCallback = std::function<bool(const std::string& json, std::string& error)>;
using ExportLeagueDataCallback = std::function<std::string()>;

class APIServer {
public:
  APIServer();
  ~APIServer();

  // Start listening on the specified port (blocks until Stop() is called)
  bool Start(int port = kDefaultPort);

  // Stop the server and close all connections
  void Stop();

  // Check if server is running
  bool IsRunning() const { return running.load(); }

  // Set callbacks for match control
  void SetResetCallback(ResetCallback cb) { resetCallback = cb; }
  void SetStepCallback(StepCallback cb) { stepCallback = cb; }
  void SetGetObservationCallback(GetObservationCallback cb) { getObservationCallback = cb; }
  void SetQuitCallback(QuitCallback cb) { quitCallback = cb; }

  // Set callbacks for league data
  void SetListLeaguesCallback(ListLeaguesCallback cb) { listLeaguesCallback = cb; }
  void SetListFixturesCallback(ListFixturesCallback cb) { listFixturesCallback = cb; }
  void SetListTeamsCallback(ListTeamsCallback cb) { listTeamsCallback = cb; }
  void SetCreateLeagueCallback(CreateLeagueCallback cb) { createLeagueCallback = cb; }
  void SetCreateTeamCallback(CreateTeamCallback cb) { createTeamCallback = cb; }
  void SetCreateFixtureCallback(CreateFixtureCallback cb) { createFixtureCallback = cb; }
  void SetImportLeagueDataCallback(ImportLeagueDataCallback cb) { importLeagueDataCallback = cb; }
  void SetExportLeagueDataCallback(ExportLeagueDataCallback cb) { exportLeagueDataCallback = cb; }

  // Get API gamepads for teams (created by server, used by match)
  std::shared_ptr<APIGamepad> GetTeamGamepad(int teamId);

  // Set action for a team (called from client messages)
  void SetTeamAction(int teamId, const PlayerAction& action);

  // Get the port the server is listening on (-1 if not running)
  int GetPort() const { return serverPort; }

protected:
  void AcceptLoop();
  void HandleClient(int clientSocket);
  std::string ProcessMessage(const std::string& message);
  void SendResponse(int socket, const std::string& response);

  std::atomic<bool> running{false};
  std::atomic<bool> shouldStop{false};
  int serverSocket = -1;
  int serverPort = -1;

  std::thread acceptThread;
  std::mutex clientsMutex;
  std::vector<int> clientSockets;

  // Callbacks
  ResetCallback resetCallback;
  StepCallback stepCallback;
  GetObservationCallback getObservationCallback;
  QuitCallback quitCallback;
  ListLeaguesCallback listLeaguesCallback;
  ListFixturesCallback listFixturesCallback;
  ListTeamsCallback listTeamsCallback;
  CreateLeagueCallback createLeagueCallback;
  CreateTeamCallback createTeamCallback;
  CreateFixtureCallback createFixtureCallback;
  ImportLeagueDataCallback importLeagueDataCallback;
  ExportLeagueDataCallback exportLeagueDataCallback;

  // API gamepads for each team
  std::mutex gamepadMutex;
  std::shared_ptr<APIGamepad> teamGamepads[2];
};

// Global API server instance (created when API mode is enabled)
APIServer* GetAPIServer();
void SetAPIServer(APIServer* server);

}  // namespace api

#endif
