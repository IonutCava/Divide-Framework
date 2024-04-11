#include "Headers/PlatformDataTypes.h"

#include "config.h"
#include <cassert> //assert
#include <cstring> //std::memcpy

namespace Divide
{
    namespace detail
    {
        void internal_assert( const bool condition )
        {
            if constexpr (Config::Build::IS_DEBUG_BUILD)
            {
                assert( condition );
            }
            else
            {
                DIVIDE_UNUSED(condition);
            }
        }

    } //namespace detail

    P32::P32( const U32 val ) noexcept : i( val )
    {
    }

    P32::P32( const U8 b1, const U8 b2, const U8 b3, const U8 b4 ) noexcept : b{ b1, b2, b3, b4 }
    {
    }

    P32::P32( U8* bytes ) noexcept : b{ bytes[0], bytes[1], bytes[2], bytes[3] }
    {
    }

    counter::counter( const size_t count ) noexcept
        : _count( count )
    {
    }

    counter& counter::operator=( const size_t val ) noexcept
    {
        _count = val;
        return *this;
    }

    counter::operator size_t() const noexcept
    {
        return _count;
    }

    counter& counter::operator++() noexcept
    {
        ++_count; return *this;
    }

    counter counter::operator++( int ) noexcept
    {
        const counter ret( _count );
        ++_count;
        return ret;
    }

} //namespace Divide
