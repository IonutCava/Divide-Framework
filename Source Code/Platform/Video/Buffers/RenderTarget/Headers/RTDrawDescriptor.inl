/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _RENDER_TARGET_DRAW_DESCRIPTOR_INL_
#define _RENDER_TARGET_DRAW_DESCRIPTOR_INL_

namespace Divide
{

    inline bool operator==( const RTDrawMask& lhs, const RTDrawMask& rhs )
    {
        return lhs._disabledDepth == rhs._disabledDepth &&
               lhs._disabledColours == rhs._disabledColours;
    }

    inline bool operator!=( const RTDrawMask& lhs, const RTDrawMask& rhs )
    {
        return lhs._disabledDepth != rhs._disabledDepth ||
               lhs._disabledColours != rhs._disabledColours;
    }

    inline bool operator==( const RTLayoutTarget& lhs, const RTLayoutTarget& rhs )
    {
        return lhs._depthUsage == rhs._depthUsage &&
               lhs._colourUsage == rhs._colourUsage;
    }

    inline bool operator!=( const RTLayoutTarget& lhs, const RTLayoutTarget& rhs )
    {
        return lhs._depthUsage != rhs._depthUsage ||
               lhs._colourUsage != rhs._colourUsage;
    }

    inline bool operator==( const RTDrawDescriptor& lhs, const RTDrawDescriptor& rhs )
    {
        return lhs._drawMask == rhs._drawMask &&
               lhs._layoutTargets == rhs._layoutTargets &&
               lhs._setViewport == rhs._setViewport;
    }

    inline bool operator!=( const RTDrawDescriptor& lhs, const RTDrawDescriptor& rhs )
    {
        return lhs._drawMask != rhs._drawMask ||
               lhs._layoutTargets != rhs._layoutTargets ||
               lhs._setViewport != rhs._setViewport;
    }

    inline bool IsValid( const BlitIndex& entry ) noexcept
    {
        return entry._index != INVALID_LAYER_INDEX && entry._layer != INVALID_LAYER_INDEX;
    }

    inline bool IsValid( const DepthBlitEntry& entry ) noexcept
    {
        return entry._inputLayer != INVALID_LAYER_INDEX && entry._outputLayer != INVALID_LAYER_INDEX;
    }

    inline bool IsValid( const ColourBlitEntry& entry ) noexcept
    {
        return IsValid( entry._input ) && IsValid( entry._output );
    }

    inline bool IsValid( const RTBlitParams::ColourArray& colours ) noexcept
    {
        for ( const auto& it : colours )
        {
            if ( IsValid( it ) )
            {
                return true;
            }
        }

        return false;
    }

    inline bool IsValid( const RTBlitParams& params ) noexcept
    {
        if ( IsValid( params._blitDepth ) )
        {
            return true;
        }

        return IsValid( params._blitColours );
    }
}; //namespace Divide
#endif// _RENDER_TARGET_DRAW_DESCRIPTOR_INL_