// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_IME_WIN_TSF_INPUT_SCOPE_H_
#define UI_BASE_IME_WIN_TSF_INPUT_SCOPE_H_

#include <InputScope.h>
#include <vector>

#include "base/component_export.h"
#include "base/win/windows_types.h"
#include "ui/base/ime/text_input_mode.h"
#include "ui/base/ime/text_input_type.h"

namespace ui {
namespace tsf_inputscope {

// Returns InputScope list corresoponding to ui::TextInputType and
// ui::TextInputMode.
// This function is only used from following functions but declared for test.
COMPONENT_EXPORT(UI_BASE_IME_WIN)
std::vector<InputScope> GetInputScopes(TextInputType text_input_type,
                                       TextInputMode text_input_mode);

// Returns an instance of ITfInputScope, which is the Windows-specific
// category representation corresponding to ui::TextInputType and
// ui::TextInputMode that we are using to specify the expected text type
// in the target field.
// The returned instance has 0 reference count. The caller must maintain its
// reference count.
COMPONENT_EXPORT(UI_BASE_IME_WIN)
ITfInputScope* CreateInputScope(TextInputType text_input_type,
                                TextInputMode text_input_mode,
                                bool should_do_learning);

// A wrapper of the SetInputScope API exported by msctf.dll.
COMPONENT_EXPORT(UI_BASE_IME_WIN)
void SetPrivateInputScope(HWND window_handle);

}  // namespace tsf_inputscope
}  // namespace ui

#endif  // UI_BASE_IME_WIN_TSF_INPUT_SCOPE_H_
