// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/desks/window_occlusion_calculator.h"

#include <memory>
#include <utility>

#include "base/check.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "ui/aura/window_occlusion_change_builder.h"
#include "ui/aura/window_tracker.h"

namespace ash {

namespace {

void NotifyObserversOfOcclusionChange(
    base::ObserverList<WindowOcclusionCalculator::Observer>& observers,
    aura::Window* window) {
  for (WindowOcclusionCalculator::Observer& obs : observers) {
    obs.OnWindowOcclusionChanged(window);
  }
}

}  // namespace

// Called by the `aura::WindowOcclusionTracker` whenever an occlusion state is
// calculated for a window. This simply relays that occlusion state back to the
// `WindowOcclusionCalculator`.
class WindowOcclusionCalculator::WindowOcclusionChangeBuilderImpl
    : public aura::WindowOcclusionChangeBuilder {
 public:
  explicit WindowOcclusionChangeBuilderImpl(
      base::WeakPtr<WindowOcclusionCalculator> occlusion_calculator)
      : occlusion_calculator_(occlusion_calculator) {}
  WindowOcclusionChangeBuilderImpl(const WindowOcclusionChangeBuilderImpl&) =
      delete;
  WindowOcclusionChangeBuilderImpl& operator=(
      const WindowOcclusionChangeBuilderImpl&) = delete;
  ~WindowOcclusionChangeBuilderImpl() override {
    if (!occlusion_calculator_) {
      return;
    }
    // `aura::WindowOcclusionChangeBuilder` is a short-lived class.
    // `aura::WindowOcclusionTracker` creates a new one whenever it recomputes
    // occlusion state, and then destroys it when the computation is done.
    //
    // During the computation, it's technically possible for
    // `aura::WindowOcclusionTracker` to set a window's occlusion state
    // incrementally during the calculation with transient values (i.e. `Add()`
    // can get called multiple times for the same window). Therefore, it's more
    // efficient to cache each window's most recent occlusion state within
    // `Add()` and flush them all here in the destructor with the occlusion
    // computation's final values. This prevents transient occlusion states made
    // during the computation from unnecessarily getting propagated.
    for (const auto& [window, occlusion_state] : occlusion_changes_) {
      // Corner case: Window might be destroyed from the time it was `Add()`ed
      // until now. If so, don't set occlusion state for a dead window.
      if (window_tracker_.Contains(window)) {
        occlusion_calculator_->SetOcclusionState(window, occlusion_state);
      }
    }
  }

  // aura::WindowOcclusionChangeBuilder:
  void Add(aura::Window* window,
           aura::Window::OcclusionState occlusion_state,
           SkRegion occluded_region) override {
    CHECK_NE(occlusion_state, aura::Window::OcclusionState::UNKNOWN);
    occlusion_changes_[window] = occlusion_state;
    window_tracker_.Add(window);
  }

 private:
  const base::WeakPtr<WindowOcclusionCalculator> occlusion_calculator_;
  WindowOcclusionCalculator::WindowOcclusionMap occlusion_changes_;
  aura::WindowTracker window_tracker_;
};

// One per parent window being observed.
class WindowOcclusionCalculator::ObservationState {
 public:
  using Observer = WindowOcclusionCalculator::Observer;

  ObservationState(aura::Window* parent_window,
                   aura::WindowOcclusionTracker* occlusion_tracker)
      : forced_visibility_(parent_window, occlusion_tracker) {}
  ObservationState(const ObservationState&) = delete;
  ObservationState& operator=(const ObservationState&) = delete;
  ~ObservationState() = default;

  base::ObserverList<Observer>& observer_list() { return observer_list_; }

 private:
  base::ObserverList<Observer> observer_list_;
  // All parent windows being observed are forced visible. This requirement is
  // specific to desk bar usage. The parent windows (and all their descendants)
  // for inactive desks are `HIDDEN` by default, which is not useful for this
  // performance optimization. Forcing each desk's parent window to be visible
  // makes the occlusion state of that desk's windows available.
  aura::WindowOcclusionTracker::ScopedForceVisible forced_visibility_;
};

WindowOcclusionCalculator::WindowOcclusionCalculator() {
  occlusion_tracker_.set_occlusion_change_builder_factory(base::BindRepeating(
      [](base::WeakPtr<WindowOcclusionCalculator> occlusion_calculator)
          -> std::unique_ptr<aura::WindowOcclusionChangeBuilder> {
        return std::make_unique<WindowOcclusionChangeBuilderImpl>(
            std::move(occlusion_calculator));
      },
      // Using a `base::WeakPtr` and having the `weak_ptr_factory_` be the
      // first member destroyed prevents errant `SetOcclusionState()` calls
      // from happening when members are destroyed in the destructor. This can
      // cause use-after-free crashes as some members, or portions of members,
      // have already been destroyed in `SetOcclusionState()`.
      weak_ptr_factory_.GetWeakPtr()));
}

WindowOcclusionCalculator::~WindowOcclusionCalculator() = default;

aura::Window::OcclusionState WindowOcclusionCalculator::GetOcclusionState(
    aura::Window* window) const {
  auto iter = occlusion_map_.find(window);
  return iter == occlusion_map_.end() ? aura::Window::OcclusionState::UNKNOWN
                                      : iter->second;
}

void WindowOcclusionCalculator::AddObserver(
    const aura::Window::Windows& parent_windows_to_track,
    Observer* observer) {
  // The `ScopedPause` is an optimization. Each call to
  // `aura::WindowOcclusionTracker::Track()` triggers a traversal of the entire
  // window tree to recompute the occlusion, and
  // `aura::WindowOcclusionTracker::Track()` must be called for all
  // `parent_windows_to_track` plus each of their descendants (which can be a
  // lot of computation). Therefore, pause the `occlusion_tracker_` first so
  // that each added window does not trigger a recalculation. Then, when the
  // `ScopedPause` is destroyed, one occlusion calculation is triggered for all
  // tracked windows. This yields the same result but with only one window tree
  // traversal.
  {
    aura::WindowOcclusionTracker::ScopedPause occlusion_calculation_barrier(
        &occlusion_tracker_);
    for (const auto& parent_window : parent_windows_to_track) {
      // Adding an entry to `occlusion_change_observers_` at this point sets up
      // the forced visibility for the `parent_window`.
      occlusion_change_observers_.try_emplace(
          parent_window.get(), std::make_unique<ObservationState>(
                                   parent_window, &occlusion_tracker_));
      if (!tracked_window_observations_.IsObservingSource(parent_window)) {
        TrackOcclusionChangesForAllDescendants(parent_window.get());
      }
    }
  }
  // Add observer after the initial occlusion calculation is done. That ensures
  // the `observer` is not notified immediately of the initial occlusion state
  // (which is not necessary for the desk bar's use case).
  for (const auto& [parent_window, observation_state] :
       occlusion_change_observers_) {
    // The `observer` should only be notified of occlusion changes for the
    // windows it cares about (those in `parent_windows_to_track`).
    if (base::Contains(parent_windows_to_track, parent_window)) {
      observation_state->observer_list().AddObserver(observer);
    }
  }
}

void WindowOcclusionCalculator::RemoveObserver(Observer* observer) {
  aura::WindowOcclusionTracker::ScopedPause occlusion_calculation_barrier(
      &occlusion_tracker_);
  auto iter = occlusion_change_observers_.begin();
  while (iter != occlusion_change_observers_.end()) {
    ObservationState& observation_state = *iter->second;
    observation_state.observer_list().RemoveObserver(observer);
    if (observation_state.observer_list().empty()) {
      iter = occlusion_change_observers_.erase(iter);
    } else {
      ++iter;
    }
  }
}

// Note `aura::WindowOcclusionTracker` automatically updates all of its
// bookkeeping internally when a tracked window gets destroyed. So this only
// has to do bookkeeping for `WindowOcclusionCalculator` specific things.
void WindowOcclusionCalculator::OnWindowDestroyed(aura::Window* window) {
  tracked_window_observations_.RemoveObservation(window);
  // Erasure makes `GetOcclusionState()` return `UNKNOWN` if the caller happens
  // to request the occlusion state of a window that was being tracked
  // but got destroyed.
  occlusion_map_.erase(window);
  // Prevents use-after-free in `SetOcclusionState()` when iterating through
  // `occlusion_change_observers_`.
  occlusion_change_observers_.erase(window);
}

void WindowOcclusionCalculator::SetOcclusionState(
    aura::Window* window,
    aura::Window::OcclusionState occlusion_state) {
  // Update the `occlusion_map_` before notifying observers of the change in
  // case the observer immediately calls `GetOcclusionState()` for the changed
  // window.
  auto [iter, inserted] = occlusion_map_.try_emplace(window, occlusion_state);
  if (!inserted && iter->second == occlusion_state) {
    return;
  }
  iter->second = occlusion_state;
  DVLOG(4) << __func__ << " window=" << window->GetName() << " occlusion="
           << aura::Window::OcclusionStateToString(occlusion_state);

  // Note the API allows for multiple observers for the same occlusion change,
  // so iterate completely through `occlusion_change_observers_` without fast
  // returning or breaking.
  for (const auto& [parent_window, observation_state] :
       occlusion_change_observers_) {
    if (parent_window->Contains(window)) {
      NotifyObserversOfOcclusionChange(observation_state->observer_list(),
                                       window);
    }
  }
}

void WindowOcclusionCalculator::TrackOcclusionChangesForAllDescendants(
    aura::Window* window) {
  CHECK(!tracked_window_observations_.IsObservingSource(window));
  tracked_window_observations_.AddObservation(window);
  occlusion_tracker_.Track(window);
  // Order in which children are tracked does not matter here.
  for (const auto& child : window->children()) {
    TrackOcclusionChangesForAllDescendants(child.get());
  }
}

}  // namespace ash
