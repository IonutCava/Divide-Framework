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
#ifndef _RENDER_TARGET_H_
#define _RENDER_TARGET_H_

#include "RTDrawDescriptor.h"
#include "RTAttachment.h"
#include "Platform/Video/Headers/GraphicsResource.h"

namespace Divide {

class RenderTarget;
struct RenderTargetHandle {
    RenderTarget* _rt{ nullptr };
    RenderTargetID _targetID{ INVALID_RENDER_TARGET_ID };
};

struct RenderTargetDescriptor {
    Str64 _name{ "" };
    InternalRTAttachmentDescriptor* _attachments{ nullptr };
    ExternalRTAttachmentDescriptor* _externalAttachments{ nullptr };
    vec2<F32> _depthRange{ 0.f, 1.f };
    vec2<U16>  _resolution{ 1u, 1u };
    F32 _depthValue{ 1.0f };
    U8 _externalAttachmentCount{ 0u };
    U8 _attachmentCount{ 0u };
    U8 _msaaSamples{ 0u };
};

class NOINITVTABLE RenderTarget : public GUIDWrapper, public GraphicsResource {
   public:
    enum class Usage : U8 {
        RT_READ_WRITE = 0,
        RT_READ_ONLY = 1,
        RT_WRITE_ONLY = 2
    };

   protected:
    explicit RenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor);

   public:
    virtual ~RenderTarget() = default;

    /// Init all attachments. Returns false if already called
    [[nodiscard]] virtual bool create();

    [[nodiscard]] bool hasAttachment(RTAttachmentType type, RTColourAttachmentSlot slot = RTColourAttachmentSlot::SLOT_0 ) const;
    [[nodiscard]] bool usesAttachment(RTAttachmentType type, RTColourAttachmentSlot slot = RTColourAttachmentSlot::SLOT_0 ) const;
    [[nodiscard]] RTAttachment* getAttachment(RTAttachmentType type, RTColourAttachmentSlot slot = RTColourAttachmentSlot::SLOT_0 ) const;
    [[nodiscard]] U8 getAttachmentCount(RTAttachmentType type) const noexcept;
    [[nodiscard]] U8 getSampleCount() const noexcept;

    virtual void readData(vec4<U16> rect, GFXImageFormat imageFormat, GFXDataFormat dataType, std::pair<bufferPtr, size_t> outData) const = 0;

    /// Resize all attachments
    bool resize(U16 width, U16 height);
    /// Change msaa sampel count for all attachments
    bool updateSampleCount(U8 newSampleCount);

    void readData(GFXImageFormat imageFormat, GFXDataFormat dataType, std::pair<bufferPtr, size_t> outData) const;

    [[nodiscard]] U16 getWidth()  const noexcept;
    [[nodiscard]] U16 getHeight() const noexcept;
    [[nodiscard]] vec2<U16> getResolution() const noexcept;
    [[nodiscard]] vec2<F32> getDepthRange() const noexcept;
    F32& depthClearValue() noexcept;

    [[nodiscard]] const Str64& name() const noexcept;

    PROPERTY_RW(bool, enableAttachmentChangeValidation, true);

   protected:
    virtual bool initAttachment(RTAttachment* att, RTAttachmentType type, RTColourAttachmentSlot slot, bool isExternal);

   protected:
    RenderTargetDescriptor _descriptor;

    std::array<RTAttachment_uptr, to_base( RTColourAttachmentSlot::COUNT ) + 1> _attachments{};
    std::array<bool, to_base( RTColourAttachmentSlot::COUNT ) + 1> _attachmentsUsed;
};

FWD_DECLARE_MANAGED_CLASS(RenderTarget);

};  // namespace Divide

#endif //_RENDER_TARGET_H_
