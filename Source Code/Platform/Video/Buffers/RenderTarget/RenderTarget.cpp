#include "stdafx.h"

#include "Headers/RenderTarget.h"

#include "Core/Headers/Kernel.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include "Utility/Headers/Localization.h"

namespace Divide {
namespace {
    const char* getAttachmentName(const RTAttachmentType type) noexcept 
    {
        switch (type)
        {
            case RTAttachmentType::COLOUR:         return "Colour";
            case RTAttachmentType::DEPTH:          return "Depth";
            case RTAttachmentType::DEPTH_STENCIL:  return "Depth_Stencil";
            default: break;
        };

        return "ERROR";
    };
};

RenderTarget::RenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor)
    : GraphicsResource(context, Type::RENDER_TARGET, getGUID(), _ID(descriptor._name.c_str())),
     _descriptor(descriptor)
{
    if ( _descriptor._name.empty() )
    {
        _descriptor._name = Util::StringFormat( "DVD_FB_%d", getGUID() );
    }
}

bool RenderTarget::autoResolveAttachment( RTAttachment* att ) const
{
    if (att == nullptr || _descriptor._msaaSamples == 0u )
    {
        return false;
    }

    return att->_descriptor._autoResolve;
}

bool RenderTarget::create()
{
    _attachmentsUsed.fill(false);
    _attachmentsAutoResolve.fill(false);

    // Avoid invalid dimensions
    assert(getWidth() != 0 && getHeight() != 0 && "glFramebuffer error: Invalid frame buffer dimensions!");

    const auto updateAttachment = [&](const RTAttachmentDescriptor& attDesc)
    {
        bool printWarning = false;

        RTAttachment* att = nullptr;
        switch (attDesc._type)
        {
            case RTAttachmentType::DEPTH:
            case RTAttachmentType::DEPTH_STENCIL:
            {
                assert(attDesc._slot == RTColourAttachmentSlot::SLOT_0 );
                printWarning = _attachments[RT_DEPTH_ATTACHMENT_IDX] != nullptr;
                _attachments[RT_DEPTH_ATTACHMENT_IDX] = eastl::make_unique<RTAttachment>(*this, attDesc);
                att = _attachments[RT_DEPTH_ATTACHMENT_IDX].get();
            } break;
            case RTAttachmentType::COLOUR:
            {
                printWarning = _attachments[to_base(attDesc._slot)] != nullptr;
                _attachments[to_base(attDesc._slot)] = eastl::make_unique<RTAttachment>(*this, attDesc);
                att = _attachments[to_base(attDesc._slot)].get();
            } break;
            default: DIVIDE_UNEXPECTED_CALL(); break;
        };

        if (printWarning)
        {
            Console::d_printfn(Locale::Get(_ID("WARNING_REPLACING_RT_ATTACHMENT")), getGUID(), getAttachmentName(attDesc._type), to_base(attDesc._slot));
        }

        return att;
    };

    for (InternalRTAttachmentDescriptor& attDesc : _descriptor._attachments)
    {
        RTAttachment* att = updateAttachment(attDesc);

        const Str64 texName = Util::StringFormat("RT_%s_Att_%s_%d_%d",
                                                 name().c_str(),
                                                 getAttachmentName(attDesc._type),
                                                 to_base(attDesc._slot),
                                                 getGUID()).c_str();
        
        attDesc._texDescriptor.addImageUsageFlag(attDesc._type == RTAttachmentType::COLOUR 
                                                                ? ImageUsage::RT_COLOUR_ATTACHMENT
                                                                : attDesc._type == RTAttachmentType::DEPTH_STENCIL
                                                                                 ? ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT
                                                                                 : ImageUsage::RT_DEPTH_ATTACHMENT);

        const bool needsMSAAResolve = autoResolveAttachment(att);

        Texture_ptr renderTexture = nullptr;
        Texture_ptr resolveTexture = nullptr;
        const TextureDescriptor::MipMappingState mipMapState = attDesc._texDescriptor.mipMappingState();
        {
            if ( needsMSAAResolve )
            {
                attDesc._texDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);
                attDesc._texDescriptor.removeImageUsageFlag( ImageUsage::SHADER_READ );
                attDesc._texDescriptor.msaaSamples( _descriptor._msaaSamples );
            }

            ResourceDescriptor textureAttachment(texName + "_RENDER");
            textureAttachment.waitForReady(true);
            textureAttachment.propertyDescriptor(attDesc._texDescriptor);

            ResourceCache* parentCache = context().context().kernel().resourceCache();
            renderTexture = CreateResource<Texture>(parentCache, textureAttachment);
            assert( renderTexture );

            renderTexture->createWithData(nullptr, 0u, vec2<U16>(getWidth(), getHeight()), {});
        }

        if ( needsMSAAResolve )
        {
            attDesc._texDescriptor.mipMappingState(mipMapState);
            attDesc._texDescriptor.addImageUsageFlag( ImageUsage::SHADER_READ );
            attDesc._texDescriptor.msaaSamples(0u);

            ResourceDescriptor textureAttachment( texName + "_RESOLVE" );
            textureAttachment.waitForReady( true );
            textureAttachment.propertyDescriptor( attDesc._texDescriptor );

            ResourceCache* parentCache = context().context().kernel().resourceCache();
            resolveTexture = CreateResource<Texture>( parentCache, textureAttachment );
            assert( resolveTexture );

            resolveTexture->createWithData( nullptr, 0u, vec2<U16>( getWidth(), getHeight() ), {} );
        }
        else
        {
            resolveTexture = renderTexture;
        }

        att->setTexture(renderTexture, resolveTexture);

        if ( !initAttachment( att, attDesc._type, attDesc._slot ) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }
    }

    for ( const ExternalRTAttachmentDescriptor& attDesc : _descriptor._externalAttachments )
    {
        RTAttachment* att = updateAttachment(attDesc);
        att->setTexture(attDesc._externalAttachment->renderTexture(), attDesc._externalAttachment->resolvedTexture());
        DIVIDE_ASSERT(attDesc._externalAttachment->renderTexture()->descriptor().msaaSamples() == _descriptor._msaaSamples);

        if ( !initAttachment( att, attDesc._type, attDesc._slot ) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }
    }

    return true;
}

bool RenderTarget::hasAttachment(const RTAttachmentType type, const RTColourAttachmentSlot slot) const
{
    return getAttachment(type, slot) != nullptr;
}

bool RenderTarget::usesAttachment(const RTAttachmentType type, const RTColourAttachmentSlot slot ) const
{
    return _attachmentsUsed[type == RTAttachmentType::COLOUR ? to_base(slot) : RT_DEPTH_ATTACHMENT_IDX];
}

RTAttachment* RenderTarget::getAttachment(const RTAttachmentType type, const RTColourAttachmentSlot slot ) const
{
    switch (type)
    {
        case RTAttachmentType::DEPTH:
        case RTAttachmentType::DEPTH_STENCIL:  DIVIDE_ASSERT(slot == RTColourAttachmentSlot::SLOT_0); return _attachments[RT_DEPTH_ATTACHMENT_IDX].get();
        case RTAttachmentType::COLOUR:         return _attachments[to_base(slot)].get();
    }

    DIVIDE_UNEXPECTED_CALL();
    return nullptr;
}

U8 RenderTarget::getAttachmentCount(const RTAttachmentType type) const noexcept
{
    switch (type)
    {
        case RTAttachmentType::DEPTH:
        case RTAttachmentType::DEPTH_STENCIL: return _attachments[RT_DEPTH_ATTACHMENT_IDX] == nullptr ? 0u : 1;
        case RTAttachmentType::COLOUR:
        {
            U8 count = 0u;
            for (const auto& rt : _attachments)
            {
                if (rt != nullptr)
                {
                    ++count;
                }
            }

            return count;
        }
    }

    DIVIDE_UNEXPECTED_CALL();
    return 0u;
}

U16 RenderTarget::getWidth() const noexcept
{
    return getResolution().width;
}

U16 RenderTarget::getHeight() const noexcept
{
    return getResolution().height;
}

vec2<U16> RenderTarget::getResolution() const noexcept
{
    return _descriptor._resolution;
}

const Str64& RenderTarget::name() const noexcept
{
    return _descriptor._name;
}

F32& RenderTarget::depthClearValue() noexcept
{
    return _descriptor._depthValue;
}

bool RenderTarget::resize(const U16 width, const U16 height)
{
    if (_descriptor._resolution != vec2<U16>(width, height))
    {
        _descriptor._resolution.set(width, height);
        return create();
    }

    return false;
}

U8 RenderTarget::getSampleCount() const noexcept
{
    return _descriptor._msaaSamples;
}

bool RenderTarget::updateSampleCount(U8 newSampleCount)
{
    CLAMP( newSampleCount, to_U8(0u), DisplayManager::MaxMSAASamples() );

    if (_descriptor._msaaSamples != newSampleCount)
    {
        _descriptor._msaaSamples = newSampleCount;
        return create();
    }

    return false;
}

bool RenderTarget::initAttachment(RTAttachment* att, const RTAttachmentType type, const RTColourAttachmentSlot slot)
{
    if (att->_descriptor._externalAttachment != nullptr)
    {
        att->setTexture( att->_descriptor._externalAttachment->renderTexture(), att->_descriptor._externalAttachment->resolvedTexture());
    }
    else
    {
        // Do we need to resize the attachment?
        const bool shouldResize = att->texture()->width() != getWidth() || att->texture()->height() != getHeight();
        if (shouldResize)
        {
            att->texture()->createWithData(nullptr, 0u, vec2<U16>(getWidth(), getHeight()), {});
        }

        const bool updateSampleCount = att->renderTexture()->descriptor().msaaSamples() != _descriptor._msaaSamples;
        if (updateSampleCount)
        {
            att->renderTexture()->setSampleCount(_descriptor._msaaSamples);
        }
    }

    att->changed(false);
    _attachmentsUsed[type == RTAttachmentType::COLOUR ? to_base(slot) : RT_DEPTH_ATTACHMENT_IDX] = true;
    _attachmentsAutoResolve[type == RTAttachmentType::COLOUR ? to_base(slot) : RT_DEPTH_ATTACHMENT_IDX] = autoResolveAttachment( att );

    return true;
}

}; //namespace Divide
