/*
   Copyright (c) 2018-2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

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
#ifndef DVD_SPOT_LIGHT_COMPONENT_H_
#define DVD_SPOT_LIGHT_COMPONENT_H_

#include "Rendering/Lighting/Headers/Light.h"
#include "Rendering/Lighting/Headers/SpotLightGeometry.h"

namespace Divide {

BEGIN_COMPONENT_EXT1(SpotLight, ComponentType::SPOT_LIGHT, Light)
   public:
    explicit SpotLightComponent(SceneGraphNode* sgn, PlatformContext& context);

    [[nodiscard]] Angle::DEGREES_F coneCutoffAngle() const noexcept { return _coneAngles._inner; }
    void coneCutoffAngle(Angle::DEGREES_F angle) noexcept;
    [[nodiscard]] Angle::DEGREES_F outerConeCutoffAngle() const noexcept { return _coneAngles._outer; }
    void outerConeCutoffAngle(Angle::DEGREES_F angle) noexcept;

    void loadFromXML(const boost::property_tree::ptree& pt) override;

    /// If this is true, we render a cone narrow cone to approximate the light's direction
    PROPERTY_RW(bool, showDirectionCone, false);

    [[nodiscard]] F32 outerConeRadius() const noexcept;
    [[nodiscard]] F32 innerConeRadius() const noexcept;
    [[nodiscard]] F32 coneSlantHeight() const noexcept;

   protected:
    void OnData(const ECS::CustomEvent& data) override;
    void setDirection(const float3& direction) const;

   private:
    SpotConeAngles _coneAngles;

END_COMPONENT(SpotLight);

}  // namespace Divide

#endif //DVD_SPOT_LIGHT_COMPONENT_H_
