// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <memory>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "cc/paint/filter_operation.h"
#include "cc/paint/filter_operations.h"
#include "cc/test/fake_output_surface_client.h"
#include "components/viz/client/client_resource_provider.h"
#include "components/viz/common/display/renderer_settings.h"
#include "components/viz/common/features.h"
#include "components/viz/common/frame_sinks/copy_output_request.h"
#include "components/viz/common/quads/aggregated_render_pass.h"
#include "components/viz/common/quads/aggregated_render_pass_draw_quad.h"
#include "components/viz/common/quads/solid_color_draw_quad.h"
#include "components/viz/common/quads/texture_draw_quad.h"
#include "components/viz/common/quads/yuv_video_draw_quad.h"
#include "components/viz/common/resources/transferable_resource.h"
#include "components/viz/service/display/aggregated_frame.h"
#include "components/viz/service/display/dc_layer_overlay.h"
#include "components/viz/service/display/display_resource_provider_skia.h"
#include "components/viz/service/display/output_surface.h"
#include "components/viz/service/display/overlay_candidate.h"
#include "components/viz/service/display/overlay_processor_win.h"
#include "components/viz/test/fake_skia_output_surface.h"
#include "components/viz/test/overlay_candidate_matchers.h"
#include "components/viz/test/test_context_provider.h"
#include "gpu/config/gpu_finch_features.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/linear_gradient.h"
#include "ui/gfx/geometry/mask_filter_info.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rrect_f.h"
#include "ui/gfx/hdr_metadata.h"
#include "ui/gfx/video_types.h"

using testing::_;
using testing::Mock;

namespace viz {
namespace {

const gfx::Rect kOverlayRect(0, 0, 256, 256);
const gfx::Rect kOverlayBottomRightRect(128, 128, 128, 128);

// An arbitrary render pass ID that can be treated as the implicit root pass ID
// by the test suites and helper functions.
const AggregatedRenderPassId kDefaultRootPassId{1};

class MockDCLayerOutputSurface : public FakeSkiaOutputSurface {
 public:
  static std::unique_ptr<MockDCLayerOutputSurface> Create() {
    auto provider = TestContextProvider::Create();
    provider->BindToCurrentSequence();
    return std::make_unique<MockDCLayerOutputSurface>(std::move(provider));
  }

  explicit MockDCLayerOutputSurface(scoped_refptr<ContextProvider> provider)
      : FakeSkiaOutputSurface(std::move(provider)) {
    capabilities_.supports_dc_layers = true;
  }

  // OutputSurface implementation.
  MOCK_METHOD1(SetEnableDCLayers, void(bool));
};

class DCTestOverlayProcessor : public OverlayProcessorWin {
 public:
  DCTestOverlayProcessor(OutputSurface* output_surface,
                         int allowed_yuv_overlay_count)
      : OverlayProcessorWin(output_surface,
                            &debug_settings_,
                            std::make_unique<DCLayerOverlayProcessor>(
                                allowed_yuv_overlay_count,
                                /*skip_initialization_for_testing=*/true)) {}
  DebugRendererSettings debug_settings_;
};

std::unique_ptr<AggregatedRenderPass> CreateRenderPass(
    AggregatedRenderPassId render_pass_id = kDefaultRootPassId) {
  gfx::Rect output_rect(0, 0, 256, 256);

  auto pass = std::make_unique<AggregatedRenderPass>();
  pass->SetNew(render_pass_id, output_rect, output_rect, gfx::Transform());

  SharedQuadState* shared_state = pass->CreateAndAppendSharedQuadState();
  shared_state->opacity = 1.f;
  return pass;
}

static ResourceId CreateResourceInLayerTree(
    ClientResourceProvider* child_resource_provider,
    const gfx::Size& size,
    bool is_overlay_candidate) {
  auto resource = TransferableResource::MakeGpu(
      gpu::Mailbox::Generate(), GL_TEXTURE_2D, gpu::SyncToken(), size,
      SinglePlaneFormat::kRGBA_8888, is_overlay_candidate);

  ResourceId resource_id =
      child_resource_provider->ImportResource(resource, base::DoNothing());

  return resource_id;
}

ResourceId CreateResource(DisplayResourceProvider* parent_resource_provider,
                          ClientResourceProvider* child_resource_provider,
                          RasterContextProvider* child_context_provider,
                          const gfx::Size& size,
                          bool is_overlay_candidate) {
  ResourceId resource_id = CreateResourceInLayerTree(
      child_resource_provider, size, is_overlay_candidate);

  int child_id =
      parent_resource_provider->CreateChild(base::DoNothing(), SurfaceId());

  // Transfer resource to the parent.
  std::vector<ResourceId> resource_ids_to_transfer;
  resource_ids_to_transfer.push_back(resource_id);
  std::vector<TransferableResource> list;
  child_resource_provider->PrepareSendToParent(resource_ids_to_transfer, &list,
                                               child_context_provider);
  parent_resource_provider->ReceiveFromChild(child_id, list);

  // Delete it in the child so it won't be leaked, and will be released once
  // returned from the parent.
  child_resource_provider->RemoveImportedResource(resource_id);

  // In DisplayResourceProvider's namespace, use the mapped resource id.
  std::unordered_map<ResourceId, ResourceId, ResourceIdHasher> resource_map =
      parent_resource_provider->GetChildToParentMap(child_id);
  return resource_map[list[0].id];
}

SolidColorDrawQuad* CreateSolidColorQuadAt(
    const SharedQuadState* shared_quad_state,
    SkColor4f color,
    AggregatedRenderPass* render_pass,
    const gfx::Rect& rect) {
  SolidColorDrawQuad* quad =
      render_pass->CreateAndAppendDrawQuad<SolidColorDrawQuad>();
  quad->SetNew(shared_quad_state, rect, rect, color, false);
  return quad;
}

TextureDrawQuad* CreateTextureQuadAt(
    DisplayResourceProvider* parent_resource_provider,
    ClientResourceProvider* child_resource_provider,
    RasterContextProvider* child_context_provider,
    const SharedQuadState* shared_quad_state,
    AggregatedRenderPass* render_pass,
    const gfx::Rect& rect,
    bool is_overlay_candidate = true) {
  ResourceId resource_id =
      CreateResource(parent_resource_provider, child_resource_provider,
                     child_context_provider, rect.size(), is_overlay_candidate);
  auto* quad = render_pass->CreateAndAppendDrawQuad<TextureDrawQuad>();
  quad->SetNew(shared_quad_state, rect, /*visible_rect=*/rect,
               /*needs_blending=*/false, resource_id, /*premultiplied=*/true,
               /*top_left=*/gfx::PointF(0, 0),
               /*bottom_right=*/gfx::PointF(1, 1),
               /*background=*/SkColors::kBlack, /*flipped=*/false,
               /*nearest=*/false, /*secure_output=*/false,
               gfx::ProtectedVideoType::kClear);
  return quad;
}

void CreateOpaqueQuadAt(DisplayResourceProvider* resource_provider,
                        const SharedQuadState* shared_quad_state,
                        AggregatedRenderPass* render_pass,
                        const gfx::Rect& rect,
                        SkColor4f color) {
  DCHECK(color.isOpaque());
  auto* color_quad = render_pass->CreateAndAppendDrawQuad<SolidColorDrawQuad>();
  color_quad->SetNew(shared_quad_state, rect, rect, color, false);
}

YUVVideoDrawQuad* CreateFullscreenCandidateYUVVideoQuad(
    DisplayResourceProvider* parent_resource_provider,
    ClientResourceProvider* child_resource_provider,
    RasterContextProvider* child_context_provider,
    const SharedQuadState* shared_quad_state,
    AggregatedRenderPass* render_pass) {
  bool needs_blending = false;
  gfx::Rect rect = render_pass->output_rect;
  gfx::Size resource_size_in_pixels = rect.size();
  bool is_overlay_candidate = true;
  ResourceId resource_id = CreateResource(
      parent_resource_provider, child_resource_provider, child_context_provider,
      resource_size_in_pixels, is_overlay_candidate);

  auto* overlay_quad = render_pass->CreateAndAppendDrawQuad<YUVVideoDrawQuad>();
  overlay_quad->SetNew(
      shared_quad_state, rect, rect, needs_blending, resource_size_in_pixels,
      gfx::Rect(resource_size_in_pixels), gfx::Size(1, 1), resource_id,
      resource_id, resource_id, resource_id, gfx::ColorSpace::CreateREC601(), 8,
      gfx::ProtectedVideoType::kClear, std::nullopt);

  return overlay_quad;
}

AggregatedRenderPassDrawQuad* CreateRenderPassDrawQuadAt(
    AggregatedRenderPass* render_pass,
    const SharedQuadState* shared_quad_state,
    const gfx::Rect& rect,
    AggregatedRenderPassId render_pass_id) {
  AggregatedRenderPassDrawQuad* quad =
      render_pass->CreateAndAppendDrawQuad<AggregatedRenderPassDrawQuad>();
  quad->SetNew(shared_quad_state, rect, rect, render_pass_id, ResourceId(2),
               gfx::RectF(), gfx::Size(), gfx::Vector2dF(1, 1), gfx::PointF(),
               gfx::RectF(), false, 1.f);
  return quad;
}

SkM44 GetIdentityColorMatrix() {
  return SkM44();
}

class DCLayerOverlayTest : public testing::Test {
 protected:
  DCLayerOverlayTest() {
    std::vector<base::test::FeatureRef> enabled_features;
    std::vector<base::test::FeatureRef> disabled_features;

    // With DisableVideoOverlayIfMoving, videos are required to be stable for a
    // certain number of frames to be considered for overlay promotion. This
    // complicates tests since it adds behavior dependent on the number of times
    // |Process| is called.
    disabled_features.push_back(features::kDisableVideoOverlayIfMoving);

    feature_list_.InitWithFeatures(enabled_features, disabled_features);
  }

  void InitializeOverlayProcessor(int allowed_yuv_overlay_count = 1) {
    overlay_processor_ = std::make_unique<DCTestOverlayProcessor>(
        output_surface_.get(), allowed_yuv_overlay_count);
    overlay_processor_->SetUsingDCLayersForTesting(kDefaultRootPassId, true);
    overlay_processor_->SetViewportSize(gfx::Size(256, 256));
    overlay_processor_
        ->set_frames_since_last_qualified_multi_overlays_for_testing(5);
    EXPECT_TRUE(overlay_processor_->IsOverlaySupported());
  }

  void SetUp() override {
    output_surface_ = MockDCLayerOutputSurface::Create();
    output_surface_->BindToClient(&output_surface_client_);

    resource_provider_ = std::make_unique<DisplayResourceProviderSkia>();
    lock_set_for_external_use_.emplace(resource_provider_.get(),
                                       output_surface_.get());

    child_provider_ = TestContextProvider::Create();
    child_provider_->BindToCurrentSequence();
    child_resource_provider_ = std::make_unique<ClientResourceProvider>();

    output_surface_plane_ =
        OverlayProcessorInterface::OutputSurfaceOverlayPlane();
  }

  void TearDown() override {
    overlay_processor_ = nullptr;
    child_resource_provider_->ShutdownAndReleaseAllResources();
    child_resource_provider_ = nullptr;
    child_provider_ = nullptr;
    lock_set_for_external_use_.reset();
    resource_provider_ = nullptr;
    output_surface_ = nullptr;
  }

  OverlayProcessorInterface::OutputSurfaceOverlayPlane*
  GetOutputSurfacePlane() {
    EXPECT_TRUE(output_surface_plane_.has_value());
    return &output_surface_plane_.value();
  }

  void TestRenderPassRootTransform(bool is_overlay);

  base::test::ScopedFeatureList feature_list_;
  std::unique_ptr<MockDCLayerOutputSurface> output_surface_;
  std::optional<OverlayProcessorInterface::OutputSurfaceOverlayPlane>
      output_surface_plane_;
  cc::FakeOutputSurfaceClient output_surface_client_;
  std::unique_ptr<DisplayResourceProviderSkia> resource_provider_;
  std::optional<DisplayResourceProviderSkia::LockSetForExternalUse>
      lock_set_for_external_use_;
  scoped_refptr<TestContextProvider> child_provider_;
  std::unique_ptr<ClientResourceProvider> child_resource_provider_;
  std::unique_ptr<OverlayProcessorWin> overlay_processor_;
  gfx::Rect damage_rect_;
  std::vector<gfx::Rect> content_bounds_;
};

TEST_F(DCLayerOverlayTest, DisableVideoOverlayIfMovingFeature) {
  InitializeOverlayProcessor();
  auto ProcessForOverlaysSingleVideoRectWithOffset =
      [&](gfx::Vector2d video_rect_offset, bool is_hdr = false,
          bool is_sdr_to_hdr = false) {
        auto pass = CreateRenderPass();
        auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
            resource_provider_.get(), child_resource_provider_.get(),
            child_provider_.get(), pass->shared_quad_state_list.back(),
            pass.get());
        video_quad->rect = gfx::Rect(0, 0, 10, 10) + video_rect_offset;
        video_quad->visible_rect = gfx::Rect(0, 0, 10, 10) + video_rect_offset;

        if (is_hdr) {
          // Render Pass has HDR content usage.
          pass->content_color_usage = gfx::ContentColorUsage::kHDR;

          // Device has RGB10A2 overlay support.
          gl::SetDirectCompositionScaledOverlaysSupportedForTesting(true);

          // Device has HDR-enabled display and no non-HDR-enabled display.
          overlay_processor_
              ->set_system_hdr_disabled_on_any_display_for_testing(false);

          // Device has video processor support.
          overlay_processor_->set_has_p010_video_processor_support_for_testing(
              true);

          // Content is 10bit P010 content.
          video_quad->bits_per_channel = 10;

          // Content has valid HDR metadata.
          video_quad->hdr_metadata = gfx::HDRMetadata();
          video_quad->hdr_metadata->cta_861_3 =
              gfx::HdrMetadataCta861_3(1000, 400);
          video_quad->hdr_metadata->smpte_st_2086 = gfx::HdrMetadataSmpteSt2086(
              SkNamedPrimariesExt::kRec2020, 1000, 0.0001);

          // Content has HDR10 colorspace.
          video_quad->video_color_space = gfx::ColorSpace::CreateHDR10();
        } else if (is_sdr_to_hdr) {
          // Render Pass has SDR content usage.
          pass->content_color_usage = gfx::ContentColorUsage::kSRGB;

          // Content is 8bit NV12 content.
          video_quad->bits_per_channel = 8;

          // Device is not using battery power.
          overlay_processor_->set_is_on_battery_power_for_testing(false);

          // Device has at least one HDR-enabled display.
          overlay_processor_->set_system_hdr_enabled_on_any_display_for_testing(
              true);

          // Device has video processor auto hdr support.
          overlay_processor_
              ->set_has_auto_hdr_video_processor_support_for_testing(true);

          // Content has 709 colorspace.
          video_quad->video_color_space = gfx::ColorSpace::CreateREC709();
        }

        OverlayCandidateList dc_layer_list;
        OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
        OverlayProcessorInterface::FilterOperationsMap
            render_pass_backdrop_filters;

        AggregatedRenderPassList pass_list;
        pass_list.push_back(std::move(pass));

        overlay_processor_->ProcessForOverlays(
            resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
            render_pass_filters, render_pass_backdrop_filters, {},
            GetOutputSurfacePlane(), &dc_layer_list, &damage_rect_,
            &content_bounds_);

        return dc_layer_list;
      };

  {
    base::test::ScopedFeatureList scoped_feature_list;
    scoped_feature_list.InitAndDisableFeature(
        features::kDisableVideoOverlayIfMoving);
    EXPECT_EQ(1U, ProcessForOverlaysSingleVideoRectWithOffset({0, 0}).size());
    EXPECT_EQ(1U, ProcessForOverlaysSingleVideoRectWithOffset({1, 0}).size());
  }

  {
    base::test::ScopedFeatureList scoped_feature_list;
    scoped_feature_list.InitAndEnableFeature(
        features::kDisableVideoOverlayIfMoving);
    // We expect an overlay promotion after a couple frames of no movement
    for (int i = 0; i < 10; i++) {
      ProcessForOverlaysSingleVideoRectWithOffset({0, 0}).size();
    }
    EXPECT_EQ(1U, ProcessForOverlaysSingleVideoRectWithOffset({0, 0}).size());

    // Since the overlay candidate moved, we expect no overlays
    EXPECT_EQ(0U, ProcessForOverlaysSingleVideoRectWithOffset({1, 0}).size());

    // After some number of frames with no movement, we expect an overlay again
    for (int i = 0; i < 10; i++) {
      ProcessForOverlaysSingleVideoRectWithOffset({1, 0}).size();
    }
    EXPECT_EQ(1U, ProcessForOverlaysSingleVideoRectWithOffset({1, 0}).size());
  }

  {
    base::test::ScopedFeatureList scoped_feature_list;
    scoped_feature_list.InitAndEnableFeature(
        features::kDisableVideoOverlayIfMoving);
    // We expect an overlay promotion after a couple frames of no movement
    for (int i = 0; i < 10; i++) {
      ProcessForOverlaysSingleVideoRectWithOffset({0, 0}, /*is_hdr=*/false,
                                                  /*is_sdr_to_hdr*/ true)
          .size();
    }
    EXPECT_EQ(1U, ProcessForOverlaysSingleVideoRectWithOffset(
                      {0, 0}, /*is_hdr=*/false, /*is_sdr_to_hdr*/ true)
                      .size());
    // We still expect an overlay promotion for SDR video when auto hdr is
    // enabled and when moving to ensure uniform tone mapping results between
    // viz and GPU driver.
    EXPECT_EQ(1U, ProcessForOverlaysSingleVideoRectWithOffset(
                      {1, 0}, /*is_hdr=*/false, /*is_sdr_to_hdr*/ true)
                      .size());
  }

  {
    base::test::ScopedFeatureList scoped_feature_list;
    scoped_feature_list.InitAndEnableFeature(
        features::kDisableVideoOverlayIfMoving);
    // We expect an overlay promotion after a couple frames of no movement
    for (int i = 0; i < 10; i++) {
      ProcessForOverlaysSingleVideoRectWithOffset({0, 0}, /*is_hdr=*/true)
          .size();
    }
    EXPECT_EQ(
        1U, ProcessForOverlaysSingleVideoRectWithOffset({0, 0}, /*is_hdr=*/true)
                .size());
    // We still expect an overlay promotion for HDR video when moving to
    // ensure uniform tone mapping results between viz and GPU driver.
    EXPECT_EQ(
        1U, ProcessForOverlaysSingleVideoRectWithOffset({1, 0}, /*is_hdr=*/true)
                .size());
  }
}

// Check that we don't accidentally end up in a case where we try to read back a
// DComp surface, which can happen if one issues a copy request while we're in
// the hysteresis when switching from a DComp surface back to a swap chain.
TEST_F(DCLayerOverlayTest, ForceSwapChainForCapture) {
  InitializeOverlayProcessor();

  // Frame with no overlays, but we expect to still be in DComp surface mode,
  // due to one-sided hysteresis intended to prevent allocation churn.
  {
    AggregatedRenderPassList pass_list;
    pass_list.push_back(CreateRenderPass());

    damage_rect_ = pass_list.back()->output_rect;

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        SurfaceDamageRectList(), GetOutputSurfacePlane(), &dc_layer_list,
        &damage_rect_, &content_bounds_);

    EXPECT_TRUE(pass_list.back()->needs_synchronous_dcomp_commit);
  }

  // Frame with a copy request. Even though we're still in the hysteresis, we
  // expect to forcibly switch to swap chain mode so that the copy request
  // succeeds.
  {
    AggregatedRenderPassList pass_list;
    pass_list.push_back(CreateRenderPass());

    pass_list.back()->copy_requests.push_back(
        CopyOutputRequest::CreateStubForTesting());

    damage_rect_ = pass_list.back()->output_rect;

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        SurfaceDamageRectList(), GetOutputSurfacePlane(), &dc_layer_list,
        &damage_rect_, &content_bounds_);

    EXPECT_FALSE(pass_list.back()->needs_synchronous_dcomp_commit);
  }
}

TEST_F(DCLayerOverlayTest, Occluded) {
  InitializeOverlayProcessor();
  {
    auto pass = CreateRenderPass();
    SharedQuadState* first_shared_state = pass->shared_quad_state_list.back();
    first_shared_state->overlay_damage_index = 0;
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(0, 3, 100, 100), SkColors::kWhite);

    SharedQuadState* second_shared_state =
        pass->CreateAndAppendSharedQuadState();
    second_shared_state->overlay_damage_index = 1;
    auto* first_video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    // Set the protected video flag will force the quad to use hw overlay
    first_video_quad->protected_video_type =
        gfx::ProtectedVideoType::kHardwareProtected;

    SharedQuadState* third_shared_state =
        pass->CreateAndAppendSharedQuadState();
    third_shared_state->overlay_damage_index = 2;
    auto* second_video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    // Set the protected video flag will force the quad to use hw overlay
    second_video_quad->protected_video_type =
        gfx::ProtectedVideoType::kHardwareProtected;
    second_video_quad->rect.set_origin(gfx::Point(2, 2));
    second_video_quad->visible_rect.set_origin(gfx::Point(2, 2));

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(1, 1, 10, 10);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = {
        gfx::Rect(1, 1, 10, 10), gfx::Rect(0, 0, 0, 0), gfx::Rect(0, 0, 0, 0)};

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    EXPECT_EQ(2U, dc_layer_list.size());
    EXPECT_EQ(-1, dc_layer_list.front().plane_z_order);
    EXPECT_EQ(-2, dc_layer_list.back().plane_z_order);
    // Entire underlay rect must be redrawn.
    EXPECT_EQ(gfx::Rect(0, 0, 256, 256), damage_rect_);
  }
  {
    auto pass = CreateRenderPass();
    SharedQuadState* first_shared_state = pass->shared_quad_state_list.back();
    first_shared_state->overlay_damage_index = 0;
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(3, 3, 100, 100), SkColors::kWhite);

    SharedQuadState* second_shared_state =
        pass->CreateAndAppendSharedQuadState();
    second_shared_state->overlay_damage_index = 1;
    auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    // Set the protected video flag will force the quad to use hw overlay
    video_quad->protected_video_type =
        gfx::ProtectedVideoType::kHardwareProtected;

    SharedQuadState* third_shared_state =
        pass->CreateAndAppendSharedQuadState();
    third_shared_state->overlay_damage_index = 2;
    auto* second_video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    second_video_quad->protected_video_type =
        gfx::ProtectedVideoType::kHardwareProtected;
    second_video_quad->rect.set_origin(gfx::Point(2, 2));
    second_video_quad->visible_rect.set_origin(gfx::Point(2, 2));

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(1, 1, 10, 10);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = {
        gfx::Rect(1, 1, 10, 10), gfx::Rect(0, 0, 0, 0), gfx::Rect(0, 0, 0, 0)};

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    EXPECT_EQ(2U, dc_layer_list.size());
    EXPECT_EQ(-1, dc_layer_list.front().plane_z_order);
    EXPECT_EQ(-2, dc_layer_list.back().plane_z_order);

    // The underlay rectangle is the same, so the damage for first video quad is
    // contained within the combined occluding rects for this and the last
    // frame. Second video quad also adds its damage.
    EXPECT_EQ(gfx::Rect(1, 1, 10, 10), damage_rect_);
  }
}

TEST_F(DCLayerOverlayTest, DamageRectWithoutVideoDamage) {
  InitializeOverlayProcessor();
  {
    auto pass = CreateRenderPass();
    SharedQuadState* shared_quad_state = pass->shared_quad_state_list.back();
    shared_quad_state->overlay_damage_index = 0;
    // Occluding quad fully contained in video rect.
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(0, 3, 100, 100), SkColors::kWhite);
    // Non-occluding quad fully outside video rect
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(210, 210, 20, 20), SkColors::kWhite);

    // Underlay video quad
    SharedQuadState* second_shared_state =
        pass->CreateAndAppendSharedQuadState();
    second_shared_state->overlay_damage_index = 1;
    auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    video_quad->rect = gfx::Rect(0, 0, 200, 200);
    video_quad->visible_rect = video_quad->rect;

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    // Damage rect fully outside video quad
    damage_rect_ = gfx::Rect(210, 210, 20, 20);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = {
        gfx::Rect(210, 210, 20, 20), gfx::Rect(0, 0, 0, 0)};

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);
    EXPECT_EQ(1U, dc_layer_list.size());
    EXPECT_EQ(-1, dc_layer_list.back().plane_z_order);
    // All rects must be redrawn at the first frame.
    EXPECT_EQ(gfx::Rect(0, 0, 230, 230), damage_rect_);
  }
  {
    auto pass = CreateRenderPass();
    SharedQuadState* shared_quad_state = pass->shared_quad_state_list.back();
    shared_quad_state->overlay_damage_index = 0;
    // Occluding quad fully contained in video rect.
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(0, 3, 100, 100), SkColors::kWhite);
    // Non-occluding quad fully outside video rect
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(210, 210, 20, 20), SkColors::kWhite);

    // Underlay video quad
    SharedQuadState* second_shared_state =
        pass->CreateAndAppendSharedQuadState();
    second_shared_state->overlay_damage_index = 1;
    auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    video_quad->rect = gfx::Rect(0, 0, 200, 200);
    video_quad->visible_rect = video_quad->rect;

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    // Damage rect fully outside video quad
    damage_rect_ = gfx::Rect(210, 210, 20, 20);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = {
        gfx::Rect(210, 210, 20, 20), gfx::Rect(0, 0, 0, 0)};

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);
    EXPECT_EQ(1U, dc_layer_list.size());
    EXPECT_EQ(-1, dc_layer_list.back().plane_z_order);
    // Only the non-overlay damaged rect need to be drawn by the gl compositor
    EXPECT_EQ(gfx::Rect(210, 210, 20, 20), damage_rect_);
  }
}

TEST_F(DCLayerOverlayTest, DamageRect) {
  InitializeOverlayProcessor();
  for (int i = 0; i < 2; i++) {
    SCOPED_TRACE(base::StringPrintf("Frame %d", i));
    auto pass = CreateRenderPass();
    SharedQuadState* shared_quad_state = pass->shared_quad_state_list.back();
    shared_quad_state->overlay_damage_index = 0;
    CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(1, 1, 10, 10);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = {gfx::Rect(1, 1, 10, 10)};

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);
    EXPECT_EQ(1U, dc_layer_list.size());
    EXPECT_EQ(1, dc_layer_list.back().plane_z_order);
    // Damage rect should be unchanged on initial frame because of resize, but
    // should be empty on the second frame because everything was put in a
    // layer.
    if (i == 1)
      EXPECT_TRUE(damage_rect_.IsEmpty());
    else
      EXPECT_EQ(gfx::Rect(1, 1, 10, 10), damage_rect_);
  }
}

TEST_F(DCLayerOverlayTest, ClipRect) {
  InitializeOverlayProcessor();
  // Process twice. The second time through the overlay list shouldn't change,
  // which will allow the damage rect to reflect just the changes in that
  // frame.
  for (size_t i = 0; i < 2; ++i) {
    auto pass = CreateRenderPass();
    pass->shared_quad_state_list.back()->overlay_damage_index = 0;
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(0, 2, 100, 100), SkColors::kWhite);
    pass->shared_quad_state_list.back()->clip_rect = gfx::Rect(0, 3, 100, 100);

    SharedQuadState* shared_state = pass->CreateAndAppendSharedQuadState();
    shared_state->opacity = 1.f;
    shared_state->overlay_damage_index = 1;
    CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), shared_state, pass.get());
    // Clipped rect shouldn't be overlapped by clipped opaque quad rect.
    shared_state->clip_rect = gfx::Rect(0, 0, 100, 3);

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    damage_rect_ = gfx::Rect(1, 1, 10, 10);
    SurfaceDamageRectList surface_damage_rect_list = {gfx::Rect(1, 3, 10, 8),
                                                      gfx::Rect(1, 1, 10, 2)};

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);
    EXPECT_EQ(1U, dc_layer_list.size());
    // Because of clip rects the overlay isn't occluded and shouldn't be an
    // underlay.
    EXPECT_EQ(1, dc_layer_list.back().plane_z_order);
    EXPECT_EQ(gfx::Rect(0, 0, 100, 3), dc_layer_list.back().clip_rect);
    if (i == 1) {
      // The damage rect should only contain contents that aren't in the
      // clipped overlay rect.
      EXPECT_EQ(gfx::Rect(1, 3, 10, 8), damage_rect_);
    }
  }
}

TEST_F(DCLayerOverlayTest, TransparentOnTop) {
  InitializeOverlayProcessor();
  // Process twice. The second time through the overlay list shouldn't change,
  // which will allow the damage rect to reflect just the changes in that
  // frame.
  for (size_t i = 0; i < 2; ++i) {
    auto pass = CreateRenderPass();
    pass->shared_quad_state_list.back()->overlay_damage_index = 0;
    CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    pass->shared_quad_state_list.back()->opacity = 0.5f;

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(1, 1, 10, 10);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = {gfx::Rect(1, 1, 10, 10)};

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);
    EXPECT_EQ(1U, dc_layer_list.size());
    EXPECT_EQ(1, dc_layer_list.back().plane_z_order);
    // Quad isn't opaque, so underlying damage must remain the same.
    EXPECT_EQ(gfx::Rect(1, 1, 10, 10), damage_rect_);
  }
}

TEST_F(DCLayerOverlayTest, UnderlayDamageRectWithQuadOnTopUnchanged) {
  InitializeOverlayProcessor();
  for (int i = 0; i < 3; i++) {
    auto pass = CreateRenderPass();
    // Add a solid color quad on top
    SharedQuadState* shared_state_on_top = pass->shared_quad_state_list.back();
    CreateSolidColorQuadAt(shared_state_on_top, SkColors::kRed, pass.get(),
                           kOverlayBottomRightRect);

    SharedQuadState* shared_state = pass->CreateAndAppendSharedQuadState();
    shared_state->opacity = 1.f;
    CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), shared_state, pass.get());

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    gfx::Rect damage_rect_ = kOverlayRect;
    shared_state->overlay_damage_index = 1;

    // The quad on top does not give damage on the third frame
    SurfaceDamageRectList surface_damage_rect_list = {kOverlayBottomRightRect,
                                                      kOverlayRect};
    if (i == 2) {
      surface_damage_rect_list[0] = gfx::Rect();
    }

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);
    EXPECT_EQ(1U, dc_layer_list.size());
    EXPECT_EQ(-1, dc_layer_list.back().plane_z_order);
    // Damage rect should be unchanged on initial frame, but should be reduced
    // to the size of quad on top, and empty on the third frame.
    if (i == 0)
      EXPECT_EQ(kOverlayRect, damage_rect_);
    else if (i == 1)
      EXPECT_EQ(kOverlayBottomRightRect, damage_rect_);
    else if (i == 2)
      EXPECT_EQ(gfx::Rect(), damage_rect_);
  }
}

// Test whether quads with rounded corners are supported.
TEST_F(DCLayerOverlayTest, RoundedCorners) {
  InitializeOverlayProcessor();
  // Frame #0
  {
    auto pass = CreateRenderPass();

    // Create a video YUV quad with rounded corner, nothing on top.
    auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    gfx::Rect rect(0, 0, 256, 256);
    video_quad->rect = rect;
    video_quad->visible_rect = rect;
    pass->shared_quad_state_list.back()->overlay_damage_index = 0;
    // Rounded corners
    pass->shared_quad_state_list.back()->mask_filter_info =
        gfx::MaskFilterInfo(gfx::RRectF(gfx::RectF(0.f, 0.f, 20.f, 30.f), 5.f));

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 256, 256);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = {
        gfx::Rect(0, 0, 256, 256)};

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    auto* root_pass = pass_list.back().get();
    auto* replaced_quad = root_pass->quad_list.back();
    auto* replaced_sqs = replaced_quad->shared_quad_state;

    // The video should be forced to an underlay mode, even there is nothing on
    // top.
    EXPECT_EQ(1U, dc_layer_list.size());
    EXPECT_EQ(-1, dc_layer_list.back().plane_z_order);

    // Check whether there is a replaced quad in the quad list.
    EXPECT_EQ(1U, root_pass->quad_list.size());

    // Check whether blend mode == kDstOut, color == black and still have the
    // rounded corner mask filter for the replaced solid quad.
    EXPECT_EQ(replaced_sqs->blend_mode, SkBlendMode::kDstOut);
    EXPECT_EQ(SolidColorDrawQuad::MaterialCast(replaced_quad)->color,
              SkColors::kBlack);
    EXPECT_TRUE(replaced_sqs->mask_filter_info.HasRoundedCorners());

    // The whole frame is damaged.
    EXPECT_EQ(gfx::Rect(0, 0, 256, 256), damage_rect_);
  }

  // Frame #1
  {
    auto pass = CreateRenderPass();
    // Create a solid quad.
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(0, 0, 32, 32), SkColors::kRed);

    // Create a video YUV quad with rounded corners below the red solid quad.
    auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    gfx::Rect rect(0, 0, 256, 256);
    video_quad->rect = rect;
    video_quad->visible_rect = rect;
    pass->shared_quad_state_list.back()->overlay_damage_index = 1;
    // Rounded corners
    pass->shared_quad_state_list.back()->mask_filter_info =
        gfx::MaskFilterInfo(gfx::RRectF(gfx::RectF(0.f, 0.f, 20.f, 30.f), 5.f));

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 256, 256);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = {
        gfx::Rect(0, 0, 32, 32), gfx::Rect(0, 0, 256, 256)};

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    auto* root_pass = pass_list.back().get();
    auto* replaced_quad = root_pass->quad_list.back();
    auto* replaced_sqs = replaced_quad->shared_quad_state;

    // still in an underlay mode.
    EXPECT_EQ(1U, dc_layer_list.size());
    EXPECT_EQ(-1, dc_layer_list.back().plane_z_order);

    // Check whether the red quad on top and the replacedment of the YUV quad
    // are still in the render pass.
    EXPECT_EQ(2U, root_pass->quad_list.size());

    // Check whether blend mode is kDstOut, color is black, and still have the
    // rounded corner mask filter for the replaced solid quad.
    EXPECT_EQ(replaced_sqs->blend_mode, SkBlendMode::kDstOut);
    EXPECT_EQ(SolidColorDrawQuad::MaterialCast(replaced_quad)->color,
              SkColors::kBlack);
    EXPECT_TRUE(replaced_sqs->mask_filter_info.HasRoundedCorners());

    // Only the UI is damaged.
    EXPECT_EQ(gfx::Rect(0, 0, 32, 32), damage_rect_);
  }

  // Frame #2
  {
    auto pass = CreateRenderPass();
    // Create a solid quad.
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(0, 0, 32, 32), SkColors::kRed);

    // Create a video YUV quad with rounded corners below the red solid quad.
    auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    gfx::Rect rect(0, 0, 256, 256);
    video_quad->rect = rect;
    video_quad->visible_rect = rect;
    pass->shared_quad_state_list.back()->overlay_damage_index = 0;
    // Rounded corners
    pass->shared_quad_state_list.back()->mask_filter_info =
        gfx::MaskFilterInfo(gfx::RRectF(gfx::RectF(0.f, 0.f, 20.f, 30.f), 5.f));

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 256, 256);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = {
        gfx::Rect(0, 0, 256, 256)};

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    auto* root_pass = pass_list.back().get();
    auto* replaced_quad = root_pass->quad_list.back();
    auto* replaced_sqs = replaced_quad->shared_quad_state;

    // still in an underlay mode.
    EXPECT_EQ(1U, dc_layer_list.size());
    EXPECT_EQ(-1, dc_layer_list.back().plane_z_order);

    // Check whether the red quad on top and the replacedment of the YUV quad
    // are still in the render pass.
    EXPECT_EQ(2U, root_pass->quad_list.size());

    // Check whether blend mode is kDstOut and color is black for the replaced
    // solid quad.
    EXPECT_EQ(replaced_sqs->blend_mode, SkBlendMode::kDstOut);
    EXPECT_EQ(SolidColorDrawQuad::MaterialCast(replaced_quad)->color,
              SkColors::kBlack);
    EXPECT_TRUE(replaced_sqs->mask_filter_info.HasRoundedCorners());

    // Zero root damage rect.
    EXPECT_TRUE(damage_rect_.IsEmpty());
  }
}

// If there are multiple yuv overlay quad candidates, no overlay will be
// promoted to save power.
TEST_F(DCLayerOverlayTest, MultipleYUVOverlays) {
  InitializeOverlayProcessor();
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(
      features::kNoUndamagedOverlayPromotion);
  {
    auto pass = CreateRenderPass();
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(0, 0, 256, 256), SkColors::kWhite);

    auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    gfx::Rect rect(10, 10, 80, 80);
    video_quad->rect = rect;
    video_quad->visible_rect = rect;
    pass->shared_quad_state_list.back()->overlay_damage_index = 1;

    auto* second_video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    gfx::Rect second_rect(100, 100, 120, 120);
    second_video_quad->rect = second_rect;
    second_video_quad->visible_rect = second_rect;
    pass->shared_quad_state_list.back()->overlay_damage_index = 2;

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 220, 220);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list;
    surface_damage_rect_list.push_back(gfx::Rect(0, 0, 256, 256));
    surface_damage_rect_list.push_back(video_quad->rect);
    surface_damage_rect_list.push_back(second_video_quad->rect);

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    // Skip overlay.
    EXPECT_EQ(0U, dc_layer_list.size());
    EXPECT_EQ(gfx::Rect(0, 0, 220, 220), damage_rect_);

    // Check whether all 3 quads including two YUV quads are still in the render
    // pass
    auto* root_pass = pass_list.back().get();
    int quad_count = root_pass->quad_list.size();
    EXPECT_EQ(3, quad_count);
  }
}

TEST_F(DCLayerOverlayTest, SetEnableDCLayers) {
  InitializeOverlayProcessor();
  overlay_processor_->SetUsingDCLayersForTesting(kDefaultRootPassId, false);
  // Draw 60 frames with overlay video quads.
  for (int i = 0; i < 60; i++) {
    SCOPED_TRACE(base::StringPrintf("Frame with overlay %d", i));
    auto pass = CreateRenderPass();
    // Use an opaque pass to check that the overlay processor makes it
    // transparent in the case of overlays.
    pass->has_transparent_background = false;

    CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());

    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    SurfaceDamageRectList surface_damage_rect_list;
    damage_rect_ = gfx::Rect(1, 1, 10, 10);

    // There will be full damage and SetEnableDCLayers(true) will be called on
    // the first frame.
    const gfx::Rect expected_damage =
        (i == 0) ? pass_list.back()->output_rect : gfx::Rect();

    EXPECT_CALL(*output_surface_.get(), SetEnableDCLayers(_)).Times(0);

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    EXPECT_TRUE(pass_list.back()->needs_synchronous_dcomp_commit);
    EXPECT_TRUE(pass_list.back()->has_transparent_background);
    ASSERT_TRUE(output_surface_plane_.has_value());
    EXPECT_TRUE(output_surface_plane_->enable_blending);

    EXPECT_EQ(1U, dc_layer_list.size());
    EXPECT_EQ(1, dc_layer_list.back().plane_z_order);
    EXPECT_EQ(damage_rect_, expected_damage);

    Mock::VerifyAndClearExpectations(output_surface_.get());
  }

  // Draw 65 frames without overlays.
  for (int i = 0; i < 65; i++) {
    SCOPED_TRACE(base::StringPrintf("Frame without overlay %d", i));
    auto pass = CreateRenderPass();
    pass->has_transparent_background = false;

    damage_rect_ = gfx::Rect(1, 1, 10, 10);
    auto* quad = pass->CreateAndAppendDrawQuad<SolidColorDrawQuad>();
    quad->SetNew(pass->CreateAndAppendSharedQuadState(), damage_rect_,
                 damage_rect_, SkColors::kRed, false);

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;

    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list;

    damage_rect_ = gfx::Rect(1, 1, 10, 10);

    // There will be full damage and SetEnableDCLayers(false) will be called
    // after 60 consecutive frames with no overlays. The first frame without
    // overlays will also have full damage, but no call to SetEnableDCLayers.
    const gfx::Rect expected_damage = (i == 0 || (i + 1) == 60)
                                          ? pass_list.back()->output_rect
                                          : damage_rect_;

    const bool in_dc_layer_hysteresis = i + 1 < 60;

    EXPECT_CALL(*output_surface_.get(), SetEnableDCLayers(_)).Times(0);

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    EXPECT_EQ(pass_list.back()->needs_synchronous_dcomp_commit,
              in_dc_layer_hysteresis);
    EXPECT_EQ(pass_list.back()->has_transparent_background,
              in_dc_layer_hysteresis);
    ASSERT_TRUE(output_surface_plane_.has_value());
    EXPECT_EQ(output_surface_plane_->enable_blending, in_dc_layer_hysteresis);

    EXPECT_EQ(0u, dc_layer_list.size());
    EXPECT_EQ(damage_rect_, expected_damage);

    Mock::VerifyAndClearExpectations(output_surface_.get());
  }
}

// Test that the video is forced to underlay if the expanded quad of pixel
// moving foreground filter is on top.
TEST_F(DCLayerOverlayTest, PixelMovingForegroundFilter) {
  InitializeOverlayProcessor();
  AggregatedRenderPassList pass_list;

  // Create a non-root render pass with a pixel-moving foreground filter.
  AggregatedRenderPassId filter_render_pass_id{2};
  gfx::Rect filter_rect = gfx::Rect(260, 260, 100, 100);
  cc::FilterOperations blur_filter;
  blur_filter.Append(cc::FilterOperation::CreateBlurFilter(10.f));
  auto filter_pass = std::make_unique<AggregatedRenderPass>();
  filter_pass->SetNew(filter_render_pass_id, filter_rect, filter_rect,
                      gfx::Transform());
  filter_pass->filters = blur_filter;

  // Add a solid quad to the non-root pass.
  SharedQuadState* shared_state_filter =
      filter_pass->CreateAndAppendSharedQuadState();
  CreateSolidColorQuadAt(shared_state_filter, SkColors::kRed, filter_pass.get(),
                         filter_rect);
  shared_state_filter->opacity = 1.f;
  pass_list.push_back(std::move(filter_pass));

  // Create a root render pass.
  auto pass = CreateRenderPass();
  // Add a RenderPassDrawQuad to the root render pass.
  SharedQuadState* shared_quad_state_rpdq = pass->shared_quad_state_list.back();
  // The pixel-moving render pass draw quad itself (rpdq->rect) doesn't
  // intersect with kOverlayRect(0, 0, 256, 256), but the expanded draw quad
  // (rpdq->rect(260, 260, 100, 100) + blur filter pixel movement (2 * 10.f) =
  // (240, 240, 140, 140)) does.

  CreateRenderPassDrawQuadAt(pass.get(), shared_quad_state_rpdq, filter_rect,
                             filter_render_pass_id);

  // Add a video quad to the root render pass.
  SharedQuadState* shared_state = pass->CreateAndAppendSharedQuadState();
  shared_state->opacity = 1.f;
  CreateFullscreenCandidateYUVVideoQuad(
      resource_provider_.get(), child_resource_provider_.get(),
      child_provider_.get(), shared_state, pass.get());
  // Make the root render pass output rect bigger enough to cover the video
  // quad kOverlayRect(0, 0, 256, 256) and the render pass draw quad (260, 260,
  // 100, 100).
  pass->output_rect = gfx::Rect(0, 0, 512, 512);

  OverlayCandidateList dc_layer_list;
  OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
  OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
  render_pass_filters[filter_render_pass_id] = &blur_filter;

  pass_list.push_back(std::move(pass));
  // filter_rect + kOverlayRect. Both are damaged.
  gfx::Rect damage_rect_ = gfx::Rect(0, 0, 360, 360);
  shared_state->overlay_damage_index = 1;

  SurfaceDamageRectList surface_damage_rect_list = {filter_rect, kOverlayRect};

  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
      render_pass_filters, render_pass_backdrop_filters,
      std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
      &dc_layer_list, &damage_rect_, &content_bounds_);

  EXPECT_EQ(1U, dc_layer_list.size());
  // Make sure the video is in an underlay mode if the overlay quad intersects
  // with expanded rpdq->rect.
  EXPECT_EQ(-1, dc_layer_list.back().plane_z_order);
  EXPECT_EQ(gfx::Rect(0, 0, 360, 360), damage_rect_);
}

// Test that the video is not promoted if a quad on top has backdrop filters.
TEST_F(DCLayerOverlayTest, BackdropFilter) {
  InitializeOverlayProcessor();
  AggregatedRenderPassList pass_list;

  // Create a non-root render pass with a backdrop filter.
  AggregatedRenderPassId backdrop_filter_render_pass_id{2};
  gfx::Rect backdrop_filter_rect = gfx::Rect(200, 200, 100, 100);
  cc::FilterOperations backdrop_filter;
  backdrop_filter.Append(cc::FilterOperation::CreateBlurFilter(10.f));
  auto backdrop_filter_pass = std::make_unique<AggregatedRenderPass>();
  backdrop_filter_pass->SetNew(backdrop_filter_render_pass_id,
                               backdrop_filter_rect, backdrop_filter_rect,
                               gfx::Transform());
  backdrop_filter_pass->backdrop_filters = backdrop_filter;

  // Add a transparent solid quad to the non-root pass.
  SharedQuadState* shared_state_backdrop_filter =
      backdrop_filter_pass->CreateAndAppendSharedQuadState();
  CreateSolidColorQuadAt(shared_state_backdrop_filter, SkColors::kGreen,
                         backdrop_filter_pass.get(), backdrop_filter_rect);
  shared_state_backdrop_filter->opacity = 0.1f;
  pass_list.push_back(std::move(backdrop_filter_pass));

  // Create a root render pass.
  auto pass = CreateRenderPass();
  // Add a RenderPassDrawQuad to the root render pass, on top of the video.
  SharedQuadState* shared_quad_state_rpdq = pass->shared_quad_state_list.back();
  shared_quad_state_rpdq->opacity = 0.1f;
  // The render pass draw quad rpdq->rect intersects with the overlay quad
  // kOverlayRect(0, 0, 256, 256).
  CreateRenderPassDrawQuadAt(pass.get(), shared_quad_state_rpdq,
                             backdrop_filter_rect,
                             backdrop_filter_render_pass_id);

  // Add a video quad to the root render pass.
  SharedQuadState* shared_state = pass->CreateAndAppendSharedQuadState();
  shared_state->opacity = 1.f;
  CreateFullscreenCandidateYUVVideoQuad(
      resource_provider_.get(), child_resource_provider_.get(),
      child_provider_.get(), shared_state, pass.get());
  // Make the root render pass output rect bigger enough to cover the video
  // quad kOverlayRect(0, 0, 256, 256) and the render pass draw quad (200, 200,
  // 100, 100).
  pass->output_rect = gfx::Rect(0, 0, 512, 512);

  OverlayCandidateList dc_layer_list;
  OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
  OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
  render_pass_backdrop_filters[backdrop_filter_render_pass_id] =
      &backdrop_filter;

  pass_list.push_back(std::move(pass));
  // backdrop_filter_rect + kOverlayRect. Both are damaged.
  gfx::Rect damage_rect_ = gfx::Rect(0, 0, 300, 300);
  shared_state->overlay_damage_index = 1;

  SurfaceDamageRectList surface_damage_rect_list = {backdrop_filter_rect,
                                                    kOverlayRect};

  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
      render_pass_filters, render_pass_backdrop_filters,
      std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
      &dc_layer_list, &damage_rect_, &content_bounds_);

  // Make sure the video is not promoted if the overlay quad intersects
  // with the backdrop filter rpdq->rect.
  EXPECT_EQ(0U, dc_layer_list.size());
  EXPECT_EQ(gfx::Rect(0, 0, 300, 300), damage_rect_);
}

// Test if overlay is not used when video capture is on.
TEST_F(DCLayerOverlayTest, VideoCapture) {
  InitializeOverlayProcessor();
  // Frame #0
  {
    auto pass = CreateRenderPass();
    pass->shared_quad_state_list.back();
    // Create a solid quad.
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(0, 0, 32, 32), SkColors::kRed);

    // Create a video YUV quad below the red solid quad.
    auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    gfx::Rect rect(0, 0, 256, 256);
    video_quad->rect = rect;
    video_quad->visible_rect = rect;
    pass->shared_quad_state_list.back()->overlay_damage_index = 1;

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 256, 256);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = {
        gfx::Rect(0, 0, 32, 32), gfx::Rect(0, 0, 256, 256)};
    // No video capture in this frame.
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    // Use overlay for the video quad.
    EXPECT_EQ(1U, dc_layer_list.size());
  }

  // Frame #1
  {
    auto pass = CreateRenderPass();
    pass->shared_quad_state_list.back();
    // Create a solid quad.
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(0, 0, 32, 32), SkColors::kRed);

    // Create a video YUV quad below the red solid quad.
    auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    gfx::Rect rect(0, 0, 256, 256);
    video_quad->rect = rect;
    video_quad->visible_rect = rect;
    pass->shared_quad_state_list.back()->overlay_damage_index = 0;

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 256, 256);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = {
        gfx::Rect(0, 0, 256, 256)};

    // Now video capture is enabled.
    pass_list.back()->video_capture_enabled = true;
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    // Should not use overlay for the video when video capture is on.
    EXPECT_EQ(0U, dc_layer_list.size());

    // Check whether both quads including the YUV quads are still in the render
    // pass.
    auto* root_pass = pass_list.back().get();
    int quad_count = root_pass->quad_list.size();
    EXPECT_EQ(2, quad_count);
  }
}

// Check that video capture on a non-root pass does not affect overlay promotion
// on the root pass itself.
TEST_F(DCLayerOverlayTest, VideoCaptureOnIsolatedRenderPass) {
  InitializeOverlayProcessor();

  AggregatedRenderPassList pass_list;

  // Create a render pass with video capture enabled. This could represent e.g.
  // capture of a background tab for stream.
  {
    auto pass = CreateRenderPass();
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(0, 0, 32, 32), SkColors::kRed);
    pass->video_capture_enabled = true;
    pass_list.push_back(std::move(pass));
  }

  // Create a root render pass with a video quad that can be promoted to
  // overlay.
  {
    auto root_pass = CreateRenderPass();
    // Create a solid quad.
    CreateOpaqueQuadAt(
        resource_provider_.get(), root_pass->shared_quad_state_list.back(),
        root_pass.get(), gfx::Rect(0, 0, 32, 32), SkColors::kRed);

    // Create a video YUV quad below the red solid quad.
    auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), root_pass->shared_quad_state_list.back(),
        root_pass.get());
    gfx::Rect rect(0, 0, 256, 256);
    video_quad->rect = rect;
    video_quad->visible_rect = rect;
    root_pass->shared_quad_state_list.back()->overlay_damage_index = 0;
    pass_list.push_back(std::move(root_pass));
  }

  OverlayCandidateList dc_layer_list;
  OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
  OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
  damage_rect_ = gfx::Rect(0, 0, 256, 256);

  SurfaceDamageRectList surface_damage_rect_list = {gfx::Rect(0, 0, 256, 256)};

  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
      render_pass_filters, render_pass_backdrop_filters,
      std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
      &dc_layer_list, &damage_rect_, &content_bounds_);

  EXPECT_EQ(1U, dc_layer_list.size());
}

TEST_F(DCLayerOverlayTest, RenderPassRootTransformOverlay) {
  InitializeOverlayProcessor();
  TestRenderPassRootTransform(/*is_overlay*/ true);
}

TEST_F(DCLayerOverlayTest, RenderPassRootTransformUnderlay) {
  InitializeOverlayProcessor();
  TestRenderPassRootTransform(/*is_overlay*/ false);
}

// Tests processing overlays/underlays in a render pass that contains a
// non-identity transform to root.
void DCLayerOverlayTest::TestRenderPassRootTransform(bool is_overlay) {
  const gfx::Rect kOutputRect = gfx::Rect(0, 0, 256, 256);
  const gfx::Rect kVideoRect = gfx::Rect(0, 0, 100, 100);
  const gfx::Rect kOpaqueRect = gfx::Rect(90, 80, 15, 30);
  const gfx::Transform kRenderPassToRootTransform =
      gfx::Transform::MakeTranslation(20, 45);
  // Surface damages in root space.
  const SurfaceDamageRectList kSurfaceDamageRectList = {
      // On top and does not intersect overlay. Translates to (110,5 20x10) in
      // render pass space.
      gfx::Rect(130, 50, 20, 10),
      // The video overlay damage rect. (0,0 100x100) in render pass space.
      gfx::Rect(20, 45, 100, 100),
      // Under and intersects the overlay. Translates to (95,25 20x10) in
      // render pass space.
      gfx::Rect(115, 70, 20, 10)};
  const size_t kOverlayDamageIndex = 1;

  for (size_t frame = 0; frame < 3; frame++) {
    auto pass = CreateRenderPass();
    pass->transform_to_root_target = kRenderPassToRootTransform;
    pass->shared_quad_state_list.back()->overlay_damage_index =
        kOverlayDamageIndex;

    if (!is_overlay) {
      // Create a quad that occludes the video to force it to an underlay.
      CreateOpaqueQuadAt(resource_provider_.get(),
                         pass->shared_quad_state_list.back(), pass.get(),
                         kOpaqueRect, SkColors::kWhite);
    }

    auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    video_quad->rect = gfx::Rect(kVideoRect);
    video_quad->visible_rect = video_quad->rect;

    std::vector<OverlayCandidate> dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = kSurfaceDamageRectList;

    damage_rect_ = kOutputRect;
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);
    LOG(INFO) << "frame " << frame
              << " damage rect: " << damage_rect_.ToString();

    EXPECT_EQ(dc_layer_list.size(), 1u);
    EXPECT_TRUE(
        absl::holds_alternative<gfx::Transform>(dc_layer_list[0].transform));
    EXPECT_EQ(absl::get<gfx::Transform>(dc_layer_list[0].transform),
              kRenderPassToRootTransform);
    if (is_overlay) {
      EXPECT_GT(dc_layer_list[0].plane_z_order, 0);
    } else {
      EXPECT_LT(dc_layer_list[0].plane_z_order, 0);
    }

    if (frame == 0) {
      // On the first frame, the damage rect should be unchanged since the
      // overlays are being processed for the first time.
      EXPECT_EQ(gfx::Rect(0, 0, 256, 256), damage_rect_);
    } else {
      // To calculate the damage rect in root space, we first subtract the video
      // damage from (115,70 20x10) since this damage is under the video. This
      // results in (120,70 20x10). This then gets unioned with (130,50 20x10),
      // which doesn't intersect the video. This results in (120,50 30x30).
      // The damage rect returned from the DCLayerOverlayProcessor is in
      // render pass space, so we apply the (20, 45) inverse transform,
      // resulting in (100,5 30x30).
      EXPECT_EQ(damage_rect_, gfx::Rect(100, 5, 30, 30));
    }
  }
}

// Tests processing overlays/underlays on multiple render passes per frame,
// where only one render pass has an overlay.
TEST_F(DCLayerOverlayTest, MultipleRenderPassesOneOverlay) {
  InitializeOverlayProcessor(/*allowed_yuv_overlay_count*/ 1);
  const gfx::Rect output_rect = {0, 0, 256, 256};
  const size_t num_render_passes = 3;
  for (size_t frame = 0; frame < 3; frame++) {
    AggregatedRenderPassList render_passes;  // Used to keep render passes alive
    DCLayerOverlayProcessor::RenderPassOverlayDataMap
        render_pass_overlay_data_map;

    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    SurfaceDamageRectList surface_damage_rect_list;

    // Create 3 render passes, with only one containing an overlay candidate.
    for (size_t id = 1; id <= num_render_passes; id++) {
      auto pass = CreateRenderPass(AggregatedRenderPassId{id});
      pass->transform_to_root_target = gfx::Transform::MakeTranslation(id, 0);

      gfx::Rect quad_rect_in_root_space =
          gfx::Rect(0, 0, id * 16, pass->output_rect.height());

      if (id == 1) {
        // Create an overlay quad in the first render pass.
        auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
            resource_provider_.get(), child_resource_provider_.get(),
            child_provider_.get(), pass->shared_quad_state_list.back(),
            pass.get());
        gfx::Rect quad_rect_in_quad_space =
            pass->transform_to_root_target
                .InverseMapRect(quad_rect_in_root_space)
                .value();
        video_quad->rect = quad_rect_in_quad_space;
        video_quad->visible_rect = quad_rect_in_quad_space;
        pass->shared_quad_state_list.back()->overlay_damage_index = id - 1;
      } else {
        // Create a quad that's not an overlay.
        CreateSolidColorQuadAt(pass->shared_quad_state_list.back(),
                               SkColors::kBlue, pass.get(),
                               pass->transform_to_root_target
                                   .InverseMapRect(quad_rect_in_root_space)
                                   .value());
      }

      surface_damage_rect_list.emplace_back(quad_rect_in_root_space);
      render_pass_overlay_data_map[pass.get()].damage_rect = output_rect;
      render_passes.emplace_back(std::move(pass));
    }

    surface_damage_rect_list.emplace_back(0, 0, 256, 256);

    overlay_processor_->ProcessOnDCLayerOverlayProcessorForTesting(
        resource_provider_.get(), render_pass_filters,
        render_pass_backdrop_filters, std::move(surface_damage_rect_list),
        /*is_page_fullscreen_mode=*/false, render_pass_overlay_data_map);

    for (auto& [render_pass, overlay_data] : render_pass_overlay_data_map) {
      LOG(INFO) << "frame " << frame << " render pass " << render_pass->id
                << " damage rect : " << overlay_data.damage_rect.ToString();
      LOG(INFO) << "frame " << frame << " render pass " << render_pass->id
                << " number of overlays: "
                << overlay_data.promoted_overlays.size();

      if (render_pass->id == AggregatedRenderPassId(1)) {
        // The render pass that contains an overlay.
        EXPECT_EQ(overlay_data.promoted_overlays.size(), 1u);
        EXPECT_EQ(absl::get<gfx::Transform>(
                      overlay_data.promoted_overlays[0].transform),
                  gfx::Transform::MakeTranslation(1, 0));
        EXPECT_GT(overlay_data.promoted_overlays[0].plane_z_order, 0);

        // The rect of the candidate should be in render pass space, which is an
        // arbitrary space. Combining with the render pass to root transform
        // results in a rect in root space. The transform is defined above as an
        // x translation of -1.
        EXPECT_EQ(overlay_data.promoted_overlays[0].display_rect,
                  gfx::RectF(-1, 0, 16, 256));

        if (frame == 0) {
          // On the first frame, the damage rect should be unchanged since the
          // overlays are being processed for the first time.
          EXPECT_EQ(overlay_data.damage_rect, output_rect);
        } else {
          // On subsequent frames, the video rect should be subtracted from
          // the damage rect. The x coordinate is 15 instead of 16 because of
          // the root_to_transform_target. The surface_damage_rect_list damages
          // are in root space, while the damage_rect output is in render pass
          // space.
          EXPECT_EQ(overlay_data.damage_rect, gfx::Rect(15, 0, 240, 256));
        }
      } else {
        // All other render passes do not have overlays.
        EXPECT_TRUE(overlay_data.promoted_overlays.empty());

        // With no overlays, the damage should be unchanged since there are no
        // overlays to subtract.
        EXPECT_EQ(overlay_data.damage_rect, output_rect);
      }
    }
  }
}

// Tests processing overlays/underlays on multiple render passes per frame, with
// each render pass having an overlay. This exceeds the maximum allowed number
// of overlays, so all overlays should be rejected.
TEST_F(DCLayerOverlayTest, MultipleRenderPassesExceedsOverlayAllowance) {
  const gfx::Rect output_rect = {0, 0, 256, 256};
  const size_t num_render_passes = 3;
  InitializeOverlayProcessor(num_render_passes - 1);
  for (size_t frame = 1; frame <= 3; frame++) {
    AggregatedRenderPassList
        render_passes;  // Used to keep render passes alive.
    DCLayerOverlayProcessor::RenderPassOverlayDataMap
        render_pass_overlay_data_map;

    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    SurfaceDamageRectList surface_damage_rect_list;

    // Create 3 render passes that all have a video overlay candidate. Start
    // at the frame number so that we switch up the render pass IDs to verify
    // that render passes that do not exist are not kept in
    // |DCLayerOverlayProcessor::previous_frame_render_pass_states_|.
    for (size_t id = frame; id < num_render_passes + frame; id++) {
      auto pass = CreateRenderPass(AggregatedRenderPassId{id});
      pass->transform_to_root_target = gfx::Transform::MakeTranslation(id, 0);
      pass->shared_quad_state_list.back()->overlay_damage_index = id - 1;

      gfx::Rect video_rect_in_root_space =
          gfx::Rect(0, 0, id * 16, pass->output_rect.height());

      gfx::Rect video_rect_in_render_pass_space =
          pass->transform_to_root_target
              .InverseMapRect(video_rect_in_root_space)
              .value();
      auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
          resource_provider_.get(), child_resource_provider_.get(),
          child_provider_.get(), pass->shared_quad_state_list.back(),
          pass.get());
      video_quad->rect = video_rect_in_render_pass_space;
      video_quad->visible_rect = video_rect_in_render_pass_space;

      surface_damage_rect_list.emplace_back(video_rect_in_root_space);
      render_pass_overlay_data_map[pass.get()].damage_rect = output_rect;
      render_passes.emplace_back(std::move(pass));
    }

    surface_damage_rect_list.emplace_back(0, 0, 256, 256);

    overlay_processor_->ProcessOnDCLayerOverlayProcessorForTesting(
        resource_provider_.get(), render_pass_filters,
        render_pass_backdrop_filters, std::move(surface_damage_rect_list),
        /*is_page_fullscreen_mode=*/false, render_pass_overlay_data_map);

    // Verify that the previous frame states contain only 3 render passes and
    // that they have the IDs that we set them to.
    EXPECT_EQ(3U, overlay_processor_->get_previous_frame_render_pass_count());
    std::vector<AggregatedRenderPassId> previous_frame_render_pass_ids =
        overlay_processor_->get_previous_frame_render_pass_ids();
    std::sort(previous_frame_render_pass_ids.begin(),
              previous_frame_render_pass_ids.end());
    for (size_t id = frame; id < num_render_passes + frame; id++) {
      EXPECT_EQ(id, previous_frame_render_pass_ids[id - frame].value());
    }

    for (auto& [render_pass, overlay_data] : render_pass_overlay_data_map) {
      LOG(INFO) << "frame " << frame << " render pass " << render_pass->id
                << " damage rect : " << overlay_data.damage_rect.ToString();
      LOG(INFO) << "frame " << frame << " render pass " << render_pass->id
                << " number of overlays: "
                << overlay_data.promoted_overlays.size();

      // Since there is more than one overlay, all overlays should be rejected.
      EXPECT_EQ(overlay_data.promoted_overlays.size(), 0u);

      // With no overlays, the damage should be unchanged since there are no
      // overlays to subtract.
      EXPECT_EQ(overlay_data.damage_rect, output_rect);
    }
  }
}

// When there are multiple videos intersected with each other, only the topmost
// of them should be considered as "overlay".
TEST_F(DCLayerOverlayTest, MultipleYUVOverlaysIntersected) {
  InitializeOverlayProcessor(/*allowed_yuv_overlay_count=*/2);
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(
      features::kNoUndamagedOverlayPromotion);
  {
    auto pass = CreateRenderPass();

    // Video 1: Topmost video.
    auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    gfx::Rect rect(150, 150, 50, 50);
    video_quad->rect = rect;
    video_quad->visible_rect = rect;
    pass->shared_quad_state_list.back()->overlay_damage_index = 1;

    // Video 2: Intersected with and under the 1st video.
    auto* second_video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
    gfx::Rect second_rect(100, 100, 120, 120);
    second_video_quad->rect = second_rect;
    second_video_quad->visible_rect = second_rect;
    pass->shared_quad_state_list.back()->overlay_damage_index = 2;

    // Background.
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(0, 0, 256, 256), SkColors::kWhite);

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 220, 220);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list;

    surface_damage_rect_list.push_back(video_quad->rect);
    surface_damage_rect_list.push_back(second_video_quad->rect);
    surface_damage_rect_list.push_back(gfx::Rect(0, 0, 256, 256));

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    int overlay_cnt = 0;
    for (auto& dc : dc_layer_list) {
      if (dc.plane_z_order > 0) {
        // The overlay video should be the topmost one.
        EXPECT_EQ(gfx::Rect(150, 150, 50, 50),
                  gfx::ToEnclosingRect(dc.display_rect));
        overlay_cnt++;
      }
    }

    EXPECT_EQ(1, overlay_cnt);
  }
}

TEST_F(DCLayerOverlayTest, HDR10VideoOverlay) {
  InitializeOverlayProcessor();
  // Prepare a valid hdr metadata.
  gfx::HDRMetadata valid_hdr_metadata;
  valid_hdr_metadata.cta_861_3 = gfx::HdrMetadataCta861_3(1000, 400);
  valid_hdr_metadata.smpte_st_2086 =
      gfx::HdrMetadataSmpteSt2086(SkNamedPrimariesExt::kRec2020, 1000, 0.0001);

  // Device has RGB10A2 overlay support.
  gl::SetDirectCompositionScaledOverlaysSupportedForTesting(true);

  // Device has HDR-enabled display and no non-HDR-enabled display.
  overlay_processor_->set_system_hdr_disabled_on_any_display_for_testing(false);

  // Device has video processor support.
  overlay_processor_->set_has_p010_video_processor_support_for_testing(true);

  // Frame 1 should promote overlay as all conditions satisfied.
  {
    auto pass = CreateRenderPass();
    pass->content_color_usage = gfx::ContentColorUsage::kHDR;
    YUVVideoDrawQuad* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());

    // Content is 10bit P010 content.
    video_quad->bits_per_channel = 10;

    // Content has valid HDR metadata.
    video_quad->hdr_metadata = valid_hdr_metadata;

    // Content has HDR10 colorspace.
    video_quad->video_color_space = gfx::ColorSpace::CreateHDR10();

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 220, 220);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list;

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    // Should promote overlay.
    EXPECT_EQ(1U, dc_layer_list.size());
  }

  // Frame 2 should skip overlay as bit depth not satisfied.
  {
    auto pass = CreateRenderPass();
    pass->content_color_usage = gfx::ContentColorUsage::kHDR;
    YUVVideoDrawQuad* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());

    // Content is 8bit NV12 content (not satisfied).
    video_quad->bits_per_channel = 8;

    // Content has valid HDR metadata.
    video_quad->hdr_metadata = valid_hdr_metadata;

    // Content has HDR10 colorspace.
    video_quad->video_color_space = gfx::ColorSpace::CreateHDR10();

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 220, 220);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list;

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    // Should skip overlay.
    EXPECT_EQ(0U, dc_layer_list.size());
  }

  // Frame 3 should skip overlay as hdr metadata is invalid.
  {
    auto pass = CreateRenderPass();
    pass->content_color_usage = gfx::ContentColorUsage::kHDR;
    YUVVideoDrawQuad* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());

    // Content is 10bit P010 content.
    video_quad->bits_per_channel = 10;

    // Content has invalid HDR metadata (not satisfied).
    gfx::HDRMetadata invalid_hdr_metadata;
    video_quad->hdr_metadata = invalid_hdr_metadata;

    // Content has HDR10 colorspace.
    video_quad->video_color_space = gfx::ColorSpace::CreateHDR10();

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 220, 220);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list;

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    // Should skip overlay.
    EXPECT_EQ(0U, dc_layer_list.size());
  }

  // Frame 4 should promote overlay as hdr metadata contains cta_861_3.
  {
    auto pass = CreateRenderPass();
    pass->content_color_usage = gfx::ContentColorUsage::kHDR;
    YUVVideoDrawQuad* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());

    // Content is 10bit P010 content.
    video_quad->bits_per_channel = 10;

    // Content has HDR metadata which contains cta_861_3.
    gfx::HDRMetadata cta_861_3_hdr_metadata;
    cta_861_3_hdr_metadata.cta_861_3 = gfx::HdrMetadataCta861_3(0, 400);
    video_quad->hdr_metadata = cta_861_3_hdr_metadata;

    // Content has HDR10 colorspace.
    video_quad->video_color_space = gfx::ColorSpace::CreateHDR10();

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 220, 220);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list;

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    // Should promote overlay.
    EXPECT_EQ(1U, dc_layer_list.size());
  }

  // Frame 5 should promote overlay as hdr metadata contains smpte_st_2086.
  {
    auto pass = CreateRenderPass();
    pass->content_color_usage = gfx::ContentColorUsage::kHDR;
    YUVVideoDrawQuad* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());

    // Content is 10bit P010 content.
    video_quad->bits_per_channel = 10;

    // Content has HDR metadata which contains smpte_st_2086.
    gfx::HDRMetadata smpte_st_2086_hdr_metadata;
    smpte_st_2086_hdr_metadata.smpte_st_2086 = gfx::HdrMetadataSmpteSt2086(
        SkNamedPrimariesExt::kRec2020, 1000, 0.0001);
    video_quad->hdr_metadata = smpte_st_2086_hdr_metadata;

    // Content has HDR10 colorspace.
    video_quad->video_color_space = gfx::ColorSpace::CreateHDR10();

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 220, 220);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list;

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    // Should promote overlay.
    EXPECT_EQ(1U, dc_layer_list.size());
  }

  // Frame 6 should skip overlay as color space not satisfied.
  {
    auto pass = CreateRenderPass();
    pass->content_color_usage = gfx::ContentColorUsage::kHDR;
    YUVVideoDrawQuad* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());

    // Content is 10bit P010 content.
    video_quad->bits_per_channel = 10;

    // Content has invalid HDR metadata.
    video_quad->hdr_metadata = valid_hdr_metadata;

    // Content has HDR colorspace but not in PQ transfer (not satisfied).
    video_quad->video_color_space = gfx::ColorSpace::CreateHLG();

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 220, 220);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list;

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    // Should skip overlay.
    EXPECT_EQ(0U, dc_layer_list.size());
  }

  // Frame 7 should skip overlay as no P010 video processor support.
  {
    overlay_processor_->set_has_p010_video_processor_support_for_testing(false);

    auto pass = CreateRenderPass();
    pass->content_color_usage = gfx::ContentColorUsage::kHDR;
    YUVVideoDrawQuad* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());

    // Content is 10bit P010 content.
    video_quad->bits_per_channel = 10;

    // Content has valid HDR metadata.
    video_quad->hdr_metadata = valid_hdr_metadata;

    // Content has HDR10 colorspace.
    video_quad->video_color_space = gfx::ColorSpace::CreateHDR10();

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 220, 220);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list;

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    // Should skip overlay.
    EXPECT_EQ(0U, dc_layer_list.size());

    // Recover config.
    overlay_processor_->set_has_p010_video_processor_support_for_testing(true);
  }

  // Frame 8 should skip overlay as non-HDR-enabled display exists.
  {
    overlay_processor_->set_system_hdr_disabled_on_any_display_for_testing(
        true);

    auto pass = CreateRenderPass();
    pass->content_color_usage = gfx::ContentColorUsage::kHDR;
    YUVVideoDrawQuad* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());

    // Content is 10bit P010 content.
    video_quad->bits_per_channel = 10;

    // Content has valid HDR metadata.
    video_quad->hdr_metadata = valid_hdr_metadata;

    // Content has HDR10 colorspace.
    video_quad->video_color_space = gfx::ColorSpace::CreateHDR10();

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 220, 220);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list;

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    // Should skip overlay.
    EXPECT_EQ(0U, dc_layer_list.size());

    // Recover config.
    overlay_processor_->set_system_hdr_disabled_on_any_display_for_testing(
        false);
  }

  // Frame 9 should skip overlay as no rgb10a2 overlay support.
  {
    gl::SetDirectCompositionScaledOverlaysSupportedForTesting(false);

    auto pass = CreateRenderPass();
    pass->content_color_usage = gfx::ContentColorUsage::kHDR;
    YUVVideoDrawQuad* video_quad = CreateFullscreenCandidateYUVVideoQuad(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());

    // Content is 10bit P010 content.
    video_quad->bits_per_channel = 10;

    // Content has valid HDR metadata.
    video_quad->hdr_metadata = valid_hdr_metadata;

    // Content has HDR10 colorspace.
    video_quad->video_color_space = gfx::ColorSpace::CreateHDR10();

    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(0, 0, 220, 220);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list;

    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);

    // Should skip overlay.
    EXPECT_EQ(0U, dc_layer_list.size());

    // Recover config.
    gl::SetDirectCompositionScaledOverlaysSupportedForTesting(true);
  }
}

MATCHER(ResourceIdEq, "") {
  return std::get<0>(arg).resource_id == ResourceId(std::get<1>(arg));
}

MATCHER(PlaneZOrdersAreUnique, "") {
  const OverlayCandidateList& candidates = arg;
  base::flat_set<int> z_orders;
  for (const auto& candidate : candidates) {
    z_orders.insert(candidate.plane_z_order);
  }
  return candidates.size() == z_orders.size();
}

// Checks that, when the overlay candidates list is sorted by z-order, the
// resource IDs of the candidates matches |expected_resource_ids|. Note these
// resource IDs are not real and a just used to identify overlay candidates in
// tests.
testing::Matcher<const OverlayCandidateList&>
WhenCandidatesAreSortedResourceIdsAre(
    const std::vector<int>& expected_resource_ids) {
  return testing::AllOf(
      PlaneZOrdersAreUnique(),
      testing::WhenSortedBy(
          test::PlaneZOrderAscendingComparator(),
          testing::Pointwise(ResourceIdEq(), expected_resource_ids)));
}

TEST_F(DCLayerOverlayTest, InsertSurfaceContentOverlay) {
  // Set up a dummy render pass and RPDQ
  AggregatedRenderPass pass;
  pass.id = AggregatedRenderPassId(1);
  AggregatedRenderPassDrawQuad rpdq;
  rpdq.render_pass_id = pass.id;

  DCLayerOverlayProcessor::RenderPassOverlayDataMap
      surface_content_render_passes;
  DCLayerOverlayProcessor::RenderPassOverlayData overlay_data;
  {
    overlay_data.promoted_overlays.emplace_back();
    overlay_data.promoted_overlays.back().resource_id = ResourceId(3);
    overlay_data.promoted_overlays.back().plane_z_order = 1;
  }
  surface_content_render_passes.insert({&pass, std::move(overlay_data)});

  OverlayCandidateList candidates;
  {
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(4);

    // Pretend this candidate is a RPDQ that we've pulled overlays from.
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(2);
    candidates.back().rpdq = &rpdq;

    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(1);
  }

  std::ignore = OverlayProcessorWin::
      InsertSurfaceContentOverlaysAndSetPlaneZOrderForTesting(
          std::move(surface_content_render_passes), candidates);

  EXPECT_THAT(candidates, WhenCandidatesAreSortedResourceIdsAre({1, 2, 3, 4}));
}

TEST_F(DCLayerOverlayTest, InsertSurfaceContentUnderlay) {
  // Set up a dummy render pass and RPDQ
  AggregatedRenderPass pass;
  pass.id = AggregatedRenderPassId(1);
  AggregatedRenderPassDrawQuad rpdq;
  rpdq.render_pass_id = pass.id;

  DCLayerOverlayProcessor::RenderPassOverlayDataMap
      surface_content_render_passes;
  DCLayerOverlayProcessor::RenderPassOverlayData overlay_data;
  {
    overlay_data.promoted_overlays.emplace_back();
    overlay_data.promoted_overlays.back().resource_id = ResourceId(3);
    overlay_data.promoted_overlays.back().plane_z_order = -1;
  }
  surface_content_render_passes.insert({&pass, std::move(overlay_data)});

  OverlayCandidateList candidates;
  {
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(4);

    // Pretend this candidate is a RPDQ that we've pulled overlays from.
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(2);
    candidates.back().rpdq = &rpdq;

    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(1);
  }

  std::ignore = OverlayProcessorWin::
      InsertSurfaceContentOverlaysAndSetPlaneZOrderForTesting(
          std::move(surface_content_render_passes), candidates);

  EXPECT_THAT(candidates, WhenCandidatesAreSortedResourceIdsAre({1, 3, 2, 4}));
}

// Check that |InsertSurfaceContentOverlaysAndSetPlaneZOrder| supports promoted
// overlay candidates that have gaps in the z-order.
TEST_F(DCLayerOverlayTest, InsertSurfaceContentOverlaysWithGapsInZOrder) {
  // Set up a dummy render pass and RPDQ
  AggregatedRenderPass pass;
  pass.id = AggregatedRenderPassId(1);
  AggregatedRenderPassDrawQuad rpdq;
  rpdq.render_pass_id = pass.id;

  DCLayerOverlayProcessor::RenderPassOverlayDataMap
      surface_content_render_passes;
  DCLayerOverlayProcessor::RenderPassOverlayData overlay_data;
  {
    overlay_data.promoted_overlays.emplace_back();
    overlay_data.promoted_overlays.back().resource_id = ResourceId(2);
    overlay_data.promoted_overlays.back().plane_z_order = -3;

    overlay_data.promoted_overlays.emplace_back();
    overlay_data.promoted_overlays.back().resource_id = ResourceId(4);
    overlay_data.promoted_overlays.back().plane_z_order = 3;
  }
  surface_content_render_passes.insert({&pass, std::move(overlay_data)});

  OverlayCandidateList candidates;
  {
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(5);

    // Pretend this candidate is a RPDQ that we've pulled overlays from.
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(3);
    candidates.back().rpdq = &rpdq;

    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(1);
  }

  std::ignore = OverlayProcessorWin::
      InsertSurfaceContentOverlaysAndSetPlaneZOrderForTesting(
          std::move(surface_content_render_passes), candidates);

  EXPECT_THAT(candidates,
              WhenCandidatesAreSortedResourceIdsAre({1, 2, 3, 4, 5}));
}

TEST_F(DCLayerOverlayTest, InsertSurfaceContentOverlaysWithNoPromotedOverlays) {
  // Set up a dummy render pass and RPDQ
  AggregatedRenderPass pass;
  pass.id = AggregatedRenderPassId(1);
  AggregatedRenderPassDrawQuad rpdq;
  rpdq.render_pass_id = pass.id;

  DCLayerOverlayProcessor::RenderPassOverlayDataMap
      surface_content_render_passes;
  DCLayerOverlayProcessor::RenderPassOverlayData overlay_data;
  // No candidates in |overlay_data|.
  surface_content_render_passes.insert({&pass, std::move(overlay_data)});

  OverlayCandidateList candidates;
  {
    // Pretend this candidate is a RPDQ that we've pulled overlays from.
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(2);
    candidates.back().rpdq = &rpdq;

    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(1);
  }

  std::ignore = OverlayProcessorWin::
      InsertSurfaceContentOverlaysAndSetPlaneZOrderForTesting(
          std::move(surface_content_render_passes), candidates);

  EXPECT_THAT(candidates, WhenCandidatesAreSortedResourceIdsAre({1, 2}));
}

TEST_F(DCLayerOverlayTest, InsertSurfaceContentOverlaysWithUnderlays) {
  // Set up a dummy render pass and RPDQ
  AggregatedRenderPass pass;
  pass.id = AggregatedRenderPassId(1);
  AggregatedRenderPassDrawQuad rpdq;
  rpdq.render_pass_id = pass.id;

  DCLayerOverlayProcessor::RenderPassOverlayDataMap
      surface_content_render_passes;
  DCLayerOverlayProcessor::RenderPassOverlayData overlay_data;
  {
    overlay_data.promoted_overlays.emplace_back();
    overlay_data.promoted_overlays.back().resource_id = ResourceId(5);
    overlay_data.promoted_overlays.back().plane_z_order = 1;

    overlay_data.promoted_overlays.emplace_back();
    overlay_data.promoted_overlays.back().resource_id = ResourceId(2);
    overlay_data.promoted_overlays.back().plane_z_order = -2;

    overlay_data.promoted_overlays.emplace_back();
    overlay_data.promoted_overlays.back().resource_id = ResourceId(3);
    overlay_data.promoted_overlays.back().plane_z_order = -1;

    overlay_data.promoted_overlays.emplace_back();
    overlay_data.promoted_overlays.back().resource_id = ResourceId(6);
    overlay_data.promoted_overlays.back().plane_z_order = 2;
  }
  surface_content_render_passes.insert({&pass, std::move(overlay_data)});

  OverlayCandidateList candidates;
  {
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(8);

    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(7);

    // Pretend this candidate is a RPDQ that we've pulled overlays from.
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(4);
    candidates.back().rpdq = &rpdq;

    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(1);
  }

  std::ignore = OverlayProcessorWin::
      InsertSurfaceContentOverlaysAndSetPlaneZOrderForTesting(
          std::move(surface_content_render_passes), candidates);

  EXPECT_THAT(candidates,
              WhenCandidatesAreSortedResourceIdsAre({1, 2, 3, 4, 5, 6, 7, 8}));
}

TEST_F(DCLayerOverlayTest, InsertSurfaceContentOverlaysMultipleSurfaces) {
  // Set up dummy render passes and RPDQs
  AggregatedRenderPass pass1;
  pass1.id = AggregatedRenderPassId(1);
  AggregatedRenderPassDrawQuad rpdq1;
  rpdq1.render_pass_id = pass1.id;
  AggregatedRenderPass pass2;
  pass2.id = AggregatedRenderPassId(2);
  AggregatedRenderPassDrawQuad rpdq2;
  rpdq2.render_pass_id = pass2.id;

  DCLayerOverlayProcessor::RenderPassOverlayDataMap
      surface_content_render_passes;
  DCLayerOverlayProcessor::RenderPassOverlayData overlay_data;
  {
    overlay_data.promoted_overlays.emplace_back();
    overlay_data.promoted_overlays.back().resource_id = ResourceId(2);
    overlay_data.promoted_overlays.back().plane_z_order = 1;
  }
  surface_content_render_passes.insert({&pass1, std::move(overlay_data)});

  {
    overlay_data.promoted_overlays.emplace_back();
    overlay_data.promoted_overlays.back().resource_id = ResourceId(4);
    overlay_data.promoted_overlays.back().plane_z_order = 1;
  }
  surface_content_render_passes.insert({&pass2, std::move(overlay_data)});

  OverlayCandidateList candidates;
  {
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(5);

    // Pretend this candidate is a RPDQ that we've pulled overlays from.
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(3);
    candidates.back().rpdq = &rpdq2;

    // Pretend this candidate is a RPDQ that we've pulled overlays from.
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(1);
    candidates.back().rpdq = &rpdq1;
  }

  std::ignore = OverlayProcessorWin::
      InsertSurfaceContentOverlaysAndSetPlaneZOrderForTesting(
          std::move(surface_content_render_passes), candidates);

  EXPECT_THAT(candidates,
              WhenCandidatesAreSortedResourceIdsAre({1, 2, 3, 4, 5}));
}

TEST_F(DCLayerOverlayTest,
       InsertSurfaceContentOverlaysSameSurfaceEmbeddedTwice) {
  // Set up a dummy render pass and RPDQ
  AggregatedRenderPass pass;
  pass.id = AggregatedRenderPassId(1);
  AggregatedRenderPassDrawQuad rpdq;
  rpdq.render_pass_id = pass.id;

  DCLayerOverlayProcessor::RenderPassOverlayDataMap
      surface_content_render_passes;
  DCLayerOverlayProcessor::RenderPassOverlayData overlay_data;
  {
    overlay_data.promoted_overlays.emplace_back();
    overlay_data.promoted_overlays.back().resource_id = ResourceId(3);
    overlay_data.promoted_overlays.back().plane_z_order = 1;
  }
  surface_content_render_passes.insert({&pass, std::move(overlay_data)});

  OverlayCandidateList candidates;
  {
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(6);

    // Pretend this candidate is a RPDQ that we've pulled overlays from.
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(4);
    candidates.back().rpdq = &rpdq;

    // Pretend this candidate is a RPDQ that we've pulled overlays from.
    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(2);
    candidates.back().rpdq = &rpdq;

    candidates.emplace_back();
    candidates.back().resource_id = ResourceId(1);
  }

  std::ignore = OverlayProcessorWin::
      InsertSurfaceContentOverlaysAndSetPlaneZOrderForTesting(
          std::move(surface_content_render_passes), candidates);

  EXPECT_THAT(candidates, WhenCandidatesAreSortedResourceIdsAre(
                              {1, 2, 3, 4,
                               3,  // We've embedded this overlay twice
                               6}));
}

// Tests that Delegated Ink in the frame correctly sets
// needs_synchronous_dcomp_commit on the render pass.
TEST_F(DCLayerOverlayTest, FrameHasDelegatedInk) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(
      features::kUseDCompSurfacesForDelegatedInk);
  InitializeOverlayProcessor();
  overlay_processor_->SetUsingDCLayersForTesting(kDefaultRootPassId, false);
  // Test that needs_synchronous_dcomp_commit on the render pass gets set to
  // false as default.
  {
    auto pass = CreateRenderPass();
    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(1, 1, 10, 10);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = {gfx::Rect(1, 1, 10, 10)};

    EXPECT_FALSE(pass_list[0]->needs_synchronous_dcomp_commit);
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);
    EXPECT_FALSE(pass_list[0]->needs_synchronous_dcomp_commit);
  }

  // Test that needs_synchronous_dcomp_commit gets set to true when the frame
  // has delegated ink.
  overlay_processor_->SetFrameHasDelegatedInk();
  auto pass = CreateRenderPass();
  OverlayCandidateList dc_layer_list;
  OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
  OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
  damage_rect_ = gfx::Rect(1, 1, 10, 10);
  AggregatedRenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  SurfaceDamageRectList surface_damage_rect_list = {gfx::Rect(1, 1, 10, 10)};

  EXPECT_FALSE(pass_list[0]->needs_synchronous_dcomp_commit);
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
      render_pass_filters, render_pass_backdrop_filters,
      std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
      &dc_layer_list, &damage_rect_, &content_bounds_);
  // Make sure |frame_has_delegated_ink_| has been set to false.
  EXPECT_FALSE(overlay_processor_->frame_has_delegated_ink_for_testing());
  EXPECT_TRUE(pass_list[0]->needs_synchronous_dcomp_commit);
}

// Ensure needs_synchronous_dcomp_commit lasts for 60 frames after
// |SetFrameHasDelegatedInk| has been called (once). Based on
// kNumberOfFramesBeforeDisablingDCLayers in
// components/viz/service/display/overlay_processor_win.cc.
TEST_F(DCLayerOverlayTest, DelegatedInkSurfaceHysteresis) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(
      features::kUseDCompSurfacesForDelegatedInk);
  InitializeOverlayProcessor();
  overlay_processor_->SetUsingDCLayersForTesting(kDefaultRootPassId, false);

  overlay_processor_->SetFrameHasDelegatedInk();
  for (int frame = 1; frame <= 61; frame++) {
    auto pass = CreateRenderPass();
    OverlayCandidateList dc_layer_list;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;
    damage_rect_ = gfx::Rect(1, 1, 10, 10);
    AggregatedRenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    SurfaceDamageRectList surface_damage_rect_list = {gfx::Rect(1, 1, 10, 10)};

    EXPECT_FALSE(pass_list[0]->needs_synchronous_dcomp_commit);
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &dc_layer_list, &damage_rect_, &content_bounds_);
    // Make sure |frame_has_delegated_ink_| has been set to false.
    EXPECT_FALSE(overlay_processor_->frame_has_delegated_ink_for_testing());
    if (frame <= 60) {
      EXPECT_TRUE(pass_list[0]->needs_synchronous_dcomp_commit);
    } else {
      EXPECT_FALSE(pass_list[0]->needs_synchronous_dcomp_commit);
    }
  }
}

class DCLayerOverlayDelegatedCompositingTest : public DCLayerOverlayTest {
 protected:
  DCLayerOverlayDelegatedCompositingTest() {
    feature_list_.InitAndEnableFeature(features::kDelegatedCompositing);
  }

  void SetUp() override {
    DCLayerOverlayTest::SetUp();
    InitializeOverlayProcessor();
  }

  class DelegationResult {
   public:
    DelegationResult(OverlayCandidateList candidates,
                     bool delegation_succeeded,
                     gfx::Rect original_root_surface_damage,
                     gfx::Rect root_surface_damage)
        : candidates_(std::move(candidates)),
          delegation_succeeded_(delegation_succeeded),
          original_root_surface_damage_(original_root_surface_damage),
          root_surface_damage_(root_surface_damage) {}

    void ExpectDelegationSuccess() const {
      EXPECT_TRUE(delegation_succeeded_);
      EXPECT_EQ(gfx::Rect(), root_surface_damage_);
    }

    void ExpectDelegationFailure() const {
      EXPECT_FALSE(delegation_succeeded_);
      EXPECT_EQ(original_root_surface_damage_, root_surface_damage_);
    }

    const OverlayCandidateList& candidates() const { return candidates_; }

   private:
    OverlayCandidateList candidates_;
    bool delegation_succeeded_ = false;
    gfx::Rect original_root_surface_damage_;
    gfx::Rect root_surface_damage_;
  };

  DelegationResult TryProcessForDelegatedOverlays(
      AggregatedRenderPassList& pass_list,
      SurfaceDamageRectList surface_damage_rect_list = {}) {
    if (!output_surface_plane_) {
      // Reset the output surface plane in case we're calling
      // |TryProcessForDelegatedOverlays| multiple times.
      output_surface_plane_ =
          OverlayProcessorInterface::OutputSurfaceOverlayPlane();
    }

    const gfx::Rect original_root_surface_damage =
        pass_list.back()->damage_rect;

    OverlayCandidateList candidates;
    OverlayProcessorInterface::FilterOperationsMap render_pass_filters;
    OverlayProcessorInterface::FilterOperationsMap render_pass_backdrop_filters;

    for (const auto& pass : pass_list) {
      if (!pass->filters.IsEmpty()) {
        render_pass_filters[pass->id] = &pass->filters;
      }
      if (!pass->backdrop_filters.IsEmpty()) {
        render_pass_backdrop_filters[pass->id] = &pass->backdrop_filters;
      }
    }

    damage_rect_ = original_root_surface_damage;
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, GetIdentityColorMatrix(),
        render_pass_filters, render_pass_backdrop_filters,
        std::move(surface_damage_rect_list), GetOutputSurfacePlane(),
        &candidates, &damage_rect_, &content_bounds_);

    overlay_processor_->AdjustOutputSurfaceOverlay(&output_surface_plane_);
    const bool delegation_succeeded = !output_surface_plane_.has_value();

    return DelegationResult(candidates, delegation_succeeded,
                            original_root_surface_damage, damage_rect_);
  }

  testing::Matcher<const OverlayCandidateList&>
  WhenCandidatesAreSortedElementsAre(
      std::vector<testing::Matcher<const OverlayCandidate&>> element_matchers) {
    return testing::AllOf(
        PlaneZOrdersAreUnique(),
        testing::WhenSortedBy(test::PlaneZOrderAscendingComparator(),
                              testing::ElementsAreArray(element_matchers)));
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

// Check that we can do delegated compositing of a single quad.
TEST_F(DCLayerOverlayDelegatedCompositingTest, SingleQuad) {
  AggregatedRenderPassList pass_list;

  auto pass = CreateRenderPass();
  CreateSolidColorQuadAt(pass->CreateAndAppendSharedQuadState(), SkColors::kRed,
                         pass.get(), gfx::Rect(0, 0, 50, 50));
  pass_list.push_back(std::move(pass));

  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationSuccess();
  EXPECT_THAT(result.candidates(),
              WhenCandidatesAreSortedElementsAre({
                  test::IsSolidColorOverlay(SkColors::kRed),
              }));
}

// Check that, when delegated compositing fails, we still successfully promote
// videos to overlay.
TEST_F(DCLayerOverlayDelegatedCompositingTest,
       DelegationFailStillPromotesVideos) {
  AggregatedRenderPassList pass_list;

  auto pass = CreateRenderPass();
  // Non-overlay candidate resource will prevent delegation
  CreateTextureQuadAt(resource_provider_.get(), child_resource_provider_.get(),
                      child_provider_.get(),
                      pass->shared_quad_state_list.back(), pass.get(),
                      gfx::Rect(0, 0, 50, 50), /*is_overlay_candidate=*/false);
  auto* video_quad = CreateFullscreenCandidateYUVVideoQuad(
      resource_provider_.get(), child_resource_provider_.get(),
      child_provider_.get(), pass->shared_quad_state_list.back(), pass.get());
  ResourceId video_resource_id = video_quad->y_plane_resource_id();
  pass_list.push_back(std::move(pass));

  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationFailure();
  EXPECT_THAT(result.candidates(),
              WhenCandidatesAreSortedElementsAre({
                  test::OverlayHasResource(video_resource_id),
              }))
      << "The overlay processor fall back to using DCLayerOverlayProcessor on "
         "the root surface.";
}

// Test that when |OverlayCandidateFactory| returns |kFailVisible| we just skip
// the quad instead of failing delegation.
TEST_F(DCLayerOverlayDelegatedCompositingTest, SkipNonVisibleOverlays) {
  AggregatedRenderPassList pass_list;

  auto pass = CreateRenderPass();
  CreateSolidColorQuadAt(pass->CreateAndAppendSharedQuadState(), SkColors::kRed,
                         pass.get(), gfx::Rect(0, 0, 0, 0));
  pass_list.push_back(std::move(pass));

  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationSuccess();
  EXPECT_THAT(result.candidates(), testing::IsEmpty());
}

// Check that delegated compositing fails when there is a color conversion pass.
TEST_F(DCLayerOverlayDelegatedCompositingTest, HdrNotSupported) {
  AggregatedRenderPassList pass_list;

  pass_list.push_back(CreateRenderPass(AggregatedRenderPassId{2}));

  auto pass = CreateRenderPass();
  pass->is_color_conversion_pass = true;
  pass_list.push_back(std::move(pass));

  damage_rect_ = pass_list.back()->damage_rect;

  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationFailure();
}

// Check that delegated compositing fails when the root is being captured.
TEST_F(DCLayerOverlayDelegatedCompositingTest, CaptureNotSupported) {
  AggregatedRenderPassList pass_list;

  auto pass = CreateRenderPass();
  pass->video_capture_enabled = true;
  pass_list.push_back(std::move(pass));

  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationFailure();
}

// Check that delegated compositing fails when there is a backdrop filter that
// would need to read another overlay candidate.
TEST_F(DCLayerOverlayDelegatedCompositingTest,
       OccludedByFilteredQuadNotSupported) {
  AggregatedRenderPassList pass_list;

  AggregatedRenderPassId child_pass_id{2};

  // Create a pass with a backdrop filter.
  {
    auto child_pass = CreateRenderPass(child_pass_id);
    child_pass->backdrop_filters = cc::FilterOperations({
        cc::FilterOperation::CreateGrayscaleFilter(1.0f),
    });
    pass_list.push_back(std::move(child_pass));
  }

  {
    auto pass = CreateRenderPass();

    const gfx::Rect rect(0, 0, 50, 50);

    CreateRenderPassDrawQuadAt(pass.get(),
                               pass->CreateAndAppendSharedQuadState(), rect,
                               child_pass_id);

    // Create a quad that will be occluded by the backdrop-filtered RPDQ above.
    CreateSolidColorQuadAt(pass->CreateAndAppendSharedQuadState(),
                           SkColors::kRed, pass.get(), rect);

    pass_list.push_back(std::move(pass));
  }

  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationFailure();
}

// Check that the various ways we can set |will_backing_be_read_by_viz| work as
// expected.
TEST_F(DCLayerOverlayDelegatedCompositingTest, BackingWillBeReadInViz) {
  AggregatedRenderPassList pass_list;

  AggregatedRenderPassId::Generator id_generator;
  base::flat_map<AggregatedRenderPassId, const char*> pass_names;
  base::flat_set<AggregatedRenderPassId> passes_to_embed_in_root;

  auto CreateNamedPass =
      [&](const char* name, bool embed_in_root,
          base::OnceCallback<void(AggregatedRenderPass*)> update_pass) {
        AggregatedRenderPassId pass_id = id_generator.GenerateNextId();
        pass_names.insert({pass_id, name});
        if (embed_in_root) {
          passes_to_embed_in_root.insert(pass_id);
        }

        std::unique_ptr<AggregatedRenderPass> pass = CreateRenderPass(pass_id);
        std::move(update_pass).Run(pass.get());
        pass_list.push_back(std::move(pass));

        return pass_id;
      };

  CreateNamedPass("video capture enabled", true,
                  base::BindOnce([](AggregatedRenderPass* pass) {
                    pass->video_capture_enabled = true;
                  }));

  CreateNamedPass("filters", true,
                  base::BindOnce([](AggregatedRenderPass* pass) {
                    pass->filters = cc::FilterOperations({
                        cc::FilterOperation::CreateGrayscaleFilter(1.0f),
                    });
                  }));

  CreateNamedPass("generate mipmaps", true,
                  base::BindOnce([](AggregatedRenderPass* pass) {
                    pass->generate_mipmap = true;
                  }));

  auto non_overlay_embeddee_id = CreateNamedPass(
      "normal pass with non-overlay embedder", true, base::DoNothing());
  CreateNamedPass("non-overlay embedder", false,
                  base::BindLambdaForTesting([&](AggregatedRenderPass* pass) {
                    CreateRenderPassDrawQuadAt(
                        pass, pass->CreateAndAppendSharedQuadState(),
                        pass->output_rect, non_overlay_embeddee_id);
                  }));

  auto complex_mask_embeddee_id = CreateNamedPass(
      "normal pass with gradient mask embedder", true, base::DoNothing());
  CreateNamedPass("gradient mask embedder", false,
                  base::BindLambdaForTesting([&](AggregatedRenderPass* pass) {
                    auto* sqs = pass->CreateAndAppendSharedQuadState();
                    CreateRenderPassDrawQuadAt(pass, sqs, pass->output_rect,
                                               complex_mask_embeddee_id);

                    // We can delegated rounded corners fine, so set a complex
                    // mask filter that we will handle with an intermediate
                    // surface in |SkiaRenderer|.
                    gfx::LinearGradient gradient;
                    gradient.AddStep(0.f, 0);
                    gradient.AddStep(1.f, 0xff);
                    sqs->mask_filter_info = gfx::MaskFilterInfo(
                        gfx::RRectF(gfx::RectF(pass->output_rect)), gradient);
                  }));

  CreateNamedPass("root pass", false,
                  base::BindLambdaForTesting([&](AggregatedRenderPass* pass) {
                    for (auto id : passes_to_embed_in_root) {
                      CreateRenderPassDrawQuadAt(
                          pass, pass->CreateAndAppendSharedQuadState(),
                          pass->output_rect, id);
                    }
                  }));

  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationSuccess();

  // In this test, we expect every pass except the root pass to be read by viz.
  // Passes that are not composited as overlays are assumed to be read by viz
  // e.g. for copy output requests, etc.
  for (size_t i = 0u; i < pass_list.size(); i++) {
    SCOPED_TRACE(base::StringPrintf("pass_list[%zu]: %s", i,
                                    pass_names[pass_list[i]->id]));
    if (pass_list[i] == pass_list.back()) {
      EXPECT_FALSE(pass_list[i]->will_backing_be_read_by_viz);
    } else {
      EXPECT_TRUE(pass_list[i]->will_backing_be_read_by_viz);
    }
  }
}

// Tests that check that overlay promotion is supported from non-root render
// passes in the partially delegated case.
class DCLayerOverlayPartiallyDelegatedCompositingTest
    : public DCLayerOverlayDelegatedCompositingTest {
 protected:
  DCLayerOverlayPartiallyDelegatedCompositingTest() {
    feature_list_.InitAndEnableFeature(
        features::kDelegatedCompositingLimitToUi);
  }

  TextureDrawQuad* CreateOverlayQuadWithSurfaceDamageAt(
      AggregatedRenderPass* pass,
      SurfaceDamageRectList& surface_damage_rect_list,
      const gfx::Rect& rect) {
    SharedQuadState* sqs = pass->CreateAndAppendSharedQuadState();
    auto* quad = CreateTextureQuadAt(resource_provider_.get(),
                                     child_resource_provider_.get(),
                                     child_provider_.get(), sqs, pass, rect,
                                     /*is_overlay_candidate=*/true);

    pass->damage_rect.Union(
        sqs->quad_to_target_transform.MapRect(quad->visible_rect));

    gfx::Transform quad_to_root_transform(sqs->quad_to_target_transform);
    quad_to_root_transform.PostConcat(pass->transform_to_root_target);

    sqs->overlay_damage_index = surface_damage_rect_list.size();
    surface_damage_rect_list.push_back(
        quad_to_root_transform.MapRect(quad->visible_rect));

    return quad;
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

// Check that an overlay candidate can be promoted from a non-root pass
// representing a surface.
TEST_F(DCLayerOverlayPartiallyDelegatedCompositingTest,
       CandidatePromotedFromNonRootSurface) {
  AggregatedRenderPassList pass_list;

  const AggregatedRenderPassId child_pass_id{2};
  ResourceId child_pass_texture_id;

  // Create a pass with just an overlay quad.
  {
    auto child_pass = CreateRenderPass(child_pass_id);
    child_pass->is_from_surface_root_pass = true;
    auto* texture_quad = CreateTextureQuadAt(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), child_pass->CreateAndAppendSharedQuadState(),
        child_pass.get(), gfx::Rect(0, 0, 50, 50),
        /*is_overlay_candidate=*/true);
    child_pass_texture_id = texture_quad->resource_id();
    pass_list.push_back(std::move(child_pass));
  }

  {
    auto pass = CreateRenderPass();
    CreateRenderPassDrawQuadAt(pass.get(),
                               pass->CreateAndAppendSharedQuadState(),
                               gfx::Rect(0, 0, 50, 50), child_pass_id);
    pass_list.push_back(std::move(pass));
  }

  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationSuccess();

  // We expect both the RPDQ and the inner video to be promoted.
  EXPECT_THAT(result.candidates(),
              WhenCandidatesAreSortedElementsAre({
                  test::IsRenderPassOverlay(child_pass_id),
                  test::OverlayHasResource(child_pass_texture_id),
              }));
}

// Check that an overlay candidate can be promoted from a non-root pass
// representing a surface, but will be placed behind the output surface plane if
// it is occluded by something in the surface.
TEST_F(DCLayerOverlayPartiallyDelegatedCompositingTest,
       CandidatePromotedFromNonRootSurfaceAsUnderlay) {
  AggregatedRenderPassList pass_list;

  const AggregatedRenderPassId child_pass_id{2};
  ResourceId child_pass_texture_id;

  // Create a pass with an overlay quad that is occluded by some other quad.
  // This forces the overlay candidate to appear as an underlay to the surface.
  {
    auto child_pass = CreateRenderPass(child_pass_id);
    child_pass->is_from_surface_root_pass = true;
    CreateSolidColorQuadAt(child_pass->CreateAndAppendSharedQuadState(),
                           SkColors::kRed, child_pass.get(),
                           gfx::Rect(5, 5, 10, 10));
    auto* texture_quad = CreateTextureQuadAt(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), child_pass->CreateAndAppendSharedQuadState(),
        child_pass.get(), gfx::Rect(0, 0, 50, 50),
        /*is_overlay_candidate=*/true);
    child_pass_texture_id = texture_quad->resource_id();
    pass_list.push_back(std::move(child_pass));
  }

  {
    auto pass = CreateRenderPass();
    CreateRenderPassDrawQuadAt(pass.get(),
                               pass->CreateAndAppendSharedQuadState(),
                               gfx::Rect(0, 0, 50, 50), child_pass_id);
    CreateSolidColorQuadAt(pass->CreateAndAppendSharedQuadState(),
                           SkColors::kBlue, pass.get(), pass->output_rect);
    pass_list.push_back(std::move(pass));
  }

  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationSuccess();

  // We expect both the RPDQ and the inner video to be promoted and in front of
  // the solid color background in the root pass.
  EXPECT_THAT(result.candidates(),
              WhenCandidatesAreSortedElementsAre({
                  test::IsSolidColorOverlay(SkColors::kBlue),
                  test::OverlayHasResource(child_pass_texture_id),
                  test::IsRenderPassOverlay(child_pass_id),
              }));
}

TEST_F(DCLayerOverlayPartiallyDelegatedCompositingTest,
       CandidatesPromotedFromMultipleSurfaces) {
  AggregatedRenderPassList pass_list;

  const AggregatedRenderPassId child_pass_id{2};
  ResourceId child_pass_video_id;
  {
    auto child_pass = CreateRenderPass(child_pass_id);
    child_pass->is_from_surface_root_pass = true;
    auto* texture_quad = CreateTextureQuadAt(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), child_pass->CreateAndAppendSharedQuadState(),
        child_pass.get(), gfx::Rect(0, 0, 50, 50),
        /*is_overlay_candidate=*/true);
    child_pass_video_id = texture_quad->resource_id();
    pass_list.push_back(std::move(child_pass));
  }

  const AggregatedRenderPassId other_child_pass_id{3};
  ResourceId other_child_pass_video_id;
  ResourceId other_child_pass_video_2_id;
  {
    auto other_child_pass = CreateRenderPass(other_child_pass_id);
    other_child_pass->is_from_surface_root_pass = true;
    // Make this first quad partially occlude the next.
    auto* texture_quad = CreateTextureQuadAt(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(),
        other_child_pass->CreateAndAppendSharedQuadState(),
        other_child_pass.get(), gfx::Rect(10, 0, 50, 50),
        /*is_overlay_candidate=*/true);
    other_child_pass_video_id = texture_quad->resource_id();
    auto* texture_quad_2 = CreateTextureQuadAt(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(),
        other_child_pass->CreateAndAppendSharedQuadState(),
        other_child_pass.get(), gfx::Rect(0, 0, 50, 50),
        /*is_overlay_candidate=*/true);
    other_child_pass_video_2_id = texture_quad_2->resource_id();
    pass_list.push_back(std::move(other_child_pass));
  }

  {
    auto pass = CreateRenderPass();
    CreateRenderPassDrawQuadAt(pass.get(),
                               pass->CreateAndAppendSharedQuadState(),
                               gfx::Rect(0, 0, 50, 50), child_pass_id);
    CreateSolidColorQuadAt(pass->CreateAndAppendSharedQuadState(),
                           SkColors::kBlue, pass.get(), pass->output_rect);
    CreateRenderPassDrawQuadAt(pass.get(),
                               pass->CreateAndAppendSharedQuadState(),
                               gfx::Rect(50, 0, 50, 50), other_child_pass_id);
    pass_list.push_back(std::move(pass));
  }

  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationSuccess();

  // We expect both the RPDQ and the inner video(s) to be promoted for both
  // RPDQs.
  EXPECT_THAT(result.candidates(),
              WhenCandidatesAreSortedElementsAre({
                  test::OverlayHasResource(other_child_pass_video_2_id),
                  test::IsRenderPassOverlay(other_child_pass_id),
                  test::OverlayHasResource(other_child_pass_video_id),
                  test::IsSolidColorOverlay(SkColors::kBlue),
                  test::IsRenderPassOverlay(child_pass_id),
                  test::OverlayHasResource(child_pass_video_id),
              }));
}

TEST_F(DCLayerOverlayPartiallyDelegatedCompositingTest,
       CandidatePromotionRespectsAllowedYuvOverlayCount) {
  AggregatedRenderPassList pass_list;

  const AggregatedRenderPassId child_pass_id{2};
  {
    auto child_pass = CreateRenderPass(child_pass_id);
    child_pass->is_from_surface_root_pass = true;
    auto* texture_quad = CreateTextureQuadAt(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), child_pass->CreateAndAppendSharedQuadState(),
        child_pass.get(), gfx::Rect(0, 0, 50, 50),
        /*is_overlay_candidate=*/true);
    texture_quad->is_video_frame = true;
    pass_list.push_back(std::move(child_pass));
  }

  const AggregatedRenderPassId other_child_pass_id{3};
  {
    auto other_child_pass = CreateRenderPass(other_child_pass_id);
    other_child_pass->is_from_surface_root_pass = true;
    auto* texture_quad = CreateTextureQuadAt(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(),
        other_child_pass->CreateAndAppendSharedQuadState(),
        other_child_pass.get(), gfx::Rect(0, 0, 50, 50));
    texture_quad->is_video_frame = true;
    pass_list.push_back(std::move(other_child_pass));
  }

  {
    auto pass = CreateRenderPass();
    CreateRenderPassDrawQuadAt(pass.get(),
                               pass->CreateAndAppendSharedQuadState(),
                               gfx::Rect(0, 0, 50, 50), child_pass_id);
    CreateRenderPassDrawQuadAt(pass.get(),
                               pass->CreateAndAppendSharedQuadState(),
                               gfx::Rect(50, 0, 50, 50), other_child_pass_id);
    pass_list.push_back(std::move(pass));
  }

  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationSuccess();

  // We expect both the RPDQs to be promoted, but neither of the videos.
  EXPECT_THAT(result.candidates(),
              WhenCandidatesAreSortedElementsAre({
                  test::IsRenderPassOverlay(other_child_pass_id),
                  test::IsRenderPassOverlay(child_pass_id),
              }));
}

TEST_F(DCLayerOverlayPartiallyDelegatedCompositingTest,
       CandidatesInheritSurfaceEmbeddersBounds) {
  AggregatedRenderPassList pass_list;

  const AggregatedRenderPassId child_pass_id{2};
  ResourceId child_pass_texture_id;

  {
    auto child_pass = CreateRenderPass(child_pass_id);
    child_pass->is_from_surface_root_pass = true;
    auto* texture_quad = CreateTextureQuadAt(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), child_pass->CreateAndAppendSharedQuadState(),
        child_pass.get(), gfx::Rect(0, 0, 50, 50),
        /*is_overlay_candidate=*/true);
    child_pass_texture_id = texture_quad->resource_id();
    pass_list.push_back(std::move(child_pass));
  }

  const gfx::Rect rpdq_bounds = gfx::Rect(0, 0, 20, 30);
  gfx::Rect expected_overlay_clip;

  {
    auto pass = CreateRenderPass();
    SharedQuadState* sqs = pass->CreateAndAppendSharedQuadState();
    sqs->quad_to_target_transform.Translate(1, 2);
    CreateRenderPassDrawQuadAt(pass.get(), sqs, rpdq_bounds, child_pass_id);

    expected_overlay_clip = sqs->quad_to_target_transform.MapRect(rpdq_bounds);

    pass_list.push_back(std::move(pass));
  }

  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationSuccess();

  EXPECT_THAT(
      result.candidates(),
      WhenCandidatesAreSortedElementsAre({
          test::IsRenderPassOverlay(child_pass_id),
          testing::AllOf(test::OverlayHasResource(child_pass_texture_id),
                         test::OverlayHasClip(expected_overlay_clip)),
      }));
}

TEST_F(DCLayerOverlayPartiallyDelegatedCompositingTest,
       CandidatesInheritSurfaceEmbeddersClip) {
  AggregatedRenderPassList pass_list;

  const AggregatedRenderPassId child_pass_id{2};
  ResourceId child_pass_texture_id;

  const gfx::Transform child_to_root = gfx::Transform::MakeTranslation(1, 2);

  {
    auto child_pass = CreateRenderPass(child_pass_id);
    child_pass->is_from_surface_root_pass = true;
    child_pass->transform_to_root_target = child_to_root;
    auto* texture_quad = CreateTextureQuadAt(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), child_pass->CreateAndAppendSharedQuadState(),
        child_pass.get(), gfx::Rect(0, 0, 50, 50),
        /*is_overlay_candidate=*/true);
    child_pass_texture_id = texture_quad->resource_id();
    pass_list.push_back(std::move(child_pass));
  }

  const gfx::Rect rpdq_clip_rect = gfx::Rect(10, 20, 5, 15);
  const gfx::Rect rpdq_bounds = gfx::Rect(0, 0, 20, 30);
  gfx::Rect expected_overlay_clip;

  {
    auto pass = CreateRenderPass();
    SharedQuadState* sqs = pass->CreateAndAppendSharedQuadState();
    sqs->quad_to_target_transform = child_to_root;
    sqs->clip_rect = rpdq_clip_rect;
    CreateRenderPassDrawQuadAt(pass.get(), sqs, rpdq_bounds, child_pass_id);

    expected_overlay_clip = sqs->quad_to_target_transform.MapRect(rpdq_bounds);
    expected_overlay_clip.Intersect(rpdq_clip_rect);

    pass_list.push_back(std::move(pass));
  }
  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationSuccess();

  EXPECT_THAT(
      result.candidates(),
      WhenCandidatesAreSortedElementsAre({
          testing::AllOf(test::IsRenderPassOverlay(child_pass_id),
                         test::OverlayHasClip(rpdq_clip_rect)),
          testing::AllOf(test::OverlayHasResource(child_pass_texture_id),
                         test::OverlayHasClip(expected_overlay_clip)),
      }));
}

TEST_F(DCLayerOverlayPartiallyDelegatedCompositingTest,
       CandidatesInheritSurfaceEmbeddersClipAndIntersect) {
  AggregatedRenderPassList pass_list;

  const AggregatedRenderPassId child_pass_id{2};
  ResourceId child_pass_texture_id;

  const gfx::Rect inner_quad_clip = gfx::Rect(10, 15, 1, 2);
  const gfx::Transform child_to_root = gfx::Transform::MakeTranslation(1, 2);

  {
    auto child_pass = CreateRenderPass(child_pass_id);
    child_pass->is_from_surface_root_pass = true;
    child_pass->transform_to_root_target = child_to_root;
    SharedQuadState* sqs = child_pass->CreateAndAppendSharedQuadState();
    sqs->clip_rect = inner_quad_clip;
    auto* texture_quad = CreateTextureQuadAt(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), sqs, child_pass.get(), gfx::Rect(0, 0, 50, 50),
        /*is_overlay_candidate=*/true);
    child_pass_texture_id = texture_quad->resource_id();
    pass_list.push_back(std::move(child_pass));
  }

  const gfx::Rect rpdq_bounds = gfx::Rect(0, 0, 20, 30);
  gfx::Rect expected_overlay_clip;

  {
    auto pass = CreateRenderPass();
    SharedQuadState* sqs = pass->CreateAndAppendSharedQuadState();
    sqs->quad_to_target_transform = child_to_root;
    CreateRenderPassDrawQuadAt(pass.get(), sqs, rpdq_bounds, child_pass_id);

    expected_overlay_clip =
        sqs->quad_to_target_transform.MapRect(inner_quad_clip);
    expected_overlay_clip.Intersect(rpdq_bounds);

    pass_list.push_back(std::move(pass));
  }
  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationSuccess();

  EXPECT_THAT(
      result.candidates(),
      WhenCandidatesAreSortedElementsAre({
          test::IsRenderPassOverlay(child_pass_id),
          testing::AllOf(test::OverlayHasResource(child_pass_texture_id),
                         test::OverlayHasClip(expected_overlay_clip)),
      }));
}

TEST_F(DCLayerOverlayPartiallyDelegatedCompositingTest,
       CandidatesInheritSurfaceEmbeddersClipAndRoundedCorners) {
  AggregatedRenderPassList pass_list;

  const AggregatedRenderPassId child_pass_id{2};
  ResourceId child_pass_texture_id;

  {
    auto child_pass = CreateRenderPass(child_pass_id);
    child_pass->is_from_surface_root_pass = true;
    SharedQuadState* sqs = child_pass->CreateAndAppendSharedQuadState();
    // We expect this rounded corner to be painted into the |child_pass|
    // surface.
    sqs->mask_filter_info =
        gfx::MaskFilterInfo(gfx::RRectF(gfx::RectF(10, 10), 1));
    auto* texture_quad = CreateTextureQuadAt(
        resource_provider_.get(), child_resource_provider_.get(),
        child_provider_.get(), sqs, child_pass.get(), gfx::Rect(0, 0, 50, 50),
        /*is_overlay_candidate=*/true);
    child_pass_texture_id = texture_quad->resource_id();
    pass_list.push_back(std::move(child_pass));
  }

  const gfx::RRectF rpdq_rounded_corners = gfx::RRectF(gfx::RectF(10, 10), 1);

  {
    auto pass = CreateRenderPass();
    SharedQuadState* sqs = pass->CreateAndAppendSharedQuadState();
    sqs->mask_filter_info = gfx::MaskFilterInfo(rpdq_rounded_corners);
    CreateRenderPassDrawQuadAt(pass.get(), sqs, gfx::Rect(0, 0, 50, 50),
                               child_pass_id);

    pass_list.push_back(std::move(pass));
  }
  auto result = TryProcessForDelegatedOverlays(pass_list);
  result.ExpectDelegationSuccess();

  // Our texture quad is behind the surface, due to having its own rounded
  // corners.
  EXPECT_THAT(
      result.candidates(),
      WhenCandidatesAreSortedElementsAre({
          testing::AllOf(test::OverlayHasResource(child_pass_texture_id),
                         test::OverlayHasClip(gfx::Rect(0, 0, 50, 50)),
                         test::OverlayHasRoundedCorners(rpdq_rounded_corners)),
          testing::AllOf(test::IsRenderPassOverlay(child_pass_id),
                         test::OverlayHasRoundedCorners(rpdq_rounded_corners)),
      }));
}

TEST_F(DCLayerOverlayPartiallyDelegatedCompositingTest,
       DamageRemovedFromSurface) {
  const AggregatedRenderPassId child_pass_id{2};
  const gfx::Rect texture_quad_rect = gfx::Rect(5, 5, 50, 50);

  for (int frame = 0; frame < 3; frame++) {
    SCOPED_TRACE(base::StringPrintf("Frame %d", frame));

    const bool is_overlay_candidate_with_damage = frame < 2;

    AggregatedRenderPassList pass_list;
    SurfaceDamageRectList surface_damage_rect_list;

    const gfx::Rect rpdq_quad = gfx::Rect(10, 10, 100, 100);

    auto child_pass = CreateRenderPass(child_pass_id);
    child_pass->transform_to_root_target =
        gfx::Transform::MakeTranslation(rpdq_quad.OffsetFromOrigin());
    child_pass->is_from_surface_root_pass = true;
    child_pass->damage_rect = gfx::Rect();
    auto* texture_quad =
        is_overlay_candidate_with_damage
            ? CreateOverlayQuadWithSurfaceDamageAt(
                  child_pass.get(), surface_damage_rect_list, texture_quad_rect)
            : CreateTextureQuadAt(resource_provider_.get(),
                                  child_resource_provider_.get(),
                                  child_provider_.get(),
                                  child_pass->CreateAndAppendSharedQuadState(),
                                  child_pass.get(), texture_quad_rect,
                                  /*is_overlay_candidate=*/false);
    ResourceId child_pass_texture_id = texture_quad->resource_id();
    pass_list.push_back(std::move(child_pass));

    auto pass = CreateRenderPass();
    CreateRenderPassDrawQuadAt(pass.get(),
                               pass->CreateAndAppendSharedQuadState(),
                               rpdq_quad, child_pass_id);
    pass_list.push_back(std::move(pass));

    switch (frame) {
      case 0:
      case 1:
        EXPECT_EQ(pass_list[0]->damage_rect, texture_quad_rect)
            << "The quad in the child surface contributes damage";
        break;

      case 2:
        EXPECT_EQ(pass_list[0]->damage_rect, gfx::Rect())
            << "No damage on the child surface before overlay processing";
        break;
    }

    auto result = TryProcessForDelegatedOverlays(
        pass_list, std::move(surface_damage_rect_list));
    result.ExpectDelegationSuccess();

    switch (frame) {
      case 0:
        EXPECT_EQ(pass_list[0]->damage_rect, pass_list[0]->output_rect)
            << "Full damage is forced on the first frame";
        EXPECT_THAT(result.candidates(),
                    WhenCandidatesAreSortedElementsAre({
                        test::IsRenderPassOverlay(child_pass_id),
                        test::OverlayHasResource(child_pass_texture_id),
                    }));
        break;

      case 1:
        EXPECT_EQ(pass_list[0]->damage_rect, gfx::Rect())
            << "Damage is removed when only from overlays";
        EXPECT_THAT(result.candidates(),
                    WhenCandidatesAreSortedElementsAre({
                        test::IsRenderPassOverlay(child_pass_id),
                        test::OverlayHasResource(child_pass_texture_id),
                    }));
        break;

      case 2:
        EXPECT_EQ(pass_list[0]->damage_rect, texture_quad_rect)
            << "Damage removed in frame 1 is re-added";
        EXPECT_THAT(result.candidates(),
                    WhenCandidatesAreSortedElementsAre({
                        test::IsRenderPassOverlay(child_pass_id),
                    }));
        break;
    }
  }
}

TEST_F(DCLayerOverlayPartiallyDelegatedCompositingTest,
       DamageRemovedFromMultipleSurfaces) {
  const AggregatedRenderPassId left_child_pass_id{2};
  const AggregatedRenderPassId right_child_pass_id{3};
  const gfx::Rect left_texture_quad_rect = gfx::Rect(5, 5, 50, 50);
  const gfx::Rect right_texture_quad_rect = gfx::Rect(10, 5, 50, 50);

  for (int frame = 0; frame < 4; frame++) {
    SCOPED_TRACE(base::StringPrintf("Frame %d", frame));

    AggregatedRenderPassList pass_list;
    SurfaceDamageRectList surface_damage_rect_list;

    const bool is_left_overlay_candidate_with_damage = frame < 3;
    const bool is_right_overlay_candidate_with_damage = frame < 2;

    auto pass = CreateRenderPass();
    gfx::Rect left_rpdq_quad;
    gfx::Rect right_rpdq_quad;
    pass->output_rect.SplitVertically(left_rpdq_quad, right_rpdq_quad);
    // Ensure the RPDQs aren't touching so their damages will be disjoint.
    left_rpdq_quad.Inset(5);
    right_rpdq_quad.Inset(5);

    auto left_child_pass = CreateRenderPass(left_child_pass_id);
    left_child_pass->transform_to_root_target =
        gfx::Transform::MakeTranslation(left_rpdq_quad.OffsetFromOrigin());
    left_child_pass->is_from_surface_root_pass = true;
    left_child_pass->output_rect.set_size(left_rpdq_quad.size());
    left_child_pass->damage_rect = gfx::Rect();
    auto* left_texture_quad =
        is_left_overlay_candidate_with_damage
            ? CreateOverlayQuadWithSurfaceDamageAt(left_child_pass.get(),
                                                   surface_damage_rect_list,
                                                   left_texture_quad_rect)
            : CreateTextureQuadAt(
                  resource_provider_.get(), child_resource_provider_.get(),
                  child_provider_.get(),
                  left_child_pass->CreateAndAppendSharedQuadState(),
                  left_child_pass.get(), left_texture_quad_rect,
                  /*is_overlay_candidate=*/false);
    ResourceId left_child_pass_texture_id = left_texture_quad->resource_id();
    pass_list.push_back(std::move(left_child_pass));

    auto right_child_pass = CreateRenderPass(right_child_pass_id);
    right_child_pass->transform_to_root_target =
        gfx::Transform::MakeTranslation(right_rpdq_quad.OffsetFromOrigin());
    right_child_pass->is_from_surface_root_pass = true;
    right_child_pass->output_rect.set_size(right_rpdq_quad.size());
    right_child_pass->damage_rect = gfx::Rect();
    auto* right_texture_quad =
        is_right_overlay_candidate_with_damage
            ? CreateOverlayQuadWithSurfaceDamageAt(right_child_pass.get(),
                                                   surface_damage_rect_list,
                                                   right_texture_quad_rect)
            : CreateTextureQuadAt(
                  resource_provider_.get(), child_resource_provider_.get(),
                  child_provider_.get(),
                  right_child_pass->CreateAndAppendSharedQuadState(),
                  right_child_pass.get(), right_texture_quad_rect,
                  /*is_overlay_candidate=*/false);
    ResourceId right_child_pass_texture_id = right_texture_quad->resource_id();
    pass_list.push_back(std::move(right_child_pass));

    CreateRenderPassDrawQuadAt(pass.get(),
                               pass->CreateAndAppendSharedQuadState(),
                               left_rpdq_quad, left_child_pass_id);
    CreateRenderPassDrawQuadAt(pass.get(),
                               pass->CreateAndAppendSharedQuadState(),
                               right_rpdq_quad, right_child_pass_id);
    pass_list.push_back(std::move(pass));

    switch (frame) {
      case 0:
      case 1:
        EXPECT_EQ(pass_list[0]->damage_rect, left_texture_quad_rect)
            << "The quad in the child surface contributes damage";
        EXPECT_EQ(pass_list[1]->damage_rect, right_texture_quad_rect)
            << "The quad in the child surface contributes damage";
        break;

      case 2:
        EXPECT_EQ(pass_list[0]->damage_rect, left_texture_quad_rect)
            << "The quad in the child surface contributes damage";
        EXPECT_EQ(pass_list[1]->damage_rect, gfx::Rect())
            << "No damage on the child surface before overlay processing";
        break;

      case 3:
        EXPECT_EQ(pass_list[0]->damage_rect, gfx::Rect())
            << "No damage on the child surface before overlay processing";
        EXPECT_EQ(pass_list[1]->damage_rect, gfx::Rect())
            << "No damage on the child surface before overlay processing";
        break;
    }

    auto result = TryProcessForDelegatedOverlays(
        pass_list, std::move(surface_damage_rect_list));
    result.ExpectDelegationSuccess();

    switch (frame) {
      case 0:
        EXPECT_EQ(pass_list[0]->damage_rect, pass_list[0]->output_rect)
            << "Full damage is forced on the first frame";
        EXPECT_EQ(pass_list[1]->damage_rect, pass_list[1]->output_rect)
            << "Full damage is forced on the first frame";
        EXPECT_THAT(result.candidates(),
                    WhenCandidatesAreSortedElementsAre({
                        test::IsRenderPassOverlay(right_child_pass_id),
                        test::OverlayHasResource(right_child_pass_texture_id),
                        test::IsRenderPassOverlay(left_child_pass_id),
                        test::OverlayHasResource(left_child_pass_texture_id),
                    }));
        break;

      case 1:
        EXPECT_EQ(pass_list[0]->damage_rect, gfx::Rect())
            << "Damage is removed when only from overlays";
        EXPECT_EQ(pass_list[1]->damage_rect, gfx::Rect())
            << "Damage is removed when only from overlays";
        EXPECT_THAT(result.candidates(),
                    WhenCandidatesAreSortedElementsAre({
                        test::IsRenderPassOverlay(right_child_pass_id),
                        test::OverlayHasResource(right_child_pass_texture_id),
                        test::IsRenderPassOverlay(left_child_pass_id),
                        test::OverlayHasResource(left_child_pass_texture_id),
                    }));
        break;

      case 2:
        EXPECT_EQ(pass_list[0]->damage_rect, gfx::Rect())
            << "Damage is removed when only from overlays";
        EXPECT_EQ(pass_list[1]->damage_rect, right_texture_quad_rect)
            << "Damage removed in frame 1 is re-added";
        EXPECT_THAT(result.candidates(),
                    WhenCandidatesAreSortedElementsAre({
                        test::IsRenderPassOverlay(right_child_pass_id),
                        test::IsRenderPassOverlay(left_child_pass_id),
                        test::OverlayHasResource(left_child_pass_texture_id),
                    }));
        break;

      case 3:
        EXPECT_EQ(pass_list[0]->damage_rect, left_texture_quad_rect)
            << "Damage removed in frame 2 is re-added";
        EXPECT_EQ(pass_list[1]->damage_rect, gfx::Rect());
        EXPECT_THAT(result.candidates(),
                    WhenCandidatesAreSortedElementsAre({
                        test::IsRenderPassOverlay(right_child_pass_id),
                        test::IsRenderPassOverlay(left_child_pass_id),
                    }));
        break;
    }
  }
}

}  // namespace
}  // namespace viz
