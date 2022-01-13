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

layout(location = TARGET_VELOCITY) out vec3 _velocityOut;
layout(location = TARGET_NORMALS)  out vec3 _normalsOut;

void writeGBuffer(in vec3 normalWV, in float roughness) {
    _velocityOut.rg = velocityCalc();
    _velocityOut.b = float(SELECTION_FLAG);
    _normalsOut.rg = packNormal(normalWV);
    _normalsOut.b = roughness;
}

void writeGBuffer(in NodeMaterialData data) {
    float normalVariation = 0.f;
    const vec3 normalWV = getNormalWV(data, vec3(VAR._texCoord, 0), normalVariation);
    const float roughness = getRoughness(data, VAR._texCoord, normalVariation);

    writeGBuffer(normalWV, roughness);
}

#endif //_PRE_PASS_FRAG_
