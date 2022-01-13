#ifndef _BRDF_FRAG_
#define _BRDF_FRAG_

#include "materialData.frag"
#if defined(MAIN_DISPLAY_PASS)
#include "debug.frag"
#else //MAIN_DISPLAY_PASS
#include "lightingCalc.frag"
#endif //MAIN_DISPLAY_PASS

//https://iquilezles.org/www/articles/fog/fog.htm
#if defined(NO_FOG)
#define ApplyFog(R) (R)
#else //NO_FOG
vec3 ApplyFog(in vec3 rgb) // original color of the pixel
{
    if (dvd_fogEnabled) {
        const float c = dvd_fogDetails._colourSunScatter.a;
        const float b = dvd_fogDetails._colourAndDensity.a;
        const float rayDir = normalize(VAR._vertexW.xyz - dvd_cameraPosition.xyz).y;  // camera to point vector
        const float distance = distance(VAR._vertexW.xyz, dvd_cameraPosition.xyz);    // camera to point distance
        const float fogAmount = c * exp(-dvd_cameraPosition.y * b) * (1.f - exp(-distance * rayDir * b)) / (rayDir + M_EPSILON);
        return mix(rgb, dvd_fogDetails._colourAndDensity.rgb, fogAmount);
    }

    return rgb;
}
#endif //NO_FOG

vec4 getPixelColour(in vec4 albedo, in NodeMaterialData materialData, in vec3 normalWV) {
    const vec2 uv = VAR._texCoord;
    const vec3 viewVec = normalize(VAR._viewDirectionWV);
    const float NdotV = max(dot(normalWV, viewVec), 0.f);
    const PBRMaterial material = initMaterialProperties(materialData, albedo.rgb, uv, normalWV, NdotV);

    vec3 radianceOut = vec3(0.f);
#if defined(MAIN_DISPLAY_PASS)
    if (getDebugColour(material, materialData, uv, normalWV, radianceOut)) {
        return vec4(radianceOut, albedo.a);
    }
#if !defined(PRE_PASS)
    if (SELECTION_FLAG > 0) {
        const float NdotV2 = max(dot(VAR._normalWV, viewVec), 0.f);
        const vec3 lineColour = vec3(SELECTION_FLAG == 1 ? 1.f : 0f, SELECTION_FLAG == 2 ? 1.f : 0.f, 0.f);
        radianceOut = mix(lineColour, radianceOut, smoothstep(0.25f, 0.45f, NdotV2));
    }
#endif //!PRE_PASS
#endif //MAIN_DISPLAY_PASS

    radianceOut+= ApplyIBL(material, viewVec, normalWV, NdotV, VAR._vertexW.xyz, dvd_probeIndex(materialData));
    radianceOut = ApplySSR(material._roughness, radianceOut);

    radianceOut = getLightContribution(material, normalWV, viewVec, dvd_receivesShadows(materialData), radianceOut);
    radianceOut = ApplyFog(radianceOut);
    radianceOut = ApplySSAO(radianceOut);

    return vec4(radianceOut, albedo.a);
}

#endif //_BRDF_FRAG_
