// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_PICKER_MODEL_PICKER_MODE_TYPE_H_
#define ASH_PICKER_MODEL_PICKER_MODE_TYPE_H_

#include "ash/ash_export.h"

namespace ash {

enum class PickerModeType {
  // Triggered without focusing on an input field.
  kUnfocused,
  // Triggered with focus, but no selected text.
  kNoSelection,
  // Triggered with focus and selected text.
  kHasSelection
};

}

#endif  // ASH_PICKER_MODEL_PICKER_MODE_TYPE_H_
