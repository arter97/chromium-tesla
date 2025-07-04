// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_PICKER_VIEWS_PICKER_WIDGET_H_
#define ASH_PICKER_VIEWS_PICKER_WIDGET_H_

#include "ash/ash_export.h"
#include "ash/bubble/bubble_event_filter.h"
#include "base/time/time.h"
#include "ui/views/widget/unique_widget_ptr.h"
#include "ui/views/widget/widget.h"

namespace gfx {
class Rect;
}  // namespace gfx

namespace ui {
class LocatedEvent;
}

namespace ash {
class PickerViewDelegate;

class ASH_EXPORT PickerWidget : public views::Widget {
 public:
  PickerWidget(const PickerWidget&) = delete;
  PickerWidget& operator=(const PickerWidget&) = delete;
  ~PickerWidget() override;

  // `delegate` must remain valid for the lifetime of the created Widget.
  // `anchor_bounds` is in screen coordinates.
  // `trigger_event_timestamp` is the timestamp of the event that triggered the
  // Widget to be created. For example, if the feature was triggered by a mouse
  // click, then it should be the timestamp of the click. By default, the
  // timestamp is the time this function is called.
  static views::UniqueWidgetPtr Create(
      PickerViewDelegate* delegate,
      const gfx::Rect& anchor_bounds,
      base::TimeTicks trigger_event_timestamp = base::TimeTicks::Now());

  // views::Widget:
  void OnNativeBlur() override;

 private:
  explicit PickerWidget(PickerViewDelegate* delegate,
                        const gfx::Rect& anchor_bounds,
                        base::TimeTicks trigger_event_timestamp);

  void OnClickOutsideWidget(const ui::LocatedEvent& event);

  // Used to close the Picker widget when the user clicks outside of it.
  BubbleEventFilter bubble_event_filter_;
};

}  // namespace ash

#endif  // ASH_PICKER_VIEWS_PICKER_WIDGET_H_
