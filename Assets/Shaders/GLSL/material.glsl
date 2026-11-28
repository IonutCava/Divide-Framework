-- Fragment

// Copyright (c) 2026 DIVIDE-Studio

#if !defined(ALPHA_TEST_PASS)
// Forced early tests would write depth before an alpha test could discard the fragment
layout(early_fragment_tests) in;
#endif //!ALPHA_TEST_PASS

#include "output.frag"
#include "BRDF.frag"
#include "transparentCoverage.cmn"
#if defined(ALPHA_TEST_PASS)
#include "alphaTest.frag"
#endif //ALPHA_TEST_PASS

void main (void) {
    const vec4 positionDx = dFdx(VAR._vertexW);
    const vec4 positionDy = dFdy(VAR._vertexW);
    NodeMaterialData data = dvd_Materials[MATERIAL_IDX];

    const vec3 texCoord = vec3( VAR._texCoord, 0 );

    vec4 albedo = getAlbedo(data, texCoord);

#if defined(ALPHA_TEST_PASS)
    albedo.a = alphaTestCoverage(albedo.a);
    if (albedo.a <= 0.f)
    {
        discard;
    }
#elif defined(OIT_PASS) || defined(TRANSPARENCY_PASS)
    if (!TRANSPARENT_HAS_COVERAGE(albedo.a))
    {
        discard;
    }
#endif

    const vec3 normalWV = getNormalWV( data, texCoord );
    const vec3 viewVec = computeViewDirectionWV();
    const float NdotV = max(dot(normalWV, viewVec), 0.f);
    const PBRMaterial material = initMaterialProperties(data, albedo.rgb, VAR._texCoord, normalWV, NdotV);
    const vec3 iblNormalW = normalize(mat3(dvd_InverseViewMatrix) * normalWV);
    const vec2 iblBRDFCoords = vec2(NdotV, material._roughness);
    const IBLGradients gradients = IBL_GRADIENTS(iblNormalW, iblBRDFCoords);

#if defined(USE_ALPHA_DISCARD)
    if (albedo.a < ALPHA_DISCARD_THRESHOLD)
    {
        discard;
    }
#endif
    writeScreenColour( getPixelColour( albedo, data, normalWV, material, viewVec, gradients, positionDx, positionDy ), vec3( 0.f ), normalWV );
}
