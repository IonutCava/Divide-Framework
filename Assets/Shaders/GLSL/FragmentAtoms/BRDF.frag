// Copyright (c) 2026 DIVIDE-Studio

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
    const float b = dvd_fogDetails._colourAndDensity.a;
    if (b > M_EPSILON) {
        const float c = dvd_fogDetails._colourSunScatter.a;
        const float rayDir = normalize(VAR._vertexW.xyz - dvd_CameraPosition).y;  // camera to point vector
        const float cameraDistance = distance(VAR._vertexW.xyz, dvd_CameraPosition);    // camera to point distance
        const float fogAmount = c * exp(-dvd_CameraPosition.y * b) * (1.f - exp(-cameraDistance * rayDir * b)) / (rayDir + M_EPSILON);
        return mix(rgb, dvd_fogDetails._colourAndDensity.rgb, fogAmount);
    }

    return rgb;
}
#endif //NO_FOG

vec4 getPixelColour(in vec4 albedo, in NodeMaterialData materialData, in vec3 normalWV,
                    in PBRMaterial material, in vec3 viewVec, in IBLGradients gradients,
                    in vec4 positionDx, in vec4 positionDy) {
    const float NdotV = max(dot(normalWV, viewVec), 0.f);

    vec3 radianceOut = vec3(0.f);
#if defined(MAIN_DISPLAY_PASS)
    if (getDebugColour(material, materialData, VAR._texCoord, normalWV, positionDx, positionDy, gradients, radianceOut))
    {
        return vec4(radianceOut, albedo.a);
    }
#if !defined(PRE_PASS)
    if (VAR._SelectionFlag != SELECTION_FLAG_NONE)
    {
        const vec3 selection_colors[4] = vec3[](
            vec3( 1.00f, 0.00f, 0.00f ), // HOVERED
            vec3( 0.75f, 0.25f, 0.00f ), // PARENT_HOVERED
            vec3( 0.00f, 1.00f, 0.00f ), // SELECTED
            vec3( 0.00f, 0.75f, 0.25f )  // PARENT_SELECTED
        );

        const float NdotV2 = max(dot(normalWV, viewVec), 0.f);
        radianceOut = mix( selection_colors[VAR._SelectionFlag - 1u], radianceOut, smoothstep(0.25f, 0.45f, NdotV2));
    }
    else
    {
        radianceOut += ApplyIBL(material, viewVec, normalWV, NdotV, VAR._vertexW.xyz, dvd_ProbeIndex(materialData), positionDx, positionDy, gradients);
        radianceOut = ApplySSAO(radianceOut);
        radianceOut = ApplySSR(material._roughness, radianceOut);
    }
#endif //!PRE_PASS
#else // MAIN_DISPLAY_PASS
    radianceOut += ApplyIBL(material, viewVec, normalWV, NdotV, VAR._vertexW.xyz, dvd_ProbeIndex(materialData), positionDx, positionDy, gradients);
    radianceOut = ApplySSAO(radianceOut);
    radianceOut = ApplySSR(material._roughness, radianceOut);
#endif //MAIN_DISPLAY_PASS

    radianceOut = getLightContribution(material, normalWV, viewVec, dvd_ReceivesShadows(materialData), positionDx, positionDy, radianceOut);
    radianceOut = ApplyFog(radianceOut);

    return vec4(radianceOut, albedo.a);
}

#endif //_BRDF_FRAG_
