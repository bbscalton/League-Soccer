// League-Soccer External Control API Server Implementation
// Apache License 2.0 - see LICENSE

#include "api_server.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "hid/apigamepad.hpp"

// Convert api::PlayerAction to APIPlayerAction
namespace {
APIPlayerAction ToAPIPlayerAction(const api::PlayerAction& src) {
  APIPlayerAction dst;
  dst.direction_x = src.direction_x;
  dst.direction_y = src.direction_y;
  dst.short_pass = src.short_pass;
  dst.high_pass = src.high_pass;
  dst.long_pass = src.long_pass;
  dst.shot = src.shot;
  dst.sprint = src.sprint;
  dst.dribble = src.dribble;
  dst.switch_player = src.switch_player;
  dst.special = src.special;
  return dst;
}
}  // namespace

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socket_t = SOCKET;
#define CLOSE_SOCKET closesocket
#define SOCKET_ERROR_CODE WSAGetLastError()
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCKET -1
#define CLOSE_SOCKET close
#define SOCKET_ERROR_CODE errno
#endif

namespace api {

namespace {

static APIServer* g_apiServer = nullptr;

bool InitSockets() {
#ifdef _WIN32
  static bool initialized = false;
  if (!initialized) {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
      printf("[API] WSAStartup failed: %d\n", result);
      return false;
    }
    initialized = true;
  }
#endif
  return true;
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

APIServer::APIServer() {
  teamGamepads[0] = std::make_shared<APIGamepad>(0);
  teamGamepads[1] = std::make_shared<APIGamepad>(1);
}

APIServer::~APIServer() {
  Stop();
}

bool APIServer::Start(int port) {
  if (running.load())
    return false;

  if (!InitSockets()) {
    return false;
  }

  serverSocket = static_cast<int>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (serverSocket == INVALID_SOCKET) {
    printf("[API] Failed to create socket: %d\n", SOCKET_ERROR_CODE);
    return false;
  }

  // Allow address reuse
  int opt = 1;
#ifdef _WIN32
  setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
  setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

  struct sockaddr_in serverAddr;
  memset(&serverAddr, 0, sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  serverAddr.sin_port = htons(static_cast<uint16_t>(port));

  if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
    printf("[API] Failed to bind socket to port %d: %d\n", port, SOCKET_ERROR_CODE);
    CLOSE_SOCKET(serverSocket);
    serverSocket = -1;
    return false;
  }

  if (listen(serverSocket, 5) < 0) {
    printf("[API] Failed to listen on socket: %d\n", SOCKET_ERROR_CODE);
    CLOSE_SOCKET(serverSocket);
    serverSocket = -1;
    return false;
  }

  serverPort = port;
  running.store(true);
  shouldStop.store(false);

  printf("[API] Server listening on 127.0.0.1:%d\n", port);

  acceptThread = std::thread(&APIServer::AcceptLoop, this);

  return true;
}

void APIServer::Stop() {
  if (!running.load())
    return;

  shouldStop.store(true);

  // Close server socket to unblock accept()
  if (serverSocket != -1) {
    CLOSE_SOCKET(serverSocket);
    serverSocket = -1;
  }

  // Close all client sockets
  {
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (int clientSocket : clientSockets) {
      CLOSE_SOCKET(clientSocket);
    }
    clientSockets.clear();
  }

  if (acceptThread.joinable()) {
    acceptThread.join();
  }

  running.store(false);
  serverPort = -1;
  printf("[API] Server stopped\n");
}

void APIServer::AcceptLoop() {
  while (!shouldStop.load()) {
    struct sockaddr_in clientAddr;
#ifdef _WIN32
    int addrLen = sizeof(clientAddr);
#else
    socklen_t addrLen = sizeof(clientAddr);
#endif

    int clientSocket =
        static_cast<int>(accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen));

    if (clientSocket == INVALID_SOCKET) {
      if (shouldStop.load())
        break;
      printf("[API] Accept failed: %d\n", SOCKET_ERROR_CODE);
      continue;
    }

    printf("[API] Client connected\n");

    {
      std::lock_guard<std::mutex> lock(clientsMutex);
      clientSockets.push_back(clientSocket);
    }

    // Handle client in a new thread
    std::thread clientThread(&APIServer::HandleClient, this, clientSocket);
    clientThread.detach();
  }
}

void APIServer::HandleClient(int clientSocket) {
  char buffer[4096];
  std::string partial;

  while (!shouldStop.load()) {
    int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead <= 0) {
      break;
    }

    buffer[bytesRead] = '\0';
    partial += buffer;

    // Process complete lines (JSON-lines protocol)
    size_t pos;
    while ((pos = partial.find('\n')) != std::string::npos) {
      std::string message = partial.substr(0, pos);
      partial = partial.substr(pos + 1);

      if (!message.empty()) {
        std::string response = ProcessMessage(message);
        if (!response.empty()) {
          SendResponse(clientSocket, response);
        }
      }
    }
  }

  // Remove from client list
  {
    std::lock_guard<std::mutex> lock(clientsMutex);
    clientSockets.erase(std::remove(clientSockets.begin(), clientSockets.end(), clientSocket),
                        clientSockets.end());
  }

  CLOSE_SOCKET(clientSocket);
  printf("[API] Client disconnected\n");
}

std::string APIServer::ProcessMessage(const std::string& message) {
  std::string type;
  if (!GetJsonString(message, "type", type)) {
    return SerializeError("Missing 'type' field");
  }

  if (type == "reset") {
    ResetParams params;
    ParseResetParams(message, params);
    std::string error;
    if (resetCallback && resetCallback(params, error)) {
      // Clear gamepad actions
      teamGamepads[0]->ClearAction();
      teamGamepads[1]->ClearAction();
      // Return initial observation
      Observation obs;
      if (getObservationCallback && getObservationCallback(obs)) {
        return SerializeObservation(obs);
      }
      return SerializeAck("reset");
    } else {
      return SerializeError(error.empty() ? "Reset failed" : error);
    }
  }

  if (type == "step") {
    StepResult result;
    if (stepCallback && stepCallback(result)) {
      return SerializeStepResult(result);
    } else {
      return SerializeError("Step failed - no match active");
    }
  }

  if (type == "observe") {
    Observation obs;
    if (getObservationCallback && getObservationCallback(obs)) {
      return SerializeObservation(obs);
    } else {
      return SerializeError("Observe failed - no match active");
    }
  }

  if (type == "action") {
    int team = 0;
    GetJsonInt(message, "team", team);
    PlayerAction action;
    ParsePlayerAction(message, action);
    SetTeamAction(team, action);
    return SerializeAck("action");
  }

  if (type == "quit") {
    if (quitCallback)
      quitCallback();
    return SerializeAck("quit");
  }

  if (type == "list_leagues") {
    if (listLeaguesCallback) {
      return listLeaguesCallback();
    }
    return SerializeError("League listing not available");
  }

  if (type == "list_fixtures") {
    int league_id = -1;
    GetJsonInt(message, "league_id", league_id);
    if (listFixturesCallback) {
      return listFixturesCallback(league_id);
    }
    return SerializeError("Fixture listing not available");
  }

  if (type == "create_fixture") {
    if (createFixtureCallback) {
      std::string error;
      if (createFixtureCallback(message, error)) {
        return SerializeAck("create_fixture");
      }
      return SerializeError(error);
    }
    return SerializeError("Fixture creation not available");
  }

  if (type == "import_league_data") {
    if (importLeagueDataCallback) {
      std::string error;
      if (importLeagueDataCallback(message, error)) {
        return SerializeAck("import_league_data");
      }
      return SerializeError(error);
    }
    return SerializeError("League import not available");
  }

  if (type == "export_league_data") {
    if (exportLeagueDataCallback) {
      return exportLeagueDataCallback();
    }
    return SerializeError("League export not available");
  }

  if (type == "create_league") {
    if (createLeagueCallback) {
      std::string error;
      int league_id = -1;
      if (createLeagueCallback(message, league_id, error)) {
        return "{\"type\":\"ack\",\"request\":\"create_league\",\"league_id\":" +
               std::to_string(league_id) + "}";
      }
      return SerializeError(error);
    }
    return SerializeError("League creation not available");
  }

  if (type == "create_team") {
    if (createTeamCallback) {
      std::string error;
      int team_id = -1;
      if (createTeamCallback(message, team_id, error)) {
        return "{\"type\":\"ack\",\"request\":\"create_team\",\"team_id\":" +
               std::to_string(team_id) + "}";
      }
      return SerializeError(error);
    }
    return SerializeError("Team creation not available");
  }

  if (type == "list_teams") {
    if (listTeamsCallback) {
      int league_id = -1;
      GetJsonInt(message, "league_id", league_id);
      return listTeamsCallback(league_id);
    }
    return SerializeError("Team listing not available");
  }

  return SerializeError("Unknown message type: " + type);
}

void APIServer::SendResponse(int socket, const std::string& response) {
  std::string line = response + "\n";
  send(socket, line.c_str(), static_cast<int>(line.size()), 0);
}

std::shared_ptr<APIGamepad> APIServer::GetTeamGamepad(int teamId) {
  std::lock_guard<std::mutex> lock(gamepadMutex);
  if (teamId < 0 || teamId > 1)
    return nullptr;
  return teamGamepads[teamId];
}

void APIServer::SetTeamAction(int teamId, const PlayerAction& action) {
  std::lock_guard<std::mutex> lock(gamepadMutex);
  if (teamId >= 0 && teamId <= 1) {
    teamGamepads[teamId]->SetAction(ToAPIPlayerAction(action));
  }
}

APIServer* GetAPIServer() {
  return g_apiServer;
}

void SetAPIServer(APIServer* server) {
  g_apiServer = server;
}

}  // namespace api
