#include "stdafx.h"

#include "Headers/glShaderBuffer.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Utility/Headers/Localization.h"

#include <iomanip>

namespace Divide
{

    glShaderBuffer::glShaderBuffer( GFXDevice& context, const ShaderBufferDescriptor& descriptor )
        : ShaderBuffer( context, descriptor )
    {
        const size_t targetElementSize = Util::GetAlignmentCorrected( _params._elementSize, _alignmentRequirement );
        if ( targetElementSize > _params._elementSize )
        {
            DIVIDE_ASSERT( (_params._elementSize * _params._elementCount) % _alignmentRequirement == 0u,
                           "ERROR: glShaderBuffer - element size and count combo is less than the minimum alignment requirement for current hardware! Pad the element size and or count a bit" );
        }
        else
        {
            DIVIDE_ASSERT( _params._elementSize == targetElementSize,
                           "ERROR: glShaderBuffer - element size is less than the minimum alignment requirement for current hardware! Pad the element size a bit" );
        }
        _alignedBufferSize = _params._elementCount * _params._elementSize;
        _alignedBufferSize = static_cast<ptrdiff_t>(realign_offset( _alignedBufferSize, _alignmentRequirement ));

        BufferImplParams implParams;
        implParams._bufferParams = _params;
        implParams._target = (getUsage() == BufferUsageType::UNBOUND_BUFFER || getUsage() == BufferUsageType::COMMAND_BUFFER)
            ? GL_SHADER_STORAGE_BUFFER
            : GL_UNIFORM_BUFFER;

        implParams._dataSize = _alignedBufferSize * queueLength();
        implParams._useChunkAllocation = getUsage() != BufferUsageType::COMMAND_BUFFER;

        _bufferImpl = eastl::make_unique<glBufferImpl>( context, implParams, descriptor._initialData, _name.empty() ? nullptr : _name.c_str() );

        // Just to avoid issues with reading undefined or zero-initialised memory.
        // This is quite fast so far so worth it for now.
        if ( descriptor._separateReadWrite && descriptor._initialData.second > 0 )
        {
            for ( U32 i = 1u; i < descriptor._ringBufferLength; ++i )
            {
                bufferImpl()->writeOrClearBytes( _alignedBufferSize * i, descriptor._initialData.second, descriptor._initialData.first, true );
            }
        }
    }

    BufferLock glShaderBuffer::writeBytesInternal( const BufferRange range, bufferPtr data )
    {
        return bufferImpl()->writeOrClearBytes( range._startOffset, range._length, data );
    }

    void glShaderBuffer::readBytesInternal( BufferRange range, std::pair<bufferPtr, size_t> outData )
    {
        range._length = std::min( std::min( range._length, outData.second ), _alignedBufferSize - range._startOffset );
        bufferImpl()->readBytes( range._startOffset, range._length, outData );
    }

    bool glShaderBuffer::bindByteRange( const U8 bindIndex, BufferRange range, I32 readIndex )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        GLStateTracker::BindResult result = GLStateTracker::BindResult::FAILED;

        DIVIDE_ASSERT( to_size( range._length ) <= _maxSize && "glShaderBuffer::bindByteRange: attempted to bind a larger shader block than is allowed on the current platform" );
        DIVIDE_ASSERT( range._startOffset == Util::GetAlignmentCorrected( range._startOffset, _alignmentRequirement ) );
        if ( readIndex == -1 )
        {
            readIndex = getStartIndex( true );
        }

        if ( bindIndex == ShaderProgram::k_commandBufferID )
        {
            result = GL_API::GetStateTracker().setActiveBuffer( GL_DRAW_INDIRECT_BUFFER, bufferImpl()->memoryBlock()._bufferHandle );
            GL_API::GetStateTracker()._drawIndirectBufferOffset = bufferImpl()->memoryBlock()._offset + (readIndex * _alignedBufferSize);
        }
        else if ( range._length > 0 ) [[likely]]
        {
            const size_t offset = bufferImpl()->memoryBlock()._offset + range._startOffset + (readIndex * _alignedBufferSize);
            // If we bind the entire buffer, offset == 0u and range == 0u is a hack to bind the entire thing instead of a subrange
            const size_t bindRange = Util::GetAlignmentCorrected( (offset == 0u && to_size( range._length ) == bufferImpl()->memoryBlock()._size) ? 0u : range._length, _alignmentRequirement );
            result = GL_API::GetStateTracker().setActiveBufferIndexRange( bufferImpl()->params()._target,
                                                                          bufferImpl()->memoryBlock()._bufferHandle,
                                                                          bindIndex,
                                                                          offset,
                                                                          bindRange );
        }

            if ( result == GLStateTracker::BindResult::FAILED )
            {
                DIVIDE_UNEXPECTED_CALL();
            }

        return result == GLStateTracker::BindResult::JUST_BOUND;
    }

    LockableBuffer* glShaderBuffer::getBufferImpl()
    {
        return _bufferImpl.get();
    }

}  // namespace Divide
