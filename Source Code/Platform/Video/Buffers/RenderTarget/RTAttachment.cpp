#include "stdafx.h"

#include "Headers/RTAttachment.h"

#include "Headers/RenderTarget.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {

RTAttachment::RTAttachment(RenderTarget& parent, const RTAttachmentDescriptor& descriptor) noexcept
    : _parent(parent),
      _descriptor(descriptor)
{
}

const Texture_ptr& RTAttachment::texture() const {
    return _texture;
}

void RTAttachment::setTexture(const Texture_ptr& tex, const bool isExternal) noexcept {
    assert(tex != nullptr);

    _texture = tex;
    changed(true);
}

bool RTAttachment::setImageUsage(const ImageUsage usage) {
    if (_texture != nullptr) {
        return _texture->imageUsage(usage);
    }

    return false;
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
    return _texture != nullptr ? _texture->descriptor().layerCount() : 0u;
}

const RTAttachmentDescriptor& RTAttachment::descriptor() const noexcept {
    return _descriptor;
}

RenderTarget& RTAttachment::parent() noexcept {
    return _parent;
}

const RenderTarget& RTAttachment::parent() const  noexcept {
    return _parent;
}

}; //namespace Divide