

#include "Headers/RenderTarget.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/DisplayManager.h"
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
        Util::StringFormatTo( _descriptor._name, "DVD_FB_{}", getGUID() );
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
    std::ranges::fill(_attachmentsUsed, false);
    std::ranges::fill(_attachmentsAutoResolve, false);

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
                _attachments[RT_DEPTH_ATTACHMENT_IDX] = std::make_unique<RTAttachment>(*this, attDesc);
                att = _attachments[RT_DEPTH_ATTACHMENT_IDX].get();
            } break;
            case RTAttachmentType::COLOUR:
            {
                printWarning = _attachments[to_base(attDesc._slot)] != nullptr;
                _attachments[to_base(attDesc._slot)] = std::make_unique<RTAttachment>(*this, attDesc);
                att = _attachments[to_base(attDesc._slot)].get();
            } break;
            default: DIVIDE_UNEXPECTED_CALL(); break;
        };

        if (printWarning)
        {
            Console::d_printfn(LOCALE_STR("WARNING_REPLACING_RT_ATTACHMENT"), getGUID(), getAttachmentName(attDesc._type), to_base(attDesc._slot));
        }

        return att;
    };

    for (InternalRTAttachmentDescriptor& attDesc : _descriptor._attachments)
    {
        RTAttachment* att = updateAttachment(attDesc);

        const string texName = Util::StringFormat("RT_{}_Att_{}_{}_{}",
                                                  name().c_str(),
                                                  getAttachmentName(attDesc._type),
                                                  to_base(attDesc._slot),
                                                  getGUID());
        AddImageUsageFlag( attDesc._texDescriptor, attDesc._type == RTAttachmentType::COLOUR
                                                                ? ImageUsage::RT_COLOUR_ATTACHMENT
                                                                : attDesc._type == RTAttachmentType::DEPTH_STENCIL
                                                                                 ? ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT
                                                                                 : ImageUsage::RT_DEPTH_ATTACHMENT);

        const bool needsMSAAResolve = autoResolveAttachment(att);

        Handle<Texture> renderTexture = INVALID_HANDLE<Texture>;
        Handle<Texture> resolveTexture = INVALID_HANDLE<Texture>;
        const MipMappingState mipMapState = attDesc._texDescriptor._mipMappingState;
        {
            if ( needsMSAAResolve )
            {
                RemoveImageUsageFlag(attDesc._texDescriptor, ImageUsage::SHADER_READ );
                attDesc._texDescriptor._mipMappingState = MipMappingState::OFF;
                attDesc._texDescriptor._msaaSamples = _descriptor._msaaSamples;
            }

            ResourceDescriptor<Texture> textureAttachment(texName + "_RENDER", attDesc._texDescriptor );
            textureAttachment.waitForReady(true);

            renderTexture = CreateResource(textureAttachment);

            Get(renderTexture)->createWithData(nullptr, 0u, vec2<U16>(getWidth(), getHeight()), {});
        }

        if ( needsMSAAResolve )
        {
            AddImageUsageFlag( attDesc._texDescriptor, ImageUsage::SHADER_READ );
            attDesc._texDescriptor._mipMappingState = mipMapState;
            attDesc._texDescriptor._msaaSamples = 0u;

            ResourceDescriptor<Texture> textureAttachment( texName + "_RESOLVE", attDesc._texDescriptor );
            textureAttachment.waitForReady( true );

            resolveTexture = CreateResource( textureAttachment );

            Get(resolveTexture)->createWithData( nullptr, 0u, vec2<U16>( getWidth(), getHeight() ), {} );
        }
        else
        {
            resolveTexture = GetResourceRef(renderTexture);
        }

        att->setTexture(renderTexture, resolveTexture);

        DIVIDE_EXPECTED_CALL( initAttachment( att, attDesc._type, attDesc._slot ) );
    }

    for ( const ExternalRTAttachmentDescriptor& attDesc : _descriptor._externalAttachments )
    {
        Handle<Texture> renderTexture  = GetResourceRef(attDesc._externalAttachment->renderTexture());
        Handle<Texture> resolveTexture = GetResourceRef(attDesc._externalAttachment->resolvedTexture());

        DIVIDE_ASSERT(Get( renderTexture )->descriptor()._msaaSamples == _descriptor._msaaSamples);

        RTAttachment* att = updateAttachment(attDesc);
        att->setTexture( renderTexture, resolveTexture );
        DIVIDE_EXPECTED_CALL( initAttachment( att, attDesc._type, attDesc._slot ) );
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

        default:
        case RTAttachmentType::COUNT: break;
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
        default:
        case RTAttachmentType::COUNT: break;
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

const Str<64>& RenderTarget::name() const noexcept
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
    CLAMP( newSampleCount, U8_ZERO, DisplayManager::MaxMSAASamples() );

    if (_descriptor._msaaSamples != newSampleCount)
    {
        _descriptor._msaaSamples = newSampleCount;
        return create();
    }

    return false;
}

bool RenderTarget::initAttachment(RTAttachment* att, const RTAttachmentType type, const RTColourAttachmentSlot slot)
{
    if (att->_descriptor._externalAttachment == nullptr)
    {
        ResourcePtr<Texture> attTex = Get(att->texture());

        // Do we need to resize the attachment?
        const bool shouldResize = attTex->width() != getWidth() || attTex->height() != getHeight();
        if (shouldResize)
        {
            attTex->createWithData(nullptr, 0u, vec2<U16>(getWidth(), getHeight()), {});
        }

        ResourcePtr<Texture> attRenderTex = Get( att->renderTexture() );
        const bool updateSampleCount = attRenderTex->descriptor()._msaaSamples != _descriptor._msaaSamples;
        if (updateSampleCount)
        {
            attRenderTex->setSampleCount(_descriptor._msaaSamples);
        }
    }

    att->changed(false);
    _attachmentsUsed[type == RTAttachmentType::COLOUR ? to_base(slot) : RT_DEPTH_ATTACHMENT_IDX] = true;
    _attachmentsAutoResolve[type == RTAttachmentType::COLOUR ? to_base(slot) : RT_DEPTH_ATTACHMENT_IDX] = autoResolveAttachment( att );

    return true;
}

}; //namespace Divide
