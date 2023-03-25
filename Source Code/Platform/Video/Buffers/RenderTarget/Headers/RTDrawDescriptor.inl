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

    inline bool IsValid( const RTBlitParams& params ) noexcept
    {
        for ( const auto& it : params )
        {
            if ( it._input != INVALID_BLIT_ENTRY && 
                 it._output != INVALID_BLIT_ENTRY)
            {
                return it._layerCount > 0u && it._mipCount > 0u;
            }
        }

        return false;
    }

    inline bool operator==( const BlitEntry& lhs, const BlitEntry& rhs ) noexcept
    {
        return lhs._index == rhs._index &&
               lhs._layerOffset == rhs._layerOffset &&
               lhs._mipOffset == rhs._mipOffset;
    }

}; //namespace Divide
#endif// _RENDER_TARGET_DRAW_DESCRIPTOR_INL_