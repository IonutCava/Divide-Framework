-- Fragment.PrePass

#include "prePass.frag"

void main() {
#if defined(HAS_TRANSPARENCY)
    NodeMaterialData data = dvd_Materials[MATERIAL_IDX];
    const float alpha = getAlpha(data, vec3(VAR._texCoord, 0));
    writeGBuffer(alpha);
#else //HAS_TRANSPARENCY
    writeGBuffer();
#endif //HAS_TRANSPARENCY
}

-- Fragment.Shadow

#if defined(HAS_TRANSPARENCY)
#include "materialData.frag"
#endif //HAS_TRANSPARENCY

void main() {
#if defined(HAS_TRANSPARENCY)
    NodeMaterialData data = dvd_Materials[MATERIAL_IDX];
    if (getAlpha(data, vec3(VAR._texCoord, 0)).a < INV_Z_TEST_SIGMA) {
        discard;
    }
#endif //HAS_TRANSPARENCY
}

--Fragment.Shadow.VSM

#include "vsm.frag"
out vec2 _colourOut;

#if defined(HAS_TRANSPARENCY)
#include "materialData.frag"
#endif //HAS_TRANSPARENCY

void main() {
#if defined(HAS_TRANSPARENCY)
    const NodeMaterialData data = dvd_Materials[MATERIAL_IDX];
    if (getAlpha(data, vec3(VAR._texCoord, 0)) < INV_Z_TEST_SIGMA) {
        discard;
    }
#endif //HAS_TRANSPARENCY

    _colourOut = computeMoments();
}

--Fragment.LineariseDepthBuffer

#include "utility.frag"

layout(binding = TEXTURE_DEPTH) uniform sampler2D texDepth;

//r - ssao, g - linear depth
out vec2 _output;
#define _colourOut _output.g

uniform vec2 zPlanes;

void main() {
    _colourOut = ToLinearDepth(texture(texDepth, VAR._texCoord).r, zPlanes);
}
