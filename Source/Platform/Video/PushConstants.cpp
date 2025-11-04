

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

bool Merge( UniformData& lhs, UniformData& rhs, bool& partial)
{
    for (const UniformData::Entry& ourUniform : lhs._data)
    {
        for (const UniformData::Entry& otherUniform : rhs._data)
        {
            // If we have the same binding check data on each side to see if it matches
            if ( ourUniform._bindingHash == otherUniform._bindingHash)
            {
                // If we have different types or ranges for the same binding, something is probably wrong, but for our specific case, we just fail to merge.
                if ( ourUniform._type != otherUniform._type || ourUniform._range != otherUniform._range)
                {
                    return false;
                }
                // If we got here, everything apart from the data (maybe) matches, so we can just overwrite it.
            }

            lhs.set(otherUniform._bindingHash, otherUniform._type, &rhs._buffer[otherUniform._range._startOffset], otherUniform._range._length);
            DIVIDE_EXPECTED_CALL( rhs.remove(otherUniform._bindingHash) );

            partial = true;
        }
    }

    return true;
}

}; //namespace Divide
