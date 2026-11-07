#include "menutask.hpp"

#include <algorithm>

#include "../main.hpp"
#include "../onthepitch/match.hpp"
#include "career/career_database.hpp"
#include "career/career_navigation.hpp"
#include "data/careerdata.hpp"
#include "framework/scheduler.hpp"
#include "gametask.hpp"
#include "hid/controllersetup.hpp"
#include "ingame/gameover.hpp"
#include "ingame/ingame.hpp"
#include "ingame/phasemenu.hpp"
#include "ingame/replaymenu.hpp"
#include "main.hpp"
#include "mainmenu.hpp"
#include "managers/resourcemanagerpool.hpp"
#include "pagefactory.hpp"
#include "visualoptions.hpp"

using namespace blunted;

void SetActiveController(int side, bool keyboard) {
  bool keyboardActive = true;
  const std::vector<SideSelection> sides = GetMenuTask()->GetControllerSetup();
  const std::vector<IHIDevice*>& controllers = GetControllers();
  int menuControllerID = -1;
  for (unsigned int i = 0; i < sides.size(); i++) {
    if (sides.at(i).side == side) {
      int controllerID = sides.at(i).controllerID;
      if (controllerID >= 0 && controllerID < static_cast<int>(controllers.size()) &&
          controllers.at(controllerID)->GetDeviceType() == e_HIDeviceType_Gamepad) {
        menuControllerID = static_cast<HIDGamepad*>(controllers.at(controllerID))->GetGamepadID();
        keyboardActive = false;
      }
      break;
    }
    if (i == sides.size() - 1)
      menuControllerID = 0;  // AI opponent, so allow choosing their team with controller
  }

  GetMenuTask()->SetActiveJoystickID(menuControllerID);
  if (keyboard) {
    if (keyboardActive) {
      GetMenuTask()->EnableKeyboard();
    } else {
      GetMenuTask()->DisableKeyboard();
    }
  } else {
    GetMenuTask()->EnableKeyboard();
  }
}

MenuTask::MenuTask(float aspectRatio, float margin, TTF_Font* defaultFont,
                   TTF_Font* defaultOutlineFont)
    : Gui2Task(GetScene2D(), aspectRatio, margin), menuBackground(nullptr) {
  Gui2Style* style = windowManager->GetStyle();

  windowManager->onSoundClick = [this]() {
    if (GetGameTask() && GetGameTask()->GetMenuScene()) {
      GetGameTask()->GetMenuScene()->PlayClickSound();
    }
  };
  windowManager->onSoundHover = [this]() {
    if (GetGameTask() && GetGameTask()->GetMenuScene()) {
      GetGameTask()->GetMenuScene()->PlayHoverSound();
    }
  };

  style->SetFont(e_TextType_Default, defaultFont);
  style->SetFont(e_TextType_DefaultOutline, defaultOutlineFont);
  style->SetFont(e_TextType_Caption, defaultFont);
  style->SetFont(e_TextType_Title, defaultFont);
  style->SetFont(e_TextType_ToolTip, defaultFont);

  // Classic PES 5/6 Theme: Deep Navy, Silver, and Broadcast Gold
  style->SetColor(e_DecorationType_Dark1, Vector3(12, 22, 45));  // Deep navy blue (backgrounds)
  style->SetColor(e_DecorationType_Dark2,
                  Vector3(45, 55, 80));  // Cool silver-blue (borders/inactive)
  style->SetColor(e_DecorationType_Bright1, Vector3(255, 255, 255));  // Pure crisp white (text)
  style->SetColor(e_DecorationType_Bright2,
                  Vector3(255, 215, 0));  // Classic PES broadcast gold (hover/focus)
  style->SetColor(e_DecorationType_Toggled, Vector3(210, 40, 40));  // PES Red (active/toggled)

  windowManager->SetTimeStep_ms(10);

  Gui2Root* root = windowManager->GetRoot();
  root->Show();

  menuBackground = new Gui2Image(windowManager, "image_menu_background", 0, 0, 100, 100);
  menuBackground->LoadImage("media/menu/backgrounds/stadium01.png");
  root->AddView(menuBackground);
  menuBackground->Show();

  PageFactory* pageFactory = new PageFactory();
  windowManager->SetPageFactory(pageFactory);

  if (IsAPIMode()) {
    // API mode: CPU vs CPU by default (api_direct_player_control=false).
    // When api_direct_player_control=true, API gamepads control home/away teams
    // and the 'action' command drives player input directly.
    bool directControl = GetConfiguration()->GetBool("api_direct_player_control", false);
    if (directControl) {
      printf("[API] API mode with DIRECT player control (api_direct_player_control=true)\n");
    } else {
      printf("[API] API mode: CPU vs CPU autoplay (api_direct_player_control=false)\n");
    }

    int size = GetControllers().size();
    for (int i = 0; i < size; i++) {
      SideSelection side;
      side.controllerID = i;
      if (directControl) {
        // Direct control: API gamepads control home (side=-1) and away (side=1)
        if (i == 0)
          side.side = -1;  // Home team controlled by first API gamepad
        else if (i == 1)
          side.side = 1;   // Away team controlled by second API gamepad
        else
          side.side = 0;   // AI for any extra controllers
      } else {
        // CPU autoplay: both teams controlled by AI (side=0)
        side.side = 0;
      }
      queuedFixture->sides.push_back(side);
    }

    // Default teams for API mode
    queuedFixture->teamID1 = "3";  // Barcelona
    queuedFixture->teamID2 = "8";  // Real Madrid
    queuedFixture->team1KitNum = 1;
    queuedFixture->team2KitNum = 2;

    menuAction = e_MenuAction_Menu;

  } else if (!QuickStart()) {
    queuedFixture->team1KitNum = 1;
    queuedFixture->team2KitNum = 2;

    menuAction = e_MenuAction_Menu;

  } else {
    int size = GetControllers().size();
    for (int i = 0; i < size; i++) {
      SideSelection side;
      side.controllerID = i;
      if ((size > 1 && i == 1) || (size == 1 && i == 0)) {
        side.side = -1;
      } else {
        side.side = 0;
      }
      queuedFixture->sides.push_back(side);
    }

    // 1 == ajax
    // 2 == arsenal
    // 3 == barcelona
    // 4 == bayern
    // 5 == borussia
    // 6 == man utd
    // 7 == psv
    // 8 == real madrid
    queuedFixture->teamID1 = "3";
    queuedFixture->teamID2 = "8";
    queuedFixture->team1KitNum = 2;
    queuedFixture->team2KitNum = 2;

    menuAction = e_MenuAction_Menu;
  }
}

MenuTask::~MenuTask() {
  if (Verbose())
    printf("exiting menutask.. ");

  delete windowManager->GetPageFactory();

  if (Verbose())
    printf("done\n");
}

void MenuTask::HandleRemovedControllers() {
  const std::vector<int> removed = TakeRemovedControllerIndices();
  if (removed.empty())
    return;
  std::vector<SideSelection> sides = GetControllerSetup();
  for (int index : removed) {
    const int side = controllersetup::RemoveControllerFromSetup(sides, index);
    if (side != 0 && GetGameTask()->GetMatch())
      disconnectedSide = side;
  }
  SetControllerSetup(sides);
  controllerSetupChanged = true;
}

void MenuTask::ProcessPhase() {
  if (quitAt_ms != 0 && EnvironmentManager::GetInstance().GetTime_ms() >= quitAt_ms) {
    quitAt_ms = 0;
    QuitGame();
  }
  HandleRemovedControllers();
  Gui2Task::ProcessPhase();

  if (menuAction != e_MenuAction_None) {
    printf("[MENU] ProcessPhase: menuAction=%d, IsAPIMode=%d\n", static_cast<int>(menuAction),
           IsAPIMode() ? 1 : 0);
  }

  if (menuAction == e_MenuAction_Menu) {
    windowManager->GetPagePath()->Clear();

    GetGameTask()->Action(e_GameTaskMessage_StopMatch);
    GetGameTask()->Action(e_GameTaskMessage_StopMenuScene);

    menuBackground->Show();
    Properties properties;
    if (GetConfiguration()->GetBool("league_resume_hub", false)) {
      GetConfiguration()->SetBool("league_resume_hub", false);
      windowManager->GetPageFactory()->CreatePage((int)e_PageID_League_Matchday, properties, 0);
    } else if (GetConfiguration()->GetBool("career_resume_hub", false)) {
      GetConfiguration()->SetBool("career_resume_hub", false);
      CareerSave* save = CareerDatabase::GetInstance().GetActiveSave();
      const int hubPage = CareerHubPageID(save);
      windowManager->GetPageFactory()->CreatePage(hubPage, properties, 0);
    } else if (IsAPIMode()) {
      // API mode: skip intro/menus completely, go straight to loading the match
      printf("[API] API mode: skipping intro, loading match immediately\n");
      printf("[API] Creating LoadingMatch page...\n");
      fflush(stdout);
      windowManager->GetPageFactory()->CreatePage((int)e_PageID_LoadingMatch, properties, 0);
      printf("[API] LoadingMatch page created\n");
      fflush(stdout);
    } else if (!QuickStart()) {
      if (!IsReleaseVersion()) {
        windowManager->GetPageFactory()->CreatePage((int)e_PageID_MainMenu, properties, 0);
      } else {
        windowManager->GetPageFactory()->CreatePage((int)e_PageID_Intro, properties, 0);
      }
    } else {
      windowManager->GetPageFactory()->CreatePage((int)e_PageID_LoadingMatch, properties, 0);
    }

  } else if (menuAction == e_MenuAction_Game) {
    printf("[MENU] menuAction == e_MenuAction_Game: starting match\n");
    fflush(stdout);
    menuBackground->Hide();
    GetGameTask()->Action(e_GameTaskMessage_StopMenuScene);
    GetGameTask()->Action(e_GameTaskMessage_StartMatch);
    printf("[MENU] Match started\n");
    fflush(stdout);
  }

  menuAction = e_MenuAction_None;
}

bool MenuTask::QuickStart() {
  // Keep the normal main-menu flow available in debug builds unless quick-start is explicitly
  // enabled in the config for local iteration.
  return !IsReleaseVersion() && GetConfiguration()->GetBool("quick_start", false) &&
         EnvironmentManager::GetInstance().GetTime_ms() <
             10000;  // after 10 seconds, quickstart disabled
}

void MenuTask::QuitGame() {
  EnvironmentManager::GetInstance().SignalQuit();
}

void MenuTask::QuitGameAfter(unsigned long delay_ms) {
  quitAt_ms = EnvironmentManager::GetInstance().GetTime_ms() + std::max(1UL, delay_ms);
}

void MenuTask::ReleaseAllButtons() {
  // when going back to game, depress all buttons, so we don't go around doing passes we don't want
  for (int joyID = 0; joyID < UserEventManager::GetInstance().GetJoystickCount(); joyID++) {
    for (unsigned int buttonID = 0; buttonID < blunted::_JOYSTICK_MAXBUTTONS; buttonID++) {
      UserEventManager::GetInstance().SetJoyButtonState(joyID, buttonID, false);
    }
  }
  UserEventManager::GetInstance().SetKeyboardState(SDLK_ESCAPE, false);
}
