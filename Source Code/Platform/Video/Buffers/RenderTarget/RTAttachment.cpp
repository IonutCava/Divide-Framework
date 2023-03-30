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