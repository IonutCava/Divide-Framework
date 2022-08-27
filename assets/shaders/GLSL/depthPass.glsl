-- Fragment.PrePass

#include "prePass.frag"
#if defined(USE_ALPHA_DISCARD)
#include "materialData.frag"
#endif //USE_ALPHA_DISCARD

void main() {
    const NodeMaterialData data = dvd_Materials[MATERIAL_IDX];
#if defined(USE_ALPHA_DISCARD)
    if (getAlpha(data, vec3(VAR._texCoord, 0)) < ALPHA_DISCARD_THRESHOLD) {
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
    if (getAlpha(data, vec3(VAR._texCoord, 0)) < ALPHA_DISCARD_THRESHOLD) {
        discard;
    }
#endif //USE_ALPHA_DISCARD
}

--Fragment.Shadow.VSM

#if defined(USE_ALPHA_DISCARD)
#include "materialData.frag"
#endif //USE_ALPHA_DISCARD
#include "vsm.frag"
layout(location = 0) out vec2 _colourOut;

void main() {
#if defined(USE_ALPHA_DISCARD)
    const NodeMaterialData data = dvd_Materials[MATERIAL_IDX];
    if (getAlpha(data, vec3(VAR._texCoord, 0)) < ALPHA_DISCARD_THRESHOLD) {
        discard;
    }
#endif //USE_ALPHA_DISCARD

    _colourOut = computeMoments();
}

--Fragment.LineariseDepthBuffer

#include "utility.frag"

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texDepth;

//linear depth
layout(location = 0) out float _output;

uniform vec2 _zPlanes;

void main() {
    _output = ToLinearDepth(texture(texDepth, VAR._texCoord).r, _zPlanes);
}
