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
#ifndef _SKY_H_
#define _SKY_H_

#include "Sun.h"
#include "Graphs/Headers/SceneNode.h"
#include "Utility/Headers/Colours.h"

namespace Divide {

class GFXDevice;
class RenderStateBlock;
struct PushConstants;

FWD_DECLARE_MANAGED_CLASS(Texture);
FWD_DECLARE_MANAGED_CLASS(Sphere3D);
FWD_DECLARE_MANAGED_CLASS(ShaderProgram);
FWD_DECLARE_MANAGED_CLASS(SceneGraphNode);

enum class RenderStage : U8;

enum class RebuildCommandsState : U8 {
    NONE,
    REQUESTED,
    DONE
};

class Sky final : public SceneNode {
   public:
    explicit Sky(GFXDevice& context, ResourceCache* parentCache, size_t descriptorHash, const Str256& name, U32 diameter);

    static void OnStartup(PlatformContext& context);
    // Returns the sun position and intensity details for the specified date-time
    const SunInfo& setDateTime(struct tm *dateTime) noexcept;
    const SunInfo& setGeographicLocation(SimpleLocation location) noexcept;
    const SunInfo& setDateTimeAndLocation(struct tm *dateTime, SimpleLocation location) noexcept;

    [[nodiscard]] SimpleTime GetTimeOfDay() const noexcept;
    [[nodiscard]] SimpleLocation GetGeographicLocation() const noexcept;

    [[nodiscard]] const SunInfo& getCurrentDetails() const;
    [[nodiscard]] vec3<F32> getSunPosition(F32 radius = 1.f) const;
    [[nodiscard]] bool isDay() const;

    PROPERTY_R(Atmosphere, atmosphere);
    PROPERTY_R(Atmosphere, defaultAtmosphere);
    PROPERTY_R(Atmosphere, initialAtmosphere);
    PROPERTY_R(size_t, skyboxSampler, 0);
    PROPERTY_R(bool, enableProceduralClouds, true);
    PROPERTY_R(bool, useDaySkybox, true);
    PROPERTY_R(bool, useNightSkybox, true);
    PROPERTY_R(F32,  moonScale, 0.5f);
    PROPERTY_R(F32,  weatherScale, 8.f);
    PROPERTY_R(FColour4, moonColour, DefaultColours::WHITE);
    PROPERTY_R(FColour4, nightSkyColour, DefaultColours::BLACK);

    void setAtmosphere(const Atmosphere& atmosphere);
    void enableProceduralClouds(bool val);
    void useDaySkybox(bool val);
    void useNightSkybox(bool val);
    void moonScale(F32 val);
    void weatherScale(F32 val);
    void moonColour(FColour4 val);
    void nightSkyColour(FColour4 val);

    [[nodiscard]] const Texture_ptr& activeSkyBox() const noexcept;

   protected:
    void postLoad(SceneGraphNode* sgn) override;

    void sceneUpdate(U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState) override;

    void buildDrawCommands(SceneGraphNode* sgn, vector_fast<GFX::DrawCommand>& cmdsOut) override;

    void prepareRender( SceneGraphNode* sgn,
                        RenderingComponent& rComp,
                        RenderPackage& pkg,
                        RenderStagePass renderStagePass,
                        const CameraSnapshot& cameraSnapshot,
                        bool refreshData) override;

   protected:
    template <typename T>
    friend class ImplResourceLoader;

    bool load() override;

    [[nodiscard]] const char* getResourceTypeName() const noexcept  override { return "Sky"; }
    void setSkyShaderData(U32 rayCount, PushConstants& constantsInOut);

protected:
    GFXDevice& _context;
    Sun _sun;
    Texture_ptr _skybox{ nullptr };
    Texture_ptr _weatherTex{ nullptr };
    Texture_ptr _curlNoiseTex{ nullptr };
    Texture_ptr _worlNoiseTex{ nullptr };
    Texture_ptr _perWorlNoiseTex{ nullptr };
    Sphere3D_ptr _sky{ nullptr };
    U32  _diameter{1u};
    bool _atmosphereChanged{true};
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(Sky);

}  // namespace Divide

#endif //_SKY_H_
