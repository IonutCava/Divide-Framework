// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/PushConstants.h"

namespace Divide 
{

const UniformData::UniformDataContainer& UniformData::entries() const noexcept
{
    return _data;
}

const Byte* UniformData::data( const size_t offset ) const noexcept
{
    DIVIDE_GPU_ASSERT(offset < _buffer.size());
    return &_buffer[offset];
}

bool UniformData::remove( const U64 bindingHash )
{
    for ( UniformData::Entry& uniform : _data )
    {
        if (uniform._bindingHash == bindingHash)
        {
            std::memset(&_buffer[uniform._range._startOffset], 0, uniform._range._length);
            uniform = {};
            return true;
        }
    }

    return false;
}

bool Merge( UniformData& lhs, UniformData& rhs)
{
    if ( &lhs == &rhs )
    {
        return true;
    }

    for ( const UniformData::Entry& otherUniform : rhs._data )
    {
        if ( otherUniform._type == PushConstantType::COUNT )
        {
            continue;
        }
        for ( const UniformData::Entry& ourUniform : lhs._data )
        {
            if ( ourUniform._type != PushConstantType::COUNT &&
                 ourUniform._bindingHash == otherUniform._bindingHash )
            {
                if ( ourUniform._type != otherUniform._type ||
                     ourUniform._range._length != otherUniform._range._length )
                {
                    return false;
                }
            }
        }
    }

    for ( const UniformData::Entry& otherUniform : rhs._data )
    {
        if ( otherUniform._type != PushConstantType::COUNT )
        {
            lhs.set( otherUniform._bindingHash, otherUniform._type,
                     &rhs._buffer[otherUniform._range._startOffset], otherUniform._range._length );
        }
    }
    rhs._data.clear();
    rhs._buffer.clear();

    return true;
}

}; //namespace Divide
