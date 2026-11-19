// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include "glStateTracker.h"

namespace Divide
{
    struct GLTextureBindQueue
    {
        static_assert( GLStateTracker::MAX_BOUND_TEXTURE_UNITS <= 32u );

        [[nodiscard]] bool queue( const U8 slot, const gl46core::GLuint texture, const gl46core::GLuint sampler )
        {
            if ( slot >= GLStateTracker::MAX_BOUND_TEXTURE_UNITS )
            {
                DIVIDE_GPU_ASSERT( false, "OpenGL texture binding slot is out of range." );
                return false;
            }
            _textures[slot] = texture;
            _samplers[slot] = sampler;
            _pendingSlots |= 1u << slot;
            _firstSlot = std::min( _firstSlot, slot );
            _lastSlot = std::max( _lastSlot, slot );
            return true;
        }

        template<typename ReadBinding, typename BindRange>
        void flush( ReadBinding&& readBinding, BindRange&& bindRange )
        {
            if ( _pendingSlots == 0u )
            {
                return;
            }

            for ( size_t slot = _firstSlot; slot <= _lastSlot; ++slot )
            {
                if ( (_pendingSlots & (1u << slot)) == 0u )
                {
                    gl46core::GLuint& texture = _textures[slot];
                    gl46core::GLuint& sampler = _samplers[slot];
                    readBinding( to_U8( slot ), texture, sampler );
                    // The state tracker records reset units as GL_NULL_HANDLE, which multi-bind calls reject as a name.
                    texture = texture == GL_NULL_HANDLE ? 0u : texture;
                    sampler = sampler == GL_NULL_HANDLE ? 0u : sampler;
                }
            }
            bindRange( _firstSlot, to_U32( _lastSlot - _firstSlot + 1u ),
                       _textures.data() + _firstSlot, _samplers.data() + _firstSlot );
            clear();
        }

        /// Turns pending bindings of a deleted texture name into explicit unbinds, keeping their samplers.
        void invalidate( const gl46core::GLuint texture ) noexcept
        {
            for ( size_t slot = 0u; slot < GLStateTracker::MAX_BOUND_TEXTURE_UNITS; ++slot )
            {
                if ( (_pendingSlots & (1u << slot)) != 0u && _textures[slot] == texture )
                {
                    _textures[slot] = 0u;
                }
            }
        }

        void clear() noexcept
        {
            _pendingSlots = 0u;
            _firstSlot = GLStateTracker::MAX_BOUND_TEXTURE_UNITS;
            _lastSlot = 0u;
        }

    private:
        std::array<gl46core::GLuint, GLStateTracker::MAX_BOUND_TEXTURE_UNITS> _textures{};
        std::array<gl46core::GLuint, GLStateTracker::MAX_BOUND_TEXTURE_UNITS> _samplers{};
        U32 _pendingSlots{ 0u };
        U8 _firstSlot{ GLStateTracker::MAX_BOUND_TEXTURE_UNITS };
        U8 _lastSlot{ 0u };
    };
}
