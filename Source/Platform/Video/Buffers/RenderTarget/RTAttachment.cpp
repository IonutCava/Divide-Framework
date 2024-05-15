

#include "Headers/RTAttachment.h"

#include "Headers/RenderTarget.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include "Core/Resources/Headers/ResourceCache.h"

namespace Divide {

RTAttachment::RTAttachment(RenderTarget& parent, const RTAttachmentDescriptor& descriptor) noexcept
    : _descriptor(descriptor)
    , _parent(parent)
{
}

RTAttachment::~RTAttachment()
{
    DestroyResource( _resolvedTexture );
    DestroyResource( _renderTexture );
}

Handle<Texture> RTAttachment::texture() const
{
    return _resolvedTexture;
}

void RTAttachment::setTexture( const Handle<Texture> renderTexture, const Handle<Texture> resolveTexture ) noexcept
{
    assert( renderTexture != INVALID_HANDLE<Texture>);

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
