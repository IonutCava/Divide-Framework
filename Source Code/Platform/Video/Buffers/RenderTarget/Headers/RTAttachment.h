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

namespace Divide {

FWD_DECLARE_MANAGED_CLASS(Texture);
FWD_DECLARE_MANAGED_CLASS(RTAttachment);

/// This enum is used when creating render targets to define the channel that the texture will attach to
enum class RTAttachmentType : U8 {
    Colour = 0,
    Depth = 1,
    Stencil = 2,
    COUNT
};

// External attachments get added last and OVERRIDE any normal attachments found at the same type+index location
struct ExternalRTAttachmentDescriptor {
    RTAttachment_ptr _attachment;
    RTAttachmentType _type = RTAttachmentType::COUNT;
    U8 _index = 0;
    FColour4 _clearColour = DefaultColours::WHITE;
};

struct RTAttachmentDescriptor {
    TextureDescriptor _texDescriptor;
    size_t _samplerHash = 0;
    RTAttachmentType _type = RTAttachmentType::COUNT;
    U8 _index = 0;
    FColour4 _clearColour = DefaultColours::WHITE;
};

using RTAttachmentDescriptors = vector<RTAttachmentDescriptor>;

class RTAttachmentPool;
class RTAttachment final {
    public:
        explicit RTAttachment(RTAttachmentPool& parent, const RTAttachmentDescriptor& descriptor) noexcept;
        explicit RTAttachment(RTAttachmentPool& parent, const RTAttachmentDescriptor& descriptor, RTAttachment_ptr externalAtt) noexcept;

        [[nodiscard]] bool used() const noexcept;
        
        [[nodiscard]] bool isExternal() const noexcept;

        [[nodiscard]] bool changed() const noexcept;
        void clearChanged() noexcept;

        void clearColour(const FColour4& clearColour) noexcept;
        [[nodiscard]] const FColour4& clearColour() const noexcept;

        bool mipWriteLevel(U16 level) noexcept;
        [[nodiscard]] U16  mipWriteLevel() const noexcept;

        bool writeLayer(U16 layer);
        [[nodiscard]] U16  writeLayer() const noexcept;

        [[nodiscard]] const Texture_ptr& texture(bool autoResolve = true) const;
        void setTexture(const Texture_ptr& tex) noexcept;

        [[nodiscard]] U16 numLayers() const;

        [[nodiscard]] const RTAttachmentDescriptor& descriptor() const noexcept;

        RTAttachmentPool& parent() noexcept;
        [[nodiscard]] const RTAttachmentPool& parent() const noexcept;

        [[nodiscard]] const RTAttachment_ptr& getExternal() const noexcept;

        PROPERTY_RW(size_t, samplerHash, 0);
        PROPERTY_RW(U32, binding, 0u);
    protected:
        RTAttachmentDescriptor _descriptor;
        Texture_ptr _texture = nullptr;
        RTAttachment_ptr _externalAttachment = nullptr;
        RTAttachmentPool& _parent;
        U16  _mipWriteLevel = 0u;
        U16  _writeLayer = 0u;
        bool _changed = false;
};

FWD_DECLARE_MANAGED_CLASS(RTAttachment);

}; //namespace Divide

#endif //_RENDER_TARGET_ATTACHMENT_H_
