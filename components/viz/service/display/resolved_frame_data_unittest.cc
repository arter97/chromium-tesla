// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/service/display/resolved_frame_data.h"

#include <memory>
#include <utility>

#include "components/viz/common/quads/compositor_render_pass.h"
#include "components/viz/common/quads/offset_tag.h"
#include "components/viz/common/quads/texture_draw_quad.h"
#include "components/viz/service/display/display_resource_provider_software.h"
#include "components/viz/service/display_embedder/server_shared_bitmap_manager.h"
#include "components/viz/service/frame_sinks/compositor_frame_sink_support.h"
#include "components/viz/service/frame_sinks/frame_sink_manager_impl.h"
#include "components/viz/service/surfaces/surface.h"
#include "components/viz/test/compositor_frame_helpers.h"
#include "components/viz/test/draw_quad_matchers.h"
#include "components/viz/test/test_surface_id_allocator.h"
#include "gpu/command_buffer/service/shared_image/shared_image_manager.h"
#include "gpu/command_buffer/service/sync_point_manager.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/vector2d_f.h"

namespace viz {
namespace {

constexpr gfx::Rect kOutputRect(100, 100);
constexpr FrameSinkId kArbitraryFrameSinkId(1, 1);

SurfaceId MakeSurfaceId() {
  return SurfaceId(kArbitraryFrameSinkId,
                   LocalSurfaceId(1, 1, base::UnguessableToken::Create()));
}

CompositorFrame MakeSimpleFrame(const gfx::Rect& damage_rect = kOutputRect) {
  return CompositorFrameBuilder()
      .AddRenderPass(RenderPassBuilder(kOutputRect)
                         .AddSolidColorQuad(kOutputRect, SkColors::kRed)
                         .SetDamageRect(kOutputRect))
      .Build();
}

std::unique_ptr<CompositorRenderPass> BuildRenderPass(int id) {
  auto render_pass = CompositorRenderPass::Create();
  render_pass->SetNew(CompositorRenderPassId::FromUnsafeValue(id), kOutputRect,
                      kOutputRect, gfx::Transform());
  return render_pass;
}

void AddRenderPassQuad(CompositorRenderPass* render_pass,
                       CompositorRenderPassId render_pass_id) {
  auto* sqs = render_pass->CreateAndAppendSharedQuadState();
  sqs->SetAll(gfx::Transform(), kOutputRect, kOutputRect, gfx::MaskFilterInfo(),
              /*clip=*/std::nullopt, /*contents_opaque=*/false, 1,
              SkBlendMode::kSrcOver, /*sorting_context=*/0,
              /*layer_id=*/0u, /*fast_rounded_corner=*/false);
  auto* quad =
      render_pass->CreateAndAppendDrawQuad<CompositorRenderPassDrawQuad>();
  quad->SetNew(sqs, kOutputRect, kOutputRect, render_pass_id,
               kInvalidResourceId, gfx::RectF(), gfx::Size(),
               gfx::Vector2dF(1.0f, 1.0f), gfx::PointF(), gfx::RectF(),
               /*force_anti_aliasing_off=*/false,
               /*backdrop_filter_quality=*/1.0f);
}

class ResolvedFrameDataTest : public testing::Test {
 public:
  ResolvedFrameDataTest()
      : support_(std::make_unique<CompositorFrameSinkSupport>(
            nullptr,
            &frame_sink_manager_,
            surface_id_.frame_sink_id(),
            /*is_root=*/true)) {}

 protected:
  // Submits a CompositorFrame so there is a fully populated surface with an
  // active CompositorFrame. Returns the corresponding surface.
  Surface* SubmitCompositorFrame(CompositorFrame frame) {
    support_->SubmitCompositorFrame(surface_id_.local_surface_id(),
                                    std::move(frame));

    Surface* surface =
        frame_sink_manager_.surface_manager()->GetSurfaceForId(surface_id_);
    EXPECT_TRUE(surface);
    EXPECT_TRUE(surface->HasActiveFrame());
    return surface;
  }

  ServerSharedBitmapManager shared_bitmap_manager_;
  gpu::SharedImageManager shared_image_manager_;
  gpu::SyncPointManager sync_point_manager_;

  DisplayResourceProviderSoftware resource_provider_{
      &shared_bitmap_manager_, &shared_image_manager_, &sync_point_manager_};
  FrameSinkManagerImpl frame_sink_manager_{
      FrameSinkManagerImpl::InitParams(&shared_bitmap_manager_)};

  TestSurfaceIdAllocator surface_id_{FrameSinkId(1, 1)};
  std::unique_ptr<CompositorFrameSinkSupport> support_;

  AggregatedRenderPassId::Generator render_pass_id_generator_;
};

// Submits a CompositorFrame with three valid render passes then checks that
// ResolvedPassData is valid and has the correct data.
TEST_F(ResolvedFrameDataTest, UpdateActiveFrame) {
  auto frame = CompositorFrameBuilder()
                   .AddRenderPass(BuildRenderPass(101))
                   .AddRenderPass(BuildRenderPass(102))
                   .AddRenderPass(BuildRenderPass(103))
                   .Build();

  Surface* surface = SubmitCompositorFrame(std::move(frame));
  ResolvedFrameData resolved_frame(&resource_provider_, surface, 0u,
                                   AggregatedRenderPassId());

  // The resolved frame should be false after construction.
  EXPECT_FALSE(resolved_frame.is_valid());

  // Resolved frame data should be valid after adding resolved render pass data
  // and have three render passes.
  resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
  EXPECT_TRUE(resolved_frame.is_valid());
  EXPECT_THAT(resolved_frame.GetResolvedPasses(), testing::SizeIs(3));

  // Looking up ResolvedPassData by CompositorRenderPassId should work.
  for (auto& render_pass : surface->GetActiveFrame().render_pass_list) {
    const ResolvedPassData& resolved_pass =
        resolved_frame.GetRenderPassDataById(render_pass->id);
    EXPECT_EQ(&resolved_pass.render_pass(), render_pass.get());
  }

  // Check invalidation also works.
  resolved_frame.SetInvalid();
  EXPECT_FALSE(resolved_frame.is_valid());
}

// Constructs a CompositorFrame with two render passes that have the same id.
// Verifies the frame is rejected as invalid.
TEST_F(ResolvedFrameDataTest, DupliateRenderPassIds) {
  auto frame = CompositorFrameBuilder()
                   .AddRenderPass(BuildRenderPass(1))
                   .AddRenderPass(BuildRenderPass(1))
                   .Build();

  Surface* surface = SubmitCompositorFrame(std::move(frame));
  ResolvedFrameData resolved_frame(&resource_provider_, surface, 0u,
                                   AggregatedRenderPassId());

  resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
  EXPECT_FALSE(resolved_frame.is_valid());
}

// Constructs a CompositorFrame with render pass that tries to embed itself
// forming a cycle. Verifies the frame is rejected as invalid.
TEST_F(ResolvedFrameDataTest, RenderPassIdsSelfCycle) {
  // Create a CompositorFrame and submit it to |surface_id| so there is a
  // fully populated Surface with an active CompositorFrame.
  auto render_pass = BuildRenderPass(1);
  AddRenderPassQuad(render_pass.get(), render_pass->id);

  auto frame =
      CompositorFrameBuilder().AddRenderPass(std::move(render_pass)).Build();

  Surface* surface = SubmitCompositorFrame(std::move(frame));
  ResolvedFrameData resolved_frame(&resource_provider_, surface, 0u,
                                   AggregatedRenderPassId());

  resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
  EXPECT_FALSE(resolved_frame.is_valid());
}

// Constructs a CompositorFrame with two render pass that tries to embed each
// other forming a cycle. Verifies the frame is rejected as invalid.
TEST_F(ResolvedFrameDataTest, RenderPassIdsCycle) {
  // Create a CompositorFrame and submit it to |surface_id| so there is a
  // fully populated Surface with an active CompositorFrame.
  auto render_pass1 = BuildRenderPass(1);
  auto render_pass2 = BuildRenderPass(2);
  AddRenderPassQuad(render_pass1.get(), render_pass2->id);
  AddRenderPassQuad(render_pass2.get(), render_pass1->id);

  auto frame = CompositorFrameBuilder()
                   .AddRenderPass(std::move(render_pass1))
                   .AddRenderPass(std::move(render_pass2))
                   .Build();
  Surface* surface = SubmitCompositorFrame(std::move(frame));
  ResolvedFrameData resolved_frame(&resource_provider_, surface, 0u,
                                   AggregatedRenderPassId());

  // RenderPasses have duplicate IDs so the resolved frame should be marked as
  // invalid.
  resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
  EXPECT_FALSE(resolved_frame.is_valid());
}

// Check GetRectDamage() handles per quad damage correctly.
TEST_F(ResolvedFrameDataTest, RenderPassWithPerQuadDamage) {
  constexpr gfx::Rect pass_damage_rect(80, 80, 10, 10);
  constexpr gfx::Rect quad_damage_rect(10, 10, 20, 20);
  constexpr ResourceId resource_id(1);

  auto frame =
      CompositorFrameBuilder()
          .AddRenderPass(RenderPassBuilder(kOutputRect)
                             .SetDamageRect(pass_damage_rect)
                             .AddSolidColorQuad(kOutputRect, SkColors::kRed)
                             .AddTextureQuad(kOutputRect, resource_id)
                             .SetQuadDamageRect(quad_damage_rect))
          .PopulateResources()
          .Build();

  Surface* surface = SubmitCompositorFrame(std::move(frame));
  EXPECT_EQ(surface->GetActiveFrameIndex(), kFrameIndexStart);
  ResolvedFrameData resolved_frame(&resource_provider_, surface, 1u,
                                   AggregatedRenderPassId());

  resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
  ASSERT_TRUE(resolved_frame.is_valid());

  resolved_frame.MarkAsUsedInAggregation();

  // The damage rect should not include TextureDrawQuad's damage_rect.
  EXPECT_EQ(resolved_frame.GetSurfaceDamage(), pass_damage_rect);
}

TEST_F(ResolvedFrameDataTest, MarkAsUsed) {
  Surface* surface = SubmitCompositorFrame(MakeSimpleFrame());
  ResolvedFrameData resolved_frame(&resource_provider_, surface, 0u,
                                   AggregatedRenderPassId());

  resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
  EXPECT_FALSE(resolved_frame.WasUsedInAggregation());

  // First aggregation.
  resolved_frame.MarkAsUsedInAggregation();
  EXPECT_TRUE(resolved_frame.WasUsedInAggregation());

  // This is the first frame this aggregation.
  EXPECT_EQ(resolved_frame.GetFrameDamageType(), FrameDamageType::kFull);

  // Nothing changes if MarkAsUsedInAggregation() is called more than once
  // before reset.
  resolved_frame.MarkAsUsedInAggregation();
  EXPECT_TRUE(resolved_frame.WasUsedInAggregation());

  // Reset after aggregation.
  resolved_frame.ResetAfterAggregation();
  EXPECT_FALSE(resolved_frame.WasUsedInAggregation());

  // Don't submit a new frame for the next aggregation.
  resolved_frame.MarkAsUsedInAggregation();
  EXPECT_TRUE(resolved_frame.WasUsedInAggregation());
  EXPECT_EQ(resolved_frame.GetFrameDamageType(), FrameDamageType::kNone);

  resolved_frame.ResetAfterAggregation();

  // Submit a new frame for the next aggregation.
  SubmitCompositorFrame(MakeSimpleFrame());
  resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
  resolved_frame.MarkAsUsedInAggregation();

  EXPECT_EQ(resolved_frame.GetFrameDamageType(), FrameDamageType::kFrame);

  resolved_frame.ResetAfterAggregation();

  // Submit two new frames before the next aggregation. The damage rect from
  // last frame aggregated
  SubmitCompositorFrame(MakeSimpleFrame());
  SubmitCompositorFrame(MakeSimpleFrame());
  resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
  resolved_frame.MarkAsUsedInAggregation();
  EXPECT_EQ(resolved_frame.GetFrameDamageType(), FrameDamageType::kFull);
}

// Verifies that SetFullDamageForNextAggregation()
TEST_F(ResolvedFrameDataTest, SetFullDamageNextAggregation) {
  Surface* surface = SubmitCompositorFrame(MakeSimpleFrame());
  ResolvedFrameData resolved_frame(&resource_provider_, surface, 0u,
                                   AggregatedRenderPassId());

  // First aggregation to setup existing state.
  resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
  resolved_frame.MarkAsUsedInAggregation();
  resolved_frame.ResetAfterAggregation();

  resolved_frame.SetFullDamageForNextAggregation();

  // Second aggregation with a new frame that has smaller damage rect.
  constexpr gfx::Rect damage_rect(10, 10);
  SubmitCompositorFrame(MakeSimpleFrame(damage_rect));
  resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
  resolved_frame.MarkAsUsedInAggregation();

  // This is the next frame so normally it would use `damage_rect` for damage.
  // SetFullDamageForNextAggregation() changes that so the full output_rect is
  // damaged.
  EXPECT_EQ(resolved_frame.GetFrameDamageType(), FrameDamageType::kFull);
  EXPECT_EQ(resolved_frame.GetSurfaceDamage(), kOutputRect);
}

// Verifies that the ResolvedFrameData will reuse a provided root pass ID
TEST_F(ResolvedFrameDataTest, ReusePreviousRootPassId) {
  Surface* surface = SubmitCompositorFrame(MakeSimpleFrame());
  AggregatedRenderPassId prev_root_pass_id =
      render_pass_id_generator_.GenerateNextId();
  ResolvedFrameData resolved_frame(&resource_provider_, surface, 0u,
                                   prev_root_pass_id);

  resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
  resolved_frame.MarkAsUsedInAggregation();

  EXPECT_EQ(resolved_frame.GetRootRenderPassData().remapped_id(),
            prev_root_pass_id);
}

// Verify OffsetTag creating a modified copy of original CompositorRenderPasses.
TEST_F(ResolvedFrameDataTest, OffsetTags) {
  auto offset_tag = OffsetTag::CreateRandom();

  OffsetTagDefinition tag_def;
  tag_def.tag = offset_tag;
  tag_def.provider = SurfaceRange(MakeSurfaceId());
  tag_def.constraints = OffsetTagConstraints(-30.0f, 30.0f, -30.0f, 30.0f);

  // Build a frame with one tagged quad and one not tagged quad.
  auto frame =
      CompositorFrameBuilder()
          .AddRenderPass(
              RenderPassBuilder(kOutputRect)
                  .AddSolidColorQuad(kOutputRect, SkColors::kRed)
                  .AddSolidColorQuad(gfx::Rect(10, 10), SkColors::kBlack)
                  .SetQuadOffsetTag(offset_tag))
          .AddOffsetTagDefinition(tag_def)
          .Build();

  Surface* surface = SubmitCompositorFrame(std::move(frame));
  ResolvedFrameData resolved_frame(&resource_provider_, surface, 0u,
                                   AggregatedRenderPassId());

  constexpr gfx::Vector2dF first_offset(20.0f, -20.0f);
  constexpr gfx::Vector2dF second_offset(20.0f, 0);

  {
    // First aggregation.
    resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
    EXPECT_TRUE(resolved_frame.is_valid());

    resolved_frame.UpdateOffsetTags(
        [&first_offset](const OffsetTagDefinition&) { return first_offset; });

    ASSERT_THAT(resolved_frame.GetResolvedPasses(), testing::SizeIs(1));
    const CompositorRenderPass& resolved_render_pass =
        resolved_frame.GetResolvedPasses()[0].render_pass();

    // When SetOffsetTagValues runs it makes a copy of the render pass to update
    // quad positions.
    auto& original_render_pass = surface->GetActiveFrame().render_pass_list[0];
    EXPECT_NE(&resolved_render_pass, original_render_pass.get());

    // Verify that the tagged quad has offset applied and the non-tagged quad
    // doesn't.
    EXPECT_THAT(
        resolved_render_pass.quad_list,
        testing::ElementsAre(
            testing::AllOf(IsSolidColorQuad(SkColors::kRed),
                           HasTransform(gfx::Transform::MakeTranslation({}))),
            testing::AllOf(
                IsSolidColorQuad(SkColors::kBlack),
                HasTransform(gfx::Transform::MakeTranslation(first_offset)))));

    resolved_frame.ResetAfterAggregation();
  }

  {
    // Next aggregation with no updated CompositorFrame.
    resolved_frame.SetRenderPassPointers();

    // Send the same OffsetTagValues. This should reuse the
    // same modified render passes as the last aggregation.
    resolved_frame.UpdateOffsetTags(
        [&first_offset](const OffsetTagDefinition&) { return first_offset; });

    // TODO: Verify that RebuildRenderPassesForOffsetTags() wasn't called.
    resolved_frame.ResetAfterAggregation();
  }

  {
    // Next aggregation with no updated CompositorFrame.
    resolved_frame.SetRenderPassPointers();

    // Change the offset. This should require a new copy of the render passes.
    resolved_frame.UpdateOffsetTags(
        [&second_offset](const OffsetTagDefinition&) { return second_offset; });

    const CompositorRenderPass& resolved_render_pass =
        resolved_frame.GetResolvedPasses()[0].render_pass();

    // Verify that the tagged quad has the new offset applied.
    EXPECT_THAT(
        resolved_render_pass.quad_list,
        testing::ElementsAre(
            testing::AllOf(IsSolidColorQuad(SkColors::kRed),
                           HasTransform(gfx::Transform::MakeTranslation({}))),
            testing::AllOf(
                IsSolidColorQuad(SkColors::kBlack),
                HasTransform(gfx::Transform::MakeTranslation(second_offset)))));

    resolved_frame.ResetAfterAggregation();
  }

  {
    // Next aggregation with new CompositorFrame but same OffsetTag values.
    auto new_frame =
        CompositorFrameBuilder()
            .AddRenderPass(
                RenderPassBuilder(kOutputRect)
                    .AddSolidColorQuad(gfx::Rect(10, 10), SkColors::kBlack)
                    .SetQuadOffsetTag(offset_tag))
            .AddOffsetTagDefinition(tag_def)
            .Build();
    SubmitCompositorFrame(std::move(new_frame));

    resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
    EXPECT_TRUE(resolved_frame.is_valid());

    resolved_frame.UpdateOffsetTags(
        [&second_offset](const OffsetTagDefinition&) { return second_offset; });

    ASSERT_THAT(resolved_frame.GetResolvedPasses(), testing::SizeIs(1));
    const CompositorRenderPass& resolved_render_pass =
        resolved_frame.GetResolvedPasses()[0].render_pass();

    // Verify that a new modified copy of render passes has been made that now
    // only contains the modified quad.
    EXPECT_THAT(
        resolved_render_pass.quad_list,
        testing::ElementsAre(testing::AllOf(
            IsSolidColorQuad(SkColors::kBlack),
            HasTransform(gfx::Transform::MakeTranslation(second_offset)))));

    resolved_frame.ResetAfterAggregation();
  }
}

TEST_F(ResolvedFrameDataTest, OffsetTagValueIsClamped) {
  auto offset_tag = OffsetTag::CreateRandom();

  OffsetTagDefinition tag_def;
  tag_def.tag = offset_tag;
  tag_def.provider = SurfaceRange(MakeSurfaceId());
  tag_def.constraints = OffsetTagConstraints(-10.0f, 10.0f, -10.0f, 10.0f);

  auto frame = CompositorFrameBuilder()
                   .AddRenderPass(RenderPassBuilder(kOutputRect)
                                      .AddSolidColorQuad(gfx::Rect(10, 10),
                                                         SkColors::kBlack)
                                      .SetQuadOffsetTag(offset_tag))
                   .AddOffsetTagDefinition(tag_def)
                   .Build();

  Surface* surface = SubmitCompositorFrame(std::move(frame));
  ResolvedFrameData resolved_frame(&resource_provider_, surface, 0u,
                                   AggregatedRenderPassId());

  constexpr gfx::Vector2dF offset(20.0f, -20.0f);

  // This offset will be clamped to 10, -10 by constraints.
  constexpr gfx::Vector2dF clamped_offset(10.0f, -10.0f);
  EXPECT_EQ(clamped_offset, tag_def.constraints.Clamp(offset));

  resolved_frame.UpdateForActiveFrame(render_pass_id_generator_);
  EXPECT_TRUE(resolved_frame.is_valid());

  resolved_frame.UpdateOffsetTags(
      [&offset](const OffsetTagDefinition&) { return offset; });

  ASSERT_THAT(resolved_frame.GetResolvedPasses(), testing::SizeIs(1));
  const CompositorRenderPass& resolved_render_pass =
      resolved_frame.GetResolvedPasses()[0].render_pass();

  // Verify that the tagged quad has clamped offset applied.
  EXPECT_THAT(
      resolved_render_pass.quad_list,
      testing::ElementsAre(testing::AllOf(
          IsSolidColorQuad(SkColors::kBlack),
          HasTransform(gfx::Transform::MakeTranslation(clamped_offset)))));

  resolved_frame.ResetAfterAggregation();
}

}  // namespace
}  // namespace viz
