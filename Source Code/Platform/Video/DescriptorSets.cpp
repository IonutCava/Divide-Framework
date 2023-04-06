#include "stdafx.h"

#include "Headers/DescriptorSets.h"

#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {
    size_t GetHash( const ImageViewDescriptor& descriptor) noexcept
    {
        size_t hash = 1337;
        Util::Hash_combine(hash,
                           descriptor._msaaSamples,
                            descriptor._dataType,
                            descriptor._baseFormat,
                            descriptor._packing);
        return hash;
    }
    
    size_t GetHash( const ImageView& imageView ) noexcept
    {
        size_t hash = 1337;
        Util::Hash_combine(hash,
                           GetHash(imageView._descriptor),
                            imageView._srcTexture != nullptr ? imageView._srcTexture->getGUID() : 0,
                            TargetType( imageView ),
                            imageView._subRange._layerRange._offset,
                            imageView._subRange._layerRange._count,
                            imageView._subRange._mipLevels._offset,
                            imageView._subRange._mipLevels._count);

        return hash;
    }

    TextureType TargetType( const ImageView& imageView ) noexcept
    {
        if (imageView._targetType == TextureType::COUNT &&
            imageView._srcTexture != nullptr)
        {
            return imageView._srcTexture->descriptor().texType();
        }

        return imageView._targetType;
    }

    bool operator==(const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs) noexcept {
        return lhs._range == rhs._range &&
               lhs._queueReadIndex == rhs._queueReadIndex &&
               Compare(lhs._buffer, rhs._buffer);
    }

    bool operator!=(const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs) noexcept {
        return lhs._range != rhs._range ||
               lhs._queueReadIndex != rhs._queueReadIndex ||
               !Compare(lhs._buffer, rhs._buffer);
    }

    void Set( DescriptorSetBindingData& dataInOut, ShaderBuffer* buffer, const BufferRange range ) noexcept
    {
        assert( buffer != nullptr );
        dataInOut._buffer = { buffer, range, buffer->queueReadIndex() };
        dataInOut._type = buffer->getUsage() == BufferUsageType::CONSTANT_BUFFER ? DescriptorSetBindingType::UNIFORM_BUFFER
                                                                                 : DescriptorSetBindingType::SHADER_STORAGE_BUFFER;
    }

}; //namespace Divide