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
#ifndef DVD_WATER_PLANE_H_
#define DVD_WATER_PLANE_H_

#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"

#include "ECS/Components/Headers/RenderingComponent.h"

namespace Divide {

class Texture;
class StaticCamera;
class ShaderProgram;

DEFINE_NODE_TYPE(WaterPlane, SceneNodeType::TYPE_WATER)
{
   public:
    explicit WaterPlane( const ResourceDescriptor<WaterPlane>& descriptor );
    ~WaterPlane() override;

    static bool PointUnderwater(const SceneGraphNode* sgn, const float3& point) noexcept;

    Handle<Quad3D> getQuad() const noexcept { return _plane; }

    void updatePlaneEquation(const SceneGraphNode* sgn,
                             Plane<F32>& plane,
                             bool reflection,
                             F32 offset) const;

    // width, length, depth
    const vec3<U16>& getDimensions() const noexcept;

    void saveToXML(boost::property_tree::ptree& pt) const override;
    void loadFromXML(const boost::property_tree::ptree& pt)  override;

    PROPERTY_RW(FColour3, refractionTint);
    PROPERTY_RW(FColour3, waterDistanceFogColour);
    PROPERTY_R(F32, reflPlaneOffset, 0.0f);
    PROPERTY_R(F32, refrPlaneOffset, 0.0f);
    PROPERTY_RW(F32, specularShininess, 200.f);
    PROPERTY_RW(float2, noiseTile);
    PROPERTY_RW(float2, noiseFactor);
    PROPERTY_RW(float2, fogStartEnd);
    PROPERTY_RW(U16, blurKernelSize, 9u);
    PROPERTY_RW(bool, blurReflections, true);
    PROPERTY_RW(bool, blurRefractions, true);

   protected:
    friend class ResourceCache;
    template <typename T> friend struct ResourcePool;

    bool postLoad() override;
    bool unload() override;

   protected:
    void buildDrawCommands(SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut) override;

    void postLoad(SceneGraphNode* sgn) override;

    void sceneUpdate(U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState) override;
    void prepareRender(SceneGraphNode* sgn,
                       RenderingComponent& rComp,
                       RenderPackage& pkg,
                       GFX::MemoryBarrierCommand& postDrawMemCmd,
                       RenderStagePass renderStagePass,
                       const CameraSnapshot& cameraSnapshot,
                       bool refreshData) override;
   protected:
    template <typename T>
    friend class ImplResourceLoader;

    bool load( PlatformContext& context ) override;
    void onEditorChange(std::string_view field) noexcept;

   private:
    bool updateReflection(RenderCbkParams& renderParams, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) const;
    bool updateRefraction(RenderCbkParams& renderParams, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) const;

   private:
    vec3<U16> _dimensions{1u};
    Handle<Quad3D> _plane{ INVALID_HANDLE<Quad3D> };
    Camera* _reflectionCam{ nullptr };
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(WaterPlane);

}  // namespace Divide

#endif //DVD_WATER_PLANE_H_
