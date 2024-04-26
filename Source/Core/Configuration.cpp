

#include "Headers/Configuration.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

bool Configuration::fromXML(const ResourcePath& xmlFilePath, const char* fileName )
{
    Console::printfn(LOCALE_STR("XML_LOAD_CONFIG"), (xmlFilePath.string() + fileName).c_str());
    if (LoadSave.read(xmlFilePath, fileName, "config."))
    {
        GET_PARAM(debug.renderer.enableRenderAPIDebugging);
        GET_PARAM(debug.renderer.enableRenderAPIBestPractices);
        GET_PARAM(debug.renderer.assertOnRenderAPIError);
        GET_PARAM(debug.renderer.useExtensions);
        GET_PARAM(debug.cache.enabled);
        GET_PARAM(debug.cache.geometry);
        GET_PARAM(debug.cache.vegetation);
        GET_PARAM(debug.cache.shaders);
        GET_PARAM(debug.cache.textureDDS);
        GET_PARAM(debug.renderFilter.primitives);
        GET_PARAM(debug.renderFilter.meshes);
        GET_PARAM(debug.renderFilter.terrain);
        GET_PARAM(debug.renderFilter.vegetation);
        GET_PARAM(debug.renderFilter.water);
        GET_PARAM(debug.renderFilter.sky);
        GET_PARAM(debug.renderFilter.particles);
        GET_PARAM(debug.renderFilter.decals);
        GET_PARAM(debug.renderFilter.treeInstances);
        GET_PARAM(debug.renderFilter.grassInstances);
        GET_PARAM(language);
        GET_PARAM(startupProject);
        GET_PARAM(runtime.title);
        GET_PARAM(runtime.targetDisplay);
        GET_PARAM(runtime.targetRenderingAPI);
        GET_PARAM(runtime.maxWorkerThreads);
        GET_PARAM(runtime.windowedMode);
        GET_PARAM(runtime.windowResizable);
        GET_PARAM(runtime.maximizeOnStart);
        GET_PARAM(runtime.frameRateLimit);
        GET_PARAM(runtime.enableVSync);
        GET_PARAM(runtime.adaptiveSync);
        GET_PARAM(runtime.usePipelineCache);
        GET_PARAM_ATTRIB(runtime.splashScreenSize, width);
        GET_PARAM_ATTRIB(runtime.splashScreenSize, height);
        GET_PARAM_ATTRIB(runtime.windowSize, width);
        GET_PARAM_ATTRIB(runtime.windowSize, height);
        GET_PARAM_ATTRIB(runtime.resolution, width);
        GET_PARAM_ATTRIB(runtime.resolution, height);
        GET_PARAM(runtime.simSpeed);
        GET_PARAM(runtime.cameraViewDistance);
        GET_PARAM(runtime.horizontalFOV);
        GET_PARAM(gui.cegui.enabled);
        GET_PARAM(gui.consoleLayoutFile);
        GET_PARAM(terrain.detailLevel);
        GET_PARAM(terrain.textureQuality);
        GET_PARAM(terrain.parallaxMode);
        GET_PARAM(terrain.wireframe);
        GET_PARAM(terrain.showNormals);
        GET_PARAM(terrain.showLoDs);
        GET_PARAM(terrain.showTessLevels);
        GET_PARAM(terrain.showBlendMap);
        GET_PARAM(rendering.MSAASamples);
        GET_PARAM(rendering.maxAnisotropicFilteringLevel);
        GET_PARAM(rendering.reflectionProbeResolution);
        GET_PARAM(rendering.reflectionPlaneResolution);
        GET_PARAM(rendering.numLightsPerCluster);
        GET_PARAM(rendering.enableFog);
        GET_PARAM(rendering.fogDensity);
        GET_PARAM(rendering.fogScatter);
        GET_PARAM_ATTRIB(rendering.fogColour, r);
        GET_PARAM_ATTRIB(rendering.fogColour, g);
        GET_PARAM_ATTRIB(rendering.fogColour, b);
        GET_PARAM_ATTRIB(rendering.lodThresholds, x);
        GET_PARAM_ATTRIB(rendering.lodThresholds, y);
        GET_PARAM_ATTRIB(rendering.lodThresholds, z);
        GET_PARAM_ATTRIB(rendering.lodThresholds, w);
        GET_PARAM(rendering.postFX.postAA.type);
        GET_PARAM(rendering.postFX.postAA.qualityLevel);
        GET_PARAM(rendering.postFX.toneMap.adaptive);
        GET_PARAM(rendering.postFX.toneMap.manualExposureFactor);
        GET_PARAM(rendering.postFX.toneMap.minLogLuminance);
        GET_PARAM(rendering.postFX.toneMap.maxLogLuminance);
        GET_PARAM(rendering.postFX.toneMap.tau);
        GET_PARAM(rendering.postFX.toneMap.mappingFunction);
        GET_PARAM(rendering.postFX.dof.enabled);
        GET_PARAM_ATTRIB(rendering.postFX.dof.focalPoint, x);
        GET_PARAM_ATTRIB(rendering.postFX.dof.focalPoint, y);
        GET_PARAM(rendering.postFX.dof.focalDepth);
        GET_PARAM(rendering.postFX.dof.focalLength);
        GET_PARAM(rendering.postFX.dof.fStop);
        GET_PARAM(rendering.postFX.dof.ndofstart);
        GET_PARAM(rendering.postFX.dof.ndofdist);
        GET_PARAM(rendering.postFX.dof.fdofstart);
        GET_PARAM(rendering.postFX.dof.fdofdist);
        GET_PARAM(rendering.postFX.dof.vignout);
        GET_PARAM(rendering.postFX.dof.vignin);
        GET_PARAM(rendering.postFX.dof.autoFocus);
        GET_PARAM(rendering.postFX.dof.vignetting);
        GET_PARAM(rendering.postFX.dof.debugFocus);
        GET_PARAM(rendering.postFX.dof.manualdof);
        GET_PARAM(rendering.postFX.ssr.enabled);
        GET_PARAM(rendering.postFX.ssr.maxDistance);
        GET_PARAM(rendering.postFX.ssr.jitterAmount);
        GET_PARAM(rendering.postFX.ssr.stride);
        GET_PARAM(rendering.postFX.ssr.zThickness);
        GET_PARAM(rendering.postFX.ssr.strideZCutoff);
        GET_PARAM(rendering.postFX.ssr.screenEdgeFadeStart);
        GET_PARAM(rendering.postFX.ssr.eyeFadeStart);
        GET_PARAM(rendering.postFX.ssr.eyeFadeEnd);
        GET_PARAM(rendering.postFX.ssr.maxSteps);
        GET_PARAM(rendering.postFX.ssr.binarySearchIterations);
        GET_PARAM(rendering.postFX.motionBlur.enablePerObject);
        GET_PARAM(rendering.postFX.motionBlur.velocityScale);
        GET_PARAM(rendering.postFX.bloom.enabled);
        GET_PARAM(rendering.postFX.bloom.luminanceBias);
        GET_PARAM(rendering.postFX.ssao.enable);
        GET_PARAM(rendering.postFX.ssao.UseHalfResolution);
        GET_PARAM(rendering.postFX.ssao.FullRes.Radius);
        GET_PARAM(rendering.postFX.ssao.FullRes.Bias);
        GET_PARAM(rendering.postFX.ssao.FullRes.Power);
        GET_PARAM(rendering.postFX.ssao.FullRes.KernelSampleCount);
        GET_PARAM(rendering.postFX.ssao.FullRes.Blur);
        GET_PARAM(rendering.postFX.ssao.FullRes.BlurThreshold);
        GET_PARAM(rendering.postFX.ssao.FullRes.BlurSharpness);
        GET_PARAM(rendering.postFX.ssao.FullRes.BlurKernelSize);
        GET_PARAM(rendering.postFX.ssao.FullRes.MaxRange);
        GET_PARAM(rendering.postFX.ssao.FullRes.FadeDistance);
        GET_PARAM(rendering.postFX.ssao.HalfRes.Radius);
        GET_PARAM(rendering.postFX.ssao.HalfRes.Bias);
        GET_PARAM(rendering.postFX.ssao.HalfRes.Power);
        GET_PARAM(rendering.postFX.ssao.HalfRes.KernelSampleCount);
        GET_PARAM(rendering.postFX.ssao.HalfRes.Blur);
        GET_PARAM(rendering.postFX.ssao.HalfRes.BlurThreshold);
        GET_PARAM(rendering.postFX.ssao.HalfRes.BlurSharpness);
        GET_PARAM(rendering.postFX.ssao.HalfRes.BlurKernelSize);
        GET_PARAM(rendering.postFX.ssao.HalfRes.MaxRange);
        GET_PARAM(rendering.postFX.ssao.HalfRes.FadeDistance);
        GET_PARAM(rendering.shadowMapping.enabled);
        GET_PARAM(rendering.shadowMapping.csm.enabled);
        GET_PARAM(rendering.shadowMapping.csm.shadowMapResolution);
        GET_PARAM(rendering.shadowMapping.csm.MSAASamples);
        GET_PARAM(rendering.shadowMapping.csm.enableBlurring);
        GET_PARAM(rendering.shadowMapping.csm.splitLambda);
        GET_PARAM(rendering.shadowMapping.csm.splitCount);
        GET_PARAM(rendering.shadowMapping.csm.maxAnisotropicFilteringLevel);
        GET_PARAM(rendering.shadowMapping.spot.enabled);
        GET_PARAM(rendering.shadowMapping.spot.shadowMapResolution);
        GET_PARAM(rendering.shadowMapping.spot.MSAASamples);
        GET_PARAM(rendering.shadowMapping.spot.enableBlurring);
        GET_PARAM(rendering.shadowMapping.spot.maxAnisotropicFilteringLevel);
        GET_PARAM(rendering.shadowMapping.point.enabled);
        GET_PARAM(rendering.shadowMapping.point.shadowMapResolution);
        GET_PARAM(defaultAssetLocation.textures);
        GET_PARAM(defaultAssetLocation.shaders);

        if ( rendering.shadowMapping.enabled &&
            !rendering.shadowMapping.csm.enabled &&
            !rendering.shadowMapping.spot.enabled &&
            !rendering.shadowMapping.point.enabled)
        {
            rendering.shadowMapping.enabled = false;
        }

        return true;
    }

    return false;
}

bool Configuration::toXML(const ResourcePath& xmlFilePath, const char* fileName ) const
{
    PUT_PARAM(debug.renderer.enableRenderAPIDebugging);
    PUT_PARAM(debug.renderer.enableRenderAPIBestPractices);
    PUT_PARAM(debug.renderer.assertOnRenderAPIError);
    PUT_PARAM(debug.renderer.useExtensions);
    PUT_PARAM(debug.cache.enabled);
    PUT_PARAM(debug.cache.geometry);
    PUT_PARAM(debug.cache.vegetation);
    PUT_PARAM(debug.cache.shaders);
    PUT_PARAM(debug.cache.textureDDS);
    PUT_PARAM(debug.renderFilter.primitives);
    PUT_PARAM(debug.renderFilter.meshes);
    PUT_PARAM(debug.renderFilter.terrain);
    PUT_PARAM(debug.renderFilter.vegetation);
    PUT_PARAM(debug.renderFilter.water);
    PUT_PARAM(debug.renderFilter.sky);
    PUT_PARAM(debug.renderFilter.particles);
    PUT_PARAM(debug.renderFilter.decals);
    PUT_PARAM(debug.renderFilter.treeInstances);
    PUT_PARAM(debug.renderFilter.grassInstances);
    PUT_PARAM(language);
    PUT_PARAM(startupProject);
    PUT_PARAM(runtime.title);
    PUT_PARAM(runtime.targetDisplay);
    PUT_PARAM(runtime.targetRenderingAPI);
    PUT_PARAM(runtime.maxWorkerThreads);
    PUT_PARAM(runtime.windowedMode);
    PUT_PARAM(runtime.windowResizable);
    PUT_PARAM(runtime.maximizeOnStart);
    PUT_PARAM(runtime.frameRateLimit);
    PUT_PARAM(runtime.enableVSync);
    PUT_PARAM(runtime.adaptiveSync);
    PUT_PARAM(runtime.usePipelineCache);
    PUT_PARAM_ATTRIB(runtime.splashScreenSize, width);
    PUT_PARAM_ATTRIB(runtime.splashScreenSize, height);
    PUT_PARAM_ATTRIB(runtime.windowSize, width);
    PUT_PARAM_ATTRIB(runtime.windowSize, height);
    PUT_PARAM_ATTRIB(runtime.resolution, width);
    PUT_PARAM_ATTRIB(runtime.resolution, height);
    PUT_PARAM(runtime.simSpeed);
    PUT_PARAM(runtime.cameraViewDistance);
    PUT_PARAM(runtime.horizontalFOV);
    PUT_PARAM(gui.cegui.enabled);
    PUT_PARAM(gui.consoleLayoutFile);
    PUT_PARAM(terrain.detailLevel);
    PUT_PARAM(terrain.textureQuality);
    PUT_PARAM(terrain.parallaxMode);
    PUT_PARAM(terrain.wireframe);
    PUT_PARAM(terrain.showNormals);
    PUT_PARAM(terrain.showLoDs);
    PUT_PARAM(terrain.showTessLevels);
    PUT_PARAM(terrain.showBlendMap);
    PUT_PARAM(rendering.MSAASamples);
    PUT_PARAM(rendering.maxAnisotropicFilteringLevel);
    PUT_PARAM(rendering.reflectionPlaneResolution);
    PUT_PARAM(rendering.numLightsPerCluster);
    PUT_PARAM(rendering.enableFog);
    PUT_PARAM(rendering.fogDensity);
    PUT_PARAM(rendering.fogScatter);
    PUT_PARAM_ATTRIB(rendering.fogColour, r);
    PUT_PARAM_ATTRIB(rendering.fogColour, g);
    PUT_PARAM_ATTRIB(rendering.fogColour, b);
    PUT_PARAM_ATTRIB(rendering.lodThresholds, x);
    PUT_PARAM_ATTRIB(rendering.lodThresholds, y);
    PUT_PARAM_ATTRIB(rendering.lodThresholds, z);
    PUT_PARAM_ATTRIB(rendering.lodThresholds, w);
    PUT_PARAM(rendering.postFX.postAA.type);
    PUT_PARAM(rendering.postFX.postAA.qualityLevel);
    PUT_PARAM(rendering.postFX.toneMap.adaptive);
    PUT_PARAM(rendering.postFX.toneMap.manualExposureFactor);
    PUT_PARAM(rendering.postFX.toneMap.minLogLuminance);
    PUT_PARAM(rendering.postFX.toneMap.maxLogLuminance);
    PUT_PARAM(rendering.postFX.toneMap.tau);
    PUT_PARAM(rendering.postFX.toneMap.mappingFunction);
    PUT_PARAM(rendering.postFX.dof.enabled);
    PUT_PARAM_ATTRIB(rendering.postFX.dof.focalPoint, x);
    PUT_PARAM_ATTRIB(rendering.postFX.dof.focalPoint, y);
    PUT_PARAM(rendering.postFX.dof.focalDepth);
    PUT_PARAM(rendering.postFX.dof.focalLength);
    PUT_PARAM(rendering.postFX.dof.fStop);
    PUT_PARAM(rendering.postFX.dof.ndofstart);
    PUT_PARAM(rendering.postFX.dof.ndofdist);
    PUT_PARAM(rendering.postFX.dof.fdofstart);
    PUT_PARAM(rendering.postFX.dof.fdofdist);
    PUT_PARAM(rendering.postFX.dof.vignout);
    PUT_PARAM(rendering.postFX.dof.vignin);
    PUT_PARAM(rendering.postFX.dof.autoFocus);
    PUT_PARAM(rendering.postFX.dof.vignetting);
    PUT_PARAM(rendering.postFX.dof.debugFocus);
    PUT_PARAM(rendering.postFX.dof.manualdof);
    PUT_PARAM(rendering.postFX.ssr.enabled);
    PUT_PARAM(rendering.postFX.ssr.maxDistance);
    PUT_PARAM(rendering.postFX.ssr.jitterAmount);
    PUT_PARAM(rendering.postFX.ssr.stride);
    PUT_PARAM(rendering.postFX.ssr.zThickness);
    PUT_PARAM(rendering.postFX.ssr.strideZCutoff);
    PUT_PARAM(rendering.postFX.ssr.screenEdgeFadeStart);
    PUT_PARAM(rendering.postFX.ssr.eyeFadeStart);
    PUT_PARAM(rendering.postFX.ssr.eyeFadeEnd);
    PUT_PARAM(rendering.postFX.ssr.maxSteps);
    PUT_PARAM(rendering.postFX.ssr.binarySearchIterations);
    PUT_PARAM(rendering.postFX.motionBlur.enablePerObject);
    PUT_PARAM(rendering.postFX.motionBlur.velocityScale);
    PUT_PARAM(rendering.postFX.bloom.enabled);
    PUT_PARAM(rendering.postFX.bloom.luminanceBias);
    PUT_PARAM(rendering.postFX.ssao.enable);
    PUT_PARAM(rendering.postFX.ssao.UseHalfResolution);
    PUT_PARAM(rendering.postFX.ssao.FullRes.Radius);
    PUT_PARAM(rendering.postFX.ssao.FullRes.Bias);
    PUT_PARAM(rendering.postFX.ssao.FullRes.Power);
    PUT_PARAM(rendering.postFX.ssao.FullRes.KernelSampleCount);
    PUT_PARAM(rendering.postFX.ssao.FullRes.Blur);
    PUT_PARAM(rendering.postFX.ssao.FullRes.BlurThreshold);
    PUT_PARAM(rendering.postFX.ssao.FullRes.BlurSharpness);
    PUT_PARAM(rendering.postFX.ssao.FullRes.BlurKernelSize);
    PUT_PARAM(rendering.postFX.ssao.FullRes.MaxRange);
    PUT_PARAM(rendering.postFX.ssao.FullRes.FadeDistance);
    PUT_PARAM(rendering.postFX.ssao.HalfRes.Radius);
    PUT_PARAM(rendering.postFX.ssao.HalfRes.Bias);
    PUT_PARAM(rendering.postFX.ssao.HalfRes.Power);
    PUT_PARAM(rendering.postFX.ssao.HalfRes.KernelSampleCount);
    PUT_PARAM(rendering.postFX.ssao.HalfRes.Blur);
    PUT_PARAM(rendering.postFX.ssao.HalfRes.BlurThreshold);
    PUT_PARAM(rendering.postFX.ssao.HalfRes.BlurSharpness);
    PUT_PARAM(rendering.postFX.ssao.HalfRes.BlurKernelSize);
    PUT_PARAM(rendering.postFX.ssao.HalfRes.MaxRange);
    PUT_PARAM(rendering.postFX.ssao.HalfRes.FadeDistance);
    PUT_PARAM(rendering.shadowMapping.enabled);
    PUT_PARAM(rendering.shadowMapping.csm.enabled);
    PUT_PARAM(rendering.shadowMapping.csm.shadowMapResolution);
    PUT_PARAM(rendering.shadowMapping.csm.MSAASamples);
    PUT_PARAM(rendering.shadowMapping.csm.enableBlurring);
    PUT_PARAM(rendering.shadowMapping.csm.splitLambda);
    PUT_PARAM(rendering.shadowMapping.csm.splitCount);
    PUT_PARAM(rendering.shadowMapping.csm.maxAnisotropicFilteringLevel);
    PUT_PARAM(rendering.shadowMapping.spot.enabled);
    PUT_PARAM(rendering.shadowMapping.spot.shadowMapResolution);
    PUT_PARAM(rendering.shadowMapping.spot.MSAASamples);
    PUT_PARAM(rendering.shadowMapping.spot.enableBlurring);
    PUT_PARAM(rendering.shadowMapping.spot.maxAnisotropicFilteringLevel);
    PUT_PARAM(rendering.shadowMapping.point.enabled);
    PUT_PARAM(rendering.shadowMapping.point.shadowMapResolution);
    PUT_PARAM(defaultAssetLocation.textures);
    PUT_PARAM(defaultAssetLocation.shaders);

    return LoadSave.write( xmlFilePath, fileName );
}

void Configuration::save()
{
    if (changed() && toXML(LoadSave._filePath, LoadSave._fileName.c_str()))
    {
        changed(false);
    }
}
}; //namespace Divide
