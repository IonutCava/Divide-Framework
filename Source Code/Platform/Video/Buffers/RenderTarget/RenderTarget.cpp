#include "stdafx.h"

#include "Headers/RenderTarget.h"
#include "Platform/Video/Headers/GFXDevice.h"


namespace Divide {

RenderTarget::RenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor)
    : GraphicsResource(context, Type::RENDER_TARGET, getGUID(), _ID(descriptor._name.c_str())),
     _descriptor(descriptor)
{
    for (U8 i = 0; i < descriptor._attachmentCount; ++i) {
        if (descriptor._attachments[i]._type == RTAttachmentType::Colour) {
            ++_colourAttachmentCount;
        }
    }
    for (U8 i = 0; i < descriptor._externalAttachmentCount; ++i) {
        if (descriptor._externalAttachments[i]._type == RTAttachmentType::Colour) {
            ++_colourAttachmentCount;
        }
    }
    assert(_colourAttachmentCount < RT_MAX_COLOUR_ATTACHMENTS);
}

bool RenderTarget::create() {
    if (_attachmentPool != nullptr) {
        return false;
    }
    _attachmentPool = eastl::make_unique<RTAttachmentPool>(*this, _colourAttachmentCount);

    for (U8 i = 0; i < _descriptor._attachmentCount; ++i) {
        _attachmentPool->update(_descriptor._attachments[i]);
    }
    for (U8 i = 0; i < _descriptor._externalAttachmentCount; ++i) {
        _attachmentPool->update(_descriptor._externalAttachments[i]);
    }

    return true;
}

void RenderTarget::destroy()noexcept {
    _attachmentPool.reset();
}

bool RenderTarget::hasAttachment(const RTAttachmentType type, const U8 index) const {
    return _attachmentPool->exists(type, index);
}

const RTAttachment_ptr& RenderTarget::getAttachmentPtr(const RTAttachmentType type, const U8 index) const {
    return _attachmentPool->get(type, index);
}

const RTAttachment& RenderTarget::getAttachment(const RTAttachmentType type, const U8 index) const {
    return *_attachmentPool->get(type, index);
}

RTAttachment& RenderTarget::getAttachment(const RTAttachmentType type, const U8 index) {
    return *_attachmentPool->get(type, index);
}

U8 RenderTarget::getAttachmentCount(const RTAttachmentType type) const noexcept {
    return _attachmentPool->attachmentCount(type);
}

void RenderTarget::readData(const GFXImageFormat imageFormat, const GFXDataFormat dataType, const bufferPtr outData) const {
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
};
