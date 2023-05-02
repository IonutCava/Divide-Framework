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

namespace CEGUI
{
    class DVDTextureTarget;
}

namespace Divide {

class GL_API;

namespace Attorney {
    class GLAPIRenderTarget;
    class CEGUIRenderTarget;
};

class glFramebuffer final : public RenderTarget {

    friend class Attorney::GLAPIRenderTarget;
    friend class Attorney::CEGUIRenderTarget;

  public:
    enum class AttachmentState : U8
    {
        STATE_DISABLED = 0,
        STATE_ENABLED,
        COUNT
    };

    struct BindingState
    {
        DrawLayerEntry _layer{};
        U16 _levelOffset{0u};
        bool _layeredRendering{false};
        AttachmentState _attState{ AttachmentState::COUNT };
    };

   public:
    explicit glFramebuffer(GFXDevice& context, const RenderTargetDescriptor& descriptor);
    ~glFramebuffer();

    void setMipLevel(U16 writeLevel);

    void blitFrom(RenderTarget* source, const RTBlitParams& params);

    /// Bake in all settings and attachments to Prepare it for rendering
    bool create() override;


  protected:
    bool checkStatus();
    bool checkStatusInternal(GLuint handle);

    void prepareBuffers(const RTDrawDescriptor& drawPolicy);

    bool initAttachment(RTAttachment* att, RTAttachmentType type, RTColourAttachmentSlot slot) override;

    bool toggleAttachment( U8 attachmentIdx, AttachmentState state, U16 levelOffset, DrawLayerEntry layerOffset, bool layeredRendering);

    void clear(const RTClearDescriptor& descriptor);
    void begin(const RTDrawDescriptor& drawPolicy, const RTClearDescriptor& clearPolicy);
    void end(const RTTransitionMask& mask);

    PROPERTY_R_IW(Str128, debugMessage, "");
    PROPERTY_R_IW(GLuint, framebufferHandle, GL_NULL_HANDLE);

   protected:
    void resolve(const RTTransitionMask& mask);
    bool setMipLevelInternal( U8 attachmentIdx, U16 writeLevel);
    static void QueueMipMapsRecomputation(const RTAttachment_uptr& attachment);

   protected:
    GLuint _framebufferResolveHandle{GL_NULL_HANDLE};

    RTDrawDescriptor _previousPolicy;
    std::array<DrawLayerEntry, RT_MAX_ATTACHMENT_COUNT> _previousDrawLayers;

    struct ColourBufferState
    {
        std::array<GLenum, to_base( RTColourAttachmentSlot::COUNT )> _glSlot = create_array<to_base( RTColourAttachmentSlot::COUNT ), GLenum>(GL_NONE);
        bool _dirty{ true };
    } _colourBuffers;
    
    GLenum _activeReadBuffer = GL_NONE;

    eastl::fixed_vector<BindingState, 8 + 2, true, eastl::dvd_allocator> _attachmentState;

    bool _isLayeredDepth = false;
    bool _statusCheckQueued = false;
    bool _activeDepthBuffer = false;
};

bool operator==(const glFramebuffer::BindingState& lhs, const glFramebuffer::BindingState& rhs) noexcept;
bool operator!=(const glFramebuffer::BindingState& lhs, const glFramebuffer::BindingState& rhs) noexcept;

namespace Attorney {
    class GLAPIRenderTarget {
        static void begin(glFramebuffer& rt, const RTDrawDescriptor& drawPolicy, const RTClearDescriptor& clearPolicy) {
            rt.begin(drawPolicy, clearPolicy);
        }
        static void end( glFramebuffer& rt, const RTTransitionMask& mask ) {
            rt.end( mask );
        }

        friend class GL_API;
    };
};  // namespace Attorney

namespace Attorney
{
    class CEGUIRenderTarget
    {
        static void clear( glFramebuffer& rt, const RTClearDescriptor& clearPolicy )
        {
            rt.clear(clearPolicy);
        }

        friend class CEGUI::DVDTextureTarget;
    };
};  // namespace Attorney
};  // namespace Divide

#endif //GL_FRAME_BUFFER_H