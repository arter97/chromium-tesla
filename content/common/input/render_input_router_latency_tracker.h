// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_INPUT_RENDER_INPUT_ROUTER_LATENCY_TRACKER_H_
#define CONTENT_COMMON_INPUT_RENDER_INPUT_ROUTER_LATENCY_TRACKER_H_

#include <stdint.h>

#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "components/input/event_with_latency_info.h"
#include "content/common/content_export.h"
#include "third_party/blink/public/mojom/input/input_event_result.mojom-shared.h"
#include "ui/latency/latency_info.h"
#include "ui/latency/latency_tracker.h"

namespace content {

class RenderInputRouterDelegate;

// Utility class for tracking the latency of events passing through
// a given RenderWidgetHost.
class CONTENT_EXPORT RenderInputRouterLatencyTracker {
 public:
  explicit RenderInputRouterLatencyTracker(RenderInputRouterDelegate* delegate);

  RenderInputRouterLatencyTracker(const RenderInputRouterLatencyTracker&) =
      delete;
  RenderInputRouterLatencyTracker& operator=(
      const RenderInputRouterLatencyTracker&) = delete;

  virtual ~RenderInputRouterLatencyTracker();

  // Populates the LatencyInfo with relevant entries for latency tracking.
  // Called when an event is received by the RenderWidgetHost, prior to
  // that event being forwarded to the renderer (via the InputRouter).
  void OnInputEvent(const blink::WebInputEvent& event,
                    ui::LatencyInfo* latency,
                    ui::EventLatencyMetadata* event_latency_metadata);

  // Populates the LatencyInfo with relevant entries for latency tracking, also
  // terminating latency tracking for events that did not trigger rendering and
  // performing relevant UMA latency reporting. Called when an event is ack'ed
  // to the RenderWidgetHost (from the InputRouter).
  void OnInputEventAck(const blink::WebInputEvent& event,
                       ui::LatencyInfo* latency,
                       blink::mojom::InputEventResultState ack_result);

  void reset_delegate() { render_input_router_delegate_ = nullptr; }

 private:
  void OnEventStart(ui::LatencyInfo* latency);

  bool has_seen_first_gesture_scroll_update_;
  int64_t gesture_scroll_id_;
  int64_t touch_trace_id_;

  // Whether the current stream of touch events includes more than one active
  // touch point. This is set in OnInputEvent, and cleared in OnInputEventAck.
  bool active_multi_finger_gesture_;
  // Whether the touch start for the current stream of touch events had its
  // default action prevented. Only valid for single finger gestures.
  bool touch_start_default_prevented_;

  raw_ptr<RenderInputRouterDelegate> render_input_router_delegate_;

  // This class expects to be constructed and used on the same sequence.
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace content

#endif  // CONTENT_COMMON_INPUT_RENDER_INPUT_ROUTER_LATENCY_TRACKER_H_
