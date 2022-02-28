#include "stdafx.h"

#include "Headers/RTAttachmentPool.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Utility/Headers/Localization.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {

namespace {
    const char* getAttachmentName(const RTAttachmentType type) noexcept {
        switch (type) {
            case RTAttachmentType::Colour:  return "Colour";
            case RTAttachmentType::Depth:   return "Depth";
            case RTAttachmentType::Stencil: return "Stencil";
            default: break;
        };

        return "ERROR";
    };
};

RTAttachmentPool::RTAttachmentPool(RenderTarget& parent, const U8 colourAttCount)
    : _attachmentCount{},
      _parent(parent)
{
    _attachmentCount.fill(0);
    _attachment[to_base(RTAttachmentType::Colour)].resize(colourAttCount, nullptr);
    _attachment[to_base(RTAttachmentType::Depth)].resize(1, nullptr);
    _attachment[to_base(RTAttachmentType::Stencil)].resize(1, nullptr);
}

void RTAttachmentPool::copy(const RTAttachmentPool& other) {
    for (U8 i = 0; i < to_base(RTAttachmentType::COUNT); ++i) {
        for (size_t j = 0; j < other._attachment[i].size(); ++j) {
            const RTAttachment_ptr& att = other._attachment[i][j];
            if (att != nullptr) {
                RTAttachmentDescriptor descriptor = {};
                descriptor._clearColour = att->clearColour();
                descriptor._index = to_U8(j);
                descriptor._type = static_cast<RTAttachmentType>(i);
                descriptor._texDescriptor = att->texture()->descriptor();

                update(descriptor);
            }
        }
    }
}

RTAttachment_ptr& RTAttachmentPool::checkAndRemoveExistingAttachment(const RTAttachmentType type, U8 index) {
    assert(index < to_U8(_attachment[to_U32(type)].size()));

    RTAttachment_ptr& ptr = getInternal(_attachment, type, index);
    if (ptr != nullptr) {
        // Replacing existing attachment
        Console::d_printfn(Locale::Get(_ID("WARNING_REPLACING_RT_ATTACHMENT")),
            _parent.getGUID(),
            getAttachmentName(type),
            index);
        // Just to be clear about our intentions
        ptr.reset();
        --_attachmentCount[to_U32(type)];

        _attachmentCache[to_U32(type)].resize(0);
        for (const RTAttachment_ptr& att : _attachment[to_U32(type)]) {
            if (att && att->used()) {
                _attachmentCache[to_U32(type)].push_back(att);
            }
        }
    }
    return ptr;
}

RTAttachment_ptr& RTAttachmentPool::update(const RTAttachmentDescriptor& descriptor) {
    const RTAttachmentType type = descriptor._type;
    RTAttachment_ptr& ptr = checkAndRemoveExistingAttachment(type, descriptor._index);

    ptr.reset(new RTAttachment(*this, descriptor));

    const Str64 texName = Util::StringFormat("FBO_%s_Att_%s_%d_%d", _parent.name().c_str(), getAttachmentName(type), descriptor._index, _parent.getGUID()).c_str();

    ResourceDescriptor textureAttachment(texName);
    textureAttachment.assetName(ResourcePath(texName));
    textureAttachment.waitForReady(true);
    textureAttachment.propertyDescriptor(descriptor._texDescriptor);

    GFXDevice& context = _parent.context();
    ResourceCache* parentCache = context.parent().resourceCache();
    Texture_ptr tex = CreateResource<Texture>(parentCache, textureAttachment);
    assert(tex);

    tex->loadData(nullptr, 0u, vec2<U16>(_parent.getWidth(), _parent.getHeight()));
    ptr->setTexture(tex);

    ++_attachmentCount[to_U32(type)];

    _attachmentCache[to_U32(type)].push_back(ptr);

    return ptr;
}

RTAttachment_ptr& RTAttachmentPool::update(const ExternalRTAttachmentDescriptor& descriptor) {
    RTAttachmentDescriptor internalDescriptor = descriptor._attachment->descriptor();
    internalDescriptor._index = descriptor._index;
    internalDescriptor._type = descriptor._type;
    internalDescriptor._clearColour = descriptor._clearColour;

    const RTAttachmentType type = internalDescriptor._type;
    RTAttachment_ptr& ptr = checkAndRemoveExistingAttachment(type, internalDescriptor._index);

    ptr.reset(new RTAttachment(descriptor._attachment->parent(), internalDescriptor, descriptor._attachment));

     ++_attachmentCount[to_U32(type)];

    _attachmentCache[to_U32(type)].push_back(ptr);

    return ptr;
}

bool RTAttachmentPool::clear(const RTAttachmentType type, const U8 index) {
    RTAttachment_ptr& ptr = getInternal(_attachment, type, index);
    if (ptr != nullptr) {
        ptr.reset();
        return true;
    }

    return false;
}

RTAttachment_ptr& RTAttachmentPool::getInternal(AttachmentPool& pool, const RTAttachmentType type, const U8 index) const {
    switch (type) {
        case RTAttachmentType::Colour:
        {
            assert(index < to_U8(_attachment[to_U32(type)].size()));
            return pool[to_base(RTAttachmentType::Colour)][index];
        }
        case RTAttachmentType::Depth:
        {
            assert(index == 0);
            return pool[to_base(RTAttachmentType::Depth)].front();
        }
        case RTAttachmentType::Stencil:
        {
            assert(index == 0);
            return pool[to_base(RTAttachmentType::Stencil)].front();
        }
        default: break;
    }

    DIVIDE_UNEXPECTED_CALL();
    return pool[0][0];
}

const RTAttachment_ptr& RTAttachmentPool::getInternal(const AttachmentPool& pool, const RTAttachmentType type, const U8 index) const {
    switch (type) {
        case RTAttachmentType::Colour:
        {
            assert(index < to_U8(_attachment[to_U32(type)].size()));
            return pool[to_base(RTAttachmentType::Colour)][index];
        }
        case RTAttachmentType::Depth:
        {
            assert(index == 0);
            return pool[to_base(RTAttachmentType::Depth)].front();
        }
        case RTAttachmentType::Stencil:
        {
            assert(index == 0);
            return pool[to_base(RTAttachmentType::Stencil)].front();
        }
        default: break;
    }

    DIVIDE_UNEXPECTED_CALL();
    return pool[0][0];
}

bool RTAttachmentPool::exists(const RTAttachmentType type, const U8 index) const {
    switch (type) {
        case RTAttachmentType::Depth:
        case RTAttachmentType::Stencil: return index == 0 && _attachmentCount[to_U32(type)] == 1u;
        case RTAttachmentType::Colour:  return index < to_U8(_attachment[to_U32(type)].size());
        default: break;
    }

    return false;
}

bool RTAttachmentPool::uses(const RTAttachmentType type, const U8 index) const {
    return exists(type, index) && getInternal(_attachment, type, index)->used();
}

RTAttachment_ptr& RTAttachmentPool::get(const RTAttachmentType type, const U8 index) {
    return getInternal(_attachment, type, index);
}

const RTAttachment_ptr& RTAttachmentPool::get(const RTAttachmentType type, const U8 index) const {
    return getInternal(_attachment, type, index);
}

const RTAttachmentPool::PoolEntry& RTAttachmentPool::get(const RTAttachmentType type) const {
    return _attachmentCache[to_base(type)];
}

U8 RTAttachmentPool::attachmentCount(const RTAttachmentType type) const noexcept {
    return _attachmentCount[to_U32(type)];
}

RenderTarget& RTAttachmentPool::parent() noexcept {
    return _parent;
}

const RenderTarget& RTAttachmentPool::parent() const noexcept {
    return _parent;
}
}; //namespace Divide
