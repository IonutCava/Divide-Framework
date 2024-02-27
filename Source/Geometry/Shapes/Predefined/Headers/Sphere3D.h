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
#ifndef _SPHERE_3D_H_
#define _SPHERE_3D_H_

#include "Geometry/Shapes/Headers/Object3D.h"

namespace Divide {

class Sphere3D final : public Object3D {
   public:
    /// Change resolution to affect the spacing between vertices
    explicit Sphere3D(GFXDevice& context, ResourceCache* parentCache, size_t descriptorHash, const Str<256>& name, F32 radius, U32 resolution);
    void saveToXML(boost::property_tree::ptree& pt) const override;
    void loadFromXML(const boost::property_tree::ptree& pt)  override;

    void setRadius(F32 radius) noexcept;
    void setResolution(U32 resolution) noexcept;

    PROPERTY_R(F32, radius, 1.f);
    PROPERTY_R(U32, resolution, 16u);

  private:
    void rebuildInternal() override;
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(Sphere3D);

};  // namespace Divide

#endif