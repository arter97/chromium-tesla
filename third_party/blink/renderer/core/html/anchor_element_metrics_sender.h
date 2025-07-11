// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_HTML_ANCHOR_ELEMENT_METRICS_SENDER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_HTML_ANCHOR_ELEMENT_METRICS_SENDER_H_

#include "third_party/blink/public/mojom/loader/navigation_predictor.mojom-blink.h"
#include "third_party/blink/public/mojom/preloading/anchor_element_interaction_host.mojom-blink.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_set.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_remote.h"
#include "third_party/blink/renderer/platform/supplementable.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class Document;
class HTMLAnchorElement;
class IntersectionObserver;
class IntersectionObserverEntry;
class PointerEvent;

// AnchorElementMetricsSender is responsible to send anchor element metrics to
// the browser process for a given document.
//
// AnchorElementMetricsSenders are created for documents in main frames. Any
// same-origin iframes reuse the AnchorElementMetricsSender of their main frame.
// Cross-origin iframes do not use any AnchorElementMetricsSender.
//
// The high level approach is:
// 1) When HTMLAnchorElements are inserted into the DOM,
//    AnchorElementMetricsSender::AddAnchorElement is called and a reference to
//    the element is stored. The first time this happens, the sender is created,
//    which registers itself for lifecycle callbacks.
// 2) If any elements enter the viewport, the intersection observer will call
//    AnchorElementMetricsSender::UpdateVisibleAnchors. Elements are collected
//    in entered_viewport_messages_ and will be reported after the next layout.
// 3) On the next layout, AnchorElementMetricsSender::DidFinishLifecycleUpdate
//    is called, and it goes over the collected anchor elements. Elements that
//    are visible are reported to the browser via ReportNewAnchorElements. We
//    also may add an element to the intersection observer that watches for
//    elements entering/leaving the viewport. The anchor elements collected in
//    AnchorElementMetricsSender are all dropped. In particular, this drops
//    elements that are not visible. They will never be reported even if they
//    become visible later, unless the are reinserted into the DOM. This is not
//    ideal, but simpler, keeps resource usage low, and seems to work well
//    enough on the sites I've looked at. Also, elements that entered the
//    viewport will be reported using ReportAnchorElementsEnteredViewport. We
//    stop observing lifecycle changes until the next anchor being added or
//    entering/existing the viewport, when we again wait for the next layout.
class CORE_EXPORT AnchorElementMetricsSender final
    : public GarbageCollected<AnchorElementMetricsSender>,
      public LocalFrameView::LifecycleNotificationObserver,
      public Supplement<Document> {
 public:
  static const char kSupplementName[];

  using AnchorId = uint32_t;

  explicit AnchorElementMetricsSender(Document&);
  AnchorElementMetricsSender(const AnchorElementMetricsSender&) = delete;
  AnchorElementMetricsSender& operator=(const AnchorElementMetricsSender&) =
      delete;
  virtual ~AnchorElementMetricsSender();

  // LocalFrameView::LifecycleNotificationObserver
  void WillStartLifecycleUpdate(const LocalFrameView&) override {}
  void DidFinishLifecycleUpdate(
      const LocalFrameView& local_frame_view) override;

  // Returns the AnchorElementMetricsSender of the given root `document`.
  // Constructs and returns a new one if it does not exist, or returns nullptr
  // if the given `document` may not have a AnchorElementMetricsSender.
  static AnchorElementMetricsSender* From(Document& document);

  // Returns the AnchorElementMetricsSender for `frame`'s main frame's document,
  // according to `From`. Returns nullptr if `frame` is a cross-origin subframe.
  static AnchorElementMetricsSender* GetForFrame(LocalFrame* frame);

  // Report the link click to the browser process, so long as the anchor
  // is an HTTP(S) link.
  void MaybeReportClickedMetricsOnClick(
      const HTMLAnchorElement& anchor_element);

  // Report the on-hover event and anchor element pointer data to the browser
  // process.
  void MaybeReportAnchorElementPointerDataOnHoverTimerFired(
      AnchorId anchor_id,
      mojom::blink::AnchorElementPointerDataPtr mouse_data);

  void AddAnchorElement(HTMLAnchorElement& element);
  void RemoveAnchorElement(HTMLAnchorElement& element);

  void SetTickClockForTesting(const base::TickClock* clock);
  void SetNowAsNavigationStartForTesting();
  void FireUpdateTimerForTesting();
  IntersectionObserver* GetIntersectionObserverForTesting();

  // Creates AnchorElementMetrics from anchor element if possible. Then records
  // the metrics, and sends them to the browser process.
  void UpdateVisibleAnchors(
      const HeapVector<Member<IntersectionObserverEntry>>& entries);

  // Report the pointer event for the anchor element.
  void MaybeReportAnchorElementPointerEvent(HTMLAnchorElement& element,
                                            const PointerEvent& pointer_event);

  // Record and send metrics to the browser process about the position of
  // tracked anchor elements in the viewport. |pointerY| is the y-coordinate of
  // the most recent pointerdown location (screen coordinates in DIPs).
  void MaybeReportAnchorElementsPositionOnScrollEnd(double pointerY);

  void Trace(Visitor*) const override;

  // The minimum time gap that is required between two consecutive UpdateMetrics
  // calls.
  static constexpr auto kUpdateMetricsTimeGap = base::Milliseconds(200);

 private:
  // Associates |metrics_host_| with the IPC interface if not already, so it can
  // be used to send messages. Returns true if associated, false otherwise.
  bool AssociateInterface();

  // Creates an AnchorElementEnteredViewportPtr for the given element and
  // enqueue it so that it gets reported after the next layout.
  void EnqueueEnteredViewport(const HTMLAnchorElement& element);

  // Creates an AnchorElementLeftViewportPtr for the given element and
  // enqueue it so that it gets reported after the next layout.
  void EnqueueLeftViewport(const HTMLAnchorElement& element);

  // Checks how long it has passed since the last call and decides whether to
  // call or reschedule a future call to UpdateMetrics.
  void MaybeUpdateMetrics();

  // Sends the metrics update, immediately.
  void UpdateMetrics(TimerBase*);

  void SetShouldSkipUpdateDelays(bool should_skip_for_testing);

  base::TimeTicks NavigationStart() const;

  void RegisterForLifecycleNotifications();

  void PositionUpdateTimerFired(TimerBase*);

  void ComputeAnchorElementsPositionUpdates();

  // Mock timestamp for navigation start used for testing.
  std::optional<base::TimeTicks> mock_navigation_start_for_testing_;

  // `anchor_elements_to_report_` and `removed_anchors_to_report_` store anchor
  // insertions and removals that have happened since the last layout. Upon
  // layout, they will be used to populate `metrics_` and
  // `metrics_removed_anchors_`.
  // Use WeakMember to make sure we don't leak memory on long-lived pages.
  HeapHashSet<WeakMember<HTMLAnchorElement>> anchor_elements_to_report_;
  WTF::Vector<AnchorId> removed_anchors_to_report_;

  // `metrics_` and `metrics_removed_anchors_` buffer metrics updates that are
  // scheduled to be sent to the browser.
  WTF::Vector<mojom::blink::AnchorElementMetricsPtr> metrics_;
  WTF::Vector<AnchorId> metrics_removed_anchors_;
  // Contains the sizes of `metrics_` and `metrics_removed_anchors_`,
  // respectively, at the completion of each layout.
  //
  // This allows buffering the outcomes of potentially multiple layouts
  // before reporting to the browser, while still representing the
  // coherent state of each. For example, if this contains:
  //   [(x0, y0), (x1, y1), (x2, y2)]
  // Then this conceptually contains the following updates:
  //   metrics_[0..x0],  metrics_removed_anchors_[0..y0]
  //   metrics_[x0..x1], metrics_removed_anchors_[y0..y1]
  //   metrics_[x1..x2], metrics_removed_anchors_[y1..y2]
  // This data is consolidated into a single report representing the
  // net change before reporting to the browser.
  WTF::Vector<std::pair<wtf_size_t, wtf_size_t>> metrics_partitions_;

  HeapMojoRemote<mojom::blink::AnchorElementMetricsHost> metrics_host_;

  // Used to limit the rate at which update IPCs are sent by UpdateMetrics.
  HeapTaskRunnerTimer<AnchorElementMetricsSender> update_timer_;
  // If `should_skip_update_delays_for_testing_` becomes true, the rate limiting
  // is no longer done.
  bool should_skip_update_delays_for_testing_ = false;

  const int random_anchor_sampling_period_;

  Member<IntersectionObserver> intersection_observer_;
  HeapHashSet<WeakMember<const HTMLAnchorElement>> anchors_in_viewport_;

  WTF::Vector<mojom::blink::AnchorElementEnteredViewportPtr>
      entered_viewport_messages_;

  struct AnchorElementTimingStats {
    bool entered_viewport_should_be_enqueued_{true};
    std::optional<base::TimeTicks> viewport_entry_time_;
    std::optional<base::TimeTicks> pointer_over_timer_;
  };
  WTF::HashMap<AnchorId, AnchorElementTimingStats>
      anchor_elements_timing_stats_;

  WTF::Vector<mojom::blink::AnchorElementLeftViewportPtr>
      left_viewport_messages_;

  WTF::Vector<mojom::blink::AnchorElementPositionUpdatePtr>
      position_update_messages_;

  WTF::Vector<mojom::blink::AnchorElementClickPtr> clicked_messages_;

  const base::TickClock* clock_;

  bool is_registered_for_lifecycle_notifications_ = false;

  bool intersection_observer_limit_exceeded_ = false;

  // The y-coordinate of the last pointerdown (in screen coordinates) reported
  // by AnchorElementInteractionTracker in DIPs. Used to compute
  // |position_update_messages_|. The value is reset after metrics are computed.
  std::optional<float> last_pointer_down_ = std::nullopt;
  // Indicates that we should populate |position_update_messages_| in
  // |DidFinishLifecycleUpdate|.
  bool should_compute_positions_after_next_lifecycle_update_ = false;
  // Used to timeout waiting for |UpdateVisibleAnchors| being called after
  // |MaybeReportAnchorElementsPositionOnScrollEnd| is called. The timer is
  // stopped when |UpdateVisibleAnchors| is called.
  HeapTaskRunnerTimer<AnchorElementMetricsSender> position_update_timer_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_HTML_ANCHOR_ELEMENT_METRICS_SENDER_H_
