// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_KEYBOARD_BRIGHTNESS_KEYBOARD_BRIGHTNESS_CONTROLLER_H_
#define ASH_SYSTEM_KEYBOARD_BRIGHTNESS_KEYBOARD_BRIGHTNESS_CONTROLLER_H_

#include <optional>

#include "ash/ash_export.h"
#include "ash/login/ui/login_data_dispatcher.h"
#include "ash/public/cpp/session/session_observer.h"
#include "ash/system/keyboard_brightness_control_delegate.h"
#include "base/memory/weak_ptr.h"
#include "chromeos/dbus/power/power_manager_client.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_registry_simple.h"

class AccountId;
class PrefService;

namespace ash {

class SessionControllerImpl;

// A class which controls keyboard brightness when Alt+F6, Alt+F7 or a
// multimedia key for keyboard brightness is pressed.
class ASH_EXPORT KeyboardBrightnessController
    : public KeyboardBrightnessControlDelegate,
      public SessionObserver,
      public LoginDataDispatcher::Observer,
      public chromeos::PowerManagerClient::Observer {
 public:
  explicit KeyboardBrightnessController(
      PrefService* local_state,
      SessionControllerImpl* session_controller);

  // Disallow copy and move.
  KeyboardBrightnessController(const KeyboardBrightnessController&) = delete;
  KeyboardBrightnessController& operator=(const KeyboardBrightnessController&) =
      delete;

  ~KeyboardBrightnessController() override;

  // static:
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // SessionObserver:
  void OnActiveUserPrefServiceChanged(PrefService* pref_service) override;
  void OnActiveUserSessionChanged(const AccountId& account_id) override;

  // PowerManagerClient::Observer:
  void KeyboardAmbientLightSensorEnabledChanged(
      const power_manager::AmbientLightSensorChange& change) override;
  void KeyboardBrightnessChanged(
      const power_manager::BacklightBrightnessChange& change) override;

  // LoginDataDispatcher::Observer:
  void OnFocusPod(const AccountId& account_id) override;

 private:
  // Overridden from KeyboardBrightnessControlDelegate:
  void HandleKeyboardBrightnessDown() override;
  void HandleKeyboardBrightnessUp() override;
  void HandleToggleKeyboardBacklight() override;
  void HandleSetKeyboardBrightness(double percent, bool gradual) override;
  void HandleGetKeyboardBrightness(
      base::OnceCallback<void(std::optional<double>)> callback) override;
  void HandleSetKeyboardAmbientLightSensorEnabled(bool enabled) override;
  void HandleGetKeyboardAmbientLightSensorEnabled(
      base::OnceCallback<void(std::optional<bool>)> callback) override;

  // Restore keyboard brightness settings during reboot.
  void RestoreKeyboardBrightnessSettings(const AccountId& account_id);

  // Restore keyboard ambient light sensor setting when first login.
  void RestoreKeyboardAmbientLightSensorSettingOnFirstLogin();

  void OnReceiveHasKeyboardBacklight(std::optional<bool> has_backlight);
  void OnReceiveKeyboardBrightnessAfterLogin(
      std::optional<double> keyboard_brightness);

  // The current AccountId, used to set and retrieve prefs. Expected to be
  // nullopt on the login screen, but will be set on login.
  std::optional<AccountId> active_account_id_;

  raw_ptr<PrefService> local_state_;                   // unowned.
  raw_ptr<PrefService> pref_service_;                  // unowned.
  raw_ptr<SessionControllerImpl> session_controller_;  // unowned.

  // True if the keyboard ambient light sensor value has already been restored
  // for a user's first login.
  bool has_keyboard_ambient_light_sensor_been_restored_for_new_user_ = false;

  // This PrefChangeRegistrar is used to check when the synced profile pref for
  // the keyboard ambient light sensor value has finished syncing.
  std::unique_ptr<PrefChangeRegistrar> pref_change_registrar_;

  base::WeakPtrFactory<KeyboardBrightnessController> weak_ptr_factory_{this};
};

}  // namespace ash

#endif  // ASH_SYSTEM_KEYBOARD_BRIGHTNESS_KEYBOARD_BRIGHTNESS_CONTROLLER_H_
