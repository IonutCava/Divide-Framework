-- Fragment.PrePass

#include "prePass.frag"
#if defined(USE_ALPHA_DISCARD)
#include "materialData.frag"
#endif //USE_ALPHA_DISCARD

void main() {
    const NodeMaterialData data = dvd_Materials[MATERIAL_IDX];

#if defined(USE_ALPHA_DISCARD)
    if (getAlpha(data, vec3(VAR._texCoord, 0)) < INV_Z_TEST_SIGMA) {
        discard;
    }
#endif //USE_ALPHA_DISCARD

    writeGBuffer(data);
}

-- Fragment.Shadow

#if defined(USE_ALPHA_DISCARD)
#include "materialData.frag"
#endif //USE_ALPHA_DISCARD

void main() {
#if defined(USE_ALPHA_DISCARD)
    const NodeMaterialData data = dvd_Materials[MATERIAL_IDX];
    if (getAlpha(data, vec3(VAR._texCoord, 0)) < INV_Z_TEST_SIGMA) {
        discard;
    }
#endif //USE_ALPHA_DISCARD
}

--Fragment.Shadow.VSM

#include "vsm.frag"
out vec2 _colourOut;

#if defined(USE_ALPHA_DISCARD)
#include "materialData.frag"
#endif //USE_ALPHA_DISCARD

void main() {
#if defined(USE_ALPHA_DISCARD)
    const NodeMaterialData data = dvd_Materials[MATERIAL_IDX];
    if (getAlpha(data, vec3(VAR._texCoord, 0)) < INV_Z_TEST_SIGMA) {
        discard;
    }
#endif //USE_ALPHA_DISCARD

    _colourOut = computeMoments();
}

--Fragment.LineariseDepthBuffer

#include "utility.frag"

layout(binding = TEXTURE_DEPTH) uniform sampler2D texDepth;

//r - ssao, g - linear depth
out float _output;

uniform vec2 zPlanes;

void main() {
    _output = ToLinearDepth(texture(texDepth, VAR._texCoord).r, zPlanes);
}
