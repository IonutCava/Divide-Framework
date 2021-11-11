// Copyright (c) 2011 NVIDIA Corporation. All rights reserved.
//
// TO  THE MAXIMUM  EXTENT PERMITTED  BY APPLICABLE  LAW, THIS SOFTWARE  IS PROVIDED
// *AS IS*  AND NVIDIA AND  ITS SUPPLIERS DISCLAIM  ALL WARRANTIES,  EITHER  EXPRESS
// OR IMPLIED, INCLUDING, BUT NOT LIMITED  TO, NONINFRINGEMENT,IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  IN NO EVENT SHALL  NVIDIA 
// OR ITS SUPPLIERS BE  LIABLE  FOR  ANY  DIRECT, SPECIAL,  INCIDENTAL,  INDIRECT,  OR  
// CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT LIMITATION,  DAMAGES FOR LOSS 
// OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR ANY 
// OTHER PECUNIARY LOSS) ARISING OUT OF THE  USE OF OR INABILITY  TO USE THIS SOFTWARE, 
// EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
//
// Please direct any bugs or questions to SDKFeedback@nvidia.com

//----------------------------------------------------------------------------------
// Defines and draws one ring of tiles in a concentric, nested set of rings.  Each ring
// is a different LOD and uses a different tile/patch size.  These are actually square
// rings.  (Is there a term for that?)  But they could conceivably change to circular.
// The inner-most LOD is a square, represented by a degenerate ring.
//

/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _TILE_RING_H_
#define _TILE_RING_H_

namespace Divide {

// Int dimensions specified to the ctor are in numbers of tiles.  It's symmetrical in
// each direction.  (Don't read much into the exact numbers of #s in this diagram.)
//
//    <-   outerWidth  ->
//    ###################
//    ###################
//    ###             ###
//    ###<-holeWidth->###
//    ###             ###
//    ###    (0,0)    ###
//    ###             ###
//    ###             ###
//    ###             ###
//    ###################
//    ###################
//
class TileRing final : NonCopyable
{
public:
    struct Adjacency
    {
        // These are the size of the neighbours along +/- x or y axes.
        // For interior tiles this is 1.  For edge tiles it is 0.5 or 2.0.
        F32 neighbourMinusX = 1.f;
        F32 neighbourMinusY = 1.f;
        F32 neighbourPlusX = 1.f;
        F32 neighbourPlusY = 1.f;
    };

    struct VertexData
    {
        F32 positionX = 0.0f;
        F32 positionZ = 0.0f;
        F32 tileScale = 1.0f;
        F32 ringID = 0.0f;
    };

    struct InstanceData
    {
        VertexData data = {};
        Adjacency adjacency = {};
    };

public:
    // holeWidth & outerWidth are nos. of tiles; tileSize is a world-space length
    explicit TileRing(I32 holeWidth, I32 outerWidth, F32 tileSize) noexcept;

    PROPERTY_R(I32, tileCount, 0);
    PROPERTY_R(F32, tileSize, 1.f);

    // We use ID as a really hacky LoD system. Ring 0 => LoD 0. Ring 1 => LoD 1 (e.g. no detail normals), etc
    [[nodiscard]] vector<InstanceData> createInstanceDataVB(I32 ringID);

private:
    [[nodiscard]] bool InRing(I32 x, I32 y) const noexcept;
    void AssignNeighbourSizes(I32 x, I32 y, Adjacency*) const noexcept;

private:
    const I32 _holeWidth = 0;
    const I32 _outerWidth = 0;
    const I32 _ringWidth = 0;
};

}  // namespace Divide

#endif //_TILE_RING_H_