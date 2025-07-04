// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/test/mock_render_input_router.h"

namespace content {

MockRenderInputRouter::MockRenderInputRouter(
    InputRouterImplClient* host,
    std::unique_ptr<input::FlingSchedulerBase> fling_scheduler,
    RenderInputRouterDelegate* delegate,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : RenderInputRouter(host,
                        std::move(fling_scheduler),
                        delegate,
                        std::move(task_runner)) {
  acked_touch_event_type_ = blink::WebInputEvent::Type::kUndefined;
  mock_widget_input_handler_ = std::make_unique<MockWidgetInputHandler>();
}

MockRenderInputRouter::~MockRenderInputRouter() = default;

void MockRenderInputRouter::OnTouchEventAck(
    const input::TouchEventWithLatencyInfo& event,
    blink::mojom::InputEventResultSource ack_source,
    blink::mojom::InputEventResultState ack_result) {
  // Sniff touch acks.
  acked_touch_event_type_ = event.event.GetType();
  RenderInputRouter::OnTouchEventAck(event, ack_source, ack_result);
}

void MockRenderInputRouter::SetupForInputRouterTest() {
  input_router_ = std::make_unique<MockInputRouter>(this);
}

void MockRenderInputRouter::ForwardTouchEventWithLatencyInfo(
    const blink::WebTouchEvent& touch_event,
    const ui::LatencyInfo& ui_latency) {
  RenderInputRouter::ForwardTouchEventWithLatencyInfo(touch_event, ui_latency);
  SetLastWheelOrTouchEventLatencyInfo(ui::LatencyInfo(ui_latency));
}

MockWidgetInputHandler::MessageVector
MockRenderInputRouter::GetAndResetDispatchedMessages() {
  return mock_widget_input_handler_->GetAndResetDispatchedMessages();
}

blink::mojom::WidgetInputHandler*
MockRenderInputRouter::GetWidgetInputHandler() {
  return mock_widget_input_handler_.get();
}

}  // namespace content
