// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <numeric>

#include "third_party/blink/renderer/modules/ml/webnn/ml_graph_utils.h"

#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_gemm_options.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_typed_array.h"
#include "third_party/blink/renderer/modules/ml/webnn/ml_operand.h"
#include "third_party/blink/renderer/modules/ml/webnn/ml_operator.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_deque.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_set.h"

namespace blink {

namespace {

using NamedArrayBufferViewsInfo =
    Vector<std::pair<String, ArrayBufferViewInfo>>;

}  // namespace

HeapVector<Member<const MLOperator>>* GetOperatorsInTopologicalOrder(
    const MLNamedOperands& named_outputs) {
  // A WebNN graph is represented by a directed acyclic graph (DAG) that has
  // operators as vertices and operand as edges. The topological sorting is
  // implemented by depth-first search (DFS) and visiting vertices in
  // post-order. It means a vertex (operator) is visited (pushed to the back of
  // the sorted list) after all its dependent vertices (operators) are visited.
  // With that, it ensures operator 'j' appears before operator 'i' in the
  // result, if 'i' depends on 'j'. The DFS algorithm is based on the
  // non-recursive implementation of:
  // https://en.wikipedia.org/wiki/Depth-first_search

  // The topologically sorted operators.
  auto* toposorted_operators =
      MakeGarbageCollected<HeapVector<Member<const MLOperator>>>();

  // The to-visit stack and visited set for DFS graph traversal.
  HeapDeque<Member<const MLOperator>> operators_to_visit;
  HeapHashSet<Member<const MLOperator>> visited_operators;
  // Enumerate output operands and initialize the to-visit stack with their
  // dependent operators.
  for (const auto& output : named_outputs) {
    const auto* operand = output.second.Get();
    operators_to_visit.push_back(operand->Operator());
  }
  while (operators_to_visit.size() > 0) {
    // Get the current operator from the top of the to-visit stack.
    const auto& current_operator = operators_to_visit.back();
    if (!visited_operators.Contains(current_operator.Get())) {
      // The current operator is not visited, check whether its dependent
      // operators are visited or not.
      bool skip_visit = false;
      for (const auto& operand : current_operator->Inputs()) {
        if (operand->Kind() == webnn::mojom::blink::Operand::Kind::kOutput) {
          const auto* dependent_operator = operand->Operator();
          CHECK(dependent_operator);
          if (!visited_operators.Contains(dependent_operator)) {
            // As there is an dependent operator is not visited, skip visiting
            // this operator and push the dependent operator into the to-visit
            // stack.
            skip_visit = true;
            operators_to_visit.push_back(dependent_operator);
          }
        }
      }
      if (!skip_visit) {
        // When all dependent operators have been visited, visit the current
        // operator and add it into the visited set.
        toposorted_operators->push_back(current_operator);
        visited_operators.insert(current_operator);
        // Pop the current operator from the to-visit stack.
        operators_to_visit.pop_back();
      }
    } else {
      // The current operator is already visited, pop it and check the next
      // one.
      operators_to_visit.pop_back();
    }
  }
  return toposorted_operators;
}

std::optional<ArrayBufferViewInfo> TransferArrayBufferView(
    v8::Isolate* isolate,
    NotShared<DOMArrayBufferView> source_view,
    ExceptionState& exception_state) {
  // Need to check whether each `ArrayBufferView` of `NamedArrayBufferViews` is
  // detached because transferring an `ArrayBuffer` would impact all
  // `ArrayBufferView`s sharing the same `ArrayBuffer`.
  if (source_view->IsDetached()) {
    exception_state.ThrowTypeError("The ArrayBuffer is detached.");
    return std::nullopt;
  }

  // Avoid transferring a non-detachable ArrayBuffer.
  // `DOMArrayBuffer::Transfer()` would make a copy if the ArrayBuffer is not
  // detachable. This behavior doesn't follow the algorithm to transfer an
  // ArrayBuffer of WebIDL spec:
  // https://webidl.spec.whatwg.org/#arraybuffer-transfer
  if (!source_view->buffer()->IsDetachable(isolate)) {
    exception_state.ThrowTypeError("The ArrayBuffer is not detachable.");
    return std::nullopt;
  }

  // Get the offset and length of the source view before transferring it.
  ArrayBufferViewInfo view_info;
  view_info.type = source_view->GetType();
  view_info.offset = source_view->byteOffset();
  view_info.length = source_view->byteLength() / source_view->TypeSize();

  ArrayBufferContents contents;
  // The following `DOMArrayBuffer::Transfer()` call would fail if the
  // detach key of the ArrayBuffer is not `undefined`.
  if (!source_view->buffer()->Transfer(isolate, view_info.contents,
                                       exception_state)) {
    return std::nullopt;
  }

  return view_info;
}

DOMArrayBufferView* CreateArrayBufferView(ArrayBufferViewInfo view_info) {
  auto* target_buffer = DOMArrayBuffer::Create(std::move(view_info.contents));

  // Align with the ArrayBufferView types supported by WebNN MLOperandDataType:
  // https://www.w3.org/TR/webnn/#appendices-MLOperandDataType-arraybufferview-compatibility
  DOMArrayBufferView* target_view = nullptr;
  switch (view_info.type) {
    case DOMArrayBufferView::kTypeFloat32:
      // Float32Array is used for MLOperandDataType::float32.
      target_view = DOMFloat32Array::Create(target_buffer, view_info.offset,
                                            view_info.length);
      break;
    case DOMArrayBufferView::kTypeUint16:
      // Using Uint16Array for float16 is a workaround of WebNN spec issue:
      // https://github.com/webmachinelearning/webnn/issues/127
      target_view = DOMUint16Array::Create(target_buffer, view_info.offset,
                                           view_info.length);
      break;
    case DOMArrayBufferView::kTypeInt32:
      // Int32Array is used for MLOperandDataType::int32.
      target_view = DOMInt32Array::Create(target_buffer, view_info.offset,
                                          view_info.length);
      break;
    case DOMArrayBufferView::kTypeUint32:
      // Uint32Array is used for MLOperandDataType::uint32.
      target_view = DOMUint32Array::Create(target_buffer, view_info.offset,
                                           view_info.length);
      break;
    case DOMArrayBufferView::kTypeBigInt64:
      // BigInt64Array is used for MLOperandDataType::int64.
      target_view = DOMBigInt64Array::Create(target_buffer, view_info.offset,
                                             view_info.length);
      break;
    case DOMArrayBufferView::kTypeBigUint64:
      // BigUint64Array is used for MLOperandDataType::uint64.
      target_view = DOMBigUint64Array::Create(target_buffer, view_info.offset,
                                              view_info.length);
      break;
    case DOMArrayBufferView::kTypeInt8:
      // Int8Array is used for MLOperandDataType::int8.
      target_view = DOMInt8Array::Create(target_buffer, view_info.offset,
                                         view_info.length);
      break;
    case DOMArrayBufferView::kTypeUint8:
      // Uint8Array is used for MLOperandDataType::uint8.
      target_view = DOMUint8Array::Create(target_buffer, view_info.offset,
                                          view_info.length);
      break;
    default:
      // Other ArrayBufferView types should not pass the
      // `ValidateNamedArrayBufferViews()` and reach here.
      NOTREACHED_NORETURN();
  }
  return target_view;
}

std::unique_ptr<NamedArrayBufferViewsInfo> TransferNamedArrayBufferViews(
    v8::Isolate* isolate,
    const MLNamedArrayBufferViews& source_views,
    ExceptionState& exception_state) {
  auto views_info = std::make_unique<NamedArrayBufferViewsInfo>();
  views_info->reserve(source_views.size());
  for (const auto& [name, source_view] : source_views) {
    auto view_info =
        TransferArrayBufferView(isolate, source_view, exception_state);
    if (!view_info) {
      return nullptr;
    }
    views_info->push_back(std::make_pair(name, std::move(view_info.value())));
  }
  return views_info;
}

MLNamedArrayBufferViews* CreateNamedArrayBufferViews(
    std::unique_ptr<NamedArrayBufferViewsInfo> views_info) {
  auto* target_views = MakeGarbageCollected<MLNamedArrayBufferViews>();
  target_views->reserve(views_info->size());
  for (auto& [name, view_info] : *views_info) {
    target_views->push_back(
        std::make_pair(name, CreateArrayBufferView(std::move(view_info))));
  }
  return target_views;
}

Vector<uint32_t> CreateDefaultPermutation(const wtf_size_t rank) {
  Vector<uint32_t> default_permutation(rank);
  for (wtf_size_t i = 0; i < rank; ++i) {
    default_permutation[i] = rank - 1 - i;
  }
  return default_permutation;
}

Vector<uint32_t> CreateAllAxes(const wtf_size_t rank) {
  Vector<uint32_t> default_axes(rank);
  std::iota(default_axes.begin(), default_axes.end(), 0);
  return default_axes;
}

Vector<uint32_t> CreateLayerNormalizationDefaultAxes(const wtf_size_t rank) {
  Vector<uint32_t> default_axes;
  if (rank > 1) {
    default_axes.resize(rank - 1);
    std::iota(default_axes.begin(), default_axes.end(), 1);
  }
  return default_axes;
}

base::expected<void, String> ValidateFilterLayout(
    bool depthwise,
    V8MLInputOperandLayout input_layout,
    V8MLConv2dFilterOperandLayout filter_layout) {
  CHECK(input_layout.AsEnum() == V8MLInputOperandLayout::Enum::kNhwc);

  if (!depthwise) {
    // For regular conv2d, NHWC input layout expects weights layout in ohwi that
    // is [groups * group_output_channels, kernel_height, kernel_width,
    // group_input_channels].
    //
    // TODO(crbug.com/1273291): support other layouts by transposing the
    // filter operand.
    if (filter_layout.AsEnum() != V8MLConv2dFilterOperandLayout::Enum::kOhwi) {
      return base::unexpected(String::Format(
          "The filter layout %s is not supported.", filter_layout.AsCStr()));
    }
  } else {
    // For depthwise conv2d, NHWC input layout expects weights layout in ihwo
    // that is [1, kernel_height, kernel_width, input_channels *
    // depth_multiplier].
    //
    // TODO(crbug.com/1273291): support other layouts by transposing the
    // filter operand.
    if (filter_layout.AsEnum() != V8MLConv2dFilterOperandLayout::Enum::kIhwo) {
      return base::unexpected(String::Format(
          "The filter layout %s is not supported.", filter_layout.AsCStr()));
    }
  }

  return base::ok();
}

webnn::Size2d<uint32_t> CalculateConvTransposeOutputSize2D(
    const blink::MLConvTranspose2dOptions* options,
    uint32_t input_height,
    uint32_t input_width,
    uint32_t filter_height,
    uint32_t filter_width,
    uint32_t stride_height,
    uint32_t stride_width,
    uint32_t dilation_height,
    uint32_t dilation_width,
    uint32_t output_padding_height,
    uint32_t output_padding_width) {
  // Set the padding from WebNN explicit padding that is in
  // [beginning_height, ending_height, beginning_width, ending_width],
  // default to 0.
  auto ml_padding = options->getPaddingOr({0, 0, 0, 0});
  CHECK_EQ(ml_padding.size(), 4u);
  const webnn::Padding2d padding{
      .beginning = webnn::Size2d<uint32_t>{.height = ml_padding[0],
                                           .width = ml_padding[2]},
      .ending = webnn::Size2d<uint32_t>{.height = ml_padding[1],
                                        .width = ml_padding[3]}};
  const auto output_height = webnn::CalculateConvTranspose2dOutputSize(
      input_height, filter_height, padding.beginning.height,
      padding.ending.height, stride_height, dilation_height,
      output_padding_height);
  CHECK(output_height.has_value());

  const auto output_width = webnn::CalculateConvTranspose2dOutputSize(
      input_width, filter_width, padding.beginning.width, padding.ending.width,
      stride_width, dilation_width, output_padding_width);
  CHECK(output_width.has_value());

  return webnn::Size2d<uint32_t>{.height = output_height.value(),
                                 .width = output_width.value()};
}

}  // namespace blink
