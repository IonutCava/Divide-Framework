

#include "Headers/vkShaderBuffer.h"
#include "Headers/vkBufferImpl.h"

#include "Platform/Video/Headers/LockManager.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide
{
    vkShaderBuffer::vkShaderBuffer( GFXDevice& context, const ShaderBufferDescriptor& descriptor )
        : ShaderBuffer( context, descriptor )
    {
        const size_t targetElementSize = Util::GetAlignmentCorrected( _params._elementSize, _alignmentRequirement );
        if ( targetElementSize > _params._elementSize )
        {
            DIVIDE_ASSERT( (_params._elementSize * _params._elementCount) % _alignmentRequirement == 0u,
                           "ERROR: vkShaderBuffer - element size and count combo is less than the minimum alignment requirement for current hardware! Pad the element size and or count a bit" );
        }
        else
        {
            DIVIDE_ASSERT( _params._elementSize == targetElementSize,
                           "ERROR: vkShaderBuffer - element size is less than the minimum alignment requirement for current hardware! Pad the element size a bit" );
        }

        _alignedBufferSize = static_cast<ptrdiff_t>(realign_offset( _params._elementCount * _params._elementSize, _alignmentRequirement ));

        const string bufferName = _name.empty() ? Util::StringFormat( "DVD_GENERAL_SHADER_BUFFER_{}", getGUID() ) : (_name + "_SHADER_BUFFER");

        _bufferImpl = std::make_unique<vkBufferImpl>( _params,
                                                        _alignedBufferSize,
                                                        queueLength(),
                                                        descriptor._initialData,
                                                        bufferName.c_str() );
    }

    BufferLock vkShaderBuffer::writeBytesInternal( const BufferRange<> range, const bufferPtr data ) noexcept
    {
        if ( !_bufferImpl->waitForLockedRange( range ) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        const bool isCommandBuffer = getUsage() == BufferUsageType::COMMAND_BUFFER;
        const VkAccessFlags2 dstAccessMask = VK_ACCESS_SHADER_READ_BIT | (isCommandBuffer ? VK_ACCESS_INDIRECT_COMMAND_READ_BIT : VK_ACCESS_NONE);
        const VkPipelineStageFlags2 dstStageMask = VK_API::ALL_SHADER_STAGES | VK_PIPELINE_STAGE_TRANSFER_BIT | (isCommandBuffer ? VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT : VK_PIPELINE_STAGE_NONE);

        LockGuard<Mutex> w_lock( _implLock );
        return _bufferImpl->writeBytes(range, dstAccessMask, dstStageMask, data);
    }

    void vkShaderBuffer::readBytesInternal( BufferRange<> range, std::pair<bufferPtr, size_t> outData ) noexcept
    {
        if ( !_bufferImpl->waitForLockedRange( range ) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        range._length = std::min( std::min( range._length, outData.second ), _alignedBufferSize - range._startOffset );

        LockGuard<Mutex> w_lock( _implLock );
        _bufferImpl->readBytes(range, outData);
    }

    LockableBuffer* vkShaderBuffer::getBufferImpl()
    {
        return _bufferImpl.get();
    }

}; //namespace Divide
