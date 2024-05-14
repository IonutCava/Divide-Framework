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

#ifndef DVD_VEGETATION_DESCRIPTOR_INL_
#define DVD_VEGETATION_DESCRIPTOR_INL_


namespace Divide
{
template<>
inline size_t GetHash( const VegetationDescriptor& descriptor ) noexcept
{
    size_t hash = 1337;

    if ( descriptor.grassMap )
    {
        Util::Hash_combine( hash, descriptor.grassMap->fullPath() );
    }

    if ( descriptor.treeMap )
    {
        Util::Hash_combine( hash, descriptor.treeMap->fullPath());
    }

    for ( U8 i = 0; i < descriptor.treeRotations.size(); ++i )
    {
        Util::Hash_combine( hash,
                            descriptor.treeRotations[i].x,
                            descriptor.treeRotations[i].y,
                            descriptor.treeRotations[i].z
        );
    }

    Util::Hash_combine( hash,
                        descriptor.name,
                        descriptor.chunkSize,
                        descriptor.billboardTextureArray,
                        descriptor.treeScales.x,
                        descriptor.treeScales.y,
                        descriptor.treeScales.z,
                        descriptor.treeScales.w,
                        descriptor.grassScales.x,
                        descriptor.grassScales.y,
                        descriptor.grassScales.z,
                        descriptor.grassScales.w
    );

    for ( const auto& treeMesh : descriptor.treeMeshes )
    {
        Util::Hash_combine( hash, treeMesh );
    }

    if ( descriptor.parentTerrain != nullptr )
    {
        Util::Hash_combine( hash, GetHash( descriptor.parentTerrain ) );
    }

    return hash;
}

} //namespace Divide

#endif //DVD_VEGETATION_DESCRIPTOR_INL_