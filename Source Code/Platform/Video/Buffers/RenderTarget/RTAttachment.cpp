#include "stdafx.h"

#include "Headers/RTAttachment.h"

#include "Headers/RenderTarget.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {

RTAttachment::RTAttachment(RTAttachmentPool& parent, const RTAttachmentDescriptor& descriptor) noexcept
    : RTAttachment(parent, descriptor, nullptr)
{
}

RTAttachment::RTAttachment(RTAttachmentPool& parent, const RTAttachmentDescriptor& descriptor, RTAttachment_ptr externalAtt) noexcept
    : _samplerHash(descriptor._samplerHash),
      _descriptor(descriptor),
      _externalAttachment(MOV(externalAtt)),
      _parent(parent)

{
}

const Texture_ptr& RTAttachment::texture(const bool autoResolve) const {
    return autoResolve && isExternal() ? _externalAttachment->texture() : _texture;
}

void RTAttachment::setTexture(const Texture_ptr& tex) noexcept {
    _texture = tex;
    if (tex != nullptr) {
        _descriptor._texDescriptor = tex->descriptor();
        assert(IsValid(tex->data()));
    }
    _changed = true;
}

bool RTAttachment::used() const noexcept {
    return _texture != nullptr ||
           _externalAttachment != nullptr;
}

bool RTAttachment::isExternal() const noexcept {
    return _externalAttachment != nullptr;
}

bool RTAttachment::mipWriteLevel(const U16 level) noexcept {
    if (_texture != nullptr && _texture->mipCount() > level && _mipWriteLevel != level) {
        _mipWriteLevel = level;
        return true;
    }

    return false;
}

U16 RTAttachment::mipWriteLevel() const noexcept {
    return _mipWriteLevel;
}

bool RTAttachment::writeLayer(const U16 layer) {
    const U16 layerCount = IsCubeTexture(texture()->descriptor().texType()) ? numLayers() * 6 : numLayers();
    if (layerCount > layer && _writeLayer != layer) {
        _writeLayer = layer;
        return true;
    }

    return false;
}

U16 RTAttachment::writeLayer() const noexcept {
    return _writeLayer;
}

U16 RTAttachment::numLayers() const {
    return to_U16(_descriptor._texDescriptor.layerCount());
}
bool RTAttachment::changed() const noexcept {
    return _changed;
}

void RTAttachment::clearColour(const FColour4& clearColour) noexcept {
    _descriptor._clearColour.set(clearColour);
}

const FColour4& RTAttachment::clearColour() const noexcept {
    return _descriptor._clearColour;
}

void RTAttachment::clearChanged() noexcept {
    _changed = false;
}

const RTAttachmentDescriptor& RTAttachment::descriptor() const noexcept {
    return _descriptor;
}

RTAttachmentPool& RTAttachment::parent() noexcept {
    return _parent;
}
const RTAttachmentPool& RTAttachment::parent() const  noexcept {
    return _parent;
}

const RTAttachment_ptr& RTAttachment::getExternal() const noexcept {
    return _externalAttachment;
}

}; //namespace Divide