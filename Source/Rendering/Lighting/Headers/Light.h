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
#ifndef DVD_LIGHT_COMPONENT_H_
#define DVD_LIGHT_COMPONENT_H_

#include "ECS/Components/Headers/SGNComponent.h"
#include "Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"

#include <ECS/Event/IEventListener.h>

namespace Divide {

namespace TypeUtil {
    [[nodiscard]] const char* LightTypeToString(LightType lightType) noexcept;
    [[nodiscard]] LightType StringToLightType(const string& name);
};

struct TransformUpdated;

class Camera;
class LightPool;
class SceneGraphNode;
class EditorComponent;
class SceneRenderState;
/// A light object placed in the scene at a certain position
class Light : public GUIDWrapper, public ECS::Event::IEventListener
{
public:

    // Worst case scenario: cube shadows = 6 passes
    struct ShadowProperties
    {
        // x = light type, y = arrayOffset, z - bias, w - strength
        vec4<F32> _lightDetails{0.f, -1.f, 0.f, 1.f};
        /// light's position in world space. w - csm split distances (or whatever else might be needed)
        std::array<vec4<F32>, 6> _lightPosition{};
        /// light viewProjection matrices
        std::array<mat4<F32>, 6> _lightVP{};
        bool _dirty = false;
    };

    /// Create a new light assigned to the specified slot with the specified range
    explicit Light(SceneGraphNode* sgn, F32 range, LightType type, LightPool& parentPool);
    virtual ~Light() override;

    [[nodiscard]] FColour3 getDiffuseColour()                    const noexcept;
                    void   getDiffuseColour(FColour3& colourOut) const noexcept;

    void setDiffuseColour(const UColour3& newDiffuseColour) noexcept;
    void setDiffuseColour(const FColour3& newDiffuseColour) noexcept;

    /// Light state (on/off)
    void toggleEnabled() noexcept;

    /// Get the light type. (see LightType enum)
    [[nodiscard]] const LightType& getLightType() const noexcept;

    /// Get the distance squared from this light to the specified position
    [[nodiscard]] F32 distanceSquared(const vec3<F32>& pos) const noexcept;

    [[nodiscard]] const ShadowProperties& getShadowProperties() const noexcept;

    [[nodiscard]] const mat4<F32>& getShadowVPMatrix(const U8 index) const noexcept;
                  void             setShadowVPMatrix(const U8 index, const mat4<F32>& newValue) noexcept;

    [[nodiscard]] F32 getShadowFloatValues(const U8 index) const noexcept;
    void setShadowFloatValue(const U8 index, const F32 newValue) noexcept;

    [[nodiscard]] const vec4<F32>& getShadowLightPos(const U8 index) const noexcept;
                  void             setShadowLightPos(const U8 index, const vec3<F32>& newValue) noexcept;

    U16  getShadowArrayOffset() const noexcept;
    void setShadowArrayOffset(const U16 offset) noexcept;

    void cleanShadowProperties() noexcept;


public:
    PROPERTY_R_IW(BoundingSphere, boundingVolume);
    PROPERTY_R(vec3<F32>, positionCache);
    PROPERTY_R(vec3<F32>, directionCache);
    /// Does this light cast shadows?
    PROPERTY_RW(bool, castsShadows);
    /// Turn the light on/off
    PROPERTY_RW(bool, enabled);
    /// Light range used for attenuation computation. Range = radius (not diameter!)
    PROPERTY_RW(F32, range, 10.f);
    /// Light intensity in "lumens" (not really). Just a colour multiplier for now. ToDo: fix that -Ionut
    PROPERTY_RW(F32, intensity, 1.f);
    /// Index used to look up shadow properties in shaders
    PROPERTY_R_IW(I32, shadowPropertyIndex, -1);
    /// A generic ID used to identify the light more easily
    PROPERTY_RW(U32, tag, 0u);

    PROPERTY_RW(bool, staticShadowsDirty, true);
    PROPERTY_RW(bool, dynamicShadowsDirty, true);

    /// Same as showDirectionCone but triggered differently (i.e. on selection in editor)
    PROPERTY_R_IW(bool, drawImpostor, false);
    PROPERTY_R_IW(SceneGraphNode*, sgn, nullptr);

   protected:
     friend class LightPool;
     void updateCache(const ECS::CustomEvent& event);
     void registerFields(EditorComponent& comp);
     virtual void updateBoundingVolume(const Camera* playerCamera);

   protected:
    LightPool& _parentPool;
    ShadowProperties _shadowProperties;
    UColour4  _colour;
    LightType _type{LightType::COUNT};
};

};  // namespace Divide

#endif //DVD_LIGHT_COMPONENT_H_

#include "Light.inl"
