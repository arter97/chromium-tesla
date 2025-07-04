// Copyright 2022 The Dawn & Tint Authors
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

#include "dawn/native/opengl/ContextEGL.h"

#include <memory>
#include <string>
#include <vector>

#include "dawn/native/opengl/UtilsEGL.h"

#ifndef EGL_DISPLAY_TEXTURE_SHARE_GROUP_ANGLE
#define EGL_DISPLAY_TEXTURE_SHARE_GROUP_ANGLE 0x33AF
#endif

namespace dawn::native::opengl {

ResultOrError<std::unique_ptr<ContextEGL>> ContextEGL::Create(const EGLFunctions& egl,
                                                              EGLenum api,
                                                              EGLDisplay display,
                                                              bool useANGLETextureSharing) {
    EGLint renderableType = api == EGL_OPENGL_ES_API ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_BIT;

    // We require at least EGL 1.4.
    DAWN_INVALID_IF(
        egl.GetMajorVersion() < 1 || (egl.GetMajorVersion() == 1 && egl.GetMinorVersion() < 4),
        "EGL version (%u.%u) must be at least 1.4", egl.GetMajorVersion(), egl.GetMinorVersion());

    // Since we're creating a surfaceless context, the only thing we really care
    // about is the RENDERABLE_TYPE.
    EGLint config_attribs[] = {EGL_RENDERABLE_TYPE, renderableType, EGL_NONE};

    EGLint num_config;
    EGLConfig config;
    DAWN_TRY(CheckEGL(egl, egl.ChooseConfig(display, config_attribs, &config, 1, &num_config),
                      "eglChooseConfig"));

    DAWN_INVALID_IF(num_config == 0, "eglChooseConfig returned zero configs");

    DAWN_TRY(CheckEGL(egl, egl.BindAPI(api), "eglBindAPI"));

    if (!egl.HasExt(EGLExt::ImageBase)) {
        return DAWN_INTERNAL_ERROR("EGL_KHR_image_base is required.");
    }
    if (!egl.HasExt(EGLExt::CreateContextRobustness)) {
        return DAWN_INTERNAL_ERROR("EGL_EXT_create_context_robustness is required.");
    }

    if (!egl.HasExt(EGLExt::FenceSync) && !egl.HasExt(EGLExt::ReusableSync)) {
        return DAWN_INTERNAL_ERROR("EGL_KHR_fence_sync or EGL_KHR_reusable_sync must be supported");
    }

    int major, minor;
    if (api == EGL_OPENGL_ES_API) {
        major = 3;
        minor = 1;
    } else {
        major = 4;
        minor = 4;
    }
    std::vector<EGLint> attrib_list{
        EGL_CONTEXT_MAJOR_VERSION,
        major,
        EGL_CONTEXT_MINOR_VERSION,
        minor,
        EGL_CONTEXT_OPENGL_ROBUST_ACCESS,  // Core in EGL 1.5
        EGL_TRUE,
    };
    if (useANGLETextureSharing) {
        if (!egl.HasExt(EGLExt::DisplayTextureShareGroup)) {
            return DAWN_INTERNAL_ERROR(
                "EGL_GL_ANGLE_display_texture_share_group must be supported to use GL texture "
                "sharing");
        }
        attrib_list.push_back(EGL_DISPLAY_TEXTURE_SHARE_GROUP_ANGLE);
        attrib_list.push_back(EGL_TRUE);
    }
    attrib_list.push_back(EGL_NONE);

    EGLContext context = egl.CreateContext(display, config, EGL_NO_CONTEXT, attrib_list.data());
    DAWN_TRY(CheckEGL(egl, context != EGL_NO_CONTEXT, "eglCreateContext"));

    return std::unique_ptr<ContextEGL>(new ContextEGL(egl, display, context));
}

ContextEGL::ContextEGL(const EGLFunctions& functions, EGLDisplay display, EGLContext context)
    : mEgl(functions), mDisplay(display), mContext(context) {}

void ContextEGL::MakeCurrent() {
    EGLBoolean success = mEgl.MakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, mContext);
    DAWN_ASSERT(success == EGL_TRUE);
}

EGLDisplay ContextEGL::GetEGLDisplay() const {
    return mDisplay;
}

const EGLFunctions& ContextEGL::GetEGL() const {
    return mEgl;
}

ContextEGL::~ContextEGL() {
    mEgl.DestroyContext(mDisplay, mContext);
}

}  // namespace dawn::native::opengl
