// Copyright (c) 2026 DIVIDE-Studio

#ifndef _PRE_PASS_FRAG_
#define _PRE_PASS_FRAG_

#if !defined(PRE_PASS)
#   define PRE_PASS
#endif //!PRE_PASS

#include "nodeBufferedInput.cmn"
#include "velocityCalc.frag"
#include "utility.frag"

#define NO_OCCLUSION_TEX
#define NO_METALNESS_TEX
#include "materialData.frag"

#if defined(MAIN_DISPLAY_PASS)
layout(location = TARGET_VELOCITY) out vec2 _velocityOut;
layout(location = TARGET_NORMALS)  out vec4 _normalsOut;
#endif //MAIN_DISPLAY_PASS

void writeGBuffer(in vec3 normalWV, in float roughness) {
#if defined(MAIN_DISPLAY_PASS)
    _velocityOut = velocityCalc();
    _normalsOut.rg = packNormal(normalWV);
    _normalsOut.b = roughness;
    _normalsOut.a = 1.f;
#endif //MAIN_DISPLAY_PASS
}

void writeGBuffer(in NodeMaterialData data) {
#if defined(MAIN_DISPLAY_PASS)
    float normalVariation = 0.f;
    const vec3 normalWV = getNormalWV(data, vec3(VAR._texCoord, 0), normalVariation);
    const float roughness = getRoughness(data, VAR._texCoord, normalVariation);

    writeGBuffer(normalWV, roughness);
#endif //MAIN_DISPLAY_PASS
}

#endif //_PRE_PASS_FRAG_
