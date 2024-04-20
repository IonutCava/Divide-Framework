

#include "Headers/RTAttachment.h"

#include "Headers/RenderTarget.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {

RTAttachment::RTAttachment(RenderTarget& parent, const RTAttachmentDescriptor& descriptor) noexcept
    : _descriptor(descriptor)
    , _parent(parent)
{
}

const Texture_ptr& RTAttachment::texture() const
{
    return _resolvedTexture;
}

void RTAttachment::setTexture( const Texture_ptr& renderTexture, const Texture_ptr& resolveTexture ) noexcept
{
    assert( renderTexture != nullptr);

    _renderTexture = renderTexture;
    _resolvedTexture = resolveTexture;
    changed(true);
}

RenderTarget& RTAttachment::parent() noexcept
{
    return _parent;
}

const RenderTarget& RTAttachment::parent() const  noexcept
{
    return _parent;
}

} //namespace Divide
