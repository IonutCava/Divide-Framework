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
#ifndef _DIVIDE_LIGHT_COMPONENT_H_
#define _DIVIDE_LIGHT_COMPONENT_H_

#include "ECS/Components/Headers/SGNComponent.h"
#include "Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"

namespace Divide {

namespace TypeUtil {
    const char* LightTypeToString(LightType lightType) noexcept;
    LightType StringToLightType(const string& name);
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
    struct ShadowProperties {
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
    virtual ~Light();

    /// Get light diffuse colour
    void getDiffuseColour(FColour3& colourOut) const noexcept {
        Util::ToFloatColour(_colour.rgb, colourOut);
    }

    [[nodiscard]] FColour3 getDiffuseColour() const noexcept {
        return Util::ToFloatColour(_colour.rgb);
    }

    void setDiffuseColour(const UColour3& newDiffuseColour) noexcept;

    void setDiffuseColour(const FColour3& newDiffuseColour) noexcept {
        setDiffuseColour(Util::ToByteColour(newDiffuseColour));
    }

    /// Light state (on/off)
    void toggleEnabled() noexcept { enabled(!enabled()); }

    /// Get the light type. (see LightType enum)
    [[nodiscard]] const LightType& getLightType() const noexcept { return _type; }

    /// Get the distance squared from this light to the specified position
    [[nodiscard]] F32 distanceSquared(const vec3<F32>& pos) const noexcept { return positionCache().distanceSquared(pos); }

    /*----------- Shadow Mapping-------------------*/
    [[nodiscard]] const ShadowProperties& getShadowProperties() const noexcept {
        return _shadowProperties;
    }

    [[nodiscard]] const mat4<F32>& getShadowVPMatrix(const U8 index) const noexcept {
        assert(index < 6);

        return _shadowProperties._lightVP[index];
    }

    [[nodiscard]] F32 getShadowFloatValues(const U8 index) const noexcept {
        assert(index < 6);

        return _shadowProperties._lightPosition[index].w;
    }

    [[nodiscard]] const vec4<F32>& getShadowLightPos(const U8 index) const noexcept {
        assert(index < 6);

        return _shadowProperties._lightPosition[index];
    }

    void setShadowVPMatrix(const U8 index, const mat4<F32>& newValue) noexcept {
        assert(index < 6);
        
        if (_shadowProperties._lightVP[index] != newValue) {
            _shadowProperties._lightVP[index].set(newValue);
            _shadowProperties._dirty = true;
        }
    }

    void setShadowFloatValue(const U8 index, const F32 newValue) noexcept {
        assert(index < 6);
        
        if (_shadowProperties._lightPosition[index].w != newValue) {
            _shadowProperties._lightPosition[index].w = newValue;
            _shadowProperties._dirty = true;
        }
    }

    void setShadowLightPos(const U8 index, const vec3<F32>& newValue) noexcept {
        if (_shadowProperties._lightPosition[index].xyz != newValue) {
            _shadowProperties._lightPosition[index].xyz = newValue;
            _shadowProperties._dirty = true;
        }
    }

    void setShadowArrayOffset(const U16 offset) noexcept {
        if (getShadowArrayOffset() != offset) {
            _shadowProperties._lightDetails.y = offset == U16_MAX ? -1.f : to_F32(offset);
            _shadowProperties._dirty = true;
        }
    }

    void cleanShadowProperties() noexcept {
        _shadowProperties._dirty = false;
        staticShadowsDirty(false);
        dynamicShadowsDirty(false);
    }

    [[nodiscard]] U16 getShadowArrayOffset() const noexcept {
        if (_shadowProperties._lightDetails.y < 0.f) {
            return U16_MAX;
        }

        return to_U16(_shadowProperties._lightDetails.y);
    }

    [[nodiscard]] SceneGraphNode* getSGN()       noexcept { return _sgn; }
    [[nodiscard]] const SceneGraphNode* getSGN() const noexcept { return _sgn; }

    PROPERTY_R_IW(BoundingSphere, boundingVolume);
    PROPERTY_R(vec3<F32>, positionCache);
    PROPERTY_R(vec3<F32>, directionCache);
    /// Does this light cast shadows?
    PROPERTY_RW(bool, castsShadows);
    /// Turn the light on/off
    PROPERTY_RW(bool, enabled);
    /// Light range used for attenuation computation. Range = radius (not diameter!)
    PROPERTY_RW(F32, range, 10.0f);
    /// Light intensity in "lumens" (not really). Just a colour multiplier for now. ToDo: fix that -Ionut
    PROPERTY_RW(F32, intensity, 1.0f);
    /// Index used to look up shadow properties in shaders
    PROPERTY_R_IW(I32, shadowPropertyIndex, -1);
    /// A generic ID used to identify the light more easily
    PROPERTY_RW(U32, tag, 0u);

    PROPERTY_RW(bool, staticShadowsDirty, true);
    PROPERTY_RW(bool, dynamicShadowsDirty, true);

    /// Same as showDirectionCone but triggered differently (i.e. on selection in editor)
    PROPERTY_R_IW(bool, drawImpostor, false);

   protected:
     friend class LightPool;
     void updateCache(const ECS::CustomEvent& event);
     void registerFields(EditorComponent& comp);
     virtual void updateBoundingVolume(const Camera* playerCamera);

   protected:
    SceneGraphNode* _sgn = nullptr;
    LightPool& _parentPool;
    // Shadow mapping properties
    ShadowProperties _shadowProperties;
    /// rgb - diffuse, a - reserved
    UColour4  _colour;
    LightType _type = LightType::COUNT;
};

};  // namespace Divide

#endif //_DIVIDE_LIGHT_COMPONENT_H_