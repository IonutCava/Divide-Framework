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
#ifndef _RENDER_TARGET_ATTACHMENT_POOL_H_
#define _RENDER_TARGET_ATTACHMENT_POOL_H_

#include "RTAttachment.h"

namespace Divide {

class RenderTarget;
class RTAttachmentPool {
public:
    using PoolEntry = vector_fast<RTAttachment_ptr>;
    using AttachmentPool = std::array<PoolEntry, to_base(RTAttachmentType::COUNT)>;

public:
    explicit RTAttachmentPool(RenderTarget& parent, U8 colourAttCount);

    void copy(const RTAttachmentPool& other);

    RTAttachment_ptr& update(const RTAttachmentDescriptor& descriptor);
    RTAttachment_ptr& update(const ExternalRTAttachmentDescriptor& descriptor);

    // Return true if the attachment was used. False if the call had no effect
    bool clear(RTAttachmentType type, U8 index);

    [[nodiscard]] bool exists(RTAttachmentType type, U8 index) const;
    RTAttachment_ptr& get(RTAttachmentType type, U8 index);
    [[nodiscard]] const RTAttachment_ptr& get(RTAttachmentType type, U8 index) const;

    [[nodiscard]] const PoolEntry& get(RTAttachmentType type) const;

    [[nodiscard]] U8 attachmentCount(RTAttachmentType type) const noexcept;

    RenderTarget& parent();
    [[nodiscard]] const RenderTarget& parent() const;

private:
    RTAttachment_ptr& getInternal(AttachmentPool& pool, RTAttachmentType type, U8 index) const;
    [[nodiscard]] const RTAttachment_ptr& getInternal(const AttachmentPool& pool, RTAttachmentType type, U8 index) const;
    RTAttachment_ptr& checkAndRemoveExistingAttachment(RTAttachmentType type, U8 index);

private:
    AttachmentPool _attachment;
    std::array<U8, to_base(RTAttachmentType::COUNT)> _attachmentCount;

    AttachmentPool _attachmentCache;
    RenderTarget& _parent;
};
}; //namespace Divide

#endif //_RENDER_TARGET_ATTACHMENT_POOL_H_