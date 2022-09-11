/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef GL_FRAME_BUFFER_H
#define GL_FRAME_BUFFER_H

#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RTAttachment.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"

namespace Divide {

class GL_API;

namespace Attorney {
    class GLAPIRenderTarget;
};

class glFramebuffer final : public RenderTarget {

    friend class Attorney::GLAPIRenderTarget;

  public:
    enum class AttachmentState : U8 {
        STATE_DISABLED = 0,
        STATE_ENABLED,
        COUNT
    };

    struct BindingState {
        AttachmentState _attState = AttachmentState::COUNT;
        U16 _writeLevel = 0;
        U16 _writeLayer = 0;
        bool _layeredRendering = false;
    };

   public:
    explicit glFramebuffer(GFXDevice& context, const RenderTargetDescriptor& descriptor);
    ~glFramebuffer();

    void drawToLayer(const RTDrawLayerParams& params);

    void setMipLevel(U16 writeLevel);

    void readData(const vec4<U16>& rect,
                  GFXImageFormat imageFormat,
                  GFXDataFormat dataType,
                  std::pair<bufferPtr, size_t> outData) const override;

    void blitFrom(RenderTarget* source, const RTBlitParams& params) override;

    /// Bake in all settings and attachments to Prepare it for rendering
    bool create() override;

    BindingState getAttachmentState(GLenum binding) const;
    void toggleAttachment(const RTAttachment_uptr& attachment, AttachmentState state, bool layeredRendering);

protected:
    void queueCheckStatus() noexcept;
    bool checkStatus();

    void prepareBuffers(const RTDrawDescriptor& drawPolicy);

    bool initAttachment(RTAttachment* att, RTAttachmentType type, U8 index, bool isExternal) override;

    void setAttachmentState(GLenum binding, BindingState state);

    void clear(const RTClearDescriptor& descriptor);
    void setAttachmentUsage(RTAttachmentType type, ImageUsage usage);
    void begin(const RTDrawDescriptor& drawPolicy, const RTClearDescriptor& clearPolicy);
    void end() const;

    PROPERTY_R_IW(Str128, debugMessage, "");
    PROPERTY_R_IW(GLuint, framebufferHandle, GLUtil::k_invalidObjectID);

   protected:
    bool setMipLevelInternal(const RTAttachment_uptr& attachment, U16 writeLevel);

    void queueMipMapRecomputation() const;
    /// Reset layer and mip back to 0 and bind the entire target texture
    void setDefaultAttachmentBinding(const RTAttachment_uptr& attachment);

    static void QueueMipMapsRecomputation(const RTAttachment_uptr& attachment);

   protected:
    RTDrawDescriptor _previousPolicy;
    std::array<GLenum, MAX_RT_COLOUR_ATTACHMENTS> _activeColourBuffers;
    GLenum _activeReadBuffer = GL_NONE;

    eastl::fixed_vector<BindingState, 8 + 2, true, eastl::dvd_allocator> _attachmentState;

    Rect<I32> _prevViewport;

    bool _isLayeredDepth = false;
    bool _statusCheckQueued = false;
    bool _activeDepthBuffer = false;
};

bool operator==(const glFramebuffer::BindingState& lhs, const glFramebuffer::BindingState& rhs) noexcept;
bool operator!=(const glFramebuffer::BindingState& lhs, const glFramebuffer::BindingState& rhs) noexcept;

namespace Attorney {
    class GLAPIRenderTarget {
        static void begin(glFramebuffer& buffer, const RTDrawDescriptor& drawPolicy, const RTClearDescriptor& clearPolicy) {
            buffer.begin(drawPolicy, clearPolicy);
        }
        static void end(const glFramebuffer& buffer) {
            buffer.end();
        }

        friend class GL_API;
    };
};  // namespace Attorney
};  // namespace Divide

#endif //GL_FRAME_BUFFER_H