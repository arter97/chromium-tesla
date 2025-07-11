// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_NAVIGATION_TRANSITIONS_BACK_FORWARD_TRANSITION_ANIMATION_MANAGER_ANDROID_H_
#define CONTENT_BROWSER_NAVIGATION_TRANSITIONS_BACK_FORWARD_TRANSITION_ANIMATION_MANAGER_ANDROID_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "content/browser/navigation_transitions/back_forward_transition_animator.h"
#include "content/public/browser/back_forward_transition_animation_manager.h"

namespace ui {
class BackGestureEvent;
}

namespace content {

class NavigationControllerImpl;
class NavigationRequest;
class WebContentsViewAndroid;
class RenderFrameHostImpl;

// A wrapper class that forwards the gesture event APIs to the `animator_`. It
// limits the APIs explosed to the embedder. Owned by `WebContentsViewAndroid`.
//
// If for some reason the history navigation couldn't be animated, this class
// won't create an `animator_`, and will start the history navigation via the
// `NavigationController`.
// TODO(crbug.com/40260440): We should always animate a gesture history
// navigation.
class CONTENT_EXPORT BackForwardTransitionAnimationManagerAndroid
    : public BackForwardTransitionAnimationManager {
 public:
  BackForwardTransitionAnimationManagerAndroid(
      WebContentsViewAndroid* web_contents_view_android,
      NavigationControllerImpl* navigation_controller);
  BackForwardTransitionAnimationManagerAndroid(
      const BackForwardTransitionAnimationManagerAndroid&) = delete;
  BackForwardTransitionAnimationManagerAndroid& operator=(
      const BackForwardTransitionAnimationManagerAndroid&) = delete;
  ~BackForwardTransitionAnimationManagerAndroid() override;

  // `NavigationTransitionAnimationManager`:
  void OnGestureStarted(const ui::BackGestureEvent& gesture,
                        ui::BackGestureEventSwipeEdge edge,
                        NavigationDirection navigation_direction) override;
  void OnGestureProgressed(const ui::BackGestureEvent& gesture) override;
  void OnGestureCancelled() override;
  void OnGestureInvoked() override;
  void OnContentForNavigationEntryShown() override;
  AnimationStage GetCurrentAnimationStage() override;

  // This is called before the `old_host` is swapped out and before the
  // `new_host` is swapped in.
  //
  // Note:
  // 1. This API won't get called if the navigation does not commit
  //    (204/205/Download), or the navigation is cancelled (aborted by the user)
  //    or replaced (by another browser-initiated navigation). We use
  //    `DidFinishNavigation()` for those cases.
  // 2. `old_host` might be the same as `new_host`. This can only happen for
  //    navigating away from a crashed frame (early-swap), or for same-RFH
  //    navigations.
  //
  // TODO(crbug.com/41487964): This also won't work for the initial
  // navigation away from "about:blank". We might be able to treat this
  // navigation as a same-doc one.
  //
  // TODO(crbug.com/40615943): Check the status of RD when it is close to
  // launch. Without RD we need to make sure no frames from the old document is
  // associated with the updated LocalSurfaceId (https://crbug.com/1445976).
  void OnDidNavigatePrimaryMainFramePreCommit(
      NavigationRequest* navigation_request,
      RenderFrameHostImpl* old_host,
      RenderFrameHostImpl* new_host);

  // Notified when a unstarted navigation request is destroyed.
  void OnNavigationCancelledBeforeStart(NavigationHandle* navigation_handle);

  // `animator_` invokes this callback to destroy itself, when all the animation
  // has finished in the browser UI. Also use this to abort processing the
  // gesture when an unrelated navigation occurs during the animation.
  void SynchronouslyDestroyAnimator();

  // `animator_` invokes this callback to notify the state changes of the
  // current animation.
  void OnAnimationStageChanged();

  WebContentsViewAndroid* web_contents_view_android() const {
    return web_contents_view_android_;
  }

  NavigationControllerImpl* navigation_controller() const {
    return navigation_controller_;
  }

  void set_animator_factory_for_testing(
      std::unique_ptr<BackForwardTransitionAnimator::Factory> factory) {
    animator_factory_ = std::move(factory);
  }

 private:
  // The browser test needs to access the test-only `animator_`.
  friend class BackForwardTransitionAnimationManagerBrowserTest;

  // The owning `WebContentsViewAndroid`. Guaranteed to outlive `this`.
  const raw_ptr<WebContentsViewAndroid> web_contents_view_android_;

  // The navigation controller of the primary `FrameTree` of this `WebContents`.
  // Its life time is bound to this `WebContents`, thus guaranteed to outlive
  // this manager.
  const raw_ptr<NavigationControllerImpl> navigation_controller_;

  // The index of the destination entry in the history list. Set when the
  // embedder notifies the animation manager upon a gesture's start. This is
  // used to ensure the navigation is initiated at gesture end, even if the
  // animation had to be terminated sooner.
  //
  // Use an index instead of an offset, in case during the animated transition
  // the session history is updated (e.g., history.pushState()) and we don't
  // want to lead the user to the wrong entry.
  int destination_entry_index_ = -1;

  // The actual implementation of the animation manager that manages the history
  // navigation animation. One instance per gesture.
  //
  // Only instantiated if the user gesture will trigger an animated session
  // history preview. Created when the eligible `OnGestureStarted()` arrives,
  // and destroyed when `SynchronouslyDestroyAnimator()` is called (either when
  // the animations finished or animations have to aborted).
  //
  // `animator_` is only instantiated via `animator_factory_`. Tests can
  // override the `animator_factory_` via `set_animator_factory_for_testing()`.
  std::unique_ptr<BackForwardTransitionAnimator> animator_;
  std::unique_ptr<BackForwardTransitionAnimator::Factory> animator_factory_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_NAVIGATION_TRANSITIONS_BACK_FORWARD_TRANSITION_ANIMATION_MANAGER_ANDROID_H_
