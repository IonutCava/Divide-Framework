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
#ifndef _RENDER_TARGET_ATTACHMENT_H_
#define _RENDER_TARGET_ATTACHMENT_H_

#include "Platform/Video/Textures/Headers/TextureDescriptor.h"
#include "Utility/Headers/Colours.h"

namespace Divide {

FWD_DECLARE_MANAGED_CLASS(Texture);
FWD_DECLARE_MANAGED_CLASS(RTAttachment);


/// This enum is used when creating render targets to define the channel that the texture will attach to
enum class RTAttachmentType : U8 {
    COLOUR = 0,
    DEPTH,
    DEPTH_STENCIL,
    COUNT
};

struct RTAttachmentDescriptor {
    explicit RTAttachmentDescriptor(const size_t samplerHash, const RTAttachmentType type, const RTColourAttachmentSlot slot) noexcept
        : _samplerHash(samplerHash)
        , _type(type)
        , _slot(slot)
    {
    }

    size_t _samplerHash{ 0u };
    RTAttachmentType _type{ RTAttachmentType::COUNT };
    RTColourAttachmentSlot _slot{ RTColourAttachmentSlot::COUNT };
};

constexpr static U32 RT_DEPTH_ATTACHMENT_IDX = to_base( RTColourAttachmentSlot::COUNT );
constexpr static U8 RT_MAX_ATTACHMENT_COUNT = to_base( RTColourAttachmentSlot::COUNT ) + 1;

// External attachments get added last and OVERRIDE any normal attachments found at the same type+index location
struct ExternalRTAttachmentDescriptor final : public RTAttachmentDescriptor {
    explicit ExternalRTAttachmentDescriptor(RTAttachment* attachment,
                                            const size_t samplerHash,
                                            const RTAttachmentType type,
                                            const RTColourAttachmentSlot slot)
        : RTAttachmentDescriptor(samplerHash, type, slot)
        , _attachment(attachment)
    {
    }

    RTAttachment* _attachment{nullptr};
};

struct InternalRTAttachmentDescriptor final : public RTAttachmentDescriptor {
    explicit InternalRTAttachmentDescriptor(TextureDescriptor& descriptor,
                                            const size_t samplerHash,
                                            const RTAttachmentType type,
                                            const RTColourAttachmentSlot slot)
        : RTAttachmentDescriptor(samplerHash, type, slot)
        , _texDescriptor(descriptor)
    {
    }

    TextureDescriptor _texDescriptor;
};

using InternalRTAttachmentDescriptors = vector<InternalRTAttachmentDescriptor>;
using ExternalRTAttachmentDescriptors = vector<ExternalRTAttachmentDescriptor>;

class RenderTarget;
class RTAttachment final {
    public:
        explicit RTAttachment(RenderTarget& parent, const RTAttachmentDescriptor& descriptor) noexcept;

        [[nodiscard]] const Texture_ptr& texture() const;
        void setTexture(const Texture_ptr& tex, const bool isExternal) noexcept;

        [[nodiscard]] U16 numMips() const noexcept;
        [[nodiscard]] U16 numLayers() const noexcept;

        [[nodiscard]] const RTAttachmentDescriptor& descriptor() const noexcept;

        RenderTarget& parent() noexcept;
        [[nodiscard]] const RenderTarget& parent() const noexcept;

        PROPERTY_RW(U32, binding, 0u);
        PROPERTY_RW(bool, changed, false);
        PROPERTY_R_IW(bool, isExternal, false);

    protected:
        RenderTarget& _parent;
        RTAttachmentDescriptor _descriptor;
        Texture_ptr _texture = nullptr;
};

FWD_DECLARE_MANAGED_CLASS(RTAttachment);

}; //namespace Divide

#endif //_RENDER_TARGET_ATTACHMENT_H_
