// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "content/common/input/render_input_router.h"

#include <utility>
#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/debug/stack_trace.h"
#include "base/lazy_instance.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "cc/input/browser_controls_offset_tags_info.h"
#include "content/common/input/input_router_config_helper.h"
#include "content/common/input/render_widget_host_input_event_router.h"
#include "content/common/input/render_widget_host_view_input.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "third_party/blink/public/common/input/web_input_event.h"

using blink::WebGestureEvent;
using blink::WebInputEvent;

namespace content {
namespace {

class UnboundWidgetInputHandler : public blink::mojom::WidgetInputHandler {
 public:
  void SetFocus(blink::mojom::FocusState focus_state) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void MouseCaptureLost() override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void SetEditCommandsForNextKeyEvent(
      std::vector<blink::mojom::EditCommandPtr> commands) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void CursorVisibilityChanged(bool visible) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void ImeSetComposition(const std::u16string& text,
                         const std::vector<ui::ImeTextSpan>& ime_text_spans,
                         const gfx::Range& range,
                         int32_t start,
                         int32_t end,
                         ImeSetCompositionCallback callback) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void ImeCommitText(const std::u16string& text,
                     const std::vector<ui::ImeTextSpan>& ime_text_spans,
                     const gfx::Range& range,
                     int32_t relative_cursor_position,
                     ImeCommitTextCallback callback) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void ImeFinishComposingText(bool keep_selection) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void RequestTextInputStateUpdate() override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void RequestCompositionUpdates(bool immediate_request,
                                 bool monitor_request) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void DispatchEvent(std::unique_ptr<blink::WebCoalescedInputEvent> event,
                     DispatchEventCallback callback) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void DispatchNonBlockingEvent(
      std::unique_ptr<blink::WebCoalescedInputEvent> event) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void WaitForInputProcessed(WaitForInputProcessedCallback callback) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
#if BUILDFLAG(IS_ANDROID)
  void AttachSynchronousCompositor(
      mojo::PendingRemote<blink::mojom::SynchronousCompositorControlHost>
          control_host,
      mojo::PendingAssociatedRemote<blink::mojom::SynchronousCompositorHost>
          host,
      mojo::PendingAssociatedReceiver<blink::mojom::SynchronousCompositor>
          compositor_request) override {
    NOTREACHED_IN_MIGRATION() << "Input request on unbound interface";
  }
#endif
  void GetFrameWidgetInputHandler(
      mojo::PendingAssociatedReceiver<blink::mojom::FrameWidgetInputHandler>
          request) override {
    NOTREACHED_IN_MIGRATION() << "Input request on unbound interface";
  }
  void UpdateBrowserControlsState(
      cc::BrowserControlsState constraints,
      cc::BrowserControlsState current,
      bool animate,
      const std::optional<cc::BrowserControlsOffsetTagsInfo>& offset_tags_info)
      override {
    NOTREACHED_IN_MIGRATION() << "Input request on unbound interface";
  }
};

base::LazyInstance<UnboundWidgetInputHandler>::Leaky g_unbound_input_handler =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

RenderInputRouter::~RenderInputRouter() = default;

RenderInputRouter::RenderInputRouter(
    InputRouterImplClient* host,
    std::unique_ptr<input::FlingSchedulerBase> fling_scheduler,
    RenderInputRouterDelegate* delegate,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : fling_scheduler_(std::move(fling_scheduler)),
      latency_tracker_(
          std::make_unique<RenderInputRouterLatencyTracker>(delegate)),
      input_router_impl_client_(host),
      delegate_(delegate),
      task_runner_(std::move(task_runner)) {}

void RenderInputRouter::SetupInputRouter(float device_scale_factor) {
  TRACE_EVENT("toplevel.flow", "RenderInputRouter::SetupInputRouter");

  input_router_ = std::make_unique<InputRouterImpl>(
      this, this, fling_scheduler_.get(),
      GetInputRouterConfigForPlatform(task_runner_));

  // input_router_ recreated, need to update the force_enable_zoom_ state.
  input_router_->SetForceEnableZoom(force_enable_zoom_);
  input_router_->SetDeviceScaleFactor(device_scale_factor);
}

void RenderInputRouter::BindRenderInputRouterInterfaces(
    mojo::PendingRemote<blink::mojom::RenderInputRouterClient> remote) {
  client_remote_.reset();

  client_remote_.Bind(std::move(remote), task_runner_);
}

void RenderInputRouter::RendererWidgetCreated(bool for_frame_widget) {
  TRACE_EVENT("toplevel.flow", "RenderInputRouter::RendererWidgetCreated");

  client_remote_->GetWidgetInputHandler(
      widget_input_handler_.BindNewPipeAndPassReceiver(task_runner_),
      input_router_->BindNewHost(task_runner_));

  if (for_frame_widget) {
    widget_input_handler_->GetFrameWidgetInputHandler(
        frame_widget_input_handler_.BindNewEndpointAndPassReceiver(
            task_runner_));
    client_remote_->BindInputTargetClient(
        input_target_client_.BindNewPipeAndPassReceiver(task_runner_));
  }
}

void RenderInputRouter::SetForceEnableZoom(bool enabled) {
  force_enable_zoom_ = enabled;
  input_router_->SetForceEnableZoom(enabled);
}

void RenderInputRouter::SetDeviceScaleFactor(float device_scale_factor) {
  input_router_->SetDeviceScaleFactor(device_scale_factor);
}

void RenderInputRouter::ProgressFlingIfNeeded(base::TimeTicks current_time) {
  TRACE_EVENT("toplevel.flow", "RenderInputRouter::ProgressFlingIfNeeded");
  fling_scheduler_->ProgressFlingOnBeginFrameIfneeded(current_time);
}

void RenderInputRouter::StopFling() {
  input_router()->StopFling();
}

blink::mojom::WidgetInputHandler* RenderInputRouter::GetWidgetInputHandler() {
  TRACE_EVENT("toplevel.flow", "RenderInputRouter::GetWidgetInputHandler");

  if (widget_input_handler_) {
    return widget_input_handler_.get();
  }
  // TODO(dtapuska): Remove the need for the unbound interface. It is
  // possible that a RVHI may make calls to a WidgetInputHandler when
  // the main frame is remote. This is because of ordering issues during
  // widget shutdown, so we present an UnboundWidgetInputHandler had
  // DLOGS the message calls.
  return g_unbound_input_handler.Pointer();
}

void RenderInputRouter::OnImeCompositionRangeChanged(
    const gfx::Range& range,
    const std::optional<std::vector<gfx::Rect>>& character_bounds,
    const std::optional<std::vector<gfx::Rect>>& line_bounds) {
  input_router_impl_client_->OnImeCompositionRangeChanged(
      range, character_bounds, line_bounds);
}
void RenderInputRouter::OnImeCancelComposition() {
  input_router_impl_client_->OnImeCancelComposition();
}

StylusInterface* RenderInputRouter::GetStylusInterface() {
  return input_router_impl_client_->GetStylusInterface();
}

void RenderInputRouter::OnStartStylusWriting() {
  input_router_impl_client_->OnStartStylusWriting();
}

bool RenderInputRouter::IsWheelScrollInProgress() {
  return is_in_gesture_scroll_[static_cast<int>(
      blink::WebGestureDevice::kTouchpad)];
}

bool RenderInputRouter::IsAutoscrollInProgress() {
  return input_router_impl_client_->IsAutoscrollInProgress();
}

void RenderInputRouter::SetMouseCapture(bool capture) {
  input_router_impl_client_->SetMouseCapture(capture);
}

void RenderInputRouter::SetAutoscrollSelectionActiveInMainFrame(
    bool autoscroll_selection) {
  input_router_impl_client_->SetAutoscrollSelectionActiveInMainFrame(
      autoscroll_selection);
}

void RenderInputRouter::RequestMouseLock(
    bool from_user_gesture,
    bool unadjusted_movement,
    InputRouterImpl::RequestMouseLockCallback response) {
  input_router_impl_client_->RequestMouseLock(
      from_user_gesture, unadjusted_movement, std::move(response));
}

gfx::Size RenderInputRouter::GetRootWidgetViewportSize() {
  return input_router_impl_client_->GetRootWidgetViewportSize();
}

blink::mojom::InputEventResultState RenderInputRouter::FilterInputEvent(
    const blink::WebInputEvent& event,
    const ui::LatencyInfo& latency_info) {
  return input_router_impl_client_->FilterInputEvent(event, latency_info);
}

void RenderInputRouter::IncrementInFlightEventCount() {
  input_router_impl_client_->IncrementInFlightEventCount();
}

void RenderInputRouter::NotifyUISchedulerOfGestureEventUpdate(
    blink::WebInputEvent::Type gesture_event) {
  input_router_impl_client_->NotifyUISchedulerOfGestureEventUpdate(
      gesture_event);
}

void RenderInputRouter::DecrementInFlightEventCount(
    blink::mojom::InputEventResultSource ack_source) {
  input_router_impl_client_->DecrementInFlightEventCount(ack_source);
}

void RenderInputRouter::DidOverscroll(const ui::DidOverscrollParams& params) {
  input_router_impl_client_->DidOverscroll(params);
}

void RenderInputRouter::DidStartScrollingViewport() {
  input_router_impl_client_->DidStartScrollingViewport();
}

void RenderInputRouter::OnInvalidInputEventSource() {
  input_router_impl_client_->OnInvalidInputEventSource();
}

void RenderInputRouter::ForwardGestureEventWithLatencyInfo(
    const blink::WebGestureEvent& gesture_event,
    const ui::LatencyInfo& latency_info) {
  input_router_impl_client_->ForwardGestureEventWithLatencyInfo(gesture_event,
                                                                latency_info);
}
void RenderInputRouter::ForwardWheelEventWithLatencyInfo(
    const blink::WebMouseWheelEvent& wheel_event,
    const ui::LatencyInfo& latency_info) {
  input_router_impl_client_->ForwardWheelEventWithLatencyInfo(wheel_event,
                                                              latency_info);
}

void RenderInputRouter::OnWheelEventAck(
    const input::MouseWheelEventWithLatencyInfo& wheel_event,
    blink::mojom::InputEventResultSource ack_source,
    blink::mojom::InputEventResultState ack_result) {
  latency_tracker_->OnInputEventAck(wheel_event.event, &wheel_event.latency,
                                    ack_result);
  delegate_->NotifyObserversOfInputEventAcks(ack_source, ack_result,
                                             wheel_event.event);

  delegate_->OnWheelEventAck(wheel_event, ack_source, ack_result);
}

void RenderInputRouter::OnTouchEventAck(
    const input::TouchEventWithLatencyInfo& event,
    blink::mojom::InputEventResultSource ack_source,
    blink::mojom::InputEventResultState ack_result) {
  latency_tracker_->OnInputEventAck(event.event, &event.latency, ack_result);
  delegate_->NotifyObserversOfInputEventAcks(ack_source, ack_result,
                                             event.event);

  auto* input_event_router = delegate_->GetInputEventRouter();

  // At present interstitial pages might not have an input event router, so we
  // just have the view process the ack directly in that case; the view is
  // guaranteed to be a top-level view with an appropriate implementation of
  // ProcessAckedTouchEvent().
  if (input_event_router) {
    input_event_router->ProcessAckedTouchEvent(event, ack_result,
                                               view_input_.get());
  } else if (view_input_) {
    view_input_->ProcessAckedTouchEvent(event, ack_result);
  }
}

void RenderInputRouter::OnGestureEventAck(
    const input::GestureEventWithLatencyInfo& event,
    blink::mojom::InputEventResultSource ack_source,
    blink::mojom::InputEventResultState ack_result) {
  latency_tracker_->OnInputEventAck(event.event, &event.latency, ack_result);
  delegate_->NotifyObserversOfInputEventAcks(ack_source, ack_result,
                                             event.event);

  delegate_->OnGestureEventAck(event, ack_source, ack_result);
}

void RenderInputRouter::DispatchInputEventWithLatencyInfo(
    const WebInputEvent& event,
    ui::LatencyInfo* latency,
    ui::EventLatencyMetadata* event_latency_metadata) {
  latency_tracker_->OnInputEvent(event, latency, event_latency_metadata);
  delegate_->NotifyObserversOfInputEvent(event);
}

void RenderInputRouter::ForwardTouchEventWithLatencyInfo(
    const blink::WebTouchEvent& touch_event,
    const ui::LatencyInfo& latency) {
  TRACE_EVENT0("input", "RenderInputRouter::ForwardTouchEvent");

  // Always forward TouchEvents for touch stream consistency. They will be
  // ignored if appropriate in FilterInputEvent().

  input::TouchEventWithLatencyInfo touch_with_latency(touch_event, latency);
  DispatchInputEventWithLatencyInfo(
      touch_with_latency.event, &touch_with_latency.latency,
      &touch_with_latency.event.GetModifiableEventLatencyMetadata());
  input_router_->SendTouchEvent(touch_with_latency);
}

std::unique_ptr<RenderInputRouterIterator>
RenderInputRouter::GetEmbeddedRenderInputRouters() {
  return delegate_->GetEmbeddedRenderInputRouters();
}

void RenderInputRouter::ShowContextMenuAtPoint(
    const gfx::Point& point,
    const ui::MenuSourceType source_type) {
  if (client_remote_) {
    client_remote_->ShowContextMenu(source_type, point);
  }
}

void RenderInputRouter::SendGestureEventWithLatencyInfo(
    const input::GestureEventWithLatencyInfo& gesture_with_latency) {
  const blink::WebGestureEvent& gesture_event = gesture_with_latency.event;
  if (gesture_event.GetType() == WebInputEvent::Type::kGestureScrollBegin) {
    DCHECK(
        !is_in_gesture_scroll_[static_cast<int>(gesture_event.SourceDevice())]);
    is_in_gesture_scroll_[static_cast<int>(gesture_event.SourceDevice())] =
        true;
  } else if (gesture_event.GetType() ==
             WebInputEvent::Type::kGestureScrollEnd) {
    DCHECK(
        is_in_gesture_scroll_[static_cast<int>(gesture_event.SourceDevice())]);
    is_in_gesture_scroll_[static_cast<int>(gesture_event.SourceDevice())] =
        false;
    is_in_touchpad_gesture_fling_ = false;
  } else if (gesture_event.GetType() ==
             WebInputEvent::Type::kGestureFlingStart) {
    if (gesture_event.SourceDevice() == blink::WebGestureDevice::kTouchpad) {
      // a GSB event is generated from the first wheel event in a sequence after
      // the event is acked as not consumed by the renderer. Sometimes when the
      // main thread is busy/slow (e.g ChromeOS debug builds) a GFS arrives
      // before the first wheel is acked. In these cases no GSB will arrive
      // before the GFS. With browser side fling the out of order GFS arrival
      // does not need a DCHECK since the fling controller will process the GFS
      // and start queuing wheel events which will follow the one currently
      // awaiting ACK and the renderer receives the events in order.

      is_in_touchpad_gesture_fling_ = true;
    } else {
      DCHECK(is_in_gesture_scroll_[static_cast<int>(
          gesture_event.SourceDevice())]);

      // The FlingController handles GFS with touchscreen source and sends GSU
      // events with inertial state to the renderer to progress the fling.
      // is_in_gesture_scroll must stay true till the fling progress is
      // finished. Then the FlingController will generate and send a GSE which
      // shows the end of a scroll sequence and resets is_in_gesture_scroll_.
    }
  }
  input_router()->SendGestureEvent(gesture_with_latency);
}

blink::mojom::FrameWidgetInputHandler*
RenderInputRouter::GetFrameWidgetInputHandler() {
  if (!frame_widget_input_handler_) {
    return nullptr;
  }
  return frame_widget_input_handler_.get();
}

void RenderInputRouter::SetView(RenderWidgetHostViewInput* view) {
  if (!view) {
    view_input_.reset();
    return;
  }
  view_input_ = view->GetInputWeakPtr();
}

void RenderInputRouter::ResetFrameWidgetInputInterfaces() {
  frame_widget_input_handler_.reset();
  input_target_client_.reset();
}

void RenderInputRouter::ResetWidgetInputInterfaces() {
  widget_input_handler_.reset();
}

void RenderInputRouter::SetInputTargetClientForTesting(
    mojo::Remote<viz::mojom::InputTargetClient> input_target_client) {
  input_target_client_ = std::move(input_target_client);
}

}  // namespace content
