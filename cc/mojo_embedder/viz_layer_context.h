// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_MOJO_EMBEDDER_VIZ_LAYER_CONTEXT_H_
#define CC_MOJO_EMBEDDER_VIZ_LAYER_CONTEXT_H_

#include "base/memory/raw_ref.h"
#include "cc/mojo_embedder/mojo_embedder_export.h"
#include "cc/trees/layer_context.h"
#include "cc/trees/layer_tree_host_impl.h"
#include "cc/trees/property_tree.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "services/viz/public/mojom/compositing/compositor_frame_sink.mojom.h"
#include "services/viz/public/mojom/compositing/layer_context.mojom.h"

namespace cc::mojo_embedder {

// A client-side implementation of LayerContext which runs over a Mojo
// connection to a GPU-side LayerContext backend within Viz.
class CC_MOJO_EMBEDDER_EXPORT VizLayerContext
    : public LayerContext,
      public viz::mojom::LayerContextClient {
 public:
  // Constructs a VizLayerContext which submits content on behalf of
  // `frame_sink`. `client` must outlive this object.
  VizLayerContext(viz::mojom::CompositorFrameSink& frame_sink,
                  LayerTreeHostImpl& host_impl);
  ~VizLayerContext() override;

  // LayerContext:
  void SetVisible(bool visible) override;
  void UpdateDisplayTreeFrom(LayerTreeImpl& tree) override;

  // viz::mojom::LayerContextClient:
  void OnRequestCommitForFrame(const viz::BeginFrameArgs& args) override;

 private:
  const raw_ref<LayerTreeHostImpl> host_impl_;

  mojo::AssociatedReceiver<viz::mojom::LayerContextClient> client_receiver_{
      this};
  mojo::AssociatedRemote<viz::mojom::LayerContext> service_;

  PropertyTrees last_committed_property_trees_{*host_impl_};
};

}  // namespace cc::mojo_embedder

#endif  // CC_MOJO_EMBEDDER_VIZ_LAYER_CONTEXT_H_
