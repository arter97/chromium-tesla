// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_AI_EXCEPTION_HELPERS_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_AI_EXCEPTION_HELPERS_H_

#include "third_party/blink/public/mojom/ai/ai_text_session.mojom-blink-forward.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"

namespace blink {

using mojom::blink::ModelStreamingResponseStatus;

extern const char kExceptionMessageSessionDestroyed[];
extern const char kExceptionMessageInvalidTemperatureAndTopKFormat[];
extern const char kExceptionMessageUnableToCreateSession[];

void ThrowInvalidContextException(ExceptionState& exception_state);

void RejectPromiseWithInternalError(ScriptPromiseResolverBase* resolver);

DOMException* ConvertModelStreamingResponseErrorToDOMException(
    ModelStreamingResponseStatus error);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_AI_EXCEPTION_HELPERS_H_
