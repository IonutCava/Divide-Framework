#ifndef _PRE_PASS_FRAG_
#define _PRE_PASS_FRAG_

#if !defined(PRE_PASS)
#   define PRE_PASS
#endif //!PRE_PASS

// This will also do our check for us
#include "velocityCalc.frag"
#include "nodeBufferedInput.cmn"
#include "utility.frag"
#include "materialData.frag"

layout(location = TARGET_VELOCITY) out vec2 _velocityOut;
layout(location = TARGET_NORMALS)  out vec3 _normalsOut;

float getRoughness(in NodeMaterialData matData, in vec2 uv, in float normalVariation) {
    float roughness = 0.f;

    const vec4 unpackedData = (unpackUnorm4x8(matData._data.z) * 255);
    const uint shadingMode = uint(unpackedData.y);
    const bool usePacked = uint(unpackedData.z) == 1u;

    vec4 OMR_Selection = unpackUnorm4x8(matData._data.x);
    const uvec4 texOpsB = dvd_texOperationsB(matData);
    getTextureRoughness(usePacked, vec3(uv, 0), texOpsB.xyz, OMR_Selection.b);
    roughness = OMR_Selection.b;

    // Deduce a roughness factor from specular colour and shininess
    if (shadingMode != SHADING_PBR_MR) {
        const vec4 specular = getSpecular(matData, vec3(uv, 0));
        const float specularIntensity = Luminance(specular.rgb);
        const float specularPower = specular.a / 1000.f;
        const float roughnessFactor = 1.f - sqrt(specularPower);
        // Specular intensity directly impacts roughness regardless of shininess
        roughness = (1.f - (saturate(pow(roughnessFactor, 2)) * specularIntensity));
    }
    // Try to reduce specular aliasing by increasing roughness when minified normal maps have high variation.
    roughness = mix(roughness, 1.f, normalVariation);

    return roughness;
}

void writeGBuffer(in vec3 normalWV, in float roughness) {
    _velocityOut = velocityCalc();
    _normalsOut.rg = packNormal(normalWV);
    _normalsOut.b = roughness;
}

void writeGBuffer(in NodeMaterialData data) {
    float normalVariation = 0.f;
    const vec3 normalWV = getNormalWV(data, vec3(VAR._texCoord, 0), normalVariation);
    const float roughness = getRoughness(data, VAR._texCoord, normalVariation);

    writeGBuffer(normalWV, roughness);
}

void writeGBuffer() {
    const NodeMaterialData data = dvd_Materials[MATERIAL_IDX];
    writeGBuffer(data);
}

#endif //_PRE_PASS_FRAG_
