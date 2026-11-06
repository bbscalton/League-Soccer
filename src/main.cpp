#ifdef WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdio>

#include <windows.h>
// clang-format off
#include <dbghelp.h>
// clang-format on
#pragma comment(lib, "dbghelp.lib")

static LONG WINAPI CustomCrashHandler(EXCEPTION_POINTERS* pExceptionInfo) {
  printf("\n================ CRASH OCCURRED ================\n");
  printf("Exception Code: 0x%08lX\n",
         (unsigned long)pExceptionInfo->ExceptionRecord->ExceptionCode);
  printf("Exception Address: 0x%p\n", pExceptionInfo->ExceptionRecord->ExceptionAddress);

  HANDLE process = GetCurrentProcess();
  HANDLE thread = GetCurrentThread();
  SymInitialize(process, NULL, TRUE);

  CONTEXT context = *pExceptionInfo->ContextRecord;
  STACKFRAME64 stackFrame;
  memset(&stackFrame, 0, sizeof(stackFrame));
#if defined(_M_X64) || defined(__x86_64__)
  DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
  stackFrame.AddrPC.Offset = context.Rip;
  stackFrame.AddrPC.Mode = AddrModeFlat;
  stackFrame.AddrFrame.Offset = context.Rbp;
  stackFrame.AddrFrame.Mode = AddrModeFlat;
  stackFrame.AddrStack.Offset = context.Rsp;
  stackFrame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_IX86)
  DWORD machineType = IMAGE_FILE_MACHINE_I386;
  stackFrame.AddrPC.Offset = context.Eip;
  stackFrame.AddrPC.Mode = AddrModeFlat;
  stackFrame.AddrFrame.Offset = context.Ebp;
  stackFrame.AddrFrame.Mode = AddrModeFlat;
  stackFrame.AddrStack.Offset = context.Esp;
  stackFrame.AddrStack.Mode = AddrModeFlat;
#endif

  printf("Call Stack:\n");
  for (int frame = 0; frame < 30; ++frame) {
    if (!StackWalk64(machineType, process, thread, &stackFrame, &context, NULL,
                     SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
      break;
    }
    DWORD64 address = stackFrame.AddrPC.Offset;
    if (address == 0)
      break;

    char buffer[sizeof(SYMBOL_INFO) + 256 * sizeof(TCHAR)];
    PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 256;

    DWORD64 displacement = 0;
    if (SymFromAddr(process, address, &displacement, symbol)) {
      IMAGEHLP_LINE64 line;
      memset(&line, 0, sizeof(line));
      line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
      DWORD lineDisplacement = 0;
      if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line)) {
        printf("  [%d] %s (%s:%lu)\n", frame, symbol->Name, line.FileName,
               (unsigned long)line.LineNumber);
      } else {
        printf("  [%d] %s + 0x%llX\n", frame, symbol->Name, displacement);
      }
    } else {
      printf("  [%d] 0x%llX\n", frame, address);
    }
  }
  printf("================================================\n");
  fflush(stdout);

  HANDLE hFile = CreateFileA("crashdump.dmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = pExceptionInfo;
    mei.ClientPointers = FALSE;
    MiniDumpWriteDump(process, GetCurrentProcessId(), hFile, MiniDumpNormal, &mei, NULL, NULL);
    CloseHandle(hFile);
    printf("Minidump written to crashdump.dmp\n");
  }
  fflush(stdout);
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#include <cstdlib>
#include <filesystem>
#include <mutex>

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include "SDL2/SDL_ttf.h"
#include "base/log.hpp"
#include "base/math/bluntmath.hpp"
#include "base/utils.hpp"
#include "framework/scheduler.hpp"
#include "hid/scriptedgamepad.hpp"
#include "hid/apigamepad.hpp"
#include "api/api_server.hpp"
#include "api/league_api.hpp"
#include "data/matchdata.hpp"
#include "main.hpp"
#include "managers/resourcemanagerpool.hpp"
#include "managers/scenemanager.hpp"
#include "managers/systemmanager.hpp"
#include "scene/objectfactory.hpp"
#include "scene/scene2d/scene2d.hpp"
#include "scene/scene3d/scene3d.hpp"
#include "systems/audio/audio_system.hpp"
#include "types/thread.hpp"
#include "utils/localization.hpp"
#include "utils/objectloader.hpp"
#include "utils/orbitcamera.hpp"
#include "utils/threadhud.hpp"
#include "utils/userdata.hpp"

#if defined(WIN32) && defined(__MINGW32__)
#undef main
#endif

using namespace blunted;

GraphicsSystem* graphicsSystem;
AudioSystem* audioSystem;

// API server for external programmatic control
static std::unique_ptr<api::APIServer> g_apiServer;

std::shared_ptr<Scene2D> scene2D;
std::shared_ptr<Scene3D> scene3D;

std::shared_ptr<TaskSequence> graphicsSequence;
std::shared_ptr<TaskSequence> gameSequence;

std::shared_ptr<GameTask> gameTask;
std::shared_ptr<MenuTask> menuTask;

boost::intrusive_ptr<Geometry> greenPilon;
boost::intrusive_ptr<Geometry> bluePilon;
boost::intrusive_ptr<Geometry> yellowPilon;
boost::intrusive_ptr<Geometry> redPilon;

boost::intrusive_ptr<Geometry> smallDebugCircle1;
boost::intrusive_ptr<Geometry> smallDebugCircle2;
boost::intrusive_ptr<Geometry> largeDebugCircle;

void SetGreenDebugPilon(const Vector3& pos) {
  greenPilon->SetPosition(pos, false);
}
void SetBlueDebugPilon(const Vector3& pos) {
  bluePilon->SetPosition(pos, false);
}
void SetYellowDebugPilon(const Vector3& pos) {
  yellowPilon->SetPosition(pos, false);
}
void SetRedDebugPilon(const Vector3& pos) {
  redPilon->SetPosition(pos, false);
}

void SetSmallDebugCircle1(const Vector3& pos) {
  smallDebugCircle1->SetPosition(pos, false);
}
void SetSmallDebugCircle2(const Vector3& pos) {
  smallDebugCircle2->SetPosition(pos, false);
}
void SetLargeDebugCircle(const Vector3& pos) {
  largeDebugCircle->SetPosition(pos, false);
}

boost::intrusive_ptr<Geometry> GetGreenDebugPilon() {
  return greenPilon;
}
boost::intrusive_ptr<Geometry> GetBlueDebugPilon() {
  return bluePilon;
}
boost::intrusive_ptr<Geometry> GetYellowDebugPilon() {
  return yellowPilon;
}
boost::intrusive_ptr<Geometry> GetRedDebugPilon() {
  return redPilon;
}

boost::intrusive_ptr<Geometry> GetSmallDebugCircle1() {
  return smallDebugCircle1;
}
boost::intrusive_ptr<Geometry> GetSmallDebugCircle2() {
  return smallDebugCircle2;
}
boost::intrusive_ptr<Geometry> GetLargeDebugCircle() {
  return largeDebugCircle;
}

Database* db;

Properties* config;

boost::intrusive_ptr<Image2D> debugImage;
boost::intrusive_ptr<Image2D> debugOverlay;

std::vector<IHIDevice*> controllers;

bool superDebug = false;
e_DebugMode debugMode = e_DebugMode_Off;

std::string activeSaveDirectory;

std::string configFile = "football.config";
std::string GetConfigFilename() {
  return configFile;
}

namespace {

std::string ResolveConfigFilename(const std::string& requestedFilename) {
  namespace fs = std::filesystem;

  fs::path requestedPath(requestedFilename);
  if (fs::exists(requestedPath)) {
    return requestedPath.generic_string();
  }

  if (requestedPath.is_relative()) {
    fs::path dataRelativePath = fs::path("data") / requestedPath.filename();
    if (fs::exists(dataRelativePath)) {
      return dataRelativePath.generic_string();
    }
  }

  return requestedFilename;
}

}  // namespace

std::shared_ptr<Scene2D> GetScene2D() {
  return scene2D;
}

std::shared_ptr<Scene3D> GetScene3D() {
  return scene3D;
}

GraphicsSystem* GetGraphicsSystem() {
  return graphicsSystem;
}

std::shared_ptr<GameTask> GetGameTask() {
  return gameTask;
}

std::shared_ptr<MenuTask> GetMenuTask() {
  return menuTask;
}

Database* GetDB() {
  return db;
}

bool IsReleaseVersion() {
  if (GetConfiguration()->GetBool("debug", false))
    return false;
  else
    return true;
}

bool Verbose() {
  return !IsReleaseVersion();
}

bool UpdateNonImportableDB() {
  if (IsReleaseVersion())
    return false;
  else
    return true;
}

Properties* GetConfiguration() {
  return config;
}

std::string GetActiveSaveDirectory() {
  return activeSaveDirectory;
}

void SetActiveSaveDirectory(const std::string& dir) {
  activeSaveDirectory = dir;
}

bool SuperDebug() {
  return superDebug;
}

e_DebugMode GetDebugMode() {
  return debugMode;
}

boost::intrusive_ptr<Image2D> GetDebugImage() {
  return debugImage;
}

boost::intrusive_ptr<Image2D> GetDebugOverlay() {
  return debugOverlay;
}

void GetDebugOverlayCoord(Match* match, const Vector3& worldPos, int& x, int& y) {
  Vector3 proj = GetProjectedCoord(worldPos, match->GetCamera());
  int dud1, dud2;
  GetMenuTask()->GetWindowManager()->GetCoordinates(proj.coords[0], proj.coords[1], 1, 1, x, y,
                                                    dud1, dud2);

  int contextW, contextH, bpp;
  GetScene2D()->GetContextSize(contextW, contextH, bpp);
  x = clamp(x, 0, contextW - 1);
  y = clamp(y, 0, contextH - 1);
}

int PredictFrameTimeToGo_ms(int frameCount) {
  int averageFrameTime_ms = GetGraphicsSystem()->GetAverageFrameTime_ms(frameCount);
  int timeSinceLastSwap_ms = GetGraphicsSystem()->GetTimeSinceLastSwap_ms();
  int timeToNextSwapPrediction_ms = averageFrameTime_ms - timeSinceLastSwap_ms;
  // printf("super prediction! %i - %i = %i\n", averageFrameTime_ms, timeSinceLastSwap_ms,
  // timeToNextSwapPrediction_ms);
  timeToNextSwapPrediction_ms = clamp(timeToNextSwapPrediction_ms, 0, 1000);
  return timeToNextSwapPrediction_ms;
}

void InitDebugImage() {
  SDL_Surface* sdlSurface = CreateSDLSurface(200, 150);

  boost::intrusive_ptr<Resource<Surface>> resource =
      ResourceManagerPool::GetInstance()
          .GetManager<Surface>(e_ResourceType_Surface)
          ->Fetch("debugimage", false, true);
  Surface* surface = resource->GetResource();

  surface->SetData(sdlSurface);

  debugImage = boost::static_pointer_cast<Image2D>(
      ObjectFactory::GetInstance().CreateObject("debugimage", e_ObjectType_Image2D));
  scene2D->CreateSystemObjects(debugImage);
  debugImage->SetImage(resource);

  int contextW, contextH, bpp;  // context
  scene2D->GetContextSize(contextW, contextH, bpp);
  debugImage->SetPosition(contextW - 210, contextH - 160);

  scene2D->AddObject(debugImage);

  debugImage->DrawRectangle(0, 0, 200, 150, Vector3(40, 20, 20), 100);
  debugImage->OnChange();
}

void InitDebugOverlay() {
  int contextW, contextH, bpp;  // context
  scene2D->GetContextSize(contextW, contextH, bpp);

  SDL_Surface* sdlSurface = CreateSDLSurface(contextW, contextH);

  boost::intrusive_ptr<Resource<Surface>> resource =
      ResourceManagerPool::GetInstance()
          .GetManager<Surface>(e_ResourceType_Surface)
          ->Fetch("debugoverlay", false, true);
  Surface* surface = resource->GetResource();

  surface->SetData(sdlSurface);

  debugOverlay = boost::static_pointer_cast<Image2D>(
      ObjectFactory::GetInstance().CreateObject("debugoverlay", e_ObjectType_Image2D));
  scene2D->CreateSystemObjects(debugOverlay);
  debugOverlay->SetImage(resource);

  debugOverlay->SetPosition(0, 0);

  scene2D->AddObject(debugOverlay);

  debugOverlay->DrawRectangle(0, 0, contextW, contextH, Vector3(0, 0, 0), 0);
  debugOverlay->OnChange();
}

const std::vector<IHIDevice*>& GetControllers() {
  return controllers;
}

// Menu confirm/back follow the first gamepad's A/B mapping. Axis mappings
// (negative IDs) cannot drive the GUI's raw-button check, so they are ignored.
static void ApplyMenuJoyButtons(HIDGamepad* gamepad) {
  if (!menuTask || !gamepad)
    return;
  const int activate = gamepad->GetControllerMapping(e_ControllerButton_A);
  const int escape = gamepad->GetControllerMapping(e_ControllerButton_B);
  if (activate >= 0 && escape >= 0 && activate != escape)
    menuTask->SetEventJoyButtons(activate, escape);
}

void AddGamepad(int deviceIndex, int gamepadID) {
  if (deviceIndex < 0 || deviceIndex >= SDL_NumJoysticks())
    return;
  HIDGamepad* gamepad = new HIDGamepad(deviceIndex, gamepadID);
  controllers.push_back(gamepad);
  printf("[main] Gamepad added: index %d (slot %d), total controllers: %zu\n", deviceIndex,
         gamepadID, controllers.size());

  // A pad plugged in after startup becomes the menu pad when it is the first one.
  bool firstGamepad = true;
  for (IHIDevice* device : controllers) {
    if (device != gamepad && dynamic_cast<HIDGamepad*>(device)) {
      firstGamepad = false;
      break;
    }
  }
  if (firstGamepad)
    ApplyMenuJoyButtons(gamepad);
}

// Unplugged gamepads are retired rather than deleted: a live match may still
// hold the raw pointer (HumanGamer) on the game thread. A retired pad samples
// slot -1, which reads as no input.
static std::vector<HIDGamepad*> retiredGamepads;

// Filled on the render thread (hot-plug events), drained by the menu thread.
static std::mutex removedControllersMutex;
static std::vector<int> removedControllerIndices;

// Device index of an SDL virtual joystick used by smoke tests, or -1.
static int smokeVirtualGamepadIndex = -1;

bool DetachSmokeVirtualGamepad() {
#if SDL_VERSION_ATLEAST(2, 0, 14)
  if (smokeVirtualGamepadIndex < 0)
    return false;
  const bool detached = SDL_JoystickDetachVirtual(smokeVirtualGamepadIndex) == 0;
  smokeVirtualGamepadIndex = -1;
  return detached;
#else
  return false;
#endif
}

std::vector<int> TakeRemovedControllerIndices() {
  std::lock_guard<std::mutex> lock(removedControllersMutex);
  std::vector<int> removed;
  removed.swap(removedControllerIndices);
  return removed;
}

void RemoveGamepad(int gamepadID) {
  for (auto it = controllers.begin(); it != controllers.end(); ++it) {
    HIDGamepad* gp = dynamic_cast<HIDGamepad*>(*it);
    if (gp && gp->GetGamepadID() == gamepadID) {
      gp->SetGamepadID(-1);
      retiredGamepads.push_back(gp);
      {
        std::lock_guard<std::mutex> lock(removedControllersMutex);
        removedControllerIndices.push_back(static_cast<int>(it - controllers.begin()));
      }
      controllers.erase(it);
      printf("[main] Gamepad removed: slot %d, total controllers: %zu\n", gamepadID,
             controllers.size());
      break;
    }
  }

  // Dense-slot renumbering: UserEventManager packs the remaining joystick slots
  // down on removal (see CompactJoystickSlots), so any gamepad that lived in a
  // higher slot must track its new, one-lower slot.
  for (IHIDevice* device : controllers) {
    HIDGamepad* gp = dynamic_cast<HIDGamepad*>(device);
    if (gp && gp->GetGamepadID() > gamepadID) {
      gp->SetGamepadID(gp->GetGamepadID() - 1);
    }
  }
}

class ThreadHudThread : public Thread {
public:
  ThreadHudThread() { hud = new ThreadHud(GetScene2D()); }
  virtual ~ThreadHudThread() { delete hud; }

  virtual void operator()() override {
    bool quit = false;
    while (!quit) {
      SetState(e_ThreadState_Busy);

      bool isMessage = false;
      boost::intrusive_ptr<Command> message = boost::intrusive_ptr<Command>();
      message = messageQueue.GetMessage(isMessage);
      if (isMessage) {
        if (!message->Handle(this))
          quit = true;
        message.reset();
      }

      hud->Execute();

      SetState(e_ThreadState_Idle);

      std::this_thread::yield();
    }
  }

protected:
  ThreadHud* hud;
};

int main(int argc, const char** argv) {
#ifdef WIN32
  SetUnhandledExceptionFilter(CustomCrashHandler);
#endif
  if (argc > 0) {
    try {
      std::filesystem::path exeDir =
          std::filesystem::absolute(std::filesystem::path(argv[0])).parent_path();
      if (!exeDir.empty()) {
        std::filesystem::current_path(exeDir);
      }
#ifdef __APPLE__
      // Inside "Gameplay Football.app" the assets are in Contents/Resources,
      // which may be read-only; run from a per-user directory instead.
      const std::filesystem::path resources = exeDir.parent_path() / "Resources";
      const char* home = getenv("HOME");
      if (home && std::filesystem::exists(resources / "football.config")) {
        const std::filesystem::path userDirectory =
            std::filesystem::path(home) / "Library" / "Application Support" / "Gameplay Football";
        std::string error;
        if (PrepareUserDataDirectory(resources, userDirectory, &error)) {
          std::filesystem::current_path(userDirectory);
        } else {
          fprintf(stderr, "Cannot use %s (%s); settings and saves will not be kept\n",
                  userDirectory.c_str(), error.c_str());
          std::filesystem::current_path(resources);
        }
      }
#endif
    } catch (...) {
    }
  }
  config = new Properties();
  if (argc > 1)
    configFile = argv[1];
  configFile = ResolveConfigFilename(configFile);
  config->LoadFile(configFile.c_str());

  // Initialize localization using saved language preference (default: "en")
  Localization::GetInstance().Load(config->Get("locale_language", "en"));

  Initialize(*config);

  // Check for API mode seed override
  int64_t apiSeed = config->GetInt("api_seed", -1);
  if (apiSeed >= 0) {
    srand(static_cast<unsigned int>(apiSeed));
    printf("[API] Using deterministic seed: %lld\n", (long long)apiSeed);
  } else {
    srand(time(nullptr));
  }
  rand();        // mingw32? buggy compiler? first value seems bogus
  randomseed();  // for the boost random
  fastrandomseed();

  int timeStep_ms = config->GetInt("physics_frametime_ms", 10);

  // Initialize API server if enabled
  if (config->GetBool("api_mode", false)) {
    int apiPort = config->GetInt("api_port", api::kDefaultPort);
    g_apiServer = std::make_unique<api::APIServer>();
    
    // Set up league data callbacks
    g_apiServer->SetListLeaguesCallback([]() { return api::ListLeagues(); });
    g_apiServer->SetListFixturesCallback([](int league_id) { return api::ListFixtures(league_id); });
    g_apiServer->SetCreateFixtureCallback([](const std::string& json, std::string& error) {
      int fixture_id;
      return api::CreateFixture(json, fixture_id, error);
    });
    g_apiServer->SetExportLeagueDataCallback([]() { return api::ExportLeagueData(); });
    
    if (g_apiServer->Start(apiPort)) {
      api::SetAPIServer(g_apiServer.get());
      printf("[API] External control API server started on port %d\n", apiPort);
    } else {
      printf("[API] Failed to start API server on port %d\n", apiPort);
      g_apiServer.reset();
    }
  }

  // database

  db = new Database();
  bool dbSuccess = db->Load("databases/default/database.sqlite");
  if (!dbSuccess)
    Log(e_FatalError, "main", "()", "Could not open database");

  // initialize systems

  SystemManager* systemManager = SystemManager::GetInstancePtr();

  graphicsSystem = new GraphicsSystem();
  bool returnvalue = systemManager->RegisterSystem("GraphicsSystem", graphicsSystem);
  if (!returnvalue)
    Log(e_FatalError, "football", "main", "Could not register GraphicsSystem");

  audioSystem = new AudioSystem();
  returnvalue = systemManager->RegisterSystem("AudioSystem", audioSystem);
  if (!returnvalue)
    Log(e_FatalError, "football", "main", "Could not register AudioSystem");

  printf("[MAIN] graphicsSystem->Initialize\n");
  fflush(stdout);
  graphicsSystem->Initialize(*config);
  printf("[MAIN] audioSystem->Initialize\n");
  fflush(stdout);
  audioSystem->Initialize(*config);

  // init scenes
  printf("[MAIN] init scenes\n");
  fflush(stdout);

  scene2D = std::shared_ptr<Scene2D>(new Scene2D("scene2D", *config));
  SceneManager::GetInstance().RegisterScene(scene2D);

  scene3D = std::shared_ptr<Scene3D>(new Scene3D("scene3D"));
  SceneManager::GetInstance().RegisterScene(scene3D);

  if (SuperDebug())
    InitDebugImage();
  if (GetDebugMode() == e_DebugMode_AI)
    InitDebugOverlay();

  ThreadHudThread* threadHudThread = nullptr;
  if (!IsReleaseVersion() && 1 == 2) {
    threadHudThread = new ThreadHudThread();
    threadHudThread->Run();
  } else {
    threadHudThread = nullptr;
  }

  // debug pilons
  printf("[MAIN] loading pilons\n");
  fflush(stdout);

  boost::intrusive_ptr<Resource<GeometryData>> geometry =
      ResourceManagerPool::GetInstance()
          .GetManager<GeometryData>(e_ResourceType_GeometryData)
          ->Fetch("media/objects/helpers/green.ase", true);
  greenPilon = boost::static_pointer_cast<Geometry>(
      ObjectFactory::GetInstance().CreateObject("greenPilon", e_ObjectType_Geometry));
  scene3D->CreateSystemObjects(greenPilon);
  greenPilon->SetGeometryData(geometry);
  greenPilon->SetLocalMode(e_LocalMode_Absolute);
  greenPilon->SetPosition(Vector3(0, 0, -10));

  geometry = ResourceManagerPool::GetInstance()
                 .GetManager<GeometryData>(e_ResourceType_GeometryData)
                 ->Fetch("media/objects/helpers/blue.ase", true);
  bluePilon = boost::static_pointer_cast<Geometry>(
      ObjectFactory::GetInstance().CreateObject("bluePilon", e_ObjectType_Geometry));
  scene3D->CreateSystemObjects(bluePilon);
  bluePilon->SetGeometryData(geometry);
  bluePilon->SetLocalMode(e_LocalMode_Absolute);
  bluePilon->SetPosition(Vector3(0, 0, -10));

  geometry = ResourceManagerPool::GetInstance()
                 .GetManager<GeometryData>(e_ResourceType_GeometryData)
                 ->Fetch("media/objects/helpers/yellow.ase", true);
  yellowPilon = boost::static_pointer_cast<Geometry>(
      ObjectFactory::GetInstance().CreateObject("yellowPilon", e_ObjectType_Geometry));
  scene3D->CreateSystemObjects(yellowPilon);
  yellowPilon->SetGeometryData(geometry);
  yellowPilon->SetLocalMode(e_LocalMode_Absolute);
  yellowPilon->SetPosition(Vector3(0, 0, -10));

  geometry = ResourceManagerPool::GetInstance()
                 .GetManager<GeometryData>(e_ResourceType_GeometryData)
                 ->Fetch("media/objects/helpers/red.ase", true);
  redPilon = boost::static_pointer_cast<Geometry>(
      ObjectFactory::GetInstance().CreateObject("redPilon", e_ObjectType_Geometry));
  scene3D->CreateSystemObjects(redPilon);
  redPilon->SetGeometryData(geometry);
  redPilon->SetLocalMode(e_LocalMode_Absolute);
  redPilon->SetPosition(Vector3(0, 0, -10));

  geometry = ResourceManagerPool::GetInstance()
                 .GetManager<GeometryData>(e_ResourceType_GeometryData)
                 ->Fetch("media/objects/helpers/smalldebugcircle.ase", true);
  smallDebugCircle1 = boost::static_pointer_cast<Geometry>(
      ObjectFactory::GetInstance().CreateObject("smallDebugCircle1", e_ObjectType_Geometry));
  scene3D->CreateSystemObjects(smallDebugCircle1);
  smallDebugCircle1->SetGeometryData(geometry);
  smallDebugCircle1->SetLocalMode(e_LocalMode_Absolute);
  smallDebugCircle1->SetPosition(Vector3(0, 0, -10));

  geometry = ResourceManagerPool::GetInstance()
                 .GetManager<GeometryData>(e_ResourceType_GeometryData)
                 ->Fetch("media/objects/helpers/smalldebugcircle.ase", true);
  smallDebugCircle2 = boost::static_pointer_cast<Geometry>(
      ObjectFactory::GetInstance().CreateObject("smallDebugCircle2", e_ObjectType_Geometry));
  scene3D->CreateSystemObjects(smallDebugCircle2);
  smallDebugCircle2->SetGeometryData(geometry);
  smallDebugCircle2->SetLocalMode(e_LocalMode_Absolute);
  smallDebugCircle2->SetPosition(Vector3(0, 0, -10));

  geometry = ResourceManagerPool::GetInstance()
                 .GetManager<GeometryData>(e_ResourceType_GeometryData)
                 ->Fetch("media/objects/helpers/largedebugcircle.ase", true);
  largeDebugCircle = boost::static_pointer_cast<Geometry>(
      ObjectFactory::GetInstance().CreateObject("largeDebugCircle", e_ObjectType_Geometry));
  scene3D->CreateSystemObjects(largeDebugCircle);
  largeDebugCircle->SetGeometryData(geometry);
  largeDebugCircle->SetLocalMode(e_LocalMode_Absolute);
  largeDebugCircle->SetPosition(Vector3(0, 0, -10));

  geometry.reset();

  // controllers
  printf("[MAIN] init controllers\n");
  fflush(stdout);

  // The keyboard is always controller 0. Gamepads present at launch were
  // already added by UserEventManager, so insert in front of them; settings
  // pages and the menu gamepad lookup rely on this order.
  HIDKeyboard* keyboard = new HIDKeyboard();
  controllers.insert(controllers.begin(), keyboard);

  // Headless "gamepad controls" smoke match: inject an autonomous scripted
  // controller that drives a human team through the real input pipeline.
  if (config->GetBool("menu_smoke_test_gamepad_match", false)) {
    controllers.push_back(new ScriptedGamepad());
    printf("[main] Scripted gamepad controller injected (%zu total controllers)\n",
           controllers.size());
  }

  // API mode: add API gamepads for external control of both teams
  if (g_apiServer) {
    auto homeGamepad = g_apiServer->GetTeamGamepad(0);
    auto awayGamepad = g_apiServer->GetTeamGamepad(1);
    if (homeGamepad) {
      controllers.push_back(homeGamepad.get());
      printf("[API] Home team API gamepad added\n");
    }
    if (awayGamepad) {
      controllers.push_back(awayGamepad.get());
      printf("[API] Away team API gamepad added\n");
    }
  }

  // sequences

  std::mutex
      graphicsGameMutex;  // todo: this mutex seems necessary for visual fluency, doesn't this imply
                          // that i'm setting positional stuff during something else than gametask
                          // put? (or reading during something else than graphics get)

  printf("[MAIN] creating GameTask\n");
  fflush(stdout);
  gameTask = std::shared_ptr<GameTask>(new GameTask());

  std::string fontfilename =
      config->Get("font_filename", "media/fonts/alegreya/AlegreyaSansSC-ExtraBold.ttf");
  printf("[MAIN] loading font: %s\n", fontfilename.c_str());
  fflush(stdout);
  TTF_Font* defaultFont = TTF_OpenFont(fontfilename.c_str(), 32);
  if (!defaultFont)
    Log(e_FatalError, "football", "main", "Could not load font " + fontfilename);
  TTF_Font* defaultOutlineFont = TTF_OpenFont(fontfilename.c_str(), 32);
  TTF_SetFontOutline(defaultOutlineFont, 2);
  printf("[MAIN] creating MenuTask\n");
  fflush(stdout);
  menuTask =
      std::shared_ptr<MenuTask>(new MenuTask(kMenuAspectRatio, 0, defaultFont, defaultOutlineFont));
  if (controllers.size() > 1)
    ApplyMenuJoyButtons(dynamic_cast<HIDGamepad*>(controllers.at(1)));

  // Set up API callbacks now that game/menu tasks exist
  if (g_apiServer) {
    // Reset callback: set up match data and start a match
    g_apiServer->SetResetCallback([](const api::ResetParams& params, std::string& error) -> bool {
      printf("[API] Reset requested: seed=%d, duration=%.1f min\n", 
             params.seed, params.match_duration_minutes);
      
      // Set seed if provided
      if (params.seed >= 0) {
        srand(static_cast<unsigned int>(params.seed));
        randomseed();
        fastrandomseed();
        printf("[API] Set deterministic seed: %d\n", params.seed);
      }
      
      // Set match duration in config (match reads it from there)
      if (params.match_duration_minutes > 0) {
        GetConfiguration()->Set("match_duration_minutes", params.match_duration_minutes);
      }
      
      // Stop any existing match
      if (gameTask && gameTask->GetMatch()) {
        gameTask->Action(e_GameTaskMessage_StopMatch);
      }
      
      // Configure teams (use defaults if not specified)
      int homeTeam = params.home_team_id >= 0 ? params.home_team_id : 3;  // Barcelona
      int awayTeam = params.away_team_id >= 0 ? params.away_team_id : 8;  // Real Madrid
      
      menuTask->SetTeamIDs(std::to_string(homeTeam), std::to_string(awayTeam));
      
      // Set up controller sides - API controls both teams
      std::vector<SideSelection> sides;
      for (size_t i = 0; i < controllers.size(); i++) {
        SideSelection side;
        side.controllerID = static_cast<int>(i);
        // First two controllers (API gamepads) control home and away
        if (i == 0) side.side = -1;  // Home team
        else if (i == 1) side.side = 1;  // Away team
        else side.side = 0;  // AI
        sides.push_back(side);
      }
      menuTask->SetControllerSetup(sides);
      
      // Create MatchData
      MatchData* matchData = new MatchData(homeTeam, awayTeam);
      menuTask->SetMatchData(matchData);
      
      // Start the match
      gameTask->Action(e_GameTaskMessage_StopMenuScene);
      gameTask->Action(e_GameTaskMessage_StartMatch);
      
      printf("[API] Match started: %s vs %s\n",
             matchData->GetTeamData(0)->GetName().c_str(),
             matchData->GetTeamData(1)->GetName().c_str());
      
      return true;
    });
    
    // Step callback: advance simulation (match processes each frame automatically)
    g_apiServer->SetStepCallback([](api::StepResult& result) -> bool {
      if (!gameTask || !gameTask->GetMatch()) {
        return false;
      }
      
      Match* match = gameTask->GetMatch();
      e_MatchPhase phase = match->GetMatchPhase();
      // Match is done after penalties or if we're past 2nd half (for non-extra-time matches)
      result.done = (phase == e_MatchPhase_Penalties && match->GetMatchTime_ms() > 0) ||
                    (phase > e_MatchPhase_2ndHalf && phase != e_MatchPhase_1stExtraTime && 
                     phase != e_MatchPhase_2ndExtraTime);
      result.observation.score_home = match->GetScore(0);
      result.observation.score_away = match->GetScore(1);
      result.observation.match_time_ms = match->GetMatchTime_ms();
      result.observation.in_play = match->IsInPlay();
      result.observation.period = static_cast<int>(phase);
      
      return true;
    });
    
    // Observe callback: get current match state
    g_apiServer->SetGetObservationCallback([](api::Observation& obs) -> bool {
      if (!gameTask || !gameTask->GetMatch()) {
        return false;
      }
      
      Match* match = gameTask->GetMatch();
      Ball* ball = match->GetBall();
      
      // Ball state
      obs.ball.position_x = ball->GetBallGeom()->GetPosition().coords[0];
      obs.ball.position_y = ball->GetBallGeom()->GetPosition().coords[1];
      obs.ball.position_z = ball->GetBallGeom()->GetPosition().coords[2];
      obs.ball.velocity_x = ball->GetMovement().coords[0];
      obs.ball.velocity_y = ball->GetMovement().coords[1];
      obs.ball.velocity_z = ball->GetMovement().coords[2];
      obs.ball.owned_team = match->GetLastTouchTeamID();
      
      // Game state
      obs.score_home = match->GetScore(0);
      obs.score_away = match->GetScore(1);
      obs.match_time_ms = match->GetMatchTime_ms();
      obs.in_play = match->IsInPlay();
      obs.in_set_piece = match->IsInSetPiece();
      obs.goal_scored = match->IsGoalScored();
      obs.last_touch_team = match->GetLastTouchTeamID();
      obs.period = static_cast<int>(match->GetMatchPhase());
      
      // Player observations for home team
      std::vector<Player*> homePlayers;
      match->GetActiveTeamPlayers(0, homePlayers);
      for (Player* player : homePlayers) {
        api::PlayerObservation pobs;
        pobs.team_id = 0;
        pobs.player_id = player->GetID();
        Vector3 pos = player->GetPosition();
        pobs.position_x = pos.coords[0];
        pobs.position_y = pos.coords[1];
        Vector3 dir = player->GetDirectionVec();
        pobs.direction = atan2(dir.coords[1], dir.coords[0]);
        pobs.has_ball = (player == match->GetDesignatedPossessionPlayer());
        pobs.is_active = player->IsActive();
        obs.home_players.push_back(pobs);
      }
      
      // Player observations for away team
      std::vector<Player*> awayPlayers;
      match->GetActiveTeamPlayers(1, awayPlayers);
      for (Player* player : awayPlayers) {
        api::PlayerObservation pobs;
        pobs.team_id = 1;
        pobs.player_id = player->GetID();
        Vector3 pos = player->GetPosition();
        pobs.position_x = pos.coords[0];
        pobs.position_y = pos.coords[1];
        Vector3 dir = player->GetDirectionVec();
        pobs.direction = atan2(dir.coords[1], dir.coords[0]);
        pobs.has_ball = (player == match->GetDesignatedPossessionPlayer());
        pobs.is_active = player->IsActive();
        obs.away_players.push_back(pobs);
      }
      
      return true;
    });
    
    // Quit callback
    g_apiServer->SetQuitCallback([]() {
      printf("[API] Quit requested\n");
      EnvironmentManager::GetInstance().SignalQuit();
    });
    
    printf("[API] Match control callbacks registered\n");
  }

  // Smoke tests: plug in an SDL virtual gamepad so gamepad menus and hot-plug
  // handling run against a real HIDGamepad without hardware. It arrives
  // through the normal device-added event, like a pad connected at runtime.
  if (config->GetBool("menu_smoke_virtual_gamepad", false)) {
#if SDL_VERSION_ATLEAST(2, 0, 14)
    smokeVirtualGamepadIndex =
        SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 1);
    printf("[menu-smoke] Virtual gamepad attached: %s\n",
           smokeVirtualGamepadIndex >= 0 ? "yes" : SDL_GetError());
#else
    printf("[menu-smoke] Virtual gamepad attached: unsupported SDL version\n");
#endif
  }

  gameSequence = std::shared_ptr<TaskSequence>(new TaskSequence("game", timeStep_ms, false));

  gameSequence->AddUserTaskEntry(menuTask, e_TaskPhase_Get);
  gameSequence->AddUserTaskEntry(menuTask, e_TaskPhase_Process);
  gameSequence->AddUserTaskEntry(menuTask, e_TaskPhase_Put);

  gameSequence->AddUserTaskEntry(gameTask, e_TaskPhase_Get);
  gameSequence->AddUserTaskEntry(gameTask, e_TaskPhase_Process);

  GetScheduler()->RegisterTaskSequence(gameSequence);

  graphicsSequence = std::shared_ptr<TaskSequence>(
      new TaskSequence("graphics", config->GetInt("graphics3d_frametime_ms", 0), true));

  graphicsSequence->AddUserTaskEntry(gameTask, e_TaskPhase_Put);
  graphicsSequence->AddSystemTaskEntry(graphicsSystem, e_TaskPhase_Get);
  graphicsSequence->AddSystemTaskEntry(graphicsSystem, e_TaskPhase_Process);
  graphicsSequence->AddSystemTaskEntry(graphicsSystem, e_TaskPhase_Put);

  GetScheduler()->RegisterTaskSequence(graphicsSequence);

  // fire!
  printf("[MAIN] calling Run()\n");
  fflush(stdout);

  Run();

  // exit

  if (SuperDebug())
    scene2D->DeleteObject(debugImage);
  if (GetDebugMode() == e_DebugMode_AI)
    scene2D->DeleteObject(debugOverlay);

  gameTask.reset();
  menuTask.reset();

  gameSequence.reset();
  graphicsSequence.reset();

  greenPilon->Exit();
  greenPilon.reset();
  bluePilon->Exit();
  bluePilon.reset();
  yellowPilon->Exit();
  yellowPilon.reset();
  redPilon->Exit();
  redPilon.reset();
  smallDebugCircle1->Exit();
  smallDebugCircle1.reset();
  smallDebugCircle2->Exit();
  smallDebugCircle2.reset();
  largeDebugCircle->Exit();
  largeDebugCircle.reset();

  if (threadHudThread) {
    boost::intrusive_ptr<Message_Shutdown> shutdownMessage = new Message_Shutdown();
    threadHudThread->messageQueue.PushMessage(shutdownMessage);
    threadHudThread->Join();
    delete threadHudThread;
    shutdownMessage.reset();
  }

  scene2D.reset();
  scene3D.reset();

  for (auto* controller : controllers) {
    delete controller;
  }
  controllers.clear();

  TTF_CloseFont(defaultFont);         // todo: better timed closefont?
  TTF_CloseFont(defaultOutlineFont);  // todo: better timed closefont?

  delete db;
  delete config;

  // Shutdown API server
  if (g_apiServer) {
    g_apiServer->Stop();
    api::SetAPIServer(nullptr);
    g_apiServer.reset();
  }

  Exit();

  return 0;
}
