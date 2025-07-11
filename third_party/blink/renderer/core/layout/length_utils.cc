// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/length_utils.h"

#include <algorithm>
#include <optional>

#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/layout/block_node.h"
#include "third_party/blink/renderer/core/layout/constraint_space.h"
#include "third_party/blink/renderer/core/layout/constraint_space_builder.h"
#include "third_party/blink/renderer/core/layout/fragmentation_utils.h"
#include "third_party/blink/renderer/core/layout/geometry/box_strut.h"
#include "third_party/blink/renderer/core/layout/geometry/logical_size.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/layout/space_utils.h"
#include "third_party/blink/renderer/core/layout/svg/layout_svg_root.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/platform/geometry/layout_unit.h"
#include "third_party/blink/renderer/platform/geometry/length.h"
#include "third_party/blink/renderer/platform/geometry/length_functions.h"

namespace blink {

LayoutUnit ResolveInlineLengthInternal(
    const ConstraintSpace& constraint_space,
    const ComputedStyle& style,
    const BoxStrut& border_padding,
    MinMaxSizesFunctionRef min_max_sizes_func,
    const Length& original_length,
    const Length* auto_length,
    LayoutUnit override_available_size,
    LayoutUnit unresolvable_length_result) {
  DCHECK_EQ(constraint_space.GetWritingMode(), style.GetWritingMode());

  CHECK(!original_length.IsAuto() || auto_length);
  // for min-inline-size, this might still be 'auto'
  const Length& length =
      LIKELY(original_length.IsAuto()) ? *auto_length : original_length;

  switch (length.GetType()) {
    case Length::kFillAvailable: {
      const LayoutUnit available_size =
          override_available_size == kIndefiniteSize
              ? constraint_space.AvailableSize().inline_size
              : override_available_size;
      if (available_size == kIndefiniteSize) {
        return unresolvable_length_result;
      }
      DCHECK_GE(available_size, LayoutUnit());
      const BoxStrut margins = ComputeMarginsForSelf(constraint_space, style);
      return std::max(border_padding.InlineSum(),
                      available_size - margins.InlineSum());
    }
    case Length::kPercent:
    case Length::kFixed:
    case Length::kCalculated: {
      const LayoutUnit percentage_resolution_size =
          constraint_space.PercentageResolutionInlineSize();
      if (length.HasPercent() &&
          percentage_resolution_size == kIndefiniteSize) {
        return unresolvable_length_result;
      }
      LayoutUnit value = MinimumValueForLength(
          length, percentage_resolution_size,
          {.intrinsic_evaluator = [&](const Length& length_to_evaluate) {
            return ResolveInlineLengthInternal(
                constraint_space, style, border_padding, min_max_sizes_func,
                length_to_evaluate, auto_length, override_available_size,
                unresolvable_length_result);
          }});

      if (style.BoxSizing() == EBoxSizing::kBorderBox)
        value = std::max(border_padding.InlineSum(), value);
      else
        value += border_padding.InlineSum();
      return value;
    }
    case Length::kMinContent:
    case Length::kMaxContent:
    case Length::kMinIntrinsic:
    case Length::kFitContent: {
      MinMaxSizes min_max_sizes =
          min_max_sizes_func(length.IsMinIntrinsic()
                                 ? MinMaxSizesType::kIntrinsic
                                 : MinMaxSizesType::kContent)
              .sizes;

      if (length.IsMinContent() || length.IsMinIntrinsic())
        return min_max_sizes.min_size;
      if (length.IsMaxContent())
        return min_max_sizes.max_size;

      const LayoutUnit available_size =
          override_available_size == kIndefiniteSize
              ? constraint_space.AvailableSize().inline_size
              : override_available_size;
      if (available_size == kIndefiniteSize) {
        return unresolvable_length_result;
      }
      DCHECK_GE(available_size, LayoutUnit());
      BoxStrut margins = ComputeMarginsForSelf(constraint_space, style);
      LayoutUnit fill_available =
          (available_size - margins.InlineSum()).ClampNegativeToZero();
      return min_max_sizes.ShrinkToFit(fill_available);
    }
    case Length::kAuto:
    case Length::kContent:
    case Length::kNone:
      return unresolvable_length_result;
    case Length::kDeviceWidth:
    case Length::kDeviceHeight:
    case Length::kExtendToZoom:
      NOTREACHED_IN_MIGRATION()
          << "These should only be used for viewport definitions";
      [[fallthrough]];
    default:
      NOTREACHED_IN_MIGRATION();
      return unresolvable_length_result;
  }
}

LayoutUnit ResolveBlockLengthInternal(
    const ConstraintSpace& constraint_space,
    const ComputedStyle& style,
    const BoxStrut& border_padding,
    const Length& original_length,
    const Length* auto_length,
    LayoutUnit override_available_size,
    const LayoutUnit* override_percentage_resolution_size,
    IntrinsicBlockSizeFunctionRef unresolvable_block_size_func) {
  DCHECK_EQ(constraint_space.GetWritingMode(), style.GetWritingMode());

  CHECK(!original_length.IsAuto() || auto_length);
  // for min-block-size, this might still be 'auto'
  const Length& length =
      LIKELY(original_length.IsAuto()) ? *auto_length : original_length;

  switch (length.GetType()) {
    case Length::kFillAvailable: {
      const LayoutUnit available_size =
          override_available_size == kIndefiniteSize
              ? constraint_space.AvailableSize().block_size
              : override_available_size;
      if (available_size == kIndefiniteSize) {
        return unresolvable_block_size_func();
      }
      DCHECK_GE(available_size, LayoutUnit());
      const BoxStrut margins = ComputeMarginsForSelf(constraint_space, style);
      return std::max(border_padding.BlockSum(),
                      available_size - margins.BlockSum());
    }
    case Length::kPercent:
    case Length::kFixed:
    case Length::kCalculated: {
      const LayoutUnit percentage_resolution_size =
          override_percentage_resolution_size
              ? *override_percentage_resolution_size
              : constraint_space.PercentageResolutionBlockSize();
      if (length.HasPercent() &&
          percentage_resolution_size == kIndefiniteSize) {
        return unresolvable_block_size_func();
      }
      LayoutUnit value = MinimumValueForLength(
          length, percentage_resolution_size,
          {.intrinsic_evaluator = [&](const Length& length_to_evaluate) {
            return ResolveBlockLengthInternal(
                constraint_space, style, border_padding, length_to_evaluate,
                auto_length, override_available_size,
                override_percentage_resolution_size,
                unresolvable_block_size_func);
          }});

      if (style.BoxSizing() == EBoxSizing::kBorderBox)
        value = std::max(border_padding.BlockSum(), value);
      else
        value += border_padding.BlockSum();
      return value;
    }
    case Length::kMinContent:
    case Length::kMaxContent:
    case Length::kMinIntrinsic:
    case Length::kFitContent: {
      LayoutUnit intrinsic_size = unresolvable_block_size_func();
#if DCHECK_IS_ON()
      // Due to how intrinsic_size is calculated, it should always include
      // border and padding. We cannot check for this if we are
      // block-fragmented, though, because then the block-start border/padding
      // may be in a different fragmentainer than the block-end border/padding.
      if (intrinsic_size != kIndefiniteSize &&
          !constraint_space.HasBlockFragmentation())
        DCHECK_GE(intrinsic_size, border_padding.BlockSum());
#endif  // DCHECK_IS_ON()
      return intrinsic_size;
    }
    case Length::kAuto:
    case Length::kContent:
    case Length::kNone:
      return unresolvable_block_size_func();
    case Length::kDeviceWidth:
    case Length::kDeviceHeight:
    case Length::kExtendToZoom:
      NOTREACHED_IN_MIGRATION()
          << "These should only be used for viewport definitions";
      [[fallthrough]];
    default:
      NOTREACHED_IN_MIGRATION();
      return border_padding.BlockSum();
  }
}

LayoutUnit InlineSizeFromAspectRatio(const BoxStrut& border_padding,
                                     const LogicalSize& aspect_ratio,
                                     EBoxSizing box_sizing,
                                     LayoutUnit block_size) {
  if (box_sizing == EBoxSizing::kBorderBox) {
    return std::max(
        border_padding.InlineSum(),
        block_size.MulDiv(aspect_ratio.inline_size, aspect_ratio.block_size));
  }
  block_size -= border_padding.BlockSum();
  return block_size.MulDiv(aspect_ratio.inline_size, aspect_ratio.block_size) +
         border_padding.InlineSum();
}

LayoutUnit BlockSizeFromAspectRatio(const BoxStrut& border_padding,
                                    const LogicalSize& aspect_ratio,
                                    EBoxSizing box_sizing,
                                    LayoutUnit inline_size) {
  DCHECK_GE(inline_size, border_padding.InlineSum());
  if (box_sizing == EBoxSizing::kBorderBox) {
    return std::max(
        border_padding.BlockSum(),
        inline_size.MulDiv(aspect_ratio.block_size, aspect_ratio.inline_size));
  }
  inline_size -= border_padding.InlineSum();
  return inline_size.MulDiv(aspect_ratio.block_size, aspect_ratio.inline_size) +
         border_padding.BlockSum();
}

namespace {

// Currently this simply sets the correct override sizes for the replaced
// element, and lets legacy layout do the result.
MinMaxSizesResult ComputeMinAndMaxContentContributionForReplaced(
    const BlockNode& child,
    const ConstraintSpace& space) {
  const auto& child_style = child.Style();
  const BoxStrut border_padding =
      ComputeBorders(space, child) + ComputePadding(space, child_style);

  MinMaxSizes result;
  result = ComputeReplacedSize(child, space, border_padding).inline_size;

  if (child_style.LogicalWidth().HasPercent() ||
      child_style.LogicalMaxWidth().HasPercent()) {
    // TODO(ikilpatrick): No browser does this today, but we'd get slightly
    // better results here if we also considered the min-block size, and
    // transferred through the aspect-ratio (if available).
    result.min_size = ResolveMinInlineLength(
        space, child_style, border_padding,
        [&](MinMaxSizesType) -> MinMaxSizesResult {
          // Behave the same as if we couldn't resolve the min-inline size.
          MinMaxSizes sizes;
          sizes = border_padding.InlineSum();
          return {sizes, /* depends_on_block_constraints */ false};
        },
        child_style.LogicalMinWidth());
  }

  // Replaced elements which have a percentage block-size always depend on
  // their block constraints (as they have an aspect-ratio which changes their
  // min/max content size).
  // TODO(https://crbug.com/40339056): These should also check for 'stretch'
  // values.  (We could add Length::MayHaveStretchOrPercentDependence or
  // similar.)
  const bool depends_on_block_constraints =
      child_style.LogicalHeight().MayHavePercentDependence() ||
      child_style.LogicalMinHeight().MayHavePercentDependence() ||
      child_style.LogicalMaxHeight().MayHavePercentDependence() ||
      (child_style.LogicalHeight().HasAuto() &&
       space.IsBlockAutoBehaviorStretch());
  return MinMaxSizesResult(result, depends_on_block_constraints);
}

}  // namespace

MinMaxSizesResult ComputeMinAndMaxContentContributionInternal(
    WritingMode parent_writing_mode,
    const BlockNode& child,
    const ConstraintSpace& space,
    MinMaxSizesFunctionRef min_max_sizes_func) {
  const auto& style = child.Style();

  const bool is_parallel_with_parent =
      IsParallelWritingMode(parent_writing_mode, style.GetWritingMode());
  const bool is_parent_writing_mode_horizontal =
      IsHorizontalWritingMode(parent_writing_mode);

  const auto border_padding =
      ComputeBorders(space, child) + ComputePadding(space, style);
  const auto& inline_size =
      is_parent_writing_mode_horizontal ? style.Width() : style.Height();

  MinMaxSizesResult result;
  // TODO(https://crbug.com/40339056): These parts need to be merged
  // together to handle calc-size() correctly.
  if (inline_size.HasAuto() || inline_size.HasPercent() ||
      inline_size.IsFillAvailable() || inline_size.IsFitContent()) {
    result = min_max_sizes_func(MinMaxSizesType::kContent);
  } else {
    const auto size =
        is_parallel_with_parent
            ? ResolveMainInlineLength(space, style, border_padding,
                                      min_max_sizes_func, inline_size,
                                      /* auto_length */ nullptr)
            : ResolveMainBlockLength(
                  space, style, border_padding, inline_size,
                  /* auto_length */ nullptr, [&]() -> LayoutUnit {
                    return min_max_sizes_func(inline_size.IsMinIntrinsic()
                                                  ? MinMaxSizesType::kIntrinsic
                                                  : MinMaxSizesType::kContent)
                        .sizes.max_size;
                  });

    // This child's contribution size is not dependent on the available size, so
    // it's considered definite. Return this size for both min and max.
    result = {{size, size}, /* depends_on_block_constraints */ false};
  }

  const auto& max_inline_size =
      is_parent_writing_mode_horizontal ? style.MaxWidth() : style.MaxHeight();
  result.sizes.Constrain(
      is_parallel_with_parent
          ? ResolveMaxInlineLength(space, style, border_padding,
                                   min_max_sizes_func, max_inline_size)
          : ResolveMaxBlockLength(space, style, border_padding,
                                  max_inline_size));

  const auto& min_inline_size =
      is_parent_writing_mode_horizontal ? style.MinWidth() : style.MinHeight();
  result.sizes.Encompass(
      is_parallel_with_parent
          ? ResolveMinInlineLength(space, style, border_padding,
                                   min_max_sizes_func, min_inline_size)
          : ResolveMinBlockLength(space, style, border_padding,
                                  min_inline_size));

  // Tables need to apply one final constraint. They are never allowed to go
  // below their min-intrinsic size (even if they have an inline-size, etc).
  if (child.IsTable()) {
    result.sizes.Encompass(
        min_max_sizes_func(MinMaxSizesType::kIntrinsic).sizes.min_size);
  }
  return result;
}

MinMaxSizesResult ComputeMinAndMaxContentContribution(
    const ComputedStyle& parent_style,
    const BlockNode& child,
    const ConstraintSpace& space,
    const MinMaxSizesFloatInput float_input) {
  const auto& child_style = child.Style();
  const auto parent_writing_mode = parent_style.GetWritingMode();
  const auto child_writing_mode = child_style.GetWritingMode();

  if (IsParallelWritingMode(parent_writing_mode, child_writing_mode)) {
    if (child.IsReplaced())
      return ComputeMinAndMaxContentContributionForReplaced(child, space);
  }

  auto MinMaxSizesFunc = [&](MinMaxSizesType type) -> MinMaxSizesResult {
    return child.ComputeMinMaxSizes(parent_writing_mode, type, space,
                                    float_input);
  };

  return ComputeMinAndMaxContentContributionInternal(parent_writing_mode, child,
                                                     space, MinMaxSizesFunc);
}

MinMaxSizesResult ComputeMinAndMaxContentContributionForSelf(
    const BlockNode& child,
    const ConstraintSpace& space) {
  DCHECK(child.CreatesNewFormattingContext());

  const ComputedStyle& child_style = child.Style();
  WritingMode writing_mode = child_style.GetWritingMode();

  if (child.IsReplaced())
    return ComputeMinAndMaxContentContributionForReplaced(child, space);

  auto MinMaxSizesFunc = [&](MinMaxSizesType type) -> MinMaxSizesResult {
    return child.ComputeMinMaxSizes(writing_mode, type, space);
  };

  return ComputeMinAndMaxContentContributionInternal(writing_mode, child, space,
                                                     MinMaxSizesFunc);
}

MinMaxSizes ComputeMinAndMaxContentContributionForTest(
    WritingMode parent_writing_mode,
    const BlockNode& child,
    const ConstraintSpace& space,
    const MinMaxSizes& min_max_sizes) {
  auto MinMaxSizesFunc = [&](MinMaxSizesType) -> MinMaxSizesResult {
    return MinMaxSizesResult(min_max_sizes,
                             /* depends_on_block_constraints */ false);
  };
  return ComputeMinAndMaxContentContributionInternal(parent_writing_mode, child,
                                                     space, MinMaxSizesFunc)
      .sizes;
}

LayoutUnit ComputeInlineSizeFromAspectRatio(const ConstraintSpace& space,
                                            const ComputedStyle& style,
                                            const BoxStrut& border_padding) {
  DCHECK(!style.AspectRatio().IsAuto());

  // Even though an implicit stretch will resolve - we return an indefinite
  // size, as we prefer the inline-axis size for this case.
  if (style.LogicalHeight().IsAuto() &&
      space.BlockAutoBehavior() != AutoSizeBehavior::kStretchExplicit) {
    return kIndefiniteSize;
  }

  const auto block_size = ComputeBlockSizeForFragment(
      space, style, border_padding, /* intrinsic_size */ kIndefiniteSize,
      /* inline_size */ kIndefiniteSize);

  if (block_size == kIndefiniteSize)
    return kIndefiniteSize;

  // Check if we can get an inline size using the aspect ratio.
  return InlineSizeFromAspectRatio(border_padding, style.LogicalAspectRatio(),
                                   style.BoxSizingForAspectRatio(), block_size);
}

LayoutUnit ComputeInlineSizeForFragmentInternal(
    const ConstraintSpace& space,
    const BlockNode& node,
    const BoxStrut& border_padding,
    MinMaxSizesFunctionRef min_max_sizes_func) {
  const auto& style = node.Style();

  auto extent = kIndefiniteSize;
  const Length& logical_width = style.LogicalWidth();
  bool apply_automatic_min_size = false;

  // TODO(https://crbug.com/40339056): This still isn't right -- we need to do
  // more math here for calc-size()!
  if (!style.AspectRatio().IsAuto() &&
      ((logical_width.HasAuto() &&
        space.InlineAutoBehavior() != AutoSizeBehavior::kStretchExplicit) ||
       logical_width.HasMinContent() || logical_width.HasMaxContent())) {
    extent = ComputeInlineSizeFromAspectRatio(space, style, border_padding);

    if (extent != kIndefiniteSize) {
      // This means we successfully applied aspect-ratio and now need to check
      // if we need to apply the implied minimum size:
      // https://drafts.csswg.org/css-sizing-4/#aspect-ratio-minimum
      if (style.OverflowInlineDirection() == EOverflow::kVisible) {
        apply_automatic_min_size = true;
      }
    }
  }

  if (LIKELY(extent == kIndefiniteSize)) {
    const Length& auto_length = ([&]() {
      if (space.AvailableSize().inline_size == kIndefiniteSize) {
        return Length::MinContent();
      }
      if (space.IsInlineAutoBehaviorStretch()) {
        return Length::FillAvailable();
      }
      return Length::FitContent();
    })();
    extent = ResolveMainInlineLength(space, style, border_padding,
                                     min_max_sizes_func, logical_width,
                                     &auto_length);
  }

  const Length& min_length =
      apply_automatic_min_size && style.LogicalMinWidth().HasAuto()
          ? Length::MinIntrinsic()
          : style.LogicalMinWidth();

  return ComputeMinMaxInlineSizes(space, node, border_padding,
                                  min_max_sizes_func, &min_length)
      .ClampSizeToMinAndMax(extent);
}

LayoutUnit ComputeInlineSizeForFragment(
    const ConstraintSpace& space,
    const BlockNode& node,
    const BoxStrut& border_padding,
    MinMaxSizesFunctionRef min_max_sizes_func) {
  if (space.IsFixedInlineSize() || space.IsAnonymous()) {
    return space.AvailableSize().inline_size;
  }

  if (node.IsTable()) {
    return To<TableNode>(node).ComputeTableInlineSize(space, border_padding);
  }

  return ComputeInlineSizeForFragmentInternal(space, node, border_padding,
                                              min_max_sizes_func);
}

LayoutUnit ComputeUsedInlineSizeForTableFragment(
    const ConstraintSpace& space,
    const BlockNode& node,
    const BoxStrut& border_padding,
    const MinMaxSizes& table_grid_min_max_sizes) {
  DCHECK(!space.IsFixedInlineSize());

  auto MinMaxSizesFunc =
      [&table_grid_min_max_sizes](MinMaxSizesType type) -> MinMaxSizesResult {
    return MinMaxSizesResult(table_grid_min_max_sizes,
                             /* depends_on_block_constraints */ false);
  };

  return ComputeInlineSizeForFragmentInternal(space, node, border_padding,
                                              MinMaxSizesFunc);
}

MinMaxSizes ComputeMinMaxBlockSizes(const ConstraintSpace& space,
                                    const ComputedStyle& style,
                                    const BoxStrut& border_padding,
                                    LayoutUnit override_available_size) {
  MinMaxSizes sizes = {
      ResolveMinBlockLength(space, style, border_padding,
                            style.LogicalMinHeight(), override_available_size,
                            /* override_percentage_resolution_size */ nullptr),
      ResolveMaxBlockLength(space, style, border_padding,
                            style.LogicalMaxHeight(), override_available_size,
                            /* override_percentage_resolution_size */ nullptr)};
  sizes.max_size = std::max(sizes.max_size, sizes.min_size);
  return sizes;
}

MinMaxSizes ComputeTransferredMinMaxInlineSizes(
    const LogicalSize& ratio,
    const MinMaxSizes& block_min_max,
    const BoxStrut& border_padding,
    const EBoxSizing sizing) {
  MinMaxSizes transferred_min_max = {LayoutUnit(), LayoutUnit::Max()};
  if (block_min_max.min_size > LayoutUnit()) {
    transferred_min_max.min_size = InlineSizeFromAspectRatio(
        border_padding, ratio, sizing, block_min_max.min_size);
  }
  if (block_min_max.max_size != LayoutUnit::Max()) {
    transferred_min_max.max_size = InlineSizeFromAspectRatio(
        border_padding, ratio, sizing, block_min_max.max_size);
  }
  // Minimum size wins over maximum size.
  transferred_min_max.max_size =
      std::max(transferred_min_max.max_size, transferred_min_max.min_size);
  return transferred_min_max;
}

MinMaxSizes ComputeTransferredMinMaxBlockSizes(
    const LogicalSize& ratio,
    const MinMaxSizes& inline_min_max,
    const BoxStrut& border_padding,
    const EBoxSizing sizing) {
  MinMaxSizes transferred_min_max = {LayoutUnit(), LayoutUnit::Max()};
  if (inline_min_max.min_size > LayoutUnit()) {
    transferred_min_max.min_size = BlockSizeFromAspectRatio(
        border_padding, ratio, sizing, inline_min_max.min_size);
  }
  if (inline_min_max.max_size != LayoutUnit::Max()) {
    transferred_min_max.max_size = BlockSizeFromAspectRatio(
        border_padding, ratio, sizing, inline_min_max.max_size);
  }
  // Minimum size wins over maximum size.
  transferred_min_max.max_size =
      std::max(transferred_min_max.max_size, transferred_min_max.min_size);
  return transferred_min_max;
}

MinMaxSizes ComputeMinMaxInlineSizesFromAspectRatio(
    const ConstraintSpace& constraint_space,
    const ComputedStyle& style,
    const BoxStrut& border_padding) {
  DCHECK(!style.AspectRatio().IsAuto());

  // The spec requires us to clamp these by the specified size (it calls it the
  // preferred size). However, we actually don't need to worry about that,
  // because we only use this if the width is indefinite.

  // We do not need to compute the min/max inline sizes; as long as we always
  // apply the transferred min/max size before the explicit min/max size, the
  // result will be identical.

  LogicalSize ratio = style.LogicalAspectRatio();
  MinMaxSizes block_min_max =
      ComputeMinMaxBlockSizes(constraint_space, style, border_padding);
  return ComputeTransferredMinMaxInlineSizes(
      ratio, block_min_max, border_padding, style.BoxSizingForAspectRatio());
}

MinMaxSizes ComputeMinMaxInlineSizes(const ConstraintSpace& space,
                                     const BlockNode& node,
                                     const BoxStrut& border_padding,
                                     MinMaxSizesFunctionRef min_max_sizes_func,
                                     const Length* opt_min_length,
                                     LayoutUnit override_available_size) {
  const ComputedStyle& style = node.Style();
  const Length& min_length =
      opt_min_length ? *opt_min_length : style.LogicalMinWidth();
  MinMaxSizes sizes = {
      ResolveMinInlineLength(space, style, border_padding, min_max_sizes_func,
                             min_length, override_available_size),
      ResolveMaxInlineLength(space, style, border_padding, min_max_sizes_func,
                             style.LogicalMaxWidth(), override_available_size)};

  // This implements the transferred min/max sizes per:
  // https://drafts.csswg.org/css-sizing-4/#aspect-ratio-size-transfers
  if (!style.AspectRatio().IsAuto() && style.LogicalWidth().HasAuto() &&
      space.InlineAutoBehavior() != AutoSizeBehavior::kStretchExplicit) {
    MinMaxSizes transferred_sizes =
        ComputeMinMaxInlineSizesFromAspectRatio(space, style, border_padding);
    sizes.min_size = std::max(
        sizes.min_size, std::min(transferred_sizes.min_size, sizes.max_size));
    sizes.max_size = std::min(sizes.max_size, transferred_sizes.max_size);
  }

  if (node.IsTable()) {
    // Tables can't shrink below their inline min-content size.
    sizes.Encompass(
        min_max_sizes_func(MinMaxSizesType::kIntrinsic).sizes.min_size);
  }

  sizes.max_size = std::max(sizes.max_size, sizes.min_size);
  return sizes;
}

namespace {

// Computes the block-size for a fragment, ignoring the fixed block-size if set.
LayoutUnit ComputeBlockSizeForFragmentInternal(
    const ConstraintSpace& space,
    const ComputedStyle& style,
    const BoxStrut& border_padding,
    LayoutUnit intrinsic_size,
    LayoutUnit inline_size,
    LayoutUnit override_available_size = kIndefiniteSize) {
  MinMaxSizes min_max = ComputeMinMaxBlockSizes(space, style, border_padding,
                                                override_available_size);

  if (space.MinBlockSizeShouldEncompassIntrinsicSize()) {
    // Encompass intrinsic block-size, but not beyond computed max-block-size.
    min_max.Encompass(std::min(intrinsic_size, min_max.max_size));
  }

  // Scrollable percentage-sized children of table cells (sometimes) are sized
  // to their min-size.
  // See: https://drafts.csswg.org/css-tables-3/#row-layout
  if (space.IsRestrictedBlockSizeTableCellChild())
    return min_max.min_size;

  const bool has_aspect_ratio = !style.AspectRatio().IsAuto();
  const Length& logical_height = style.LogicalHeight();
  const bool has_implicit_stretch =
      logical_height.HasAuto() &&
      space.BlockAutoBehavior() == AutoSizeBehavior::kStretchImplicit;

  const Length& auto_length =
      (space.IsBlockAutoBehaviorStretch() &&
       space.AvailableSize().block_size != kIndefiniteSize)
          ? Length::FillAvailable()
          : Length::FitContent();

  LayoutUnit extent = kIndefiniteSize;
  if (has_aspect_ratio && inline_size != kIndefiniteSize) {
    DCHECK_GE(inline_size, LayoutUnit());

    if (!has_implicit_stretch) {
      extent = ResolveMainBlockLength(space, style, border_padding,
                                      logical_height, &auto_length,
                                      kIndefiniteSize, override_available_size);
    }

    if (extent == kIndefiniteSize) {
      extent = BlockSizeFromAspectRatio(
          border_padding, style.LogicalAspectRatio(),
          style.BoxSizingForAspectRatio(), inline_size);

      DCHECK_NE(extent, kIndefiniteSize);
      // Apply the automatic minimum size for aspect ratio:
      // https://drafts.csswg.org/css-sizing-4/#aspect-ratio-minimum
      // We also check for LayoutUnit::Max() because flexbox uses that as a
      // "placeholder" to compute the flex line length while still respecting
      // max-block-size.
      if (style.LogicalMinHeight().HasAuto() &&
          style.OverflowBlockDirection() == EOverflow::kVisible &&
          intrinsic_size != kIndefiniteSize &&
          intrinsic_size != LayoutUnit::Max()) {
        min_max.min_size = intrinsic_size;
      }
    }
  }

  if (extent == kIndefiniteSize) {
    // TODO(cbiesinger): Audit callers of ResolveMainBlockLength to see whether
    // they need to respect aspect ratio.
    extent = ResolveMainBlockLength(space, style, border_padding,
                                    logical_height, &auto_length,
                                    intrinsic_size, override_available_size);
  }

  if (extent == kIndefiniteSize) {
    DCHECK_EQ(intrinsic_size, kIndefiniteSize);
    return extent;
  }

  return min_max.ClampSizeToMinAndMax(extent);
}

}  // namespace

LayoutUnit ComputeBlockSizeForFragment(const ConstraintSpace& constraint_space,
                                       const ComputedStyle& style,
                                       const BoxStrut& border_padding,
                                       LayoutUnit intrinsic_size,
                                       LayoutUnit inline_size,
                                       LayoutUnit override_available_size) {
  // The |override_available_size| should only be used for <table>s.
  DCHECK(override_available_size == kIndefiniteSize ||
         style.IsDisplayTableBox());

  if (constraint_space.IsFixedBlockSize()) {
    LayoutUnit block_size = override_available_size == kIndefiniteSize
                                ? constraint_space.AvailableSize().block_size
                                : override_available_size;
    if (constraint_space.MinBlockSizeShouldEncompassIntrinsicSize())
      return std::max(intrinsic_size, block_size);
    return block_size;
  }

  if (constraint_space.IsTableCell() && intrinsic_size != kIndefiniteSize)
    return intrinsic_size;

  if (constraint_space.IsAnonymous())
    return intrinsic_size;

  return ComputeBlockSizeForFragmentInternal(
      constraint_space, style, border_padding, intrinsic_size, inline_size,
      override_available_size);
}

LayoutUnit ComputeInitialBlockSizeForFragment(
    const ConstraintSpace& space,
    const ComputedStyle& style,
    const BoxStrut& border_padding,
    LayoutUnit intrinsic_size,
    LayoutUnit inline_size,
    LayoutUnit override_available_size) {
  if (space.IsInitialBlockSizeIndefinite())
    return intrinsic_size;
  return ComputeBlockSizeForFragment(space, style, border_padding,
                                     intrinsic_size, inline_size,
                                     override_available_size);
}

namespace {

// Returns the default natural size.
LogicalSize ComputeDefaultNaturalSize(const BlockNode& node) {
  const auto& style = node.Style();
  PhysicalSize natural_size(LayoutUnit(300), LayoutUnit(150));
  natural_size.Scale(style.EffectiveZoom());
  return natural_size.ConvertToLogical(style.GetWritingMode());
}

// This takes the aspect-ratio, and natural-sizes and normalizes them returning
// the border-box natural-size.
//
// The following combinations are possible:
//  - an aspect-ratio with a natural-size
//  - an aspect-ratio with no natural-size
//  - no aspect-ratio with a natural-size
//
// It is not possible to have no aspect-ratio with no natural-size (as we'll
// use the default replaced size of 300x150 as a last resort).
// https://www.w3.org/TR/CSS22/visudet.html#inline-replaced-width
std::optional<LogicalSize> ComputeNormalizedNaturalSize(
    const BlockNode& node,
    const BoxStrut& border_padding,
    const EBoxSizing box_sizing,
    const LogicalSize& aspect_ratio) {
  std::optional<LayoutUnit> intrinsic_inline;
  std::optional<LayoutUnit> intrinsic_block;
  node.IntrinsicSize(&intrinsic_inline, &intrinsic_block);

  // Add the border-padding. If we *don't* have an aspect-ratio use the default
  // natural size (300x150).
  if (intrinsic_inline) {
    intrinsic_inline = *intrinsic_inline + border_padding.InlineSum();
  } else if (aspect_ratio.IsEmpty()) {
    intrinsic_inline = ComputeDefaultNaturalSize(node).inline_size +
                       border_padding.InlineSum();
  }

  if (intrinsic_block) {
    intrinsic_block = *intrinsic_block + border_padding.BlockSum();
  } else if (aspect_ratio.IsEmpty()) {
    intrinsic_block =
        ComputeDefaultNaturalSize(node).block_size + border_padding.BlockSum();
  }

  // If we have one natural size reflect via. the aspect-ratio.
  if (!intrinsic_inline && intrinsic_block) {
    DCHECK(!aspect_ratio.IsEmpty());
    intrinsic_inline = InlineSizeFromAspectRatio(border_padding, aspect_ratio,
                                                 box_sizing, *intrinsic_block);
  }
  if (intrinsic_inline && !intrinsic_block) {
    DCHECK(!aspect_ratio.IsEmpty());
    intrinsic_block = BlockSizeFromAspectRatio(border_padding, aspect_ratio,
                                               box_sizing, *intrinsic_inline);
  }

  DCHECK(intrinsic_inline.has_value() == intrinsic_block.has_value());
  if (intrinsic_inline && intrinsic_block)
    return LogicalSize(*intrinsic_inline, *intrinsic_block);

  return std::nullopt;
}

// The main part of ComputeReplacedSize(). This function doesn't handle a
// case of <svg> as the documentElement.
LogicalSize ComputeReplacedSizeInternal(const BlockNode& node,
                                        const ConstraintSpace& space,
                                        const BoxStrut& border_padding,
                                        ReplacedSizeMode mode) {
  DCHECK(node.IsReplaced());

  const ComputedStyle& style = node.Style();
  const EBoxSizing box_sizing = style.BoxSizingForAspectRatio();
  const Length& block_length = style.LogicalHeight();

  MinMaxSizes block_min_max_sizes;
  std::optional<LayoutUnit> replaced_block;
  if (mode == ReplacedSizeMode::kIgnoreBlockLengths) {
    // Don't resolve any block lengths or constraints.
    block_min_max_sizes = {LayoutUnit(), LayoutUnit::Max()};
  } else {
    // Replaced elements in quirks-mode resolve their min/max block-sizes
    // against a different size than the main size. See:
    //  - https://www.w3.org/TR/CSS21/visudet.html#min-max-heights
    //  - https://bugs.chromium.org/p/chromium/issues/detail?id=385877
    // For the history on this behavior. Fortunately if this is the case we can
    // just use the given available size to resolve these sizes against.
    const LayoutUnit min_max_percentage_resolution_size =
        node.GetDocument().InQuirksMode() && !node.IsOutOfFlowPositioned()
            ? space.AvailableSize().block_size
            : space.ReplacedPercentageResolutionBlockSize();

    block_min_max_sizes = {
        ResolveMinBlockLength(space, style, border_padding,
                              style.LogicalMinHeight(),
                              /* override_available_size */ kIndefiniteSize,
                              &min_max_percentage_resolution_size),
        ResolveMaxBlockLength(space, style, border_padding,
                              style.LogicalMaxHeight(),
                              /* override_available_size */ kIndefiniteSize,
                              &min_max_percentage_resolution_size)};

    if (space.IsFixedBlockSize()) {
      replaced_block = space.AvailableSize().block_size;
      DCHECK_GE(*replaced_block, 0);
    } else if (!block_length.HasAutoOrContentOrIntrinsic() ||
               (space.IsBlockAutoBehaviorStretch() &&
                space.AvailableSize().block_size != kIndefiniteSize)) {
      const Length& block_length_to_resolve =
          block_length.HasAuto() ? Length::FillAvailable() : block_length;

      const LayoutUnit main_percentage_resolution_size =
          space.ReplacedPercentageResolutionBlockSize();
      const LayoutUnit block_size = ResolveMainBlockLength(
          space, style, border_padding, block_length_to_resolve,
          /* auto_length*/ nullptr,
          /* intrinsic_size */ kIndefiniteSize,
          /* override_available_size */ kIndefiniteSize,
          &main_percentage_resolution_size);
      if (block_size != kIndefiniteSize) {
        DCHECK_GE(block_size, LayoutUnit());
        replaced_block = block_min_max_sizes.ClampSizeToMinAndMax(block_size);
      }
    }
  }

  const LogicalSize aspect_ratio = node.GetAspectRatio();
  const std::optional<LogicalSize> natural_size = ComputeNormalizedNaturalSize(
      node, border_padding, box_sizing, aspect_ratio);
  const Length& inline_length = style.LogicalWidth();

  auto StretchFit = [&]() -> LayoutUnit {
    LayoutUnit size;
    if (space.AvailableSize().inline_size == kIndefiniteSize) {
      size = border_padding.InlineSum();
      // TODO(crbug.com/1218055): Instead of using the default natural size, we
      // should be using the initial containing block size. When doing this
      // we'll need to invalidated (sparingly) on window resize.
      // TODO(https://crbug.com/313072): Values with intrinsic sizing or
      // content sizing keywords should perhaps also get the natural size here
      // (or be zero).
      if (inline_length.HasPercent()) {
        size += ComputeDefaultNaturalSize(node).inline_size;
      }
    } else {
      // Stretch to the available-size if it is definite.
      size = ResolveMainInlineLength(
          space, style, border_padding,
          [](MinMaxSizesType) -> MinMaxSizesResult {
            NOTREACHED_IN_MIGRATION();
            return MinMaxSizesResult();
          },
          Length::FillAvailable(), /* auto_length */ nullptr,
          /* override_available_size */ kIndefiniteSize);
    }

    // If stretch-fit applies we must have an aspect-ratio.
    DCHECK(!aspect_ratio.IsEmpty());

    // Apply the transferred min/max sizes.
    const MinMaxSizes transferred_min_max_sizes =
        ComputeTransferredMinMaxInlineSizes(aspect_ratio, block_min_max_sizes,
                                            border_padding, box_sizing);
    size = transferred_min_max_sizes.ClampSizeToMinAndMax(size);

    return size;
  };

  auto MinMaxSizesFunc = [&](MinMaxSizesType type) -> MinMaxSizesResult {
    LayoutUnit size;
    if (aspect_ratio.IsEmpty()) {
      DCHECK(natural_size);
      size = natural_size->inline_size;
    } else if (replaced_block) {
      size = InlineSizeFromAspectRatio(border_padding, aspect_ratio, box_sizing,
                                       *replaced_block);
    } else if (natural_size) {
      DCHECK_NE(mode, ReplacedSizeMode::kIgnoreInlineLengths);
      size = ComputeReplacedSize(node, space, border_padding,
                                 ReplacedSizeMode::kIgnoreInlineLengths)
                 .inline_size;
    } else {
      // We don't have a natural size - default to stretching.
      size = StretchFit();
    }

    // |depends_on_block_constraints| doesn't matter in this context.
    MinMaxSizes sizes;
    sizes += size;
    return {sizes, /* depends_on_block_constraints */ false};
  };

  MinMaxSizes inline_min_max_sizes;
  std::optional<LayoutUnit> replaced_inline;
  if (mode == ReplacedSizeMode::kIgnoreInlineLengths) {
    // Don't resolve any inline lengths or constraints.
    inline_min_max_sizes = {LayoutUnit(), LayoutUnit::Max()};
  } else {
    inline_min_max_sizes = {
        ResolveMinInlineLength(space, style, border_padding, MinMaxSizesFunc,
                               style.LogicalMinWidth()),
        ResolveMaxInlineLength(space, style, border_padding, MinMaxSizesFunc,
                               style.LogicalMaxWidth())};

    if (space.IsFixedInlineSize()) {
      replaced_inline = space.AvailableSize().inline_size;
      DCHECK_GE(*replaced_inline, 0);
    } else if (!inline_length.HasAuto() ||
               (space.IsInlineAutoBehaviorStretch() &&
                space.AvailableSize().inline_size != kIndefiniteSize)) {
      const Length& auto_length = space.IsInlineAutoBehaviorStretch()
                                      ? Length::FillAvailable()
                                      : Length::FitContent();
      const LayoutUnit inline_size =
          ResolveMainInlineLength(space, style, border_padding, MinMaxSizesFunc,
                                  inline_length, &auto_length);
      if (inline_size != kIndefiniteSize) {
        DCHECK_GE(inline_size, LayoutUnit());
        replaced_inline =
            inline_min_max_sizes.ClampSizeToMinAndMax(inline_size);
      }
    }
  }

  if (replaced_inline && replaced_block)
    return LogicalSize(*replaced_inline, *replaced_block);

  // We have *only* an aspect-ratio with no sizes (natural or otherwise), we
  // default to stretching.
  if (!natural_size && !replaced_inline && !replaced_block) {
    replaced_inline = StretchFit();
    replaced_inline =
        inline_min_max_sizes.ClampSizeToMinAndMax(*replaced_inline);
  }

  // We only know one size, the other gets computed via the aspect-ratio (if
  // present), or defaults to the natural-size.
  if (replaced_inline) {
    DCHECK(!replaced_block);
    DCHECK(natural_size || !aspect_ratio.IsEmpty());
    replaced_block = aspect_ratio.IsEmpty() ? natural_size->block_size
                                            : BlockSizeFromAspectRatio(
                                                  border_padding, aspect_ratio,
                                                  box_sizing, *replaced_inline);
    replaced_block = block_min_max_sizes.ClampSizeToMinAndMax(*replaced_block);
    return LogicalSize(*replaced_inline, *replaced_block);
  }

  if (replaced_block) {
    DCHECK(!replaced_inline);
    DCHECK(natural_size || !aspect_ratio.IsEmpty());
    replaced_inline = aspect_ratio.IsEmpty() ? natural_size->inline_size
                                             : InlineSizeFromAspectRatio(
                                                   border_padding, aspect_ratio,
                                                   box_sizing, *replaced_block);
    replaced_inline =
        inline_min_max_sizes.ClampSizeToMinAndMax(*replaced_inline);
    return LogicalSize(*replaced_inline, *replaced_block);
  }

  // Both lengths are unknown, start with the natural-size.
  DCHECK(!replaced_inline);
  DCHECK(!replaced_block);
  replaced_inline = natural_size->inline_size;
  replaced_block = natural_size->block_size;

  // Apply the min/max sizes to the natural-size.
  const LayoutUnit constrained_inline =
      inline_min_max_sizes.ClampSizeToMinAndMax(*replaced_inline);
  const LayoutUnit constrained_block =
      block_min_max_sizes.ClampSizeToMinAndMax(*replaced_block);

  // If the min/max sizes had no effect, just return the natural-size.
  if (constrained_inline == replaced_inline &&
      constrained_block == replaced_block)
    return LogicalSize(*replaced_inline, *replaced_block);

  // If we have no aspect-ratio, use both constrained sizes.
  if (aspect_ratio.IsEmpty())
    return {constrained_inline, constrained_block};

  // The min/max sizes have applied, try to respect the aspect-ratio.

  // The following implements the table from section 10.4 at:
  // https://www.w3.org/TR/CSS22/visudet.html#min-max-widths
  const bool is_min_inline_constrained = constrained_inline > *replaced_inline;
  const bool is_max_inline_constrained = constrained_inline < *replaced_inline;
  const bool is_min_block_constrained = constrained_block > *replaced_block;
  const bool is_max_block_constrained = constrained_block < *replaced_block;

  // Constraints caused us to grow in one dimension and shrink in the other.
  // Use both constrained sizes.
  if ((is_max_inline_constrained && is_min_block_constrained) ||
      (is_min_inline_constrained && is_max_block_constrained))
    return {constrained_inline, constrained_block};

  const LayoutUnit hypothetical_block = BlockSizeFromAspectRatio(
      border_padding, aspect_ratio, box_sizing, constrained_inline);
  const LayoutUnit hypothetical_inline = InlineSizeFromAspectRatio(
      border_padding, aspect_ratio, box_sizing, constrained_block);

  // If the inline-size got constrained more extremely than the block-size, use
  // the constrained inline-size, and recalculate the block-size.
  if (constrained_block == *replaced_block ||
      (is_max_inline_constrained && hypothetical_block <= constrained_block) ||
      (is_min_inline_constrained &&
       constrained_inline >= hypothetical_inline)) {
    return {constrained_inline,
            block_min_max_sizes.ClampSizeToMinAndMax(hypothetical_block)};
  }

  // If the block-size got constrained more extremely than the inline-size, use
  // the constrained block-size, and recalculate the inline-size.
  return {inline_min_max_sizes.ClampSizeToMinAndMax(hypothetical_inline),
          constrained_block};
}

}  // namespace

// Computes size for a replaced element.
LogicalSize ComputeReplacedSize(const BlockNode& node,
                                const ConstraintSpace& space,
                                const BoxStrut& border_padding,
                                ReplacedSizeMode mode) {
  DCHECK(node.IsReplaced());

  const auto* svg_root = DynamicTo<LayoutSVGRoot>(node.GetLayoutBox());
  if (!svg_root || !svg_root->IsDocumentElement()) {
    return ComputeReplacedSizeInternal(node, space, border_padding, mode);
  }

  PhysicalSize container_size(svg_root->GetContainerSize());
  if (!container_size.IsEmpty()) {
    LogicalSize size =
        container_size.ConvertToLogical(node.Style().GetWritingMode());
    size.inline_size += border_padding.InlineSum();
    size.block_size += border_padding.BlockSum();
    return size;
  }

  if (svg_root->IsEmbeddedThroughFrameContainingSVGDocument()) {
    LogicalSize size = space.AvailableSize();
    size.block_size = node.Style().IsHorizontalWritingMode()
                          ? node.InitialContainingBlockSize().height
                          : node.InitialContainingBlockSize().width;
    return size;
  }

  LogicalSize size =
      ComputeReplacedSizeInternal(node, space, border_padding, mode);

  if (node.Style().LogicalWidth().HasPercent()) {
    double factor = svg_root->LogicalSizeScaleFactorForPercentageLengths();
    if (factor != 1.0) {
      // TODO(https://crbug.com/313072): Just because a calc *has* percentages
      // doesn't mean *all* the lengths are percentages.
      size.inline_size *= factor;
    }
  }

  const Length& logical_height = node.Style().LogicalHeight();
  if (logical_height.HasPercent()) {
    // TODO(https://crbug.com/313072): Might this also be needed for intrinsic
    // sizing keywords?
    LayoutUnit height = ValueForLength(
        logical_height,
        node.GetDocument().GetLayoutView()->ViewLogicalHeightForPercentages());
    double factor = svg_root->LogicalSizeScaleFactorForPercentageLengths();
    if (factor != 1.0) {
      // TODO(https://crbug.com/313072): Just because a calc *has* percentages
      // doesn't mean *all* the lengths are percentages.
      height *= factor;
    }
    size.block_size = height;
  }
  return size;
}

int ResolveUsedColumnCount(int computed_count,
                           LayoutUnit computed_size,
                           LayoutUnit used_gap,
                           LayoutUnit available_size) {
  if (computed_size == kIndefiniteSize) {
    DCHECK(computed_count);
    return computed_count;
  }
  DCHECK_GT(computed_size, LayoutUnit());
  int count_from_width =
      ((available_size + used_gap) / (computed_size + used_gap)).ToInt();
  count_from_width = std::max(1, count_from_width);
  if (!computed_count)
    return count_from_width;
  return std::max(1, std::min(computed_count, count_from_width));
}

int ResolveUsedColumnCount(LayoutUnit available_size,
                           const ComputedStyle& style) {
  LayoutUnit computed_column_inline_size =
      style.HasAutoColumnWidth()
          ? kIndefiniteSize
          : std::max(LayoutUnit(1), LayoutUnit(style.ColumnWidth()));
  LayoutUnit gap = ResolveUsedColumnGap(available_size, style);
  int computed_count = style.HasAutoColumnCount() ? 0 : style.ColumnCount();
  return ResolveUsedColumnCount(computed_count, computed_column_inline_size,
                                gap, available_size);
}

LayoutUnit ResolveUsedColumnInlineSize(int computed_count,
                                       LayoutUnit computed_size,
                                       LayoutUnit used_gap,
                                       LayoutUnit available_size) {
  int used_count = ResolveUsedColumnCount(computed_count, computed_size,
                                          used_gap, available_size);
  return std::max(((available_size + used_gap) / used_count) - used_gap,
                  LayoutUnit());
}

LayoutUnit ResolveUsedColumnInlineSize(LayoutUnit available_size,
                                       const ComputedStyle& style) {
  // Should only attempt to resolve this if columns != auto.
  DCHECK(!style.HasAutoColumnCount() || !style.HasAutoColumnWidth());

  LayoutUnit computed_size =
      style.HasAutoColumnWidth()
          ? kIndefiniteSize
          : std::max(LayoutUnit(1), LayoutUnit(style.ColumnWidth()));
  int computed_count = style.HasAutoColumnCount() ? 0 : style.ColumnCount();
  LayoutUnit used_gap = ResolveUsedColumnGap(available_size, style);
  return ResolveUsedColumnInlineSize(computed_count, computed_size, used_gap,
                                     available_size);
}

LayoutUnit ResolveUsedColumnGap(LayoutUnit available_size,
                                const ComputedStyle& style) {
  if (const std::optional<Length>& column_gap = style.ColumnGap()) {
    return ValueForLength(*column_gap, available_size);
  }
  return LayoutUnit(style.GetFontDescription().ComputedPixelSize());
}

LayoutUnit ColumnInlineProgression(LayoutUnit available_size,
                                   const ComputedStyle& style) {
  LayoutUnit column_inline_size =
      ResolveUsedColumnInlineSize(available_size, style);
  return column_inline_size + ResolveUsedColumnGap(available_size, style);
}

PhysicalBoxStrut ComputePhysicalMargins(
    const ComputedStyle& style,
    LogicalSize percentage_resolution_size) {
  if (!style.MayHaveMargin())
    return PhysicalBoxStrut();

  // This function may be called for determining intrinsic margins, clamp
  // indefinite %-sizes to zero. See:
  // https://drafts.csswg.org/css-sizing-3/#min-percentage-contribution
  percentage_resolution_size =
      percentage_resolution_size.ClampIndefiniteToZero();

  PhysicalSize physical_resolution_size =
      ToPhysicalSize(percentage_resolution_size, style.GetWritingMode());

  return PhysicalBoxStrut(
      MinimumValueForLength(style.MarginTop(), physical_resolution_size.height),
      MinimumValueForLength(style.MarginRight(),
                            physical_resolution_size.width),
      MinimumValueForLength(style.MarginBottom(),
                            physical_resolution_size.height),
      MinimumValueForLength(style.MarginLeft(),
                            physical_resolution_size.width));
}

BoxStrut ComputeMarginsFor(const ConstraintSpace& constraint_space,
                           const ComputedStyle& style,
                           const ConstraintSpace& compute_for) {
  if (!style.MayHaveMargin() || constraint_space.IsAnonymous())
    return BoxStrut();
  LogicalSize percentage_resolution_size =
      constraint_space.MarginPaddingPercentageResolutionSize();
  return ComputePhysicalMargins(style, percentage_resolution_size)
      .ConvertToLogical(compute_for.GetWritingDirection());
}

namespace {

BoxStrut ComputeBordersInternal(const ComputedStyle& style) {
  return {LayoutUnit(style.BorderInlineStartWidth()),
          LayoutUnit(style.BorderInlineEndWidth()),
          LayoutUnit(style.BorderBlockStartWidth()),
          LayoutUnit(style.BorderBlockEndWidth())};
}

}  // namespace

BoxStrut ComputeBorders(const ConstraintSpace& constraint_space,
                        const BlockNode& node) {
  // If we are producing an anonymous fragment (e.g. a column), it has no
  // borders, padding or scrollbars. Using the ones from the container can only
  // cause trouble.
  if (constraint_space.IsAnonymous())
    return BoxStrut();

  // If we are a table cell we just access the values set by the parent table
  // layout as border may be collapsed etc.
  if (constraint_space.IsTableCell())
    return constraint_space.TableCellBorders();

  if (node.IsTable()) {
    return To<TableNode>(node).GetTableBorders()->TableBorder();
  }

  return ComputeBordersInternal(node.Style());
}

BoxStrut ComputeBordersForInline(const ComputedStyle& style) {
  return ComputeBordersInternal(style);
}

BoxStrut ComputeNonCollapsedTableBorders(const ComputedStyle& style) {
  return ComputeBordersInternal(style);
}

BoxStrut ComputeBordersForTest(const ComputedStyle& style) {
  return ComputeBordersInternal(style);
}

BoxStrut ComputePadding(const ConstraintSpace& constraint_space,
                        const ComputedStyle& style) {
  // If we are producing an anonymous fragment (e.g. a column) we shouldn't
  // have any padding.
  if (!style.MayHavePadding() || constraint_space.IsAnonymous())
    return BoxStrut();

  // Tables with collapsed borders don't have any padding.
  if (style.IsDisplayTableBox() &&
      style.BorderCollapse() == EBorderCollapse::kCollapse) {
    return BoxStrut();
  }

  // This function may be called for determining intrinsic padding, clamp
  // indefinite %-sizes to zero. See:
  // https://drafts.csswg.org/css-sizing-3/#min-percentage-contribution
  LogicalSize percentage_resolution_size =
      constraint_space.MarginPaddingPercentageResolutionSize()
          .ClampIndefiniteToZero();
  return {MinimumValueForLength(style.PaddingInlineStart(),
                                percentage_resolution_size.inline_size),
          MinimumValueForLength(style.PaddingInlineEnd(),
                                percentage_resolution_size.inline_size),
          MinimumValueForLength(style.PaddingBlockStart(),
                                percentage_resolution_size.block_size),
          MinimumValueForLength(style.PaddingBlockEnd(),
                                percentage_resolution_size.block_size)};
}

BoxStrut ComputeScrollbarsForNonAnonymous(const BlockNode& node) {
  const ComputedStyle& style = node.Style();
  if (!style.IsScrollContainer() && style.IsScrollbarGutterAuto())
    return BoxStrut();
  const LayoutBox* layout_box = node.GetLayoutBox();
  return layout_box->ComputeLogicalScrollbars();
}

void ResolveInlineAutoMargins(const ComputedStyle& style,
                              const ComputedStyle& container_style,
                              LayoutUnit available_inline_size,
                              LayoutUnit inline_size,
                              BoxStrut* margins) {
  const LayoutUnit used_space = inline_size + margins->InlineSum();
  const LayoutUnit available_space = available_inline_size - used_space;
  bool is_start_auto = style.MarginInlineStartUsing(container_style).IsAuto();
  bool is_end_auto = style.MarginInlineEndUsing(container_style).IsAuto();
  if (is_start_auto && is_end_auto) {
    margins->inline_start = (available_space / 2).ClampNegativeToZero();
    margins->inline_end =
        available_inline_size - inline_size - margins->inline_start;
  } else if (is_start_auto) {
    margins->inline_start = available_space.ClampNegativeToZero();
  } else if (is_end_auto) {
    margins->inline_end =
        available_inline_size - inline_size - margins->inline_start;
  }
}

void ResolveAutoMargins(Length start_length,
                        Length end_length,
                        LayoutUnit additional_space,
                        LayoutUnit* start_result,
                        LayoutUnit* end_result) {
  bool start_is_auto = start_length.IsAuto();
  bool end_is_auto = end_length.IsAuto();
  if (start_is_auto) {
    if (end_is_auto) {
      *start_result = additional_space / 2;
      additional_space -= *start_result;
    } else {
      *start_result = additional_space;
    }
  }
  if (end_is_auto) {
    *end_result = additional_space;
  }
}

void ResolveAutoMargins(Length inline_start_length,
                        Length inline_end_length,
                        Length block_start_length,
                        Length block_end_length,
                        LayoutUnit additional_inline_space,
                        LayoutUnit additional_block_space,
                        BoxStrut* margins) {
  ResolveAutoMargins(inline_start_length, inline_end_length,
                     additional_inline_space, &margins->inline_start,
                     &margins->inline_end);
  ResolveAutoMargins(block_start_length, block_end_length,
                     additional_block_space, &margins->block_start,
                     &margins->block_end);
}

LayoutUnit LineOffsetForTextAlign(ETextAlign text_align,
                                  TextDirection direction,
                                  LayoutUnit space_left) {
  bool is_ltr = IsLtr(direction);
  if (text_align == ETextAlign::kStart || text_align == ETextAlign::kJustify)
    text_align = is_ltr ? ETextAlign::kLeft : ETextAlign::kRight;
  else if (text_align == ETextAlign::kEnd)
    text_align = is_ltr ? ETextAlign::kRight : ETextAlign::kLeft;

  switch (text_align) {
    case ETextAlign::kLeft:
    case ETextAlign::kWebkitLeft: {
      // The direction of the block should determine what happens with wide
      // lines. In particular with RTL blocks, wide lines should still spill
      // out to the left.
      if (is_ltr)
        return LayoutUnit();
      return space_left.ClampPositiveToZero();
    }
    case ETextAlign::kRight:
    case ETextAlign::kWebkitRight: {
      // In RTL, trailing spaces appear on the left of the line.
      if (UNLIKELY(!is_ltr))
        return space_left;
      // Wide lines spill out of the block based off direction.
      // So even if text-align is right, if direction is LTR, wide lines
      // should overflow out of the right side of the block.
      if (space_left > LayoutUnit())
        return space_left;
      return LayoutUnit();
    }
    case ETextAlign::kCenter:
    case ETextAlign::kWebkitCenter: {
      if (is_ltr)
        return (space_left / 2).ClampNegativeToZero();
      // In RTL, trailing spaces appear on the left of the line.
      if (space_left > LayoutUnit())
        return (space_left / 2).ClampNegativeToZero();
      // In RTL, wide lines should spill out to the left, same as kRight.
      return space_left;
    }
    default:
      NOTREACHED_IN_MIGRATION();
      return LayoutUnit();
  }
}

// Calculates default content size for html and body elements in quirks mode.
// Returns |kIndefiniteSize| in all other cases.
LayoutUnit CalculateDefaultBlockSize(const ConstraintSpace& space,
                                     const BlockNode& node,
                                     const BlockBreakToken* break_token,
                                     const BoxStrut& border_scrollbar_padding) {
  // In quirks mode, html and body elements will completely fill the ICB, block
  // percentages should resolve against this size.
  if (node.IsQuirkyAndFillsViewport() && !IsBreakInside(break_token)) {
    LayoutUnit block_size = space.AvailableSize().block_size;
    block_size -= ComputeMarginsForSelf(space, node.Style()).BlockSum();
    return std::max(block_size.ClampNegativeToZero(),
                    border_scrollbar_padding.BlockSum());
  }
  return kIndefiniteSize;
}

FragmentGeometry CalculateInitialFragmentGeometry(
    const ConstraintSpace& space,
    const BlockNode& node,
    const BlockBreakToken* break_token,
    MinMaxSizesFunctionRef min_max_sizes_func,
    bool is_intrinsic) {
  const auto& style = node.Style();

  if (node.IsFrameSet()) {
    if (node.IsParentNGFrameSet()) {
      const auto size = space.AvailableSize();
      DCHECK_NE(size.inline_size, kIndefiniteSize);
      DCHECK_NE(size.block_size, kIndefiniteSize);
      DCHECK(space.IsFixedInlineSize());
      DCHECK(space.IsFixedBlockSize());
      return {size, {}, {}, {}};
    }

    const auto size = node.InitialContainingBlockSize();
    return {size.ConvertToLogical(style.GetWritingMode()), {}, {}, {}};
  }

  const auto border = ComputeBorders(space, node);
  const auto padding = ComputePadding(space, style);
  auto scrollbar = ComputeScrollbars(space, node);

  const auto border_padding = border + padding;
  const auto border_scrollbar_padding = border_padding + scrollbar;

  if (node.IsReplaced()) {
    const auto border_box_size = ComputeReplacedSize(
        node, space, border_padding,
        is_intrinsic ? ReplacedSizeMode::kIgnoreInlineLengths
                     : ReplacedSizeMode::kNormal);
    return {border_box_size, border, scrollbar, padding};
  }

  const LayoutUnit inline_size =
      is_intrinsic ? kIndefiniteSize
                   : ComputeInlineSizeForFragment(space, node, border_padding,
                                                  min_max_sizes_func);

  if (UNLIKELY(inline_size != kIndefiniteSize &&
               inline_size < border_scrollbar_padding.InlineSum() &&
               scrollbar.InlineSum() && !space.IsAnonymous())) {
    // Clamp the inline size of the scrollbar, unless it's larger than the
    // inline size of the content box, in which case we'll return that instead.
    // Scrollbar handling is quite bad in such situations, and this method here
    // is just to make sure that left-hand scrollbars don't mess up scrollWidth.
    // For the full story, visit http://crbug.com/724255.
    const auto content_box_inline_size =
        inline_size - border_padding.InlineSum();
    if (scrollbar.InlineSum() > content_box_inline_size) {
      if (scrollbar.inline_end) {
        DCHECK(!scrollbar.inline_start);
        scrollbar.inline_end = content_box_inline_size;
      } else {
        DCHECK(scrollbar.inline_start);
        scrollbar.inline_start = content_box_inline_size;
      }
    }
  }

  const auto default_block_size = CalculateDefaultBlockSize(
      space, node, break_token, border_scrollbar_padding);
  const auto block_size = ComputeInitialBlockSizeForFragment(
      space, style, border_padding, default_block_size, inline_size);

  return {LogicalSize(inline_size, block_size), border, scrollbar, padding};
}

FragmentGeometry CalculateInitialFragmentGeometry(
    const ConstraintSpace& space,
    const BlockNode& node,
    const BlockBreakToken* break_token,
    bool is_intrinsic) {
  auto MinMaxSizesFunc = [&](MinMaxSizesType type) -> MinMaxSizesResult {
    return node.ComputeMinMaxSizes(space.GetWritingMode(), type, space);
  };

  return CalculateInitialFragmentGeometry(space, node, break_token,
                                          MinMaxSizesFunc, is_intrinsic);
}

LogicalSize ShrinkLogicalSize(LogicalSize size, const BoxStrut& insets) {
  if (size.inline_size != kIndefiniteSize) {
    size.inline_size =
        (size.inline_size - insets.InlineSum()).ClampNegativeToZero();
  }
  if (size.block_size != kIndefiniteSize) {
    size.block_size =
        (size.block_size - insets.BlockSum()).ClampNegativeToZero();
  }

  return size;
}

LogicalSize CalculateChildAvailableSize(
    const ConstraintSpace& space,
    const BlockNode& node,
    const LogicalSize border_box_size,
    const BoxStrut& border_scrollbar_padding) {
  LogicalSize child_available_size =
      ShrinkLogicalSize(border_box_size, border_scrollbar_padding);

  if (space.IsAnonymous() ||
      (node.IsAnonymousBlock() &&
       child_available_size.block_size == kIndefiniteSize)) {
    child_available_size.block_size = space.AvailableSize().block_size;
  }

  return child_available_size;
}

namespace {

// Implements the common part of the child percentage size calculation. Deals
// with how percentages are propagated from parent to child in quirks mode.
LogicalSize AdjustChildPercentageSize(const ConstraintSpace& space,
                                      const BlockNode node,
                                      LogicalSize child_percentage_size,
                                      LayoutUnit parent_percentage_block_size) {
  // In quirks mode the percentage resolution height is passed from parent to
  // child.
  // https://quirks.spec.whatwg.org/#the-percentage-height-calculation-quirk
  if (child_percentage_size.block_size == kIndefiniteSize &&
      node.UseParentPercentageResolutionBlockSizeForChildren())
    child_percentage_size.block_size = parent_percentage_block_size;

  return child_percentage_size;
}

}  // namespace

LogicalSize CalculateChildPercentageSize(
    const ConstraintSpace& space,
    const BlockNode node,
    const LogicalSize child_available_size) {
  // Anonymous block or spaces should use the parent percent block-size.
  if (space.IsAnonymous() || node.IsAnonymousBlock()) {
    return {child_available_size.inline_size,
            space.PercentageResolutionBlockSize()};
  }

  // Table cell children don't apply the "percentage-quirk". I.e. if their
  // percentage resolution block-size is indefinite, they don't pass through
  // their parent's percentage resolution block-size.
  if (space.IsTableCellChild())
    return child_available_size;

  return AdjustChildPercentageSize(space, node, child_available_size,
                                   space.PercentageResolutionBlockSize());
}

LogicalSize CalculateReplacedChildPercentageSize(
    const ConstraintSpace& space,
    const BlockNode node,
    const LogicalSize child_available_size,
    const BoxStrut& border_scrollbar_padding,
    const BoxStrut& border_padding) {
  // Anonymous block or spaces should use the parent percent block-size.
  if (space.IsAnonymous() || node.IsAnonymousBlock()) {
    return {child_available_size.inline_size,
            space.PercentageResolutionBlockSize()};
  }

  // Table cell children don't apply the "percentage-quirk". I.e. if their
  // percentage resolution block-size is indefinite, they don't pass through
  // their parent's percentage resolution block-size.
  if (space.IsTableCellChild())
    return child_available_size;

  // Replaced descendants of a table-cell which has a definite block-size,
  // always resolve their percentages against this size (even during the
  // "layout" pass where the fixed block-size may be different).
  //
  // This ensures that between the table-cell "measure" and "layout" passes
  // the replaced descendants remain the same size.
  const ComputedStyle& style = node.Style();
  if (space.IsTableCell() && style.LogicalHeight().IsFixed()) {
    LayoutUnit block_size = ComputeBlockSizeForFragmentInternal(
        space, style, border_padding, kIndefiniteSize /* intrinsic_size */,
        kIndefiniteSize /* inline_size */);
    DCHECK_NE(block_size, kIndefiniteSize);
    return {child_available_size.inline_size,
            (block_size - border_scrollbar_padding.BlockSum())
                .ClampNegativeToZero()};
  }

  return AdjustChildPercentageSize(
      space, node, child_available_size,
      space.ReplacedPercentageResolutionBlockSize());
}

LayoutUnit ClampIntrinsicBlockSize(
    const ConstraintSpace& space,
    const BlockNode& node,
    const BlockBreakToken* break_token,
    const BoxStrut& border_scrollbar_padding,
    LayoutUnit current_intrinsic_block_size,
    std::optional<LayoutUnit> body_margin_block_sum) {
  // Tables don't respect size containment, or apply the "fill viewport" quirk.
  DCHECK(!node.IsTable());
  const ComputedStyle& style = node.Style();

  // Check if the intrinsic size was overridden.
  LayoutUnit override_intrinsic_size = node.OverrideIntrinsicContentBlockSize();
  if (override_intrinsic_size != kIndefiniteSize)
    return override_intrinsic_size + border_scrollbar_padding.BlockSum();

  // Check if we have a "default" block-size (e.g. a <textarea>).
  LayoutUnit default_intrinsic_size = node.DefaultIntrinsicContentBlockSize();
  if (default_intrinsic_size != kIndefiniteSize) {
    // <textarea>'s intrinsic size should ignore scrollbar existence.
    if (node.IsTextArea()) {
      return default_intrinsic_size -
             ComputeScrollbars(space, node).BlockSum() +
             border_scrollbar_padding.BlockSum();
    }
    return default_intrinsic_size + border_scrollbar_padding.BlockSum();
  }

  // If we have size containment, we ignore child contributions to intrinsic
  // sizing.
  if (node.ShouldApplyBlockSizeContainment())
    return border_scrollbar_padding.BlockSum();

  // Apply the "fills viewport" quirk if needed.
  if (!IsBreakInside(break_token) && node.IsQuirkyAndFillsViewport() &&
      style.LogicalHeight().HasAuto() &&
      space.AvailableSize().block_size != kIndefiniteSize) {
    DCHECK_EQ(node.IsBody() && !node.CreatesNewFormattingContext(),
              body_margin_block_sum.has_value());
    LayoutUnit margin_sum = body_margin_block_sum.value_or(
        ComputeMarginsForSelf(space, style).BlockSum());
    current_intrinsic_block_size = std::max(
        current_intrinsic_block_size,
        (space.AvailableSize().block_size - margin_sum).ClampNegativeToZero());
  }

  return current_intrinsic_block_size;
}

std::optional<MinMaxSizesResult> CalculateMinMaxSizesIgnoringChildren(
    const BlockNode& node,
    const BoxStrut& border_scrollbar_padding) {
  MinMaxSizes sizes;
  sizes += border_scrollbar_padding.InlineSum();

  // If intrinsic size was overridden, then use that.
  const LayoutUnit intrinsic_size_override =
      node.OverrideIntrinsicContentInlineSize();
  if (intrinsic_size_override != kIndefiniteSize) {
    sizes += intrinsic_size_override;
    return MinMaxSizesResult{sizes,
                             /* depends_on_block_constraints */ false};
  } else {
    LayoutUnit default_inline_size = node.DefaultIntrinsicContentInlineSize();
    if (default_inline_size != kIndefiniteSize) {
      sizes += default_inline_size;
      // <textarea>'s intrinsic size should ignore scrollbar existence.
      if (node.IsTextArea())
        sizes -= ComputeScrollbarsForNonAnonymous(node).InlineSum();
      return MinMaxSizesResult{sizes,
                               /* depends_on_block_constraints */ false};
    }
  }

  // Size contained elements don't consider children for intrinsic sizing.
  // Also, if we don't have children, we can determine the size immediately.
  if (node.ShouldApplyInlineSizeContainment() || !node.FirstChild()) {
    return MinMaxSizesResult{sizes,
                             /* depends_on_block_constraints */ false};
  }

  return std::nullopt;
}

void AddScrollbarFreeze(const BoxStrut& scrollbars_before,
                        const BoxStrut& scrollbars_after,
                        WritingDirectionMode writing_direction,
                        bool* freeze_horizontal,
                        bool* freeze_vertical) {
  PhysicalBoxStrut physical_before =
      scrollbars_before.ConvertToPhysical(writing_direction);
  PhysicalBoxStrut physical_after =
      scrollbars_after.ConvertToPhysical(writing_direction);
  *freeze_horizontal |= (!physical_before.top && physical_after.top) ||
                        (!physical_before.bottom && physical_after.bottom);
  *freeze_vertical |= (!physical_before.left && physical_after.left) ||
                      (!physical_before.right && physical_after.right);
}

}  // namespace blink
