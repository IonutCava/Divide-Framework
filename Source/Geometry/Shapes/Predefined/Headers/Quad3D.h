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
#ifndef DVD_QUAD_3D_H_
#define DVD_QUAD_3D_H_

#include "Geometry/Shapes/Headers/Object3D.h"

namespace Divide {

DEFINE_3D_OBJECT_TYPE(Quad3D, SceneNodeType::TYPE_QUAD_3D)
{
  public:
    enum class CornerLocation : U8 {
        TOP_LEFT = 0,
        TOP_RIGHT,
        BOTTOM_LEFT,
        BOTTOM_RIGHT,
        CORNER_ALL
    };

    explicit Quad3D( const ResourceDescriptor<Quad3D>& descriptor );

    bool load( PlatformContext& context ) override;

    float3 getCorner(CornerLocation corner);

    void setNormal(CornerLocation corner, const float3& normal);

    void setCorner(CornerLocation corner, const float3& value);

    // rect.xy = Top Left; rect.zw = Bottom right
    // Remember to invert for 2D mode
    void setDimensions(const float4& rect);

   protected:
     void recomputeBounds();

     const ResourceDescriptor<Quad3D> _descriptor;
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(Quad3D);

};  // namespace Divide

#endif // DVD_QUAD_3D_H_
