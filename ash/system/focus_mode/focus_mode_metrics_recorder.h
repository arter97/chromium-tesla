// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_FOCUS_MODE_FOCUS_MODE_METRICS_RECORDER_H_
#define ASH_SYSTEM_FOCUS_MODE_FOCUS_MODE_METRICS_RECORDER_H_

#include "ash/ash_export.h"
#include "ash/system/focus_mode/focus_mode_histogram_names.h"
#include "base/time/time.h"
#include "ui/message_center/message_center_observer.h"

namespace ash {

class ASH_EXPORT FocusModeMetricsRecorder
    : public message_center::MessageCenterObserver {
 public:
  explicit FocusModeMetricsRecorder(
      const base::TimeDelta& initial_session_duration);
  FocusModeMetricsRecorder(const FocusModeMetricsRecorder&) = delete;
  FocusModeMetricsRecorder& operator=(const FocusModeMetricsRecorder&) = delete;
  ~FocusModeMetricsRecorder() override;

  void IncrementTasksSelectedCount();
  void IncrementTasksCompletedCount();

  // message_center::MessageCenterObserver:
  void OnQuietModeChanged(bool in_quiet_mode) override;

  void RecordHistogramsOnStart(focus_mode_histogram_names::ToggleSource source,
                               const std::string& selected_task_id);

  // Called by `FocusModeController::ResetFocusSession` to record the data on a
  // session completing.
  void RecordHistogramsOnEnd();

  void RecordHistogramOnEndingMoment(
      focus_mode_histogram_names::EndingMomentBubbleClosedReason reason);

 private:
  // Counts the number of tasks selected during a session.
  int tasks_selected_count_ = 0;

  // Counts the number of tasks marked as completed during a session.
  int tasks_completed_count_ = 0;

  // True if the user turns DND on or off in an active session.
  bool has_user_interactions_on_dnd_in_focus_session_ = false;
  const base::TimeDelta initial_session_duration_;
};

}  // namespace ash

#endif  // ASH_SYSTEM_FOCUS_MODE_FOCUS_MODE_METRICS_RECORDER_H_
