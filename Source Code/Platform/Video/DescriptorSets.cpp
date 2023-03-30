#include "stdafx.h"

#include "Headers/DescriptorSets.h"

#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {
    size_t ImageView::Descriptor::getHash() const noexcept {
        _hash = 1337;
        Util::Hash_combine(_hash, _msaaSamples, _dataType, _baseFormat, _srgb, _normalized);
        return _hash;
    }
    
    TextureType ImageView::targetType() const noexcept {
        if (_targetType != TextureType::COUNT) {
            return _targetType;
        }

        if (_srcTexture != nullptr) {
            return _srcTexture->descriptor().texType();
        }

        return _targetType;
    }

    void ImageView::targetType(const TextureType type) noexcept {
        _targetType = type;
    }

    size_t ImageView::getHash() const noexcept {
        _hash = 1337;
        Util::Hash_combine(_hash,
                           _descriptor.getHash(),
                           _srcTexture != nullptr ? _srcTexture->getGUID() : 0,
                           targetType(),
                           _subRange._layerRange.offset,
                           _subRange._layerRange.count,
                           _subRange._mipLevels.offset,
                           _subRange._mipLevels.count);

        return _hash;
    }

    bool operator==(const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs) noexcept {
        return lhs._bufferQueueReadIndex == rhs._bufferQueueReadIndex &&
               lhs._range == rhs._range &&
               Compare(lhs._buffer, rhs._buffer);
    }

    bool operator!=(const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs) noexcept {
        return lhs._bufferQueueReadIndex != rhs._bufferQueueReadIndex ||
               lhs._range != rhs._range ||
               !Compare(lhs._buffer, rhs._buffer);
    }

    DescriptorSetBindingType Type( const DescriptorSetBindingData& data ) noexcept
    {
        switch ( data.index())
        {
            case 1:
            {
                switch ( As<ShaderBufferEntry>( data )._buffer->getUsage() )
                {
                    case BufferUsageType::COMMAND_BUFFER:
                    case BufferUsageType::UNBOUND_BUFFER: return DescriptorSetBindingType::SHADER_STORAGE_BUFFER;
                    case BufferUsageType::CONSTANT_BUFFER: return DescriptorSetBindingType::UNIFORM_BUFFER;
                    default: DIVIDE_UNEXPECTED_CALL();
                }
            } break;
            case 2: return DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
            case 3: return DescriptorSetBindingType::IMAGE;
            default: DIVIDE_UNEXPECTED_CALL(); break;
        }

        return DescriptorSetBindingType::COUNT;
    }

    void Set( DescriptorSetBindingData& dataInOut, ShaderBuffer* buffer, BufferRange range ) noexcept
    {
        assert( buffer != nullptr );
        As<ShaderBufferEntry>( dataInOut ) = { buffer, range, buffer->queueReadIndex() };
    }

}; //namespace Divide