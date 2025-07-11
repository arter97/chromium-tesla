// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/navigation_transitions/navigation_entry_screenshot.h"

#include "content/browser/renderer_host/navigation_transitions/navigation_entry_screenshot_cache.h"

namespace content {

// static
const void* const NavigationEntryScreenshot::kUserDataKey =
    &NavigationEntryScreenshot::kUserDataKey;

NavigationEntryScreenshot::NavigationEntryScreenshot(
    const SkBitmap& bitmap,
    int navigation_entry_id,
    bool is_copied_from_embedder)
    : bitmap_(cc::UIResourceBitmap(bitmap)),
      navigation_entry_id_(navigation_entry_id),
      is_copied_from_embedder_(is_copied_from_embedder) {
  CHECK(AreBackForwardTransitionsEnabled());
}

NavigationEntryScreenshot::~NavigationEntryScreenshot() {
  if (cache_) {
    cache_->OnNavigationEntryGone(navigation_entry_id_, SizeInBytes());
  }
}

cc::UIResourceBitmap NavigationEntryScreenshot::GetBitmap(cc::UIResourceId uid,
                                                          bool resource_lost) {
  // TODO(liuwilliam): Currently none of the impls of `GetBitmap` uses `uid` or
  // `resource_lost`. Consider deleting them from the interface.
  return bitmap_;
}

gfx::Size NavigationEntryScreenshot::GetDimensions() const {
  return bitmap_.GetSize();
}

size_t NavigationEntryScreenshot::SizeInBytes() const {
  return bitmap_.SizeInBytes();
}

SkBitmap NavigationEntryScreenshot::GetBitmapForTesting() const {
  return bitmap_.GetBitmapForTesting();  // IN-TEST
}

}  // namespace content
