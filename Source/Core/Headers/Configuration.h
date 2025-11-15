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
#ifndef DVD_CORE_CONFIGURATION_H_
#define DVD_CORE_CONFIGURATION_H_

#include "Utility/Headers/XMLParser.h"

namespace Divide
{

struct Configuration final : XML::IXMLSerializable {
    struct Debug
    {
        struct Renderer
        {
            bool enableRenderAPIDebugging = false;
            bool enableRenderAPIBestPractices = false;
            bool enableRenderAPIDebugGrouping = false;
            bool assertOnRenderAPIError = false;
        } renderer = {};
        struct Cache
        {
            bool enabled = true;
            bool geometry = true;
            bool vegetation = true;
            bool shaders = true;
            bool textureDDS = true;
        } cache = {};
        struct RenderFilter
        {
            bool primitives = true;
            bool meshes = true;
            bool terrain = true;
            bool vegetation = true;
            bool water = true;
            bool sky = true;
            bool particles = true;
            bool decals = true;
            bool treeInstances = true;
            bool grassInstances = true;
        } renderFilter = {};
    } debug = {};
    
    string language = "enGB";
    string serverAddress = "127.0.0.1";
    string startupProject = "Default";
    struct Runtime
    {
        string title = "DIVIDE Framework";
        bool enableEditor = true;
        U8 targetDisplay = 0;
        U8 targetRenderingAPI = 0;
        I16 maxWorkerThreads = -1;
        U8   windowedMode = 0;
        bool windowResizable = false;
        bool maximizeOnStart = true;
        bool enableVSync = true;
        bool adaptiveSync = false;
        bool usePipelineCache = true;
        I16  frameRateLimit = -1;
        vec2<U16> splashScreenSize = { 400, 300 };
        vec2<U16> windowSize = { 1280, 720 };
        vec2<U16> resolution = { 1024, 768 };
        F32 simSpeed = 1.f;
        F32 cameraViewDistance = 1000.0f;
        U8  horizontalFOV = 90u;
    } runtime = {};

    struct GUI
    {
        struct CEGUI
        {
            bool enabled = true;
        } cegui = {};
        string consoleLayoutFile = "console.layout";
    } gui;

    struct ShadowSettings
    {
        U32 shadowMapResolution = 512u;
        U8 MSAASamples = 0u;
        U8 maxAnisotropicFilteringLevel = 0u;
        bool enableBlurring = false;
        bool enabled = true;
    };

    struct Terrain
    {
        U8 detailLevel = 0;
        U8 textureQuality = 0;
        U8 parallaxMode = 0;
        bool wireframe = false;
        bool showNormals = false;
        bool showLoDs = false;
        bool showTessLevels = false;
        bool showBlendMap = false;
    } terrain;

    struct SSAOSettings
    {
        F32  Radius = 0.5f;
        F32  Bias = 0.05f;
        F32  Power = 2.0f;
        U8   KernelSampleCount = 0u;
        bool Blur = true;
        F32  BlurThreshold = 0.05f;
        F32  BlurSharpness = 40.0f;
        I32  BlurKernelSize = 3;
        F32  MaxRange = 1.f;
        F32  FadeDistance = 0.99f;
    };

    struct Rendering
    {
        U8 MSAASamples = 0u;
        U8 maxAnisotropicFilteringLevel = 16;
        U16 reflectionProbeResolution = 256;
        U16 reflectionPlaneResolution = 512;
        I32 numLightsPerCluster = -1;
        bool enableFog = true;
        F32 fogDensity = 0.01f;
        F32 fogScatter = 0.01f;
        float3 fogColour = { 0.2f, 0.2f, 0.2f };
        vec4<U16> lodThresholds = { 25u, 45u, 85u, 165u };
        struct PostFX
        {
            struct PostAA
            {
                string type = "FXAA";
                U8 qualityLevel = 2;
            } postAA;
            struct ToneMap
            {
                bool adaptive = true;
                F32 manualExposureFactor = 1.f;
                F32 minLogLuminance = -4.f;
                F32 maxLogLuminance = 3.f;
                F32 tau = 1.1f;
                string mappingFunction = "UNCHARTED_2";
            } toneMap;
            struct DOF
            {
                bool enabled = false;
                float2 focalPoint = { 0.5f };
                F32 focalDepth = 10.0f;
                F32 focalLength = 12.0f;
                string fStop = "f/1.4";
                F32 ndofstart = 1.0f;
                F32 ndofdist = 2.0f;
                F32 fdofstart = 1.0f;
                F32 fdofdist = 3.0f;
                F32 vignout = 1.3f;
                F32 vignin = 0.0f;
                bool autoFocus = true;
                bool vignetting = false;
                bool debugFocus = false;
                bool manualdof = false;
            } dof;
            struct SSR
            {
                bool enabled = true;
                F32 maxDistance = 100.f;
                F32 jitterAmount = 1.f;
                F32 stride = 8.f;
                F32 zThickness = 1.5f;
                F32 strideZCutoff = 100.f;
                F32 screenEdgeFadeStart = 0.75f;
                F32 eyeFadeStart = 0.5f;
                F32 eyeFadeEnd = 1.f;
                U16 maxSteps = 256u;
                U8 binarySearchIterations = 4u;
            } ssr;
            struct MotionBlur
            {
                bool enablePerObject = true;
                F32 velocityScale = 1.f;
            } motionBlur;
            struct Bloom
            {
                bool enabled = true;
                F32 filterRadius = 0.005f;
                F32 strength = 0.04f;
                bool useThreshold = true;
                F32 threshold = 1.1f;
                F32 knee = 0.1f;
            } bloom;
            struct SSAO
            {
                bool enable = false;
                bool UseHalfResolution = true;
                SSAOSettings FullRes;
                SSAOSettings HalfRes;
            } ssao;
        } postFX = {};
        struct ShadowMapping
        {
            bool enabled = true;
            struct CSMSettings : public ShadowSettings
            {
                F32 splitLambda = 0.95f;
                U8  splitCount = 3u;
            } csm;
            ShadowSettings spot = {};
            ShadowSettings point = {};
        } shadowMapping = {};
    } rendering = {};
    struct DefaultAssetLocation
    {
        string textures = "textures/";
        string shaders = "shaders/";
    } defaultAssetLocation;

    PROPERTY_RW(bool, changed, false);

    void save();

protected:
    bool fromXML(const ResourcePath& xmlFilePath, const char* fileName) override;
    bool toXML(const ResourcePath& xmlFilePath, const char* fileName) const override;
};
}; //namespace Divide

#endif//DVD_CORE_CONFIGURATION_H_
