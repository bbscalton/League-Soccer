// APIGamepad implementation
// Apache License 2.0 - see LICENSE

#include "apigamepad.hpp"

#include <cstring>

APIGamepad::APIGamepad(int teamId) : teamId(teamId) {
  deviceType = e_HIDeviceType_Scripted;
  identifier = "api_gamepad_team" + std::to_string(teamId);
  displayName = "API Controller (Team " + std::to_string(teamId) + ")";

  memset(buttonStates, 0, sizeof(buttonStates));
  memset(previousButtonStates, 0, sizeof(previousButtonStates));
}

APIGamepad::~APIGamepad() {}

void APIGamepad::LoadConfig() {}

void APIGamepad::SaveConfig() {}

void APIGamepad::Process() {
  std::lock_guard<std::mutex> lock(actionMutex);

  // Copy current to previous
  memcpy(previousButtonStates, buttonStates, sizeof(buttonStates));
  previousAction = currentAction;

  // Map PlayerAction to button states
  buttonStates[e_ButtonFunction_ShortPass] = currentAction.short_pass;
  buttonStates[e_ButtonFunction_HighPass] = currentAction.high_pass;
  buttonStates[e_ButtonFunction_LongPass] = currentAction.long_pass;
  buttonStates[e_ButtonFunction_Shot] = currentAction.shot;
  buttonStates[e_ButtonFunction_Sprint] = currentAction.sprint;
  buttonStates[e_ButtonFunction_Dribble] = currentAction.dribble;
  buttonStates[e_ButtonFunction_Switch] = currentAction.switch_player;
  buttonStates[e_ButtonFunction_Special] = currentAction.special;

  // Defending actions mirror attacking (context-dependent in HumanController)
  buttonStates[e_ButtonFunction_Pressure] = currentAction.short_pass;
  buttonStates[e_ButtonFunction_Sliding] = currentAction.high_pass;
  buttonStates[e_ButtonFunction_TeamPressure] = currentAction.shot;
  buttonStates[e_ButtonFunction_KeeperRush] = currentAction.long_pass;
}

bool APIGamepad::GetButton(e_ButtonFunction buttonFunction) {
  std::lock_guard<std::mutex> lock(actionMutex);
  if (buttonFunction < 0 || buttonFunction >= e_ButtonFunction_Size) return false;
  return buttonStates[buttonFunction];
}

float APIGamepad::GetButtonValue(e_ButtonFunction buttonFunction) {
  return GetButton(buttonFunction) ? 1.0f : 0.0f;
}

void APIGamepad::SetButton(e_ButtonFunction buttonFunction, bool state) {
  std::lock_guard<std::mutex> lock(actionMutex);
  if (buttonFunction >= 0 && buttonFunction < e_ButtonFunction_Size) {
    buttonStates[buttonFunction] = state;
  }
}

bool APIGamepad::GetPreviousButtonState(e_ButtonFunction buttonFunction) {
  std::lock_guard<std::mutex> lock(actionMutex);
  if (buttonFunction < 0 || buttonFunction >= e_ButtonFunction_Size) return false;
  return previousButtonStates[buttonFunction];
}

Vector3 APIGamepad::GetDirection() {
  std::lock_guard<std::mutex> lock(actionMutex);
  return Vector3(currentAction.direction_x, currentAction.direction_y, 0.0f);
}

void APIGamepad::SetAction(const APIPlayerAction& action) {
  std::lock_guard<std::mutex> lock(actionMutex);
  currentAction = action;
}

void APIGamepad::ClearAction() {
  std::lock_guard<std::mutex> lock(actionMutex);
  currentAction = APIPlayerAction{};
  memset(buttonStates, 0, sizeof(buttonStates));
}
