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
#ifndef DVD_PLAYER_H_
#define DVD_PLAYER_H_

#include "Character.h"

namespace Divide
{

    class Camera;

    /// User controlled Unit
    class Player final : public Character
    {
        public:
        explicit Player( U8 index );
        ~Player() override;

        /// Do not allow or allow the user again to control this player
        PROPERTY_R( bool, lockedControls, false );
        PROPERTY_R( U8, index, 0u );
        PROPERTY_R( float3, extents, VECTOR3_UNIT );
        POINTER_R( Camera, camera, nullptr );

       protected:
        void setParentNode( SceneGraphNode* node ) override;
    };

}  // namespace Divide

#endif //DVD_PLAYER_H_
