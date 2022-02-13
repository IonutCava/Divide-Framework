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
#ifndef _SPOT_LIGHT_COMPONENT_H_
#define _SPOT_LIGHT_COMPONENT_H_

#include "Rendering/Lighting/Headers/Light.h"

namespace Divide {

BEGIN_COMPONENT_EXT1(SpotLight, ComponentType::SPOT_LIGHT, Light)
   public:
    explicit SpotLightComponent(SceneGraphNode* sgn, PlatformContext& context);

    PROPERTY_RW(Angle::DEGREES<F32>, coneCutoffAngle, 35.0f);
    PROPERTY_RW(Angle::DEGREES<F32>, outerConeCutoffAngle, 15.0f);
    /// If this is true, we render a cone narrow cone to approximate the light's direction
    PROPERTY_RW(bool, showDirectionCone, false);

    [[nodiscard]] F32 outerConeRadius() const noexcept;
    [[nodiscard]] F32 innerConeRadius() const noexcept;
    [[nodiscard]] F32 coneSlantHeight() const noexcept;

   protected:
    void OnData(const ECS::CustomEvent& data) override;
    void setDirection(const vec3<F32>& direction) const;

END_COMPONENT(SpotLight);

}  // namespace Divide

#endif //_SPOT_LIGHT_COMPONENT_H_