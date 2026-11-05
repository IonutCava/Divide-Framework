-- Vertex

// Copyright (c) 2026 DIVIDE-Studio

#define COMPUTE_TBN
#include "vbInputData.vert"
#include "lightingDefaults.vert"

layout(location = ATTRIB_FREE_START + 0) out flat int _underwater;
layout(location = ATTRIB_FREE_START + 1) out vec4 _vertexWVP;

void main(void)
{
    const NodeTransformData data = fetchInputData();
    _vertexWVP = computeData(data);
    setClipPlanes();
    computeLightVectors(data);

    _underwater = dvd_CameraPosition.y < VAR._vertexW.y ? 1 : 0;
    gl_Position = _vertexWVP;
}

-- Fragment

layout(location = ATTRIB_FREE_START + 0) in flat int _underwater;
layout(location = ATTRIB_FREE_START + 1) in vec4 _vertexWVP;

#define NO_OMR_TEX
#define NO_ENV_MAPPING
#define NO_SSAO

#if defined(PRE_PASS)
#include "prePass.frag"
#else //PRE_PASS
#include "BRDF.frag"
#include "output.frag"
#endif //PRE_PASS

#include "bumpMapping.frag"
#include "waterComposite.cmn"

void main()
{
#if !defined(PRE_PASS)
    const vec4 positionDx = dFdx(VAR._vertexW);
    const vec4 positionDy = dFdy(VAR._vertexW);
#endif
#if defined(MAIN_DISPLAY_PASS) && !defined(PRE_PASS) && !defined(OIT_PASS) && !defined(TRANSPARENCY_PASS)
    const vec3 normalWV = normalize(unpackNormal(sampleTexSceneNormals().rg));
#else
    const float time2 = MSToSeconds(dvd_GameTimeMS) * 0.05f;
    const vec2 uvNormal0 = (VAR._texCoord * _noiseTile) + vec2( time2, time2);
    const vec2 uvNormal1 = (VAR._texCoord * _noiseTile) + vec2(-time2, time2);

    const vec3 normal0 = getNormalMap(texNormalMap, vec3(uvNormal0, 0));
    const vec3 normal1 = getNormalMap(texNormalMap, vec3(uvNormal1, 0));

    vec3 normal = normalPartialDerivativesBlend(normal0, normal1);
    const vec3 normalWV = normalize(getTBNWV() * normal);
#endif

    NodeMaterialData data = dvd_Materials[MATERIAL_IDX];

#if !defined(PRE_PASS)
    const vec3 viewWV = computeViewDirectionWV();
    const float NdotV = max(dot(normalWV, viewWV), 0.f);
    const IBLGradients gradients = IBL_GRADIENTS(vec3(0.f), vec2(0.f));
#if defined(MAIN_DISPLAY_PASS)
#if defined(OIT_PASS) || defined(TRANSPARENCY_PASS)
    const float waterRoughness = getRoughness(data, VAR._texCoord, 0.f);
#else
    const float waterRoughness = sampleTexSceneNormals().b;
#endif
#endif
    const vec3 normalW = normalize(mat3(dvd_InverseViewMatrix) * normalWV);
    const vec3 incident = normalize(dvd_CameraPosition - VAR._vertexW.xyz);

    const vec2 waterUV = clamp(0.5f * Homogenize(_vertexWVP).xy + 0.5f, vec2(0.001f), vec2(0.999f));
#if !defined(SKIP_REFLECT_REFRACT) && (defined(USE_PLANAR_REFLECTOR) || defined(USE_PLANAR_REFRACTOR))
    const vec2 waveOffset = WATER_WAVE_OFFSET(normalWV.xy, normalize(VAR._normalWV).xy, _noiseFactor);
    const vec2 distortedUV = clamp(waterUV + waveOffset, vec2(0.001f), vec2(0.999f));
#endif
#if !defined(SKIP_REFLECT_REFRACT) && defined(USE_PLANAR_REFLECTOR)
    const vec2 reflectionUVDx = dFdx(distortedUV);
    const vec2 reflectionUVDy = dFdy(distortedUV);
#endif
#if !defined(SKIP_REFLECT_REFRACT) && defined(USE_PLANAR_REFRACTOR)
    const vec3 refractColour = texture(texRefract, distortedUV).rgb;
#else
    const vec3 refractColour = vec3(0.f);
#endif
    const vec3 refractionColour = overlayVec( refractColour, _refractionTint);

#if defined(MAIN_DISPLAY_PASS)
    switch (dvd_MaterialDebugFlag) {
        case DEBUG_REFRACTIONS:
            writeScreenColour(vec4(refractionColour, 1.f), vec3( 0.f ), normalWV );
            return;
        case DEBUG_REFLECTIONS:
#if !defined(SKIP_REFLECT_REFRACT) && defined(USE_PLANAR_REFLECTOR)
            writeScreenColour(textureGrad(texReflect, distortedUV, reflectionUVDx, reflectionUVDy), vec3( 0.f ), normalWV );
#else
            writeScreenColour(vec4(0.f, 0.f, 0.f, 1.f), vec3( 0.f ), normalWV );
#endif
            return;
    }
#endif //MAIN_DISPLAY_PASS

    vec3 surfaceColour = dvd_BaseColour(data).rgb;
#if !defined(SKIP_REFLECT_REFRACT) && defined(USE_PLANAR_REFRACTOR)
    surfaceColour = refractionColour;
#if defined(USE_PLANAR_REFLECTOR)
    if (_underwater == 0)
    {
        vec3 reflectionColour = textureGrad(texReflect, distortedUV, reflectionUVDx, reflectionUVDy).rgb;
#if defined(MAIN_DISPLAY_PASS)
        reflectionColour = ApplySSR(waterRoughness, reflectionColour);
#endif
        const float cosine = Saturate(dot(incident, normalW));
        const float fresnel = WATER_FRESNEL(cosine);
        surfaceColour = WATER_COMPOSITE(refractionColour, reflectionColour, fresnel);
    }
#endif
#elif !defined(SKIP_REFLECT_REFRACT) && defined(USE_PLANAR_REFLECTOR)
    if (_underwater == 1)
    {
        const vec4 baseColour = dvd_BaseColour(data);
        const PBRMaterial material = initMaterialProperties(data, baseColour.rgb, VAR._texCoord, normalWV, NdotV);
        writeScreenColour(getPixelColour(baseColour, data, normalWV, material, viewWV, gradients, positionDx, positionDy), vec3(0.f), normalWV);
        return;
    }
    surfaceColour = textureGrad(texReflect, distortedUV, reflectionUVDx, reflectionUVDy).rgb;
#if defined(MAIN_DISPLAY_PASS)
    surfaceColour = ApplySSR(waterRoughness, surfaceColour);
#endif
#else
    const vec4 baseColour = dvd_BaseColour(data);
    const PBRMaterial material = initMaterialProperties(data, baseColour.rgb, VAR._texCoord, normalWV, NdotV);
    writeScreenColour(getPixelColour(baseColour, data, normalWV, material, viewWV, gradients, positionDx, positionDy), vec3(0.f), normalWV);
    return;
#endif

#if defined(MAIN_DISPLAY_PASS)
    if (dvd_MaterialDebugFlag != DEBUG_NONE)
    {
        const PBRMaterial material = initMaterialProperties(data, surfaceColour, VAR._texCoord, normalWV, NdotV);
        vec3 debugColour;
        if (getDebugColour(material, data, VAR._texCoord, normalWV, positionDx, positionDy, gradients, debugColour))
        {
            writeScreenColour(vec4(debugColour, 1.f), vec3(0.f), normalWV);
            return;
        }
    }
    if (VAR._SelectionFlag != SELECTION_FLAG_NONE)
    {
        const vec3 selectionColours[4] = vec3[](
            vec3(1.f, 0.f, 0.f), vec3(0.75f, 0.25f, 0.f),
            vec3(0.f, 1.f, 0.f), vec3(0.f, 0.75f, 0.25f)
        );
        surfaceColour = mix(selectionColours[VAR._SelectionFlag - 1u], surfaceColour, smoothstep(0.25f, 0.45f, NdotV));
    }
#endif

    vec4 outColour = vec4(ApplyFog(surfaceColour), 1.f);

    // Add some distance based fog to the water to hide reflection/refraction artifacts where no geometry is rendered
    const float fogDensity = smoothstep(_fogStartEndDistances.x, _fogStartEndDistances.y, length(VAR._vertexWV));
    //const float fogDensity = smoothstep(1.1f, 2.1f, fragDepth);
    outColour.rgb = mix(outColour.rgb, _waterDistanceFogColour, fogDensity);

    // Guess work based on what "look right"
    const vec3 sunDirection = -GetSunFWDDirection();
    const float lerpValue = Saturate(2.95f * (-sunDirection.y + 0.15f));
    const vec3 lightDirection = mix(sunDirection, -sunDirection, lerpValue);

    // Calculate the reflection vector using the normal and the direction of the light.
    const vec3 reflection = -reflect(lightDirection, normalW);
    // Calculate the specular light based on the reflection and the camera position.
    const float specular = dot(normalize(reflection), incident);
    // Increase the specular light by the shininess value and add the specular to the final color.
    // Check to make sure the specular was positive so we aren't adding black spots to the water.

    outColour.rgb += specular > 0.f ? pow(specular, _specularShininess) : 0.f;
    writeScreenColour(outColour, vec3( 0.f ), normalWV );

#else //!PRE_PASS
    const float roughness = dvd_OMR(data).b;
    writeGBuffer(normalWV, roughness);
#endif //PRE_PASS
}
