// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_FOCUS_MODE_FOCUS_MODE_CONTROLLER_H_
#define ASH_SYSTEM_FOCUS_MODE_FOCUS_MODE_CONTROLLER_H_

#include <optional>

#include "ash/ash_export.h"
#include "ash/public/cpp/session/session_observer.h"
#include "ash/system/focus_mode/focus_mode_delegate.h"
#include "ash/system/focus_mode/focus_mode_histogram_names.h"
#include "ash/system/focus_mode/focus_mode_session.h"
#include "ash/system/focus_mode/focus_mode_tasks_provider.h"
#include "ash/system/focus_mode/sounds/focus_mode_sounds_controller.h"
#include "base/observer_list.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

class PrefRegistrySimple;

namespace views {
class Widget;
}  // namespace views

namespace ash {

namespace youtube_music {
class YouTubeMusicController;
}  //  namespace youtube_music

class AshWebView;
class FocusModeMetricsRecorder;
class FocusModeSoundsController;

// Controls starting and ending a Focus Mode session and its behavior. Also
// keeps track of the system state to restore after a Focus Mode session ends.
// Has a timer that runs while a session is active and notifies `observers_` on
// every timer tick.
class ASH_EXPORT FocusModeController
    : public SessionObserver,
      public FocusModeSoundsController::Observer {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // Called whenever Focus Mode changes as a result of user action or when the
    // session duration expires.
    virtual void OnFocusModeChanged(bool in_focus_session) = 0;

    // Called every `timer_` tick for updating UI elements during a Focus Mode
    // session.
    virtual void OnTimerTick(
        const FocusModeSession::Snapshot& session_snapshot) {}

    // Notifies when the session duration is changed in the focus panel without
    // an active session.
    virtual void OnInactiveSessionDurationChanged(
        const base::TimeDelta& session_duration) {}

    // Notifies clients every time the session duration is changed during an
    // active session.
    virtual void OnActiveSessionDurationChanged(
        const FocusModeSession::Snapshot& session_snapshot) {}
  };

  explicit FocusModeController(std::unique_ptr<FocusModeDelegate> delegate);
  FocusModeController(const FocusModeController&) = delete;
  FocusModeController& operator=(const FocusModeController&) = delete;
  ~FocusModeController() override;

  // Convenience function to get the controller instance, which is created and
  // owned by Shell.
  static FocusModeController* Get();

  // Verifies that the session duration hasn't reached `kMaximumDuration`.
  static bool CanExtendSessionDuration(
      const FocusModeSession::Snapshot& snapshot);

  // Registers user profile prefs with the specified `registry`.
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  bool in_focus_session() const {
    return current_session_ && current_session_->GetState(base::Time::Now()) ==
                                   FocusModeSession::State::kOn;
  }
  bool in_ending_moment() const {
    return current_session_ && current_session_->GetState(base::Time::Now()) ==
                                   FocusModeSession::State::kEnding;
  }
  base::TimeDelta session_duration() const { return session_duration_; }
  bool turn_on_do_not_disturb() const { return turn_on_do_not_disturb_; }
  void set_turn_on_do_not_disturb(bool turn_on) {
    turn_on_do_not_disturb_ = turn_on;
  }
  const std::optional<FocusModeSession>& current_session() const {
    return current_session_;
  }
  const std::string& selected_task_list_id() const {
    return selected_task_.task_list_id;
  }
  const std::string& selected_task_id() const { return selected_task_.task_id; }
  const std::string& selected_task_title() const {
    return selected_task_.title;
  }
  FocusModeTasksProvider& tasks_provider() { return tasks_provider_; }
  FocusModeSoundsController* focus_mode_sounds_controller() const {
    return focus_mode_sounds_controller_.get();
  }
  youtube_music::YouTubeMusicController* youtube_music_controller() const {
    return youtube_music_controller_.get();
  }
  FocusModeDelegate* delegate() { return delegate_.get(); }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Starts or ends a focus session by a toggle `source`.
  void ToggleFocusMode(
      focus_mode_histogram_names::ToggleSource source =
          focus_mode_histogram_names::ToggleSource::kFocusPanel);

  // SessionObserver:
  void OnActiveUserSessionChanged(const AccountId& account_id) override;

  // FocusModeSoundsController::Observer:
  // Will close/create the media widget for an active focus session depending on
  // if there is a selected playlist or not.
  void OnSelectedPlaylistChanged() override;

  // Extends an active focus session by ten minutes by clicking the `+10 min`
  // button.
  void ExtendSessionDuration();

  // Resets the focus session state for when the session needs to end (i.e. the
  // user manually ends the session, or when the ending moment is terminated).
  // This ensures that states are all reverted (especially DND and UI elements).
  void ResetFocusSession();

  // Used when we want to extend the ending state indefinitely, and requires
  // direct user action to terminate the ending moment.
  void EnablePersistentEnding();

  // Sets a specific value for `session_duration_`. We have two different
  // notions of a session, so this one is only in charge of updating the session
  // duration that will be applied to the next active session. Also notifies
  // observers that the session duration was changed. An "inactive" session can
  // either be no `current_session_`, or if we are in the ending moment, since
  // the user should still be able to adjust and start a new session during that
  // time.
  void SetInactiveSessionDuration(const base::TimeDelta& new_session_duration);

  // Returns whether the user has ever started a focus session previously.
  bool HasStartedSessionBefore() const;

  // Creates and returns a snapshot of the current session based on `now`.
  // Returns a default struct if there is no session.
  FocusModeSession::Snapshot GetSnapshot(const base::Time& now) const;

  // Returns the session duration of either the current session, or what the
  // upcoming session will be set to.
  base::TimeDelta GetSessionDuration() const;

  // Returns the end time of an active session. This end time is meant to be
  // displayed, and may be different depending on the session state (e.g. the
  // ending moment needs to account for the extra duration).
  base::Time GetActualEndTime() const;

  // Stores the provided `task`.
  void SetSelectedTask(const FocusModeTask& task);

  // Returns whether there is a currently selected task.
  bool HasSelectedTask() const;

  // Marks the task as completed, and also clears the selected task data.
  // Updates the tasks provider if `update` is `true`. For example, if the task
  // has been completed outside of focus mode, `update` should be `false`.
  void CompleteTask(bool update = true);

  // Shows the ending moment nudge that is anchored to the focus mode tray. Only
  // show if there isn't already showing and if there is no tray bubble open.
  void MaybeShowEndingMomentNudge();

  // This is currently only used in testing to trigger an ending moment
  // immediately if there is an ongoing session.
  void TriggerEndingMomentImmediately();

 private:
  // Starts a focus session by updating UI elements, starting `timer_`, and
  // setting `current_session_` to the desired session duration and end time.
  void StartFocusSession(focus_mode_histogram_names::ToggleSource source);

  // Called every time a second passes on `timer_` while the session is active.
  void OnTimerTick();

  // This is called when the active user changes, and is important to update our
  // cached values in case different users have different stored preferences.
  void UpdateFromUserPrefs();

  // Called by `UpdateFromUserPrefs` to update our cached values for the active
  // user about the selected task.
  void UpdateSelectedTaskFromUserPrefs();

  // Called once a session starts. Saves the current selected settings to user
  // prefs so we can provide the same set-up the next time the user comes back
  // to Focus Mode.
  void SaveSettingsToUserPrefs();

  // Called once a session starts, and when a task is selected or deselected in
  // focus session.
  void SaveSelectedTaskSettingsToUserPrefs();

  // Closes any open system tray bubbles. This is done whenever we start a focus
  // session.
  void CloseSystemTrayBubble();

  // Sets the visibility of the focus tray on the shelf.
  void SetFocusTrayVisibility(bool visible);

  // This tells us if there is an open focus mode tray bubble on any of the
  // displays.
  bool IsFocusTrayBubbleVisible() const;

  // Creates the media widget if one doesn't already exist and if there is a
  // selected playlist.
  void MaybeCreateMediaWidget();
  void CloseMediaWidget();

  // Gives Focus Mode access to the Google Tasks API.
  FocusModeTasksProvider tasks_provider_;

  // This is the expected duration of a Focus Mode session once it starts.
  // Depends on previous session data (from user prefs) or user input.
  base::TimeDelta session_duration_;

  // This will dictate whether DND will be turned on when a Focus Mode session
  // starts. Depends on previous session data (from user prefs) or user input.
  bool turn_on_do_not_disturb_ = true;

  // This timer is used for keeping track of the Focus Mode session duration and
  // will trigger a callback every second during a session. It will terminate
  // once the session goes into the `kEnding` state, or if a user toggles off
  // Focus Mode.
  base::MetronomeTimer timer_;

  // This is used to track the current session, if any.
  std::optional<FocusModeSession> current_session_;

  // This is the selected task, which can be populated from an existing task or
  // created by the user.
  FocusModeTask selected_task_;

  std::unique_ptr<FocusModeMetricsRecorder> focus_mode_metrics_recorder_;

  // This is used to display focus mode playlists. Playback controls will be
  // added later.
  std::unique_ptr<FocusModeSoundsController> focus_mode_sounds_controller_;

  // Controller for YouTube Music API integration.
  std::unique_ptr<youtube_music::YouTubeMusicController>
      youtube_music_controller_;

  // The media widget and its contents view.
  std::unique_ptr<views::Widget> media_widget_;
  raw_ptr<AshWebView> focus_mode_media_view_ = nullptr;

  std::unique_ptr<FocusModeDelegate> delegate_;

  base::ObserverList<Observer> observers_;
};

}  // namespace ash

#endif  // ASH_SYSTEM_FOCUS_MODE_FOCUS_MODE_CONTROLLER_H_
