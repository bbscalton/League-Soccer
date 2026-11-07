#ifndef _HPP_FOOTBALL_GAMETASK
#define _HPP_FOOTBALL_GAMETASK

#include <atomic>
#include <condition_variable>

#include "hid/gamepad.hpp"
#include "hid/keyboard.hpp"
#include "menu/menuscene.hpp"
#include "menu/menutask.hpp"
#include "onthepitch/match.hpp"
#include "types/iusertask.hpp"
#include "types/thread.hpp"

// Pending API reset request - populated by API thread, executed on main thread.
// This avoids race conditions from destroying/creating match resources across threads.
struct PendingResetRequest {
  bool pending = false;
  int seed = -1;
  int home_team_id = -1;
  int away_team_id = -1;
  float match_duration_minutes = 0.0f;
  
  // Completion signaling
  bool completed = false;
  bool success = false;
  std::string error;
  std::condition_variable completionCV;
  std::mutex completionMutex;
};

// Pending API quit request - scheduled on main thread for safe teardown.
struct PendingQuitRequest {
  std::atomic<bool> pending{false};
};

// Pending rematch request - scheduled on main thread for safe match cycling.
// Used by standalone auto-rematch to avoid GUI thread race conditions.
struct PendingRematchRequest {
  std::atomic<bool> pending{false};
  int home_team_id = -1;
  int away_team_id = -1;
};

using namespace blunted;

class UpdateFullbodyModel : public Command {
public:
  UpdateFullbodyModel(const std::vector<PlayerBase*>& playersToProcess)
      : Command("UpdateFullBodyModel"), playersToProcess(playersToProcess){};
  virtual ~UpdateFullbodyModel(){};

protected:
  void Update() {
    for (auto* player : playersToProcess) {
      player->UpdateFullbodyModel();
    }
  }

  virtual bool Execute(void* = nullptr) override {
    Update();
    return true;
  }

  std::vector<PlayerBase*> playersToProcess;
};

class UploadFullbodyModel : public Command {
public:
  UploadFullbodyModel(std::vector<boost::intrusive_ptr<Geometry>> geometryToUpload)
      : Command("UploadFullBodyModel"), geometryToUpload(geometryToUpload){};
  virtual ~UploadFullbodyModel(){};

protected:
  void Update();

  virtual bool Execute(void* = nullptr) {
    Update();
    return true;
  }

  std::vector<boost::intrusive_ptr<Geometry>>
      geometryToUpload;  // use shared pointers to geometry objects instead, to assure lifetime
};

enum e_GameTaskMessage {
  e_GameTaskMessage_StartMatch,
  e_GameTaskMessage_StopMatch,
  e_GameTaskMessage_StartMenuScene,
  e_GameTaskMessage_StopMenuScene,
  e_GameTaskMessage_None
};

class GameTask : public IUserTask {
public:
  GameTask();
  virtual ~GameTask();

  void Exit();

  void Action(e_GameTaskMessage message);

  virtual void GetPhase();
  virtual void ProcessPhase();
  virtual void PutPhase();

  Match* GetMatch() { return match; }
  MenuScene* GetMenuScene() { return menuScene; }

  // Thread-safe reset request from API - queues for main thread execution
  bool RequestReset(int seed, int home_team_id, int away_team_id,
                    float match_duration_minutes, std::string& error);

  // Thread-safe quit request from API - queues for main thread execution
  void RequestQuit();

  // Thread-safe rematch request - queues for main thread execution
  // Used by standalone auto-rematch to avoid GUI thread race conditions.
  void RequestRematch(int home_team_id, int away_team_id);

  MessageQueue<e_GameTaskMessage> messageQueue;

  virtual std::string GetName() const { return "game"; }

  std::mutex matchLifetimeMutex;

protected:
  Match* match;
  MenuScene* menuScene;

  std::timed_mutex matchPutBufferMutex;
  std::mutex menuSceneLifetimeMutex;

  std::shared_ptr<Scene3D> scene3D;

  // Pending reset from API thread - processed in ProcessPhase on main thread
  PendingResetRequest pendingReset;
  std::mutex pendingResetMutex;
  void ProcessPendingReset();

  // Pending quit from API thread - processed in ProcessPhase on main thread
  PendingQuitRequest pendingQuit;
  void ProcessPendingQuit();

  // Pending rematch - processed in ProcessPhase on main thread
  PendingRematchRequest pendingRematch;
  std::mutex pendingRematchMutex;
  void ProcessPendingRematch();
};

#endif
