// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/saved_tab_groups/types.h"

namespace tab_groups {

EventDetails::EventDetails(TabGroupEvent event_type) : event_type(event_type) {}
EventDetails::~EventDetails() = default;

EventDetails::EventDetails(const EventDetails& other) = default;
EventDetails& EventDetails::operator=(const EventDetails& other) = default;

}  // namespace tab_groups
