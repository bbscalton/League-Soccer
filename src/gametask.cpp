#include "gametask.hpp"

#include "base/math/bluntmath.hpp"
#include "blunted.hpp"
#include "data/matchdata.hpp"
#include "framework/scheduler.hpp"
#include "main.hpp"
#include "managers/resourcemanagerpool.hpp"
#include "managers/taskmanager.hpp"

void UploadFullbodyModel::Update() {
  for (unsigned int i = 0; i < geometryToUpload.size(); i++) {
    geometryToUpload.at(i)->OnUpdateGeometryData(false);
  }
}

GameTask::GameTask() {
  match = 0;
  menuScene = 0;

  // prohibits deletion of the scene before this object is dead
  scene3D = GetScene3D();
}

GameTask::~GameTask() {
  if (Verbose())
    printf("exiting gametask.. ");
  Exit();
  if (Verbose())
    printf("done\n");
}

void GameTask::Exit() {
  Action(e_GameTaskMessage_StopMatch);
  Action(e_GameTaskMessage_StopMenuScene);

  ResourceManagerPool::GetInstance().CleanUp();

  scene3D.reset();
}

void GameTask::Action(e_GameTaskMessage message) {
  switch (message) {
    case e_GameTaskMessage_StartMatch: {
      if (Verbose())
        printf("*gametaskmessage: starting match\n");

      GetGraphicsSystem()->getPhaseMutex.lock();
      MatchData* matchData = GetMenuTask()->GetMatchData();
      assert(matchData);
      Match* tmpMatch = new Match(matchData, GetControllers());

      matchLifetimeMutex.lock();
      matchPutBufferMutex.lock();
      assert(!match);
      match = tmpMatch;
      GetScheduler()->ResetTaskSequenceTime("game");
      matchPutBufferMutex.unlock();
      matchLifetimeMutex.unlock();
      GetGraphicsSystem()->getPhaseMutex.unlock();
    } break;

    case e_GameTaskMessage_StopMatch:
      if (Verbose())
        printf("*gametaskmessage: stopping match\n");

      GetGraphicsSystem()->getPhaseMutex.lock();
      matchLifetimeMutex.lock();
      matchPutBufferMutex.lock();
      // assert(match);
      if (match) {
        match->Exit();
        delete match;
        match = 0;
      }
      matchPutBufferMutex.unlock();
      matchLifetimeMutex.unlock();
      GetGraphicsSystem()->getPhaseMutex.unlock();
      break;

    case e_GameTaskMessage_StartMenuScene:
      if (Verbose())
        printf("*gametaskmessage: starting menu scene\n");

      GetGraphicsSystem()->getPhaseMutex.lock();
      menuSceneLifetimeMutex.lock();
      assert(!menuScene);
      menuScene = new MenuScene();
      GetScheduler()->ResetTaskSequenceTime("game");
      menuSceneLifetimeMutex.unlock();
      GetGraphicsSystem()->getPhaseMutex.unlock();
      break;

    case e_GameTaskMessage_StopMenuScene:
      if (Verbose())
        printf("*gametaskmessage: stopping menu scene\n");

      GetGraphicsSystem()->getPhaseMutex.lock();
      menuSceneLifetimeMutex.lock();
      // assert(menuScene);
      if (menuScene) {
        delete menuScene;
        menuScene = 0;
      }
      menuSceneLifetimeMutex.unlock();
      GetGraphicsSystem()->getPhaseMutex.unlock();
      break;

    default:
      break;
  }
}

void GameTask::GetPhase() {
  // process messageQueue
  if (match)
    match->Get();
  if (menuScene)
    menuScene->Get();
}

void GameTask::ProcessPhase() {
  // Check for pending requests - execute on main thread for safety
  ProcessPendingReset();
  ProcessPendingQuit();
  ProcessPendingRematch();

  for (unsigned int i = 0; i < GetControllers().size(); i++) {
    GetControllers().at(i)->Process();
  }

  if (match) {
    match->Process();

    matchPutBufferMutex.lock();
    match->PreparePutBuffers();
    matchPutBufferMutex.unlock();
  }

  if (menuScene) {
    menuScene->Process();
  }
}

bool GameTask::RequestReset(int seed, int home_team_id, int away_team_id,
                            float match_duration_minutes, std::string& error) {
  // Queue the reset request for main thread execution
  {
    std::lock_guard<std::mutex> lock(pendingResetMutex);
    if (pendingReset.pending) {
      error = "Another reset is already pending";
      return false;
    }
    pendingReset.pending = true;
    pendingReset.seed = seed;
    pendingReset.home_team_id = home_team_id;
    pendingReset.away_team_id = away_team_id;
    pendingReset.match_duration_minutes = match_duration_minutes;
    pendingReset.completed = false;
    pendingReset.success = false;
    pendingReset.error.clear();
  }

  // Wait for completion (main thread will process and signal)
  {
    std::unique_lock<std::mutex> lock(pendingReset.completionMutex);
    // Wait up to 10 seconds for the reset to complete
    bool completed = pendingReset.completionCV.wait_for(
        lock, std::chrono::seconds(10),
        [this] { return pendingReset.completed; });

    if (!completed) {
      std::lock_guard<std::mutex> resetLock(pendingResetMutex);
      pendingReset.pending = false;
      error = "Reset timed out waiting for main thread";
      return false;
    }

    error = pendingReset.error;
    return pendingReset.success;
  }
}

void GameTask::ProcessPendingReset() {
  std::lock_guard<std::mutex> lock(pendingResetMutex);
  if (!pendingReset.pending) {
    return;
  }

  printf("[API] Processing pending reset on main thread: seed=%d, home=%d, away=%d\n",
         pendingReset.seed, pendingReset.home_team_id, pendingReset.away_team_id);

  bool success = true;
  std::string error;

  // Set seed if provided
  if (pendingReset.seed >= 0) {
    srand(static_cast<unsigned int>(pendingReset.seed));
    randomseed();
    fastrandomseed();
    printf("[API] Set deterministic seed: %d\n", pendingReset.seed);
  }

  // Set match duration in config
  if (pendingReset.match_duration_minutes > 0) {
    GetConfiguration()->Set("match_duration_minutes", pendingReset.match_duration_minutes);
    printf("[API] Set match duration: %.1f minutes\n", pendingReset.match_duration_minutes);
  }

  // Stop existing match
  if (match) {
    printf("[API] Stopping existing match\n");
    Action(e_GameTaskMessage_StopMatch);
  }

  // Configure teams
  int homeTeam = pendingReset.home_team_id >= 0 ? pendingReset.home_team_id : 3;
  int awayTeam = pendingReset.away_team_id >= 0 ? pendingReset.away_team_id : 8;

  printf("[API] Setting up teams: home=%d, away=%d\n", homeTeam, awayTeam);
  GetMenuTask()->SetTeamIDs(std::to_string(homeTeam), std::to_string(awayTeam));

  // Set up controller sides
  bool directControl = GetConfiguration()->GetBool("api_direct_player_control", false);
  std::vector<SideSelection> sides;
  for (size_t i = 0; i < GetControllers().size(); i++) {
    SideSelection side;
    side.controllerID = static_cast<int>(i);
    if (directControl) {
      if (i == 0)
        side.side = -1;
      else if (i == 1)
        side.side = 1;
      else
        side.side = 0;
    } else {
      side.side = 0;
    }
    sides.push_back(side);
  }
  GetMenuTask()->SetControllerSetup(sides);
  printf("[API] Controller setup: %zu controllers, direct_control=%s\n",
         sides.size(), directControl ? "true" : "false");

  // Create MatchData
  MatchData* matchData = new MatchData(homeTeam, awayTeam);
  if (!matchData) {
    error = "Failed to create match data";
    printf("[API] Reset failed: could not create MatchData\n");
    success = false;
  } else {
    GetMenuTask()->SetMatchData(matchData);

    // Start the match
    printf("[API] Starting match...\n");
    Action(e_GameTaskMessage_StopMenuScene);
    Action(e_GameTaskMessage_StartMatch);

    // Verify match was created
    if (!match) {
      error = "Match failed to start";
      printf("[API] Reset failed: GetMatch() returned null after StartMatch\n");
      success = false;
    } else {
      printf("[API] Match started successfully: %s vs %s\n",
             matchData->GetTeamData(0)->GetName().c_str(),
             matchData->GetTeamData(1)->GetName().c_str());
    }
  }

  // Signal completion
  {
    std::lock_guard<std::mutex> completionLock(pendingReset.completionMutex);
    pendingReset.success = success;
    pendingReset.error = error;
    pendingReset.completed = true;
    pendingReset.pending = false;
  }
  pendingReset.completionCV.notify_all();
}

void GameTask::RequestQuit() {
  // Queue quit for main thread execution - safe during loading/gameplay.
  printf("[API] Quit queued for main thread execution\n");
  pendingQuit.pending.store(true);
}

void GameTask::ProcessPendingQuit() {
  if (!pendingQuit.pending.load()) {
    return;
  }
  
  printf("[API] Processing pending quit on main thread\n");
  pendingQuit.pending.store(false);
  EnvironmentManager::GetInstance().SignalQuit();
}

void GameTask::RequestRematch(int home_team_id, int away_team_id) {
  std::lock_guard<std::mutex> lock(pendingRematchMutex);
  printf("[API] Rematch queued for main thread: home=%d, away=%d\n", home_team_id, away_team_id);
  pendingRematch.home_team_id = home_team_id;
  pendingRematch.away_team_id = away_team_id;
  pendingRematch.pending.store(true);
}

void GameTask::ProcessPendingRematch() {
  if (!pendingRematch.pending.load()) {
    return;
  }

  std::lock_guard<std::mutex> lock(pendingRematchMutex);
  if (!pendingRematch.pending.load()) {
    return;  // Double-check after lock
  }

  printf("[API] Processing pending rematch on main thread: home=%d, away=%d\n",
         pendingRematch.home_team_id, pendingRematch.away_team_id);
  pendingRematch.pending.store(false);

  int homeTeam = pendingRematch.home_team_id;
  int awayTeam = pendingRematch.away_team_id;

  // Stop existing match
  if (match) {
    printf("[API] Stopping existing match for rematch\n");
    Action(e_GameTaskMessage_StopMatch);
  }

  // Stop menu scene (cleans up GUI resources safely)
  Action(e_GameTaskMessage_StopMenuScene);

  // Configure teams
  GetMenuTask()->SetTeamIDs(std::to_string(homeTeam), std::to_string(awayTeam));

  // Set up controller sides for CPU vs CPU
  std::vector<SideSelection> sides;
  for (size_t i = 0; i < GetControllers().size(); i++) {
    SideSelection side;
    side.controllerID = static_cast<int>(i);
    side.side = 0;  // CPU control
    sides.push_back(side);
  }
  GetMenuTask()->SetControllerSetup(sides);

  // Create fresh MatchData
  MatchData* matchData = new MatchData(homeTeam, awayTeam);
  GetMenuTask()->SetMatchData(matchData);

  // Start the new match
  printf("[API] Starting rematch...\n");
  Action(e_GameTaskMessage_StartMatch);

  if (match) {
    printf("[API] Rematch started successfully: %s vs %s\n",
           matchData->GetTeamData(0)->GetName().c_str(),
           matchData->GetTeamData(1)->GetName().c_str());
  } else {
    printf("[API] Rematch failed: match is null after StartMatch\n");
  }
}

void GameTask::PutPhase() {
  std::vector<boost::intrusive_ptr<UpdateFullbodyModel>> updateFullbodyModels;
  std::vector<boost::intrusive_ptr<UploadFullbodyModel>> uploadFullbodyModels;
  std::vector<PlayerBase*> playersToProcess;

  matchLifetimeMutex.lock();

  if (match) {
    matchPutBufferMutex.lock();
    match->FetchPutBuffers();
    matchPutBufferMutex.unlock();

    match->Put();

    std::vector<Player*> players;
    match->GetActiveTeamPlayers(0, players);
    match->GetActiveTeamPlayers(1, players);
    std::vector<PlayerBase*> officials;
    match->GetOfficialPlayers(officials);

    for (unsigned int i = 0; i < players.size(); i++) {
      if (match->GetPause() || players.at(i)->NeedsModelUpdate())
        playersToProcess.push_back(players.at(i));
    }
    for (unsigned int i = 0; i < officials.size(); i++) {
      playersToProcess.push_back(officials.at(i));
    }

    // printf("%i players, %i threads.\n", playersToProcess.size(), threadCount);
    unsigned int playersPerThread = 7;
    unsigned int playerStartIndex = 0;
    while (playerStartIndex < playersToProcess.size()) {
      std::vector<PlayerBase*> playersToProcessInThread;
      for (unsigned int p = 0; p < playersPerThread; p++) {
        if (playerStartIndex + p >= playersToProcess.size())
          break;
        playersToProcessInThread.push_back(playersToProcess.at(playerStartIndex + p));
        // printf("adding player %i\n", playerStartIndex + p);
        //  unthreaded version: playersToProcess.at(playerStartIndex + p)->UpdateFullbodyModel();
      }
      playerStartIndex += playersPerThread;

      boost::intrusive_ptr<UpdateFullbodyModel> updateFullbodyModel(
          new UpdateFullbodyModel(playersToProcessInThread));
      updateFullbodyModels.push_back(updateFullbodyModel);
      TaskManager::GetInstance().EnqueueWork(updateFullbodyModel, true);
    }

    match->UploadGoalNetting();  // won't this block the whole process thing too? (opengl busy ==
                                 // wait, while mutex locked == no process)
  }

  for (unsigned int t = 0; t < updateFullbodyModels.size(); t++) {
    updateFullbodyModels.at(t)->Wait();
  }

  if (match) {
    unsigned int playersPerThread = 7;
    unsigned int playerStartIndex = 0;
    while (playerStartIndex < playersToProcess.size()) {
      std::vector<boost::intrusive_ptr<Geometry>> geometryToUploadInThread;
      for (unsigned int p = 0; p < playersPerThread; p++) {
        if (playerStartIndex + p >= playersToProcess.size())
          break;
        geometryToUploadInThread.push_back(boost::static_pointer_cast<Geometry>(
            playersToProcess.at(playerStartIndex + p)->GetFullbodyNode()->GetObject("fullbody")));
      }
      playerStartIndex += playersPerThread;

      boost::intrusive_ptr<UploadFullbodyModel> uploadFullbodyModel(
          new UploadFullbodyModel(geometryToUploadInThread));
      uploadFullbodyModels.push_back(uploadFullbodyModel);
      TaskManager::GetInstance().EnqueueWork(uploadFullbodyModel, true);

      // working on: maybe we need to use the gfx system get pointer somewhere here? too tired to
      // analyse this now :p
    }

  }  // !match

  matchLifetimeMutex.unlock();

  menuSceneLifetimeMutex.lock();
  if (menuScene)
    menuScene->Put();
  menuSceneLifetimeMutex.unlock();
}
