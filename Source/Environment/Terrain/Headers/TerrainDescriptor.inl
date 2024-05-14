
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
#ifndef DVD_TERRAIN_DESCRIPTOR_INL_
#define DVD_TERRAIN_DESCRIPTOR_INL_

namespace Divide
{
    template<>
    inline size_t GetHash( const TerrainDescriptor& descriptor ) noexcept
    {
        size_t hash = 1337;

        for ( const auto& it : descriptor._variables )
        {
            Util::Hash_combine( hash, it.first, it.second );
        }

        for ( hashMap<U64, F32>::value_type it : descriptor._variablesf )
        {
            Util::Hash_combine( hash, it.first, it.second );
        }

        Util::Hash_combine( hash,
                            descriptor._active,
                            descriptor._textureLayers,
                            descriptor._altitudeRange.x,
                            descriptor._altitudeRange.y,
                            descriptor._dimensions.x,
                            descriptor._dimensions.y,
                            descriptor._startWidth
                           );

        for ( U8 i = 0; i < descriptor._ringCount; ++i )
        {
            Util::Hash_combine( hash, descriptor._ringTileCount[i] );
        }

        return hash;
    }
} //namespace Divide

#endif //DVD_TERRAIN_DESCRIPTOR_INL_
