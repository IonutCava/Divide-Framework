#include "stdafx.h"

#include "Headers/vkShaderBuffer.h"

namespace Divide {
    vkShaderBuffer::vkShaderBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor)
        : ShaderBuffer(context, descriptor)
    {}

    BufferLock vkShaderBuffer::clearBytes([[maybe_unused]] BufferRange range) noexcept {
        return {};
    }

    BufferLock vkShaderBuffer::writeBytes([[maybe_unused]] BufferRange range, [[maybe_unused]] bufferPtr data) noexcept {
        return {};
    }

    void vkShaderBuffer::readBytes([[maybe_unused]] BufferRange range, [[maybe_unused]] bufferPtr result) const noexcept {
    }

    bool vkShaderBuffer::bindByteRange([[maybe_unused]] U8 bindIndex, [[maybe_unused]] BufferRange range) noexcept {
        return true;
    }

    bool vkShaderBuffer::lockByteRange([[maybe_unused]] BufferRange range, [[maybe_unused]] SyncObject* sync) const {
        return true;
    }
}; //namespace Divide
