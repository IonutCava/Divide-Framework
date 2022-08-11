#include "stdafx.h"

#include "Headers/RenderTarget.h"

#include "Core/Headers/Kernel.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/Video/Headers/GFXDevice.h"

#include "Utility/Headers/Localization.h"

namespace Divide {
namespace {
    const char* getAttachmentName(const RTAttachmentType type) noexcept {
        switch (type) {
            case RTAttachmentType::Colour:         return "Colour";
            case RTAttachmentType::Depth_Stencil:  return "Depth_Stencil";
            default: break;
        };

        return "ERROR";
    };
};

RenderTarget::RenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor)
    : GraphicsResource(context, Type::RENDER_TARGET, getGUID(), _ID(descriptor._name.c_str())),
     _descriptor(descriptor)
{
}

bool RenderTarget::create() {
    const auto updateAttachment = [&](const RTAttachmentDescriptor& attDesc) {
        bool printWarning = false;

        RTAttachment* att = nullptr;
        switch (attDesc._type) {
            case RTAttachmentType::Depth_Stencil: {
                assert(attDesc._index == 0u);
                printWarning = _depthAttachment != nullptr;
                _depthAttachment = eastl::make_unique<RTAttachment>(*this, attDesc);
                att = _depthAttachment.get();
            } break;
            case RTAttachmentType::Colour: {
                assert(attDesc._index < RT_MAX_COLOUR_ATTACHMENTS);
                printWarning = _attachments[attDesc._index] != nullptr;
                _attachments[attDesc._index] = eastl::make_unique<RTAttachment>(*this, attDesc);
                att = _attachments[attDesc._index].get();
            } break;
            default: DIVIDE_UNEXPECTED_CALL(); break;
        };

        if (printWarning) {
            Console::d_printfn(Locale::Get(_ID("WARNING_REPLACING_RT_ATTACHMENT")), getGUID(), getAttachmentName(attDesc._type), attDesc._index);
        }

        return att;
    };

    const auto updateInternalAttachment = [&](const InternalRTAttachmentDescriptor& attDesc) {
        RTAttachment* att = updateAttachment(attDesc);

        const Str64 texName = Util::StringFormat("RT_%s_Att_%s_%d_%d",
                                                 name().c_str(),
                                                 getAttachmentName(attDesc._type),
                                                 attDesc._index,
                                                 getGUID()).c_str();

        ResourceDescriptor textureAttachment(texName);
        textureAttachment.assetName(ResourcePath(texName));
        textureAttachment.waitForReady(true);
        textureAttachment.propertyDescriptor(attDesc._texDescriptor);

        ResourceCache* parentCache = context().parent().resourceCache();
        Texture_ptr tex = CreateResource<Texture>(parentCache, textureAttachment);
        assert(tex);

        tex->loadData(nullptr, 0u, vec2<U16>(getWidth(), getHeight()));
        att->setTexture(tex , false);
    };

    const auto updateExternalAttachment = [&](const ExternalRTAttachmentDescriptor& attDesc) {
        RTAttachment* att = updateAttachment(attDesc);
        att->setTexture(attDesc._attachment->texture(), true);
    };

    for (U8 i = 0u; i < _descriptor._attachmentCount; ++i) {
        updateInternalAttachment(_descriptor._attachments[i]);
    }

    for (U8 i = 0u; i < _descriptor._externalAttachmentCount; ++i) {
        updateExternalAttachment(_descriptor._externalAttachments[i]);
    }

    return true;
}

bool RenderTarget::hasAttachment(const RTAttachmentType type, const U8 index) const {
    return getAttachment(type, index) != nullptr;
}

bool RenderTarget::usesAttachment(const RTAttachmentType type, const U8 index) const {
    RTAttachment* const att = getAttachment(type, index);
    return att != nullptr && att->used();
}

RTAttachment* RenderTarget::getAttachment(const RTAttachmentType type, const U8 index) const {
    switch (type) {
        case RTAttachmentType::Depth_Stencil:  DIVIDE_ASSERT(index == 0u); return _depthAttachment.get();
        case RTAttachmentType::Colour: DIVIDE_ASSERT(index < RT_MAX_COLOUR_ATTACHMENTS); return _attachments[index].get();
    }

    DIVIDE_UNEXPECTED_CALL();
    return nullptr;
}

U8 RenderTarget::getAttachmentCount(const RTAttachmentType type) const noexcept {
    switch (type) {
        case RTAttachmentType::Depth_Stencil: return _depthAttachment == nullptr ? 0u : 1;
        case RTAttachmentType::Colour: {
            U8 count = 0u;
            for (const auto& rt : _attachments) {
                if (rt != nullptr) {
                    ++count;
                }
            }
            return count;
        }
    }

    DIVIDE_UNEXPECTED_CALL();
    return 0u;
}

void RenderTarget::readData(const GFXImageFormat imageFormat, const GFXDataFormat dataType, const std::pair<bufferPtr, size_t> outData) const {
    readData(vec4<U16>(0u, 0u, _descriptor._resolution.width, _descriptor._resolution.height), imageFormat, dataType, outData);
}

U16 RenderTarget::getWidth() const noexcept {
    return getResolution().width;
}

U16 RenderTarget::getHeight() const noexcept {
    return getResolution().height;
}

vec2<U16> RenderTarget::getResolution() const noexcept {
    return _descriptor._resolution;
}

vec2<F32> RenderTarget::getDepthRange() const noexcept {
    return _descriptor._depthRange;
}

const Str64& RenderTarget::name() const noexcept {
    return _descriptor._name;
}

F32& RenderTarget::depthClearValue() noexcept {
    return _descriptor._depthValue;
}

bool RenderTarget::resize(const U16 width, const U16 height) {
    if (_descriptor._resolution != vec2<U16>(width, height)) {
        _descriptor._resolution.set(width, height);
        return create();
    }

    return false;
}

bool RenderTarget::updateSampleCount(U8 newSampleCount) {
    CLAMP(newSampleCount, to_U8(0u), _context.gpuState().maxMSAASampleCount());

    if (_descriptor._msaaSamples != newSampleCount) {
        _descriptor._msaaSamples = newSampleCount;
        return create();
    }

    return false;
}

}; //namespace Divide
