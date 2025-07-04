// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/ml/webnn/ml_graph_type_converter.h"

#include <array>

#include "base/notreached.h"
#include "base/ranges/algorithm.h"
#include "base/types/expected_macros.h"
#include "components/ml/webnn/graph_validation_utils.h"
#include "services/webnn/public/mojom/webnn_graph.mojom-blink-forward.h"
#include "services/webnn/public/mojom/webnn_graph.mojom-blink.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_arg_min_max_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_batch_normalization_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_clamp_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_conv_2d_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_conv_transpose_2d_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_elu_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_gather_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_gemm_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_gru_cell_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_gru_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_hard_sigmoid_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_input_operand_layout.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_instance_normalization_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_layer_normalization_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_leaky_relu_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_linear_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_lstm_cell_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_lstm_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_pad_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_pool_2d_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_reduce_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_resample_2d_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_split_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_transpose_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ml_triangular_options.h"
#include "third_party/blink/renderer/modules/ml/webnn/ml_activation.h"
#include "third_party/blink/renderer/modules/ml/webnn/ml_graph_utils.h"
#include "third_party/blink/renderer/modules/ml/webnn/ml_operand.h"
#include "third_party/blink/renderer/modules/ml/webnn/ml_operator.h"

namespace blink_mojom = webnn::mojom::blink;

namespace mojo {

blink_mojom::Operand::DataType BlinkOperandTypeToMojo(
    blink::V8MLOperandDataType::Enum data_type) {
  switch (data_type) {
    case blink::V8MLOperandDataType::Enum::kFloat32:
      return blink_mojom::Operand::DataType::kFloat32;
    case blink::V8MLOperandDataType::Enum::kFloat16:
      return blink_mojom::Operand::DataType::kFloat16;
    case blink::V8MLOperandDataType::Enum::kInt32:
      return blink_mojom::Operand::DataType::kInt32;
    case blink::V8MLOperandDataType::Enum::kUint32:
      return blink_mojom::Operand::DataType::kUint32;
    case blink::V8MLOperandDataType::Enum::kInt64:
      return blink_mojom::Operand::DataType::kInt64;
    case blink::V8MLOperandDataType::Enum::kUint64:
      return blink_mojom::Operand::DataType::kUint64;
    case blink::V8MLOperandDataType::Enum::kInt8:
      return blink_mojom::Operand::DataType::kInt8;
    case blink::V8MLOperandDataType::Enum::kUint8:
      return blink_mojom::Operand::DataType::kUint8;
  }
  NOTREACHED_NORETURN();
}

blink_mojom::RecurrentNetworkDirection BlinkRecurrentNetworkDirectionToMojo(
    blink::V8MLRecurrentNetworkDirection::Enum direction) {
  switch (direction) {
    case blink::V8MLRecurrentNetworkDirection::Enum::kForward:
      return blink_mojom::RecurrentNetworkDirection::kForward;
    case blink::V8MLRecurrentNetworkDirection::Enum::kBackward:
      return blink_mojom::RecurrentNetworkDirection::kBackward;
    case blink::V8MLRecurrentNetworkDirection::Enum::kBoth:
      return blink_mojom::RecurrentNetworkDirection::kBoth;
  }
}

blink_mojom::LstmWeightLayout BlinkLstmWeightLayoutToMojo(
    blink::V8MLLstmWeightLayout::Enum layout) {
  switch (layout) {
    case blink::V8MLLstmWeightLayout::Enum::kIofg:
      return blink_mojom::LstmWeightLayout::kIofg;
    case blink::V8MLLstmWeightLayout::Enum::kIfgo:
      return blink_mojom::LstmWeightLayout::kIfgo;
  }
}

blink_mojom::GruWeightLayout BlinkGruWeightLayoutToMojo(
    blink::V8MLGruWeightLayout::Enum layout) {
  switch (layout) {
    case blink::V8MLGruWeightLayout::Enum::kZrn:
      return blink_mojom::GruWeightLayout::kZrn;
    case blink::V8MLGruWeightLayout::Enum::kRzn:
      return blink_mojom::GruWeightLayout::kRzn;
  }
}

// Converters from IDL to Mojo.
blink_mojom::OperandPtr
TypeConverter<blink_mojom::OperandPtr, blink::MLOperand*>::Convert(
    const blink::MLOperand* ml_operand) {
  if (!ml_operand) {
    return nullptr;
  }
  auto mojo_operand = blink_mojom::Operand::New();
  switch (ml_operand->Kind()) {
    case webnn::mojom::blink::Operand::Kind::kInput:
      mojo_operand->kind = blink_mojom::Operand::Kind::kInput;
      mojo_operand->name = ml_operand->Name();
      break;
    case webnn::mojom::blink::Operand::Kind::kConstant:
      mojo_operand->kind = blink_mojom::Operand::Kind::kConstant;
      break;
    case webnn::mojom::blink::Operand::Kind::kOutput:
      mojo_operand->kind = blink_mojom::Operand::Kind::kOutput;
      break;
  }
  mojo_operand->data_type = BlinkOperandTypeToMojo(ml_operand->DataType());
  mojo_operand->dimensions = ml_operand->Dimensions();
  return mojo_operand;
}

// Get height and width of input operand.
webnn::Size2d<uint32_t> GetInputOperandSize2d(
    const blink::MLOperand* input,
    blink::V8MLInputOperandLayout::Enum type) {
  CHECK(input);
  const auto input_shape = input->Dimensions();
  CHECK_EQ(input_shape.size(), 4u);
  uint32_t input_height, input_width;
  switch (type) {
    case blink::V8MLInputOperandLayout::Enum::kNchw:
      // "nchw": [batches, channels, height, width]
      input_height = input_shape[2];
      input_width = input_shape[3];
      break;
    case blink::V8MLInputOperandLayout::Enum::kNhwc:
      // "nhwc": [batches, height, width, channels]
      input_height = input_shape[1];
      input_width = input_shape[2];
      break;
  }
  return {.height = input_height, .width = input_width};
}

}  // namespace mojo

namespace blink {

namespace {

using blink_mojom::ActivationPtr;
using blink_mojom::ElementWiseBinary;
using blink_mojom::ElementWiseUnary;
using blink_mojom::Operation;
using blink_mojom::OperationPtr;
using blink_mojom::Size2d;

// Maps MLOperand to its id which is used to identify the `mojo::Operand` across
// processes.
using OperandToIdMap = HeapHashMap<Member<const MLOperand>, uint64_t>;

uint64_t GetOperatorInputId(const MLOperator* op,
                            const OperandToIdMap& operand_to_id_map,
                            wtf_size_t index = 0) {
  CHECK_NE(op, nullptr);
  CHECK_LE(index, op->Inputs().size());
  const auto* input = op->Inputs()[index].Get();
  return operand_to_id_map.at(input);
}

uint64_t GetOperatorOutputId(const MLOperator* op,
                             const OperandToIdMap& operand_to_id_map,
                             wtf_size_t index = 0) {
  CHECK_NE(op, nullptr);
  CHECK_LE(index, op->Outputs().size());
  const auto* output = op->Outputs()[index].Get();
  return operand_to_id_map.at(output);
}

uint64_t InsertTemporaryOperand(const OperandToIdMap& operand_to_id_map,
                                V8MLOperandDataType data_type,
                                base::span<const uint32_t> dimensions,
                                blink_mojom::GraphInfo* graph_info) {
  uint64_t operand_id = NextOperandId(*graph_info);
  auto mojo_operand = blink_mojom::Operand::New();
  mojo_operand->kind = blink_mojom::Operand::Kind::kOutput;
  mojo_operand->data_type = mojo::BlinkOperandTypeToMojo(data_type.AsEnum());
  mojo_operand->dimensions = Vector<uint32_t>(dimensions);
  graph_info->id_to_operand_map.insert(operand_id, std::move(mojo_operand));
  return operand_id;
}

Vector<uint32_t> PermuteArray(const Vector<uint32_t>& array,
                              base::span<const uint32_t> permutation) {
  wtf_size_t array_size = array.size();
  Vector<uint32_t> permuted_array(array_size);

  CHECK_EQ(array_size, permutation.size());
  for (wtf_size_t i = 0; i < array_size; ++i) {
    permuted_array[i] = array[permutation[i]];
  }

  return permuted_array;
}

// Insert a transpose operation after the given operand. Returns the ID of the
// operand holding the transposed result.
uint64_t InsertInputTranspose(const OperandToIdMap& operand_to_id_map,
                              const MLOperand* operand,
                              base::span<const uint32_t> permutation,
                              blink_mojom::GraphInfo* graph_info) {
  uint64_t operand_id = InsertTemporaryOperand(
      operand_to_id_map, operand->dataType(),
      PermuteArray(operand->Dimensions(), permutation), graph_info);

  auto transpose = blink_mojom::Transpose::New();
  transpose->input_operand_id = operand_to_id_map.at(operand);
  transpose->output_operand_id = operand_id;
  transpose->permutation = Vector<uint32_t>(permutation);
  graph_info->operations.push_back(
      blink_mojom::Operation::NewTranspose(std::move(transpose)));

  return operand_id;
}

blink_mojom::ClampPtr CreateClamp(const OperandToIdMap& operand_to_id_map,
                                  const MLOperator* clamp,
                                  bool is_activation) {
  auto clamp_mojo = blink_mojom::Clamp::New();
  // Activation has no input or output operands.
  if (!is_activation) {
    clamp_mojo->input_operand_id = GetOperatorInputId(clamp, operand_to_id_map);
    clamp_mojo->output_operand_id =
        GetOperatorOutputId(clamp, operand_to_id_map);
  }

  const auto* options = static_cast<const MLClampOptions*>(clamp->Options());
  CHECK(options);
  clamp_mojo->min_value =
      options->getMinValueOr(-std::numeric_limits<float>::infinity());
  clamp_mojo->max_value =
      options->getMaxValueOr(+std::numeric_limits<float>::infinity());
  return clamp_mojo;
}

blink_mojom::EluPtr CreateElu(const OperandToIdMap& operand_to_id_map,
                              const MLOperator* elu,
                              bool is_activation) {
  auto elu_mojo = blink_mojom::Elu::New();
  // Activation has no input or output operands.
  if (!is_activation) {
    elu_mojo->input_operand_id = GetOperatorInputId(elu, operand_to_id_map);
    elu_mojo->output_operand_id = GetOperatorOutputId(elu, operand_to_id_map);
  }

  const auto* options = static_cast<const MLEluOptions*>(elu->Options());
  CHECK(options);
  elu_mojo->alpha = options->alpha();
  return elu_mojo;
}

blink_mojom::HardSigmoidPtr CreateHardSigmoid(
    const OperandToIdMap& operand_to_id_map,
    const MLOperator* hard_sigmoid,
    bool is_activation) {
  auto hard_sigmoid_mojo = blink_mojom::HardSigmoid::New();
  // Activation has no input or output operands.
  if (!is_activation) {
    hard_sigmoid_mojo->input_operand_id =
        GetOperatorInputId(hard_sigmoid, operand_to_id_map);
    hard_sigmoid_mojo->output_operand_id =
        GetOperatorOutputId(hard_sigmoid, operand_to_id_map);
  }

  const auto* options =
      static_cast<const MLHardSigmoidOptions*>(hard_sigmoid->Options());
  CHECK(options);
  hard_sigmoid_mojo->alpha = options->alpha();
  hard_sigmoid_mojo->beta = options->beta();
  return hard_sigmoid_mojo;
}

OperationPtr CreateExpandOperation(const OperandToIdMap& operand_to_id_map,
                                   const MLOperator* expand) {
  auto expand_mojo = blink_mojom::Expand::New();
  expand_mojo->input_operand_id = GetOperatorInputId(expand, operand_to_id_map);
  expand_mojo->output_operand_id =
      GetOperatorOutputId(expand, operand_to_id_map);
  return blink_mojom::Operation::NewExpand(std::move(expand_mojo));
}

blink_mojom::LeakyReluPtr CreateLeakyRelu(
    const OperandToIdMap& operand_to_id_map,
    const MLOperator* leaky_relu,
    bool is_activation) {
  auto leaky_relu_mojo = blink_mojom::LeakyRelu::New();
  // Activation has no input or output operands.
  if (!is_activation) {
    leaky_relu_mojo->input_operand_id =
        GetOperatorInputId(leaky_relu, operand_to_id_map);
    leaky_relu_mojo->output_operand_id =
        GetOperatorOutputId(leaky_relu, operand_to_id_map);
  }

  const auto* options =
      static_cast<const MLLeakyReluOptions*>(leaky_relu->Options());
  CHECK(options);
  leaky_relu_mojo->alpha = options->alpha();
  return leaky_relu_mojo;
}

blink_mojom::LinearPtr CreateLinear(const OperandToIdMap& operand_to_id_map,
                                    const MLOperator* linear,
                                    bool is_activation) {
  auto linear_mojo = blink_mojom::Linear::New();
  // Activation has no input and output operand.
  if (!is_activation) {
    linear_mojo->input_operand_id =
        GetOperatorInputId(linear, operand_to_id_map);
    linear_mojo->output_operand_id =
        GetOperatorOutputId(linear, operand_to_id_map);
  }

  const auto* options = static_cast<const MLLinearOptions*>(linear->Options());
  CHECK(options);
  linear_mojo->alpha = options->alpha();
  linear_mojo->beta = options->beta();
  return linear_mojo;
}

OperationPtr CreateSoftplus(const OperandToIdMap& operand_to_id_map,
                            const MLOperator* softplus) {
  auto softplus_mojo = blink_mojom::Softplus::New(
      GetOperatorInputId(softplus, operand_to_id_map),
      GetOperatorOutputId(softplus, operand_to_id_map));
  return blink_mojom::Operation::NewSoftplus(std::move(softplus_mojo));
}

blink_mojom::InputOperandLayout BlinkInputOperandLayoutToMojo(
    blink::V8MLInputOperandLayout::Enum type) {
  switch (type) {
    case blink::V8MLInputOperandLayout::Enum::kNchw:
      return blink_mojom::InputOperandLayout::kChannelsFirst;
    case blink::V8MLInputOperandLayout::Enum::kNhwc:
      return blink_mojom::InputOperandLayout::kChannelsLast;
  }
  NOTREACHED_NORETURN();
}

constexpr std::array<uint32_t, 4> kNchwToNhwcPermutation = {0u, 2u, 3u, 1u};
constexpr std::array<uint32_t, 4> kNhwcToNchwPermutation = {0u, 3u, 1u, 2u};

std::optional<base::span<const uint32_t>> GetConv2DInputPermutation(
    blink::V8MLInputOperandLayout input_layout,
    const webnn::mojom::blink::ContextProperties& context_properties) {
  if (BlinkInputOperandLayoutToMojo(input_layout.AsEnum()) ==
      context_properties.conv2d_input_layout) {
    return std::nullopt;
  }

  switch (input_layout.AsEnum()) {
    case blink::V8MLInputOperandLayout::Enum::kNchw:
      CHECK_EQ(context_properties.conv2d_input_layout,
               blink_mojom::InputOperandLayout::kChannelsLast);
      return kNchwToNhwcPermutation;
    case blink::V8MLInputOperandLayout::Enum::kNhwc:
      CHECK_EQ(context_properties.conv2d_input_layout,
               blink_mojom::InputOperandLayout::kChannelsFirst);
      return kNhwcToNchwPermutation;
  }
}

std::optional<base::span<const uint32_t>> GetConv2DOutputPermutation(
    blink::V8MLInputOperandLayout input_layout,
    const blink_mojom::ContextProperties& context_properties) {
  if (BlinkInputOperandLayoutToMojo(input_layout.AsEnum()) ==
      context_properties.conv2d_input_layout) {
    return std::nullopt;
  }

  // The output layout is the same as the input layout and so the output
  // needs to have the inverse of the permutation returned by
  // `GetConv2DInputPermutation()` applied.
  switch (input_layout.AsEnum()) {
    case blink::V8MLInputOperandLayout::Enum::kNchw:
      CHECK_EQ(context_properties.conv2d_input_layout,
               blink_mojom::InputOperandLayout::kChannelsLast);
      return kNhwcToNchwPermutation;
    case blink::V8MLInputOperandLayout::Enum::kNhwc:
      CHECK_EQ(context_properties.conv2d_input_layout,
               blink_mojom::InputOperandLayout::kChannelsFirst);
      return kNchwToNhwcPermutation;
  }
}

std::optional<base::span<const uint32_t>> GetConv2DFilterPermutation(
    blink_mojom::InputOperandLayout input_layout,
    bool depthwise,
    blink::V8MLConv2dFilterOperandLayout filter_layout) {
  switch (input_layout) {
    case blink_mojom::InputOperandLayout::kChannelsFirst:
      // Mojo expects the OIHW layout.
      switch (filter_layout.AsEnum()) {
        case blink::V8MLConv2dFilterOperandLayout::Enum::kOihw:
          return std::nullopt;
        case blink::V8MLConv2dFilterOperandLayout::Enum::kHwio:
          return base::span({3u, 2u, 0u, 1u});
        case blink::V8MLConv2dFilterOperandLayout::Enum::kOhwi:
          return base::span({0u, 3u, 1u, 2u});
        case blink::V8MLConv2dFilterOperandLayout::Enum::kIhwo:
          return base::span({3u, 0u, 1u, 2u});
      }
      break;
    case blink_mojom::InputOperandLayout::kChannelsLast:
      if (depthwise) {
        // Mojo expects the IHWO layout.
        switch (filter_layout.AsEnum()) {
          case blink::V8MLConv2dFilterOperandLayout::Enum::kOihw:
            return base::span({1u, 2u, 3u, 0u});
          case blink::V8MLConv2dFilterOperandLayout::Enum::kHwio:
            return base::span({2u, 0u, 1u, 3u});
          case blink::V8MLConv2dFilterOperandLayout::Enum::kOhwi:
            return base::span({3u, 1u, 2u, 0u});
          case blink::V8MLConv2dFilterOperandLayout::Enum::kIhwo:
            return std::nullopt;
        }
      } else {
        switch (filter_layout.AsEnum()) {
          // Mojo expects the OHWI layout.
          case blink::V8MLConv2dFilterOperandLayout::Enum::kOihw:
            return base::span({0u, 2u, 3u, 1u});
          case blink::V8MLConv2dFilterOperandLayout::Enum::kHwio:
            return base::span({3u, 0u, 1u, 2u});
          case blink::V8MLConv2dFilterOperandLayout::Enum::kOhwi:
            return std::nullopt;
          case blink::V8MLConv2dFilterOperandLayout::Enum::kIhwo:
            return base::span({3u, 1u, 2u, 0u});
        }
      }
      break;
  }
}

std::optional<base::span<const uint32_t>> GetConvTranspose2DFilterPermutation(
    blink::V8MLConvTranspose2dFilterOperandLayout filter_layout) {
  // Mojo always expects IOHW layout.
  switch (filter_layout.AsEnum()) {
    case blink::V8MLConvTranspose2dFilterOperandLayout::Enum::kIohw:
      return std::nullopt;
    case blink::V8MLConvTranspose2dFilterOperandLayout::Enum::kHwoi:
      return base::span({3u, 2u, 0u, 1u});
    case blink::V8MLConvTranspose2dFilterOperandLayout::Enum::kOhwi:
      return base::span({3u, 0u, 1u, 2u});
  }
}

ActivationPtr CreateActivation(const OperandToIdMap& operand_to_id_map,
                               const MLActivation* ml_activation) {
  switch (ml_activation->Kind()) {
    case blink_mojom::Activation::Tag::kClamp:
      return blink_mojom::Activation::NewClamp(
          CreateClamp(operand_to_id_map, ml_activation->Operator(), true));
    case blink_mojom::Activation::Tag::kElu:
      return blink_mojom::Activation::NewElu(
          CreateElu(operand_to_id_map, ml_activation->Operator(), true));
    case blink_mojom::Activation::Tag::kGelu:
      return blink_mojom::Activation::NewGelu(blink_mojom::Gelu::New());
    case blink_mojom::Activation::Tag::kHardSigmoid:
      return blink_mojom::Activation::NewHardSigmoid(CreateHardSigmoid(
          operand_to_id_map, ml_activation->Operator(), true));
    case blink_mojom::Activation::Tag::kLeakyRelu:
      return blink_mojom::Activation::NewLeakyRelu(
          CreateLeakyRelu(operand_to_id_map, ml_activation->Operator(), true));
    case blink_mojom::Activation::Tag::kLinear:
      return blink_mojom::Activation::NewLinear(
          CreateLinear(operand_to_id_map, ml_activation->Operator(), true));
    case blink_mojom::Activation::Tag::kRelu:
      return blink_mojom::Activation::NewRelu(blink_mojom::Relu::New());
    case blink_mojom::Activation::Tag::kSigmoid:
      return blink_mojom::Activation::NewSigmoid(blink_mojom::Sigmoid::New());
    case blink_mojom::Activation::Tag::kSoftmax:
      return blink_mojom::Activation::NewSoftmax(blink_mojom::Softmax::New());
    case blink_mojom::Activation::Tag::kSoftplus:
      return blink_mojom::Activation::NewSoftplus(blink_mojom::Softplus::New());
    case blink_mojom::Activation::Tag::kSoftsign:
      return blink_mojom::Activation::NewSoftsign(blink_mojom::Softsign::New());
    case blink_mojom::Activation::Tag::kTanh:
      return blink_mojom::Activation::NewTanh(blink_mojom::Tanh::New());
  }
}

OperationPtr CreateArgMinMaxOperation(const OperandToIdMap& operand_to_id_map,
                                      const MLOperator* arg_min_max,
                                      blink_mojom::ArgMinMax::Kind kind) {
  auto arg_min_max_mojo = blink_mojom::ArgMinMax::New();
  arg_min_max_mojo->kind = kind;
  arg_min_max_mojo->input_operand_id =
      GetOperatorInputId(arg_min_max, operand_to_id_map);
  arg_min_max_mojo->output_operand_id =
      GetOperatorOutputId(arg_min_max, operand_to_id_map);

  const auto* options =
      static_cast<const blink::MLArgMinMaxOptions*>(arg_min_max->Options());
  CHECK(options);
  const auto input_rank = arg_min_max->Inputs()[0]->Dimensions().size();
  const auto axes = options->getAxesOr(CreateAllAxes(input_rank));
  CHECK_LE(axes.size(), input_rank);
  arg_min_max_mojo->axes = axes;
  arg_min_max_mojo->keep_dimensions = options->keepDimensions();
  arg_min_max_mojo->select_last_index = options->selectLastIndex();

  return blink_mojom::Operation::NewArgMinMax(std::move(arg_min_max_mojo));
}

OperationPtr CreateBatchNormalizationOperation(
    const OperandToIdMap& operand_to_id_map,
    const MLOperator* batch_normalization) {
  auto batch_normalization_mojo =
      webnn::mojom::blink::BatchNormalization::New();
  batch_normalization_mojo->input_operand_id =
      GetOperatorInputId(batch_normalization, operand_to_id_map, 0);
  batch_normalization_mojo->mean_operand_id =
      GetOperatorInputId(batch_normalization, operand_to_id_map, 1);
  batch_normalization_mojo->variance_operand_id =
      GetOperatorInputId(batch_normalization, operand_to_id_map, 2);
  batch_normalization_mojo->output_operand_id =
      GetOperatorOutputId(batch_normalization, operand_to_id_map);

  const auto* options = static_cast<const MLBatchNormalizationOptions*>(
      batch_normalization->Options());
  CHECK(options);
  if (options->hasScale()) {
    batch_normalization_mojo->scale_operand_id =
        operand_to_id_map.at(options->scale());
  }
  if (options->hasBias()) {
    batch_normalization_mojo->bias_operand_id =
        operand_to_id_map.at(options->bias());
  }
  batch_normalization_mojo->axis = options->axis();
  batch_normalization_mojo->epsilon = options->epsilon();
  return webnn::mojom::blink::Operation::NewBatchNormalization(
      std::move(batch_normalization_mojo));
}

OperationPtr CreateConcatOperation(const OperandToIdMap& operand_to_id_map,
                                   const MLOperator* concat) {
  const auto& inputs = concat->Inputs();

  Vector<uint64_t> input_operand_ids;
  input_operand_ids.reserve(inputs.size());
  base::ranges::transform(inputs, std::back_inserter(input_operand_ids),
                          [operand_to_id_map](const auto& input) {
                            return operand_to_id_map.at(input);
                          });

  auto concat_mojo = blink_mojom::Concat::New();
  concat_mojo->input_operand_ids = std::move(input_operand_ids);
  concat_mojo->output_operand_id =
      GetOperatorOutputId(concat, operand_to_id_map);
  const auto* concat_operator = static_cast<const MLConcatOperator*>(concat);

  concat_mojo->axis = concat_operator->Axis();
  return blink_mojom::Operation::NewConcat(std::move(concat_mojo));
}

bool IsDepthwiseConv2d(const MLOperator* conv2d) {
  const auto* options = static_cast<const MLConv2dOptions*>(conv2d->Options());
  CHECK(options);

  const MLOperand* input = conv2d->Inputs()[0];
  CHECK(input);
  const Vector<uint32_t>& input_shape = input->Dimensions();
  CHECK_EQ(input_shape.size(), 4u);
  const MLOperand* output = conv2d->Outputs()[0].Get();
  CHECK(output);
  const Vector<uint32_t>& output_shape = output->Dimensions();
  CHECK_EQ(output_shape.size(), 4u);

  uint32_t input_channels, output_channels;
  switch (options->inputLayout().AsEnum()) {
    case blink::V8MLInputOperandLayout::Enum::kNchw:
      input_channels = input_shape[1];
      output_channels = output_shape[1];
      break;
    case blink::V8MLInputOperandLayout::Enum::kNhwc:
      input_channels = input_shape[3];
      output_channels = output_shape[3];
      break;
  }

  const uint32_t groups = base::checked_cast<uint32_t>(options->groups());
  return webnn::IsDepthwiseConv2d(input_channels, output_channels, groups);
}


template <typename MLConv2dOptionsType>
std::optional<String> SerializeConv2dOperation(
    const OperandToIdMap& operand_to_id_map,
    const webnn::mojom::blink::ContextProperties& context_properties,
    const MLOperator* conv2d,
    blink_mojom::GraphInfo* graph_info) {
  auto conv2d_mojo = blink_mojom::Conv2d::New();

  const auto* options =
      static_cast<const MLConv2dOptionsType*>(conv2d->Options());
  CHECK(options);

  // If strides is not present, the values are assumed to be [1,1].
  auto strides = options->getStridesOr({1, 1});
  CHECK_EQ(strides.size(), 2u);
  conv2d_mojo->strides = Size2d::New(strides[0], strides[1]);

  // If dilations is not present, the values are assumed to be [1, 1].
  auto dilations = options->getDilationsOr({1, 1});
  CHECK_EQ(dilations.size(), 2u);
  conv2d_mojo->dilations = Size2d::New(dilations[0], dilations[1]);
  conv2d_mojo->groups = options->groups();
  if (options->hasBias()) {
    conv2d_mojo->bias_operand_id = operand_to_id_map.at(options->bias());
  }

  const MLOperand* input_operand = conv2d->Inputs()[0];
  const MLOperand* output_operand = conv2d->Outputs()[0];
  uint64_t output_operand_id = operand_to_id_map.at(output_operand);

  const std::optional<base::span<const uint32_t>> input_permutation =
      GetConv2DInputPermutation(options->inputLayout(), context_properties);
  if (input_permutation.has_value()) {
    conv2d_mojo->input_operand_id = InsertInputTranspose(
        operand_to_id_map, input_operand, *input_permutation, graph_info);

    output_operand_id = InsertTemporaryOperand(
        operand_to_id_map, output_operand->dataType(),
        PermuteArray(output_operand->Dimensions(), *input_permutation),
        graph_info);
  } else {
    conv2d_mojo->input_operand_id = operand_to_id_map.at(input_operand);
  }
  conv2d_mojo->output_operand_id = output_operand_id;

  const MLOperand* filter_operand = conv2d->Inputs()[1];
  std::optional<base::span<const uint32_t>> filter_permutation;

  if constexpr (std::is_same<MLConv2dOptionsType, MLConv2dOptions>::value) {
    conv2d_mojo->kind = blink_mojom::Conv2d::Kind::kDirect;

    bool depthwise = IsDepthwiseConv2d(conv2d);
    filter_permutation =
        GetConv2DFilterPermutation(context_properties.conv2d_input_layout,
                                   depthwise, options->filterLayout());
  } else if constexpr (std::is_same<MLConv2dOptionsType,
                                    MLConvTranspose2dOptions>::value) {
    conv2d_mojo->kind = blink_mojom::Conv2d::Kind::kTransposed;

    filter_permutation =
        GetConvTranspose2DFilterPermutation(options->filterLayout());
  } else {
    NOTREACHED_NORETURN();
  }

  if (filter_permutation) {
    conv2d_mojo->filter_operand_id = InsertInputTranspose(
        operand_to_id_map, filter_operand, *filter_permutation, graph_info);
  } else {
    conv2d_mojo->filter_operand_id = operand_to_id_map.at(filter_operand);
  }

  // Set the padding from WebNN explicit padding that is in
  // [beginning_height, ending_height, beginning_width, ending_width],
  // default to 0.
  auto ml_padding = options->getPaddingOr({0, 0, 0, 0});
  CHECK_EQ(ml_padding.size(), 4u);
  conv2d_mojo->padding = blink_mojom::Padding2d::New(
      /*beginning padding*/ Size2d::New(ml_padding[0], ml_padding[2]),
      /*ending padding*/ Size2d::New(ml_padding[1], ml_padding[3]));

  graph_info->operations.push_back(
      blink_mojom::Operation::NewConv2d(std::move(conv2d_mojo)));

  const std::optional<base::span<const uint32_t>> output_permutation =
      GetConv2DOutputPermutation(options->inputLayout(), context_properties);
  if (output_permutation) {
    auto output_transpose = blink_mojom::Transpose::New();
    output_transpose->input_operand_id = output_operand_id;
    output_transpose->output_operand_id = operand_to_id_map.at(output_operand);
    output_transpose->permutation = Vector<uint32_t>(*output_permutation);

    graph_info->operations.push_back(
        blink_mojom::Operation::NewTranspose(std::move(output_transpose)));
  }

  return std::nullopt;
}

OperationPtr CreateElementWiseBinaryOperator(
    const OperandToIdMap& operand_to_id_map,
    const MLOperator* binary,
    const blink_mojom::ElementWiseBinary::Kind& kind) {
  const uint64_t lhs_operand_id =
      GetOperatorInputId(binary, operand_to_id_map, 0);
  const uint64_t rhs_operand_id =
      GetOperatorInputId(binary, operand_to_id_map, 1);
  const uint64_t output_operand_id =
      GetOperatorOutputId(binary, operand_to_id_map);

  auto operator_mojo = ElementWiseBinary::New();
  operator_mojo->kind = kind;
  operator_mojo->lhs_operand_id = lhs_operand_id;
  operator_mojo->rhs_operand_id = rhs_operand_id;
  operator_mojo->output_operand_id = output_operand_id;
  return webnn::mojom::blink::Operation::NewElementWiseBinary(
      std::move(operator_mojo));
}

OperationPtr CreateElementWiseUnaryOperator(
    const OperandToIdMap& operand_to_id_map,
    const MLOperator* unary,
    const blink_mojom::ElementWiseUnary::Kind& kind) {
  auto operator_mojo = ElementWiseUnary::New();
  operator_mojo->input_operand_id =
      GetOperatorInputId(unary, operand_to_id_map);
  operator_mojo->output_operand_id =
      GetOperatorOutputId(unary, operand_to_id_map);
  operator_mojo->kind = kind;
  return webnn::mojom::blink::Operation::NewElementWiseUnary(
      std::move(operator_mojo));
}

OperationPtr CreateGatherOperation(const OperandToIdMap& operand_to_id_map,
                                   const MLOperator* gather) {
  auto gather_mojo = webnn::mojom::blink::Gather::New();
  gather_mojo->input_operand_id =
      GetOperatorInputId(gather, operand_to_id_map, 0);
  gather_mojo->indices_operand_id =
      GetOperatorInputId(gather, operand_to_id_map, 1);
  gather_mojo->output_operand_id =
      GetOperatorOutputId(gather, operand_to_id_map);

  const auto* options = static_cast<const MLGatherOptions*>(gather->Options());
  CHECK(options);
  gather_mojo->axis = options->axis();

  return webnn::mojom::blink::Operation::NewGather(std::move(gather_mojo));
}

OperationPtr CreateGeluOperation(const OperandToIdMap& operand_to_id_map,
                                 const MLOperator* gelu) {
  auto gelu_mojo =
      blink_mojom::Gelu::New(GetOperatorInputId(gelu, operand_to_id_map),
                             GetOperatorOutputId(gelu, operand_to_id_map));
  return blink_mojom::Operation::NewGelu(std::move(gelu_mojo));
}

OperationPtr CreateGemmOperation(const OperandToIdMap& operand_to_id_map,
                                 const MLOperator* gemm) {
  auto gemm_mojo = webnn::mojom::blink::Gemm::New();
  gemm_mojo->a_operand_id = GetOperatorInputId(gemm, operand_to_id_map, 0);
  gemm_mojo->b_operand_id = GetOperatorInputId(gemm, operand_to_id_map, 1);
  gemm_mojo->output_operand_id = GetOperatorOutputId(gemm, operand_to_id_map);

  const auto* options = static_cast<const MLGemmOptions*>(gemm->Options());
  CHECK(options);
  if (options->hasC()) {
    gemm_mojo->c_operand_id = operand_to_id_map.at(options->c());
  }
  gemm_mojo->alpha = options->alpha();
  gemm_mojo->beta = options->beta();
  gemm_mojo->a_transpose = options->aTranspose();
  gemm_mojo->b_transpose = options->bTranspose();

  return webnn::mojom::blink::Operation::NewGemm(std::move(gemm_mojo));
}

OperationPtr CreateGruOperation(const OperandToIdMap& operand_to_id_map,
                                const MLOperator* gru) {
  auto gru_mojo = blink_mojom::Gru::New();
  gru_mojo->input_operand_id = GetOperatorInputId(gru, operand_to_id_map, 0);
  gru_mojo->weight_operand_id = GetOperatorInputId(gru, operand_to_id_map, 1);
  gru_mojo->recurrent_weight_operand_id =
      GetOperatorInputId(gru, operand_to_id_map, 2);

  const auto* gru_operator = static_cast<const MLGruOperator*>(gru);
  gru_mojo->hidden_size = gru_operator->hidden_size();
  gru_mojo->steps = gru_operator->steps();

  const auto* options = static_cast<const MLGruOptions*>(gru->Options());
  CHECK(options);

  if (options->hasBias()) {
    gru_mojo->bias_operand_id = operand_to_id_map.at(options->bias());
  }
  if (options->hasRecurrentBias()) {
    gru_mojo->recurrent_bias_operand_id =
        operand_to_id_map.at(options->recurrentBias());
  }
  if (options->hasInitialHiddenState()) {
    gru_mojo->initial_hidden_state_operand_id =
        operand_to_id_map.at(options->initialHiddenState());
  }
  gru_mojo->reset_after = options->resetAfter();
  gru_mojo->return_sequence = options->returnSequence();
  gru_mojo->direction =
      mojo::BlinkRecurrentNetworkDirectionToMojo(options->direction().AsEnum());
  gru_mojo->layout =
      mojo::BlinkGruWeightLayoutToMojo(options->layout().AsEnum());

  const auto& activations = options->activations();
  CHECK_EQ(activations.size(), 2u);
  gru_mojo->activations.reserve(activations.size());
  for (const auto& activation : activations) {
    gru_mojo->activations.push_back(
        CreateActivation(operand_to_id_map, activation));
  }

  const wtf_size_t output_count = gru->Outputs().size();
  gru_mojo->output_operand_ids.reserve(output_count);
  for (wtf_size_t i = 0; i < output_count; ++i) {
    gru_mojo->output_operand_ids.push_back(
        GetOperatorOutputId(gru, operand_to_id_map, i));
  }

  return blink_mojom::Operation::NewGru(std::move(gru_mojo));
}

base::expected<OperationPtr, String> CreateGruCellOperation(
    const OperandToIdMap& operand_to_id_map,
    const MLOperator* gru_cell) {
  uint64_t input_operand_id =
      GetOperatorInputId(gru_cell, operand_to_id_map, 0);
  uint64_t weight_operand_id =
      GetOperatorInputId(gru_cell, operand_to_id_map, 1);
  uint64_t recurrent_weight_operand_id =
      GetOperatorInputId(gru_cell, operand_to_id_map, 2);
  uint64_t hidden_state_operand_id =
      GetOperatorInputId(gru_cell, operand_to_id_map, 3);

  const auto* gru_cell_operator =
      static_cast<const MLGruCellOperator*>(gru_cell);
  uint32_t hidden_size = gru_cell_operator->hidden_size();

  const auto* options =
      static_cast<const MLGruCellOptions*>(gru_cell->Options());
  CHECK(options);

  std::optional<uint64_t> bias_operand_id;
  if (options->hasBias()) {
    bias_operand_id = operand_to_id_map.at(options->bias());
  }
  std::optional<uint64_t> recurrent_bias_operand_id;
  if (options->hasRecurrentBias()) {
    recurrent_bias_operand_id = operand_to_id_map.at(options->recurrentBias());
  }
  // gru_cell_mojo->reset_after = options->resetAfter();
  // gru_cell_mojo->layout =
  //     mojo::BlinkGruWeightLayoutToMojo(options->layout().AsEnum());

  // const auto& activations = options->activations();
  const HeapVector<Member<MLActivation>>& ml_activations =
      options->activations();
  CHECK_EQ(ml_activations.size(), 2u);
  Vector<ActivationPtr> activations;
  activations.reserve(ml_activations.size());
  for (const auto& activation : ml_activations) {
    base::expected<ActivationPtr, String> validated_activation =
        CreateActivation(operand_to_id_map, activation);
    if (!validated_activation.has_value()) {
      return base::unexpected(validated_activation.error());
    }
    activations.push_back(std::move(validated_activation.value()));
  }

  uint64_t output_operand_id = GetOperatorOutputId(gru_cell, operand_to_id_map);

  auto gru_cell_mojo = blink_mojom::GruCell::New(
      input_operand_id, weight_operand_id, recurrent_weight_operand_id,
      hidden_state_operand_id, hidden_size, output_operand_id, bias_operand_id,
      recurrent_bias_operand_id, options->resetAfter(),
      mojo::BlinkGruWeightLayoutToMojo(options->layout().AsEnum()),
      std::move(activations));

  return blink_mojom::Operation::NewGruCell(std::move(gru_cell_mojo));
}

OperationPtr CreateHardSwishOperation(const OperandToIdMap& operand_to_id_map,
                                      const MLOperator* hard_swish) {
  auto hard_swish_mojo = blink_mojom::HardSwish::New();
  hard_swish_mojo->input_operand_id =
      GetOperatorInputId(hard_swish, operand_to_id_map);
  hard_swish_mojo->output_operand_id =
      GetOperatorOutputId(hard_swish, operand_to_id_map);
  return blink_mojom::Operation::NewHardSwish(std::move(hard_swish_mojo));
}

OperationPtr CreateLayerNormalizationOperation(
    const OperandToIdMap& operand_to_id_map,
    const MLOperator* layer_normalization) {
  auto layer_normalization_mojo =
      webnn::mojom::blink::LayerNormalization::New();
  layer_normalization_mojo->input_operand_id =
      GetOperatorInputId(layer_normalization, operand_to_id_map);
  layer_normalization_mojo->output_operand_id =
      GetOperatorOutputId(layer_normalization, operand_to_id_map);

  const auto* options = static_cast<const MLLayerNormalizationOptions*>(
      layer_normalization->Options());
  CHECK(options);

  if (options->hasScale()) {
    layer_normalization_mojo->scale_operand_id =
        operand_to_id_map.at(options->scale());
  }
  if (options->hasBias()) {
    layer_normalization_mojo->bias_operand_id =
        operand_to_id_map.at(options->bias());
  }

  wtf_size_t input_rank = layer_normalization->Inputs()[0]->Dimensions().size();
  layer_normalization_mojo->axes =
      options->getAxesOr(CreateLayerNormalizationDefaultAxes(input_rank));

  layer_normalization_mojo->epsilon = options->epsilon();

  return webnn::mojom::blink::Operation::NewLayerNormalization(
      std::move(layer_normalization_mojo));
}

OperationPtr CreateInstanceNormalizationOperation(
    const OperandToIdMap& operand_to_id_map,
    const MLOperator* instance_normalization) {
  auto instance_normalization_mojo =
      webnn::mojom::blink::InstanceNormalization::New();
  instance_normalization_mojo->input_operand_id =
      GetOperatorInputId(instance_normalization, operand_to_id_map, 0);
  instance_normalization_mojo->output_operand_id =
      GetOperatorOutputId(instance_normalization, operand_to_id_map);

  const auto* options = static_cast<const MLInstanceNormalizationOptions*>(
      instance_normalization->Options());
  CHECK(options);
  if (options->hasScale()) {
    instance_normalization_mojo->scale_operand_id =
        operand_to_id_map.at(options->scale());
  }
  if (options->hasBias()) {
    instance_normalization_mojo->bias_operand_id =
        operand_to_id_map.at(options->bias());
  }
  instance_normalization_mojo->layout =
      BlinkInputOperandLayoutToMojo(options->layout().AsEnum());
  instance_normalization_mojo->epsilon = options->epsilon();

  return webnn::mojom::blink::Operation::NewInstanceNormalization(
      std::move(instance_normalization_mojo));
}

OperationPtr CreateLstmOperation(const OperandToIdMap& operand_to_id_map,
                                 const MLOperator* lstm) {
  auto lstm_mojo = blink_mojom::Lstm::New();
  lstm_mojo->input_operand_id = GetOperatorInputId(lstm, operand_to_id_map, 0);
  lstm_mojo->weight_operand_id = GetOperatorInputId(lstm, operand_to_id_map, 1);
  lstm_mojo->recurrent_weight_operand_id =
      GetOperatorInputId(lstm, operand_to_id_map, 2);

  const auto* lstm_operator = static_cast<const MLLstmOperator*>(lstm);
  lstm_mojo->hidden_size = lstm_operator->hidden_size();
  lstm_mojo->steps = lstm_operator->steps();

  const auto* options = static_cast<const MLLstmOptions*>(lstm->Options());
  CHECK(options);

  if (options->hasBias()) {
    lstm_mojo->bias_operand_id = operand_to_id_map.at(options->bias());
  }
  if (options->hasRecurrentBias()) {
    lstm_mojo->recurrent_bias_operand_id =
        operand_to_id_map.at(options->recurrentBias());
  }
  if (options->hasPeepholeWeight()) {
    lstm_mojo->peephole_weight_operand_id =
        operand_to_id_map.at(options->peepholeWeight());
  }
  if (options->hasInitialHiddenState()) {
    lstm_mojo->initial_hidden_state_operand_id =
        operand_to_id_map.at(options->initialHiddenState());
  }
  if (options->hasInitialCellState()) {
    lstm_mojo->initial_cell_state_operand_id =
        operand_to_id_map.at(options->initialCellState());
  }
  lstm_mojo->return_sequence = options->returnSequence();
  lstm_mojo->direction =
      mojo::BlinkRecurrentNetworkDirectionToMojo(options->direction().AsEnum());
  lstm_mojo->layout =
      mojo::BlinkLstmWeightLayoutToMojo(options->layout().AsEnum());

  const auto& activations = options->activations();
  lstm_mojo->activations.reserve(activations.size());
  for (const auto& activation : activations) {
    lstm_mojo->activations.push_back(
        CreateActivation(operand_to_id_map, activation));
  }

  const wtf_size_t output_count = lstm->Outputs().size();
  lstm_mojo->output_operand_ids.reserve(output_count);
  for (wtf_size_t i = 0; i < output_count; ++i) {
    lstm_mojo->output_operand_ids.push_back(
        GetOperatorOutputId(lstm, operand_to_id_map, i));
  }

  return blink_mojom::Operation::NewLstm(std::move(lstm_mojo));
}

base::expected<OperationPtr, String> CreateLstmCellOperation(
    const OperandToIdMap& operand_to_id_map,
    const MLOperator* lstm_cell) {
  uint64_t input_operand_id =
      GetOperatorInputId(lstm_cell, operand_to_id_map, 0);
  uint64_t weight_operand_id =
      GetOperatorInputId(lstm_cell, operand_to_id_map, 1);
  uint64_t recurrent_weight_operand_id =
      GetOperatorInputId(lstm_cell, operand_to_id_map, 2);
  uint64_t hidden_state_operand_id =
      GetOperatorInputId(lstm_cell, operand_to_id_map, 3);
  uint64_t cell_state_operand_id =
      GetOperatorInputId(lstm_cell, operand_to_id_map, 4);

  const auto* options =
      static_cast<const MLLstmCellOptions*>(lstm_cell->Options());
  CHECK(options);

  std::optional<uint64_t> bias_operand_id;
  if (options->hasBias()) {
    bias_operand_id = operand_to_id_map.at(options->bias());
  }
  std::optional<uint64_t> recurrent_bias_operand_id;
  if (options->hasRecurrentBias()) {
    recurrent_bias_operand_id = operand_to_id_map.at(options->recurrentBias());
  }
  std::optional<uint64_t> peephole_weight_operand_id;
  if (options->hasPeepholeWeight()) {
    peephole_weight_operand_id =
        operand_to_id_map.at(options->peepholeWeight());
  }

  const HeapVector<Member<MLActivation>>& ml_activations =
      options->activations();
  Vector<ActivationPtr> activations;
  activations.reserve(activations.size());
  for (const auto& activation : ml_activations) {
    base::expected<ActivationPtr, String> validated_activation =
        CreateActivation(operand_to_id_map, activation);
    if (!validated_activation.has_value()) {
      return base::unexpected(validated_activation.error());
    }
    activations.push_back(std::move(validated_activation.value()));
  }

  Vector<uint64_t> output_operand_ids;
  CHECK_EQ(lstm_cell->Outputs().size(), 2u);
  output_operand_ids.reserve(lstm_cell->Outputs().size());
  output_operand_ids.push_back(
      GetOperatorOutputId(lstm_cell, operand_to_id_map, 0));
  output_operand_ids.push_back(
      GetOperatorOutputId(lstm_cell, operand_to_id_map, 1));

  const auto* lstm_cell_operator =
      static_cast<const MLLstmCellOperator*>(lstm_cell);

  auto lstm_cell_mojo = blink_mojom::LstmCell::New(
      input_operand_id, weight_operand_id, recurrent_weight_operand_id,
      hidden_state_operand_id, cell_state_operand_id,
      std::move(output_operand_ids), lstm_cell_operator->hidden_size(),
      bias_operand_id, recurrent_bias_operand_id, peephole_weight_operand_id,
      mojo::BlinkLstmWeightLayoutToMojo(options->layout().AsEnum()),
      std::move(activations));

  return blink_mojom::Operation::NewLstmCell(std::move(lstm_cell_mojo));
}

OperationPtr CreateMatmulOperation(const OperandToIdMap& operand_to_id_map,
                                   const MLOperator* matmul) {
  auto matmul_mojo = blink_mojom::Matmul::New();
  matmul_mojo->a_operand_id = GetOperatorInputId(matmul, operand_to_id_map, 0);
  matmul_mojo->b_operand_id = GetOperatorInputId(matmul, operand_to_id_map, 1);
  matmul_mojo->output_operand_id =
      GetOperatorOutputId(matmul, operand_to_id_map);

  return blink_mojom::Operation::NewMatmul(std::move(matmul_mojo));
}

OperationPtr CreatePadOperation(const OperandToIdMap& operand_to_id_map,
                                const MLOperator* op) {
  const auto* pad = static_cast<const blink::MLPadOperator*>(op);
  CHECK(pad);
  auto pad_mojo = blink_mojom::Pad::New();
  pad_mojo->input_operand_id = GetOperatorInputId(pad, operand_to_id_map);
  pad_mojo->output_operand_id = GetOperatorOutputId(pad, operand_to_id_map);
  pad_mojo->beginning_padding = pad->BeginningPadding();
  pad_mojo->ending_padding = pad->EndingPadding();

  const auto* options = static_cast<const blink::MLPadOptions*>(pad->Options());
  CHECK(options);
  switch (options->mode().AsEnum()) {
    case blink::V8MLPaddingMode::Enum::kConstant: {
      auto constant_padding = blink_mojom::ConstantPadding::New();
      constant_padding->value = options->value();
      pad_mojo->mode =
          blink_mojom::PaddingMode::NewConstant(std::move(constant_padding));
      break;
    }
    case blink::V8MLPaddingMode::Enum::kEdge:
      pad_mojo->mode =
          blink_mojom::PaddingMode::NewEdge(blink_mojom::EdgePadding::New());
      break;
    case blink::V8MLPaddingMode::Enum::kReflection:
      pad_mojo->mode = blink_mojom::PaddingMode::NewReflection(
          blink_mojom::ReflectionPadding::New());
      break;
    case blink::V8MLPaddingMode::Enum::kSymmetric:
      pad_mojo->mode = blink_mojom::PaddingMode::NewSymmetric(
          blink_mojom::SymmetricPadding::New());
      break;
  }

  return blink_mojom::Operation::NewPad(std::move(pad_mojo));
}

OperationPtr CreatePool2dOperation(const OperandToIdMap& operand_to_id_map,
                                   const MLOperator* pool2d,
                                   const blink_mojom::Pool2d::Kind& kind) {
  auto pool2d_mojo = blink_mojom::Pool2d::New();
  pool2d_mojo->kind = kind;
  pool2d_mojo->input_operand_id = GetOperatorInputId(pool2d, operand_to_id_map);
  pool2d_mojo->output_operand_id =
      GetOperatorOutputId(pool2d, operand_to_id_map);

  const auto* options =
      static_cast<const blink::MLPool2dOptions*>(pool2d->Options());
  CHECK(options);
  // If strides is not present, the values are assumed to be [1,1].
  auto strides = options->getStridesOr({1, 1});
  CHECK_EQ(strides.size(), 2u);
  pool2d_mojo->strides = Size2d::New(strides[0], strides[1]);

  // If dilations is not present, the values are assumed to be [1, 1].
  auto dilations = options->getDilationsOr({1, 1});
  CHECK_EQ(dilations.size(), 2u);
  pool2d_mojo->dilations = Size2d::New(dilations[0], dilations[1]);
  pool2d_mojo->layout =
      BlinkInputOperandLayoutToMojo(options->layout().AsEnum());

  // Get height and width of input for calculating padding.
  auto input_size = mojo::GetInputOperandSize2d(pool2d->Inputs()[0].Get(),
                                                options->layout().AsEnum());
  // The dimensions of the sliding window are the height and width of input
  // operand if they are not supplied by user.
  uint32_t window_height = input_size.height;
  uint32_t window_width = input_size.width;
  if (options->hasWindowDimensions()) {
    auto& window_dimensions = options->windowDimensions();
    CHECK_EQ(window_dimensions.size(), 2u);
    window_height = window_dimensions[0];
    window_width = window_dimensions[1];
  }
  pool2d_mojo->window_dimensions = Size2d::New(window_height, window_width);

  // Set the padding from WebNN explicit padding that is in
  // [beginning_height, ending_height, beginning_width, ending_width],
  // default to 0.
  auto ml_padding = options->getPaddingOr({0, 0, 0, 0});
  CHECK_EQ(ml_padding.size(), 4u);
  pool2d_mojo->padding = blink_mojom::Padding2d::New(
      /*beginning padding*/ Size2d::New(ml_padding[0], ml_padding[2]),
      /*ending padding*/ Size2d::New(ml_padding[1], ml_padding[3]));

  return blink_mojom::Operation::NewPool2d(std::move(pool2d_mojo));
}

OperationPtr CreatePreluOperation(const OperandToIdMap& operand_to_id_map,
                                  const MLOperator* prelu) {
  auto prelu_mojo = blink_mojom::Prelu::New();
  prelu_mojo->input_operand_id =
      GetOperatorInputId(prelu, operand_to_id_map, 0);
  prelu_mojo->slope_operand_id =
      GetOperatorInputId(prelu, operand_to_id_map, 1);
  prelu_mojo->output_operand_id = GetOperatorOutputId(prelu, operand_to_id_map);

  return blink_mojom::Operation::NewPrelu(std::move(prelu_mojo));
}

OperationPtr CreateReduceOperator(const OperandToIdMap& operand_to_id_map,
                                  const MLOperator* reduce,
                                  const blink_mojom::Reduce::Kind kind) {
  auto reduce_mojo = blink_mojom::Reduce::New();
  reduce_mojo->kind = kind;
  reduce_mojo->input_operand_id = GetOperatorInputId(reduce, operand_to_id_map);
  reduce_mojo->output_operand_id =
      GetOperatorOutputId(reduce, operand_to_id_map);

  const auto* options =
      static_cast<const blink::MLReduceOptions*>(reduce->Options());
  CHECK(options);
  const auto input_rank = reduce->Inputs()[0]->Dimensions().size();
  const auto axes = options->getAxesOr(CreateAllAxes(input_rank));
  CHECK_LE(axes.size(), input_rank);
  reduce_mojo->axes = axes;
  reduce_mojo->keep_dimensions = options->keepDimensions();

  return blink_mojom::Operation::NewReduce(std::move(reduce_mojo));
}

OperationPtr CreateResample2dOperation(const OperandToIdMap& operand_to_id_map,
                                       const MLOperator* resample2d) {
  auto resample2d_mojo = blink_mojom::Resample2d::New();

  resample2d_mojo->input_operand_id =
      GetOperatorInputId(resample2d, operand_to_id_map);
  resample2d_mojo->output_operand_id =
      GetOperatorOutputId(resample2d, operand_to_id_map);

  const auto* options =
      static_cast<const blink::MLResample2dOptions*>(resample2d->Options());
  CHECK(options);
  switch (options->mode().AsEnum()) {
    case blink::V8MLInterpolationMode::Enum::kNearestNeighbor:
      resample2d_mojo->mode =
          blink_mojom::Resample2d::InterpolationMode::kNearestNeighbor;
      break;
    case blink::V8MLInterpolationMode::Enum::kLinear:
      resample2d_mojo->mode =
          blink_mojom::Resample2d::InterpolationMode::kLinear;
      break;
  }

  // When the target sizes are specified, the scales argument is ignored.
  if (!options->hasSizes()) {
    // If scales are not present, the values are assumed to be [1.0, 1.0].
    auto scales = options->getScalesOr({1.0, 1.0});
    CHECK_EQ(scales.size(), 2u);
    resample2d_mojo->scales = {scales[0], scales[1]};
  }

  // If axes are not present, the values are assumed to be [2, 3].
  auto axes = options->getAxesOr({2, 3});
  CHECK_EQ(axes.size(), 2u);
  resample2d_mojo->axes = {axes[0], axes[1]};

  return blink_mojom::Operation::NewResample2d(std::move(resample2d_mojo));
}

OperationPtr CreateReluOperation(const OperandToIdMap& operand_to_id_map,
                                 const MLOperator* relu) {
  auto relu_mojo = blink_mojom::Relu::New();
  relu_mojo->input_operand_id = GetOperatorInputId(relu, operand_to_id_map);
  relu_mojo->output_operand_id = GetOperatorOutputId(relu, operand_to_id_map);
  return blink_mojom::Operation::NewRelu(std::move(relu_mojo));
}

OperationPtr CreateReshapeOperation(const OperandToIdMap& operand_to_id_map,
                                    const MLOperator* reshape) {
  auto reshape_mojo = blink_mojom::Reshape::New();
  reshape_mojo->input_operand_id =
      GetOperatorInputId(reshape, operand_to_id_map);
  reshape_mojo->output_operand_id =
      GetOperatorOutputId(reshape, operand_to_id_map);
  return blink_mojom::Operation::NewReshape(std::move(reshape_mojo));
}

OperationPtr CreateSigmoidOperation(const OperandToIdMap& operand_to_id_map,
                                    const MLOperator* sigmoid) {
  auto sigmoid_mojo = blink_mojom::Sigmoid::New();
  sigmoid_mojo->input_operand_id =
      GetOperatorInputId(sigmoid, operand_to_id_map);
  sigmoid_mojo->output_operand_id =
      GetOperatorOutputId(sigmoid, operand_to_id_map);
  return blink_mojom::Operation::NewSigmoid(std::move(sigmoid_mojo));
}

OperationPtr CreateSliceOperation(const OperandToIdMap& operand_to_id_map,
                                  const MLOperator* slice) {
  auto slice_mojo = webnn::mojom::blink::Slice::New();
  slice_mojo->input_operand_id = GetOperatorInputId(slice, operand_to_id_map);
  slice_mojo->output_operand_id = GetOperatorOutputId(slice, operand_to_id_map);
  const MLSliceOperator* slice_operator =
      static_cast<const MLSliceOperator*>(slice);
  CHECK_EQ(slice_operator->Sizes().size(), slice_operator->Starts().size());
  slice_mojo->starts_and_sizes.reserve(slice_operator->Starts().size());
  for (uint32_t i = 0; i < slice_operator->Starts().size(); ++i) {
    webnn::mojom::blink::StartAndSizePtr start_and_size =
        webnn::mojom::blink::StartAndSize::New();
    start_and_size->start = slice_operator->Starts()[i];
    start_and_size->size = slice_operator->Sizes()[i];
    slice_mojo->starts_and_sizes.push_back(std::move(start_and_size));
  }
  return webnn::mojom::blink::Operation::NewSlice(std::move(slice_mojo));
}

OperationPtr CreateSoftmaxOperation(const OperandToIdMap& operand_to_id_map,
                                    const MLOperator* softmax) {
  auto softmax_mojo = blink_mojom::Softmax::New();
  softmax_mojo->input_operand_id =
      GetOperatorInputId(softmax, operand_to_id_map);
  softmax_mojo->output_operand_id =
      GetOperatorOutputId(softmax, operand_to_id_map);
  return blink_mojom::Operation::NewSoftmax(std::move(softmax_mojo));
}

OperationPtr CreateSoftsignOperation(const OperandToIdMap& operand_to_id_map,
                                     const MLOperator* softsign) {
  auto softsign_mojo = blink_mojom::Softsign::New();
  softsign_mojo->input_operand_id =
      GetOperatorInputId(softsign, operand_to_id_map);
  softsign_mojo->output_operand_id =
      GetOperatorOutputId(softsign, operand_to_id_map);
  return blink_mojom::Operation::NewSoftsign(std::move(softsign_mojo));
}

OperationPtr CreateSplitOperation(const OperandToIdMap& operand_to_id_map,
                                  const MLOperator* split) {
  auto split_mojo = blink_mojom::Split::New();
  split_mojo->input_operand_id = GetOperatorInputId(split, operand_to_id_map);
  const wtf_size_t number_of_splits = split->Outputs().size();
  split_mojo->output_operand_ids.reserve(number_of_splits);
  for (uint32_t i = 0; i < number_of_splits; ++i) {
    split_mojo->output_operand_ids.push_back(
        GetOperatorOutputId(split, operand_to_id_map, i));
  }
  const auto* options =
      static_cast<const blink::MLSplitOptions*>(split->Options());
  CHECK(options);
  if (options->hasAxis()) {
    split_mojo->axis = options->axis();
  }
  return blink_mojom::Operation::NewSplit(std::move(split_mojo));
}

OperationPtr CreateTanhOperation(const OperandToIdMap& operand_to_id_map,
                                 const MLOperator* tanh) {
  auto tanh_mojo = blink_mojom::Tanh::New();
  tanh_mojo->input_operand_id = GetOperatorInputId(tanh, operand_to_id_map);
  tanh_mojo->output_operand_id = GetOperatorOutputId(tanh, operand_to_id_map);
  return blink_mojom::Operation::NewTanh(std::move(tanh_mojo));
}

OperationPtr CreateTransposeOperation(const OperandToIdMap& operand_to_id_map,
                                      const MLOperator* transpose) {
  auto transpose_mojo = blink_mojom::Transpose::New();
  transpose_mojo->input_operand_id =
      GetOperatorInputId(transpose, operand_to_id_map);
  transpose_mojo->output_operand_id =
      GetOperatorOutputId(transpose, operand_to_id_map);
  const auto* options =
      static_cast<const MLTransposeOptions*>(transpose->Options());
  CHECK(options);

  auto input_rank = transpose->Inputs()[0]->Dimensions().size();
  transpose_mojo->permutation =
      options->getPermutationOr(CreateDefaultPermutation(input_rank));
  CHECK_EQ(transpose_mojo->permutation.size(), input_rank);

  return blink_mojom::Operation::NewTranspose(std::move(transpose_mojo));
}

OperationPtr CreateTriangularOperation(const OperandToIdMap& operand_to_id_map,
                                       const MLOperator* triangular) {
  const auto input_operand_id =
      GetOperatorInputId(triangular, operand_to_id_map);
  const auto output_operand_id =
      GetOperatorOutputId(triangular, operand_to_id_map);

  const auto* options =
      static_cast<const MLTriangularOptions*>(triangular->Options());
  CHECK(options);

  auto triangular_mojo =
      blink_mojom::Triangular::New(input_operand_id, output_operand_id,
                                   options->upper(), options->diagonal());
  return blink_mojom::Operation::NewTriangular(std::move(triangular_mojo));
}

OperationPtr CreateWhereOperation(const OperandToIdMap& operand_to_id_map,
                                  const MLOperator* where) {
  auto where_mojo = blink_mojom::Where::New();
  where_mojo->condition_operand_id =
      GetOperatorInputId(where, operand_to_id_map, 0);
  where_mojo->true_value_operand_id =
      GetOperatorInputId(where, operand_to_id_map, 1);
  where_mojo->false_value_operand_id =
      GetOperatorInputId(where, operand_to_id_map, 2);
  where_mojo->output_operand_id = GetOperatorOutputId(where, operand_to_id_map);

  return blink_mojom::Operation::NewWhere(std::move(where_mojo));
}

}  // namespace

uint64_t NextOperandId(const webnn::mojom::blink::GraphInfo& graph_info) {
  // This count must start at 1 because 0 is a reserved element in a
  // WTF::HashMap (yes, really).
  return graph_info.id_to_operand_map.size() + 1;
}

// TODO(crbug.com/1504405): Use a lookup table to simplifie the switch logic.
std::optional<String> SerializeMojoOperation(
    const HeapHashMap<Member<const MLOperand>, uint64_t>& operand_to_id_map,
    const webnn::mojom::blink::ContextProperties& context_properties,
    const MLOperator* op,
    webnn::mojom::blink::GraphInfo* graph_info) {
  switch (op->Kind()) {
    case blink_mojom::Operation::Tag::kArgMinMax:
      graph_info->operations.push_back(CreateArgMinMaxOperation(
          operand_to_id_map, op, op->SubKind<blink_mojom::ArgMinMax::Kind>()));
      break;
    case blink_mojom::Operation::Tag::kBatchNormalization:
      graph_info->operations.push_back(
          CreateBatchNormalizationOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kClamp:
      graph_info->operations.push_back(blink_mojom::Operation::NewClamp(
          CreateClamp(operand_to_id_map, op, false)));
      break;
    case blink_mojom::Operation::Tag::kConcat:
      graph_info->operations.push_back(
          CreateConcatOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kConv2d: {
      std::optional<String> error;
      switch (op->SubKind<blink_mojom::Conv2d::Kind>()) {
        case blink_mojom::Conv2d::Kind::kDirect: {
          error = SerializeConv2dOperation<MLConv2dOptions>(
              operand_to_id_map, context_properties, op, graph_info);
          break;
        }
        case blink_mojom::Conv2d::Kind::kTransposed: {
          error = SerializeConv2dOperation<MLConvTranspose2dOptions>(
              operand_to_id_map, context_properties, op, graph_info);
          break;
        }
      }
      if (error) {
        return error.value();
      }
      break;
    }
    case blink_mojom::Operation::Tag::kElementWiseBinary:
      graph_info->operations.push_back(CreateElementWiseBinaryOperator(
          operand_to_id_map, op,
          op->SubKind<blink_mojom::ElementWiseBinary::Kind>()));
      break;
    case blink_mojom::Operation::Tag::kElementWiseUnary:
      graph_info->operations.push_back(CreateElementWiseUnaryOperator(
          operand_to_id_map, op,
          op->SubKind<blink_mojom::ElementWiseUnary::Kind>()));
      break;
    case blink_mojom::Operation::Tag::kElu:
      graph_info->operations.push_back(blink_mojom::Operation::NewElu(
          CreateElu(operand_to_id_map, op, false)));
      break;
    case blink_mojom::Operation::Tag::kExpand:
      graph_info->operations.push_back(
          CreateExpandOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kGather:
      graph_info->operations.push_back(
          CreateGatherOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kGelu:
      graph_info->operations.push_back(
          CreateGeluOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kGemm:
      graph_info->operations.push_back(
          CreateGemmOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kGru:
      graph_info->operations.push_back(
          CreateGruOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kGruCell: {
      ASSIGN_OR_RETURN(auto mojo_op,
                       CreateGruCellOperation(operand_to_id_map, op));
      graph_info->operations.push_back(std::move(mojo_op));
      break;
    }
    case blink_mojom::Operation::Tag::kHardSigmoid:
      graph_info->operations.push_back(blink_mojom::Operation::NewHardSigmoid(
          CreateHardSigmoid(operand_to_id_map, op, false)));
      break;
    case blink_mojom::Operation::Tag::kHardSwish:
      graph_info->operations.push_back(
          CreateHardSwishOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kInstanceNormalization:
      graph_info->operations.push_back(
          CreateInstanceNormalizationOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kLayerNormalization:
      graph_info->operations.push_back(
          CreateLayerNormalizationOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kLeakyRelu:
      graph_info->operations.push_back(blink_mojom::Operation::NewLeakyRelu(
          CreateLeakyRelu(operand_to_id_map, op, false)));
      break;
    case blink_mojom::Operation::Tag::kLinear:
      graph_info->operations.push_back(blink_mojom::Operation::NewLinear(
          CreateLinear(operand_to_id_map, op, false)));
      break;
    case blink_mojom::Operation::Tag::kLstm:
      graph_info->operations.push_back(
          CreateLstmOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kLstmCell: {
      ASSIGN_OR_RETURN(auto mojo_op,
                       CreateLstmCellOperation(operand_to_id_map, op));
      graph_info->operations.push_back(std::move(mojo_op));
      break;
    }
    case blink_mojom::Operation::Tag::kMatmul:
      graph_info->operations.push_back(
          CreateMatmulOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kPad:
      graph_info->operations.push_back(
          CreatePadOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kPool2d:
      graph_info->operations.push_back(CreatePool2dOperation(
          operand_to_id_map, op, op->SubKind<blink_mojom::Pool2d::Kind>()));
      break;
    case blink_mojom::Operation::Tag::kPrelu:
      graph_info->operations.push_back(
          CreatePreluOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kReduce:
      graph_info->operations.push_back(CreateReduceOperator(
          operand_to_id_map, op, op->SubKind<blink_mojom::Reduce::Kind>()));
      break;
    case blink_mojom::Operation::Tag::kResample2d:
      graph_info->operations.push_back(
          CreateResample2dOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kRelu:
      graph_info->operations.push_back(
          CreateReluOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kReshape:
      graph_info->operations.push_back(
          CreateReshapeOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kSigmoid:
      graph_info->operations.push_back(
          CreateSigmoidOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kSlice:
      graph_info->operations.push_back(
          CreateSliceOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kSoftmax:
      graph_info->operations.push_back(
          CreateSoftmaxOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kSoftplus:
      graph_info->operations.push_back(CreateSoftplus(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kSoftsign:
      graph_info->operations.push_back(
          CreateSoftsignOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kSplit:
      graph_info->operations.push_back(
          CreateSplitOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kTanh:
      graph_info->operations.push_back(
          CreateTanhOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kTranspose:
      graph_info->operations.push_back(
          CreateTransposeOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kTriangular:
      graph_info->operations.push_back(
          CreateTriangularOperation(operand_to_id_map, op));
      break;
    case blink_mojom::Operation::Tag::kWhere:
      graph_info->operations.push_back(
          CreateWhereOperation(operand_to_id_map, op));
      break;
  }
  return std::nullopt;
}

}  // namespace blink
