// Copyright 2021 The Dawn & Tint Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef SRC_TINT_LANG_WGSL_AST_TRANSFORM_MULTIPLANAR_EXTERNAL_TEXTURE_H_
#define SRC_TINT_LANG_WGSL_AST_TRANSFORM_MULTIPLANAR_EXTERNAL_TEXTURE_H_

#include <unordered_map>
#include <utility>

#include "src/tint/api/common/binding_point.h"
#include "src/tint/lang/core/builtin_fn.h"
#include "src/tint/lang/core/common/multiplanar_options.h"
#include "src/tint/lang/wgsl/ast/struct_member.h"
#include "src/tint/lang/wgsl/ast/transform/transform.h"

namespace tint::ast::transform {

/// Within the MultiplanarExternalTexture transform, each instance of a
/// texture_external binding is unpacked into two texture_2d<f32> bindings
/// representing two possible planes of a texture and a uniform buffer binding
/// representing a struct of parameters. Calls to textureLoad or
/// textureSampleLevel that contain a texture_external parameter will be
/// transformed into a newly generated version of the function, which can
/// perform the desired operation on a single RGBA plane or on separate Y and UV
/// planes, and do colorspace conversions including yuv->rgb conversion, gamma
/// decoding, gamut conversion, and gamma encoding steps. Specifically
// for BT.709 to SRGB conversion, it takes the fast path only doing the yuv->rgb
// step and skipping all other steps.
class MultiplanarExternalTexture final : public Castable<MultiplanarExternalTexture, Transform> {
  public:
    /// NewBindingPoints is consumed by the MultiplanarExternalTexture transform.
    /// Data holds information about location of each texture_external binding and
    /// which binding slots it should expand into.
    struct NewBindingPoints final : public Castable<NewBindingPoints, Data> {
        /// Constructor
        NewBindingPoints();

        /// Constructor
        /// @param bm a map to the new binding slots to use.
        /// @param may_collide if true, then validation will be disabled for binding point
        /// collisions generated by this transform
        explicit NewBindingPoints(tint::transform::multiplanar::BindingsMap bm,
                                  bool may_collide = false);

        /// Destructor
        ~NewBindingPoints() override;

        /// A map of new binding points to use.
        tint::transform::multiplanar::BindingsMap bindings_map;

        /// If true, then validation will be disabled for bindign poitn collisions generated by this
        /// transform.
        bool allow_collisions = false;

        /// Reflection for this struct
        TINT_REFLECT(NewBindingPoints, bindings_map, allow_collisions);
    };

    /// Constructor
    MultiplanarExternalTexture();
    /// Destructor
    ~MultiplanarExternalTexture() override;

    /// @copydoc Transform::Apply
    ApplyResult Apply(const Program& program,
                      const DataMap& inputs,
                      DataMap& outputs) const override;

  private:
    struct State;
};

}  // namespace tint::ast::transform

#endif  // SRC_TINT_LANG_WGSL_AST_TRANSFORM_MULTIPLANAR_EXTERNAL_TEXTURE_H_
