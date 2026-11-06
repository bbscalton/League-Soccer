// APIGamepad: an IHIDevice controlled by the external API server.
// Receives player actions from the socket API and exposes them through
// the standard input device interface so they flow into HumanController.
// Apache License 2.0 - see LICENSE

#ifndef _HPP_HID_APIGAMEPAD
#define _HPP_HID_APIGAMEPAD

#include <mutex>

#include "base/math/vector3.hpp"
#include "ihidevice.hpp"

using namespace blunted;

// Local copy of action structure to avoid circular dependency with api/
struct APIPlayerAction {
  float direction_x = 0.0f;
  float direction_y = 0.0f;
  bool short_pass = false;
  bool high_pass = false;
  bool long_pass = false;
  bool shot = false;
  bool sprint = false;
  bool dribble = false;
  bool switch_player = false;
  bool special = false;
};

class APIGamepad : public IHIDevice {
public:
  APIGamepad(int teamId);
  virtual ~APIGamepad();

  virtual void LoadConfig() override;
  virtual void SaveConfig() override;

  virtual void Process() override;

  virtual bool GetButton(e_ButtonFunction buttonFunction) override;
  virtual float GetButtonValue(e_ButtonFunction buttonFunction) override;
  virtual void SetButton(e_ButtonFunction buttonFunction, bool state) override;
  virtual bool GetPreviousButtonState(e_ButtonFunction buttonFunction) override;
  virtual Vector3 GetDirection() override;
  virtual void SetRumble(float low_frequency, float high_frequency,
                         unsigned int duration_ms) override {}

  virtual bool IsScriptedInput() const override { return true; }

  // Set the current action from the API server (thread-safe)
  void SetAction(const APIPlayerAction& action);

  // Clear all input (called on reset)
  void ClearAction();

  int GetTeamId() const { return teamId; }

protected:
  int teamId;
  std::mutex actionMutex;
  APIPlayerAction currentAction;
  APIPlayerAction previousAction;
  bool buttonStates[e_ButtonFunction_Size];
  bool previousButtonStates[e_ButtonFunction_Size];
};

#endif
