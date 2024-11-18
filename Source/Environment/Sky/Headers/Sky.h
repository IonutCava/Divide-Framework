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
#ifndef DVD_SKY_H_
#define DVD_SKY_H_

#include "Sun.h"
#include "Graphs/Headers/SceneNode.h"
#include "Utility/Headers/Colours.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

namespace Divide {

class GFXDevice;
struct PushConstantsStruct;

class Texture;
class Sphere3D;
class ShaderProgram;

FWD_DECLARE_MANAGED_CLASS(SceneGraphNode);

enum class RenderStage : U8;

enum class RebuildCommandsState : U8 
{
    NONE,
    REQUESTED,
    DONE
};

DEFINE_NODE_TYPE( Sky, SceneNodeType::TYPE_SKY )
{
   public:
    explicit Sky( const ResourceDescriptor<Sky>& descriptor );

    static void OnStartup(PlatformContext& context);
    // Returns the sun position and intensity details for the specified date-time
    const SunInfo& setDateTime(struct tm *dateTime) noexcept;
    const SunInfo& setGeographicLocation(SimpleLocation location) noexcept;
    const SunInfo& setDateTimeAndLocation(struct tm *dateTime, SimpleLocation location) noexcept;

    [[nodiscard]] SimpleTime GetTimeOfDay() const noexcept;
    [[nodiscard]] SimpleLocation GetGeographicLocation() const noexcept;

    [[nodiscard]] const SunInfo& getCurrentDetails() const;
    [[nodiscard]] float3 getSunPosition(F32 radius = 1.f) const;
    [[nodiscard]] float3 getSunDirection(F32 radius = 1.f) const;
    [[nodiscard]] bool isDay() const;

    PROPERTY_R(Atmosphere, atmosphere);
    PROPERTY_R(Atmosphere, defaultAtmosphere);
    PROPERTY_R(Atmosphere, initialAtmosphere);
    PROPERTY_R(bool, enableProceduralClouds, true);
    PROPERTY_R(bool, use);
    PROPERTY_R(bool, useDaySkybox, true);
    PROPERTY_R(bool, useNightSkybox, true);
    PROPERTY_R(F32,  moonScale, 0.5f);
    PROPERTY_R(F32,  exposure, 0.1f);
    PROPERTY_R(U16,  rayCount, 128u);
    PROPERTY_R(FColour4, moonColour, DefaultColours::WHITE);
    PROPERTY_R(FColour4, nightSkyColour, DefaultColours::BLACK);
    PROPERTY_R(FColour4, groundColour, DefaultColours::WHITE);
    PROPERTY_R(SamplerDescriptor, skyboxSampler);

    void setAtmosphere(const Atmosphere& atmosphere);
    void enableProceduralClouds(bool val);
    void useDaySkybox(bool val);
    void useNightSkybox(bool val);
    void moonScale(F32 val);
    void exposure(F32 val);
    void rayCount(U16 val);
    void moonColour(FColour4 val);
    void nightSkyColour(FColour4 val);
    void groundColour(FColour4 val);

    [[nodiscard]] Handle<Texture> activeSkyBox() const noexcept;

   protected:
    friend class ResourceCache;
    template <typename T> friend struct ResourcePool;

    bool postLoad() override;
    bool unload() override;

   protected:
    void postLoad(SceneGraphNode* sgn) override;

    void sceneUpdate(U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState) override;

    void buildDrawCommands(SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut) override;

    void prepareRender( SceneGraphNode* sgn,
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

    void setSkyShaderData( RenderStagePass renderStagePass, PushConstantsStruct& constantsInOut);

protected:
    Sun _sun;
    Handle<Texture> _skybox{ INVALID_HANDLE<Texture> };
    Handle<Texture> _weatherTex{ INVALID_HANDLE<Texture> };
    Handle<Texture> _curlNoiseTex{ INVALID_HANDLE<Texture> };
    Handle<Texture> _worlNoiseTex{ INVALID_HANDLE<Texture> };
    Handle<Texture> _perlWorlNoiseTex{ INVALID_HANDLE<Texture> };
    Handle<Sphere3D> _sky{ INVALID_HANDLE<Sphere3D> };
    U32  _diameter{1u};
    bool _atmosphereChanged{true};
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(Sky);

}  // namespace Divide

#endif //DVD_SKY_H_
