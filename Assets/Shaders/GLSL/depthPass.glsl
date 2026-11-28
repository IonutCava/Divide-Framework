-- Fragment.PrePass

// Copyright (c) 2026 DIVIDE-Studio

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
#include "alphaTest.frag"
#endif //USE_ALPHA_DISCARD

#if defined(ALPHA_TO_COVERAGE)
// Alpha-to-coverage needs an alpha output at location 0, even without a colour target
layout(location = 0) out vec4 _coverageOut;
#endif //ALPHA_TO_COVERAGE

void main() {
#if defined(USE_ALPHA_DISCARD)
    const NodeMaterialData data = dvd_Materials[MATERIAL_IDX];
    const float coverage = alphaTestCoverage(getAlpha(data, vec3(VAR._texCoord, 0)));
    if (coverage <= 0.f) {
        discard;
    }
#else //USE_ALPHA_DISCARD
    const float coverage = 1.f;
#endif //USE_ALPHA_DISCARD

#if defined(ALPHA_TO_COVERAGE)
    _coverageOut = vec4(0.f, 0.f, 0.f, coverage);
#endif //ALPHA_TO_COVERAGE
}

--Fragment.Shadow.VSM

#if defined(USE_ALPHA_DISCARD)
#include "materialData.frag"
#include "alphaTest.frag"
#endif //USE_ALPHA_DISCARD
#include "vsm.frag"

#if defined(ALPHA_TO_COVERAGE)
// Alpha-to-coverage reads the alpha of output 0. The two-channel moments target ignores it.
layout(location = 0) out vec4 _colourOut;
#else //ALPHA_TO_COVERAGE
layout(location = 0) out vec2 _colourOut;
#endif //ALPHA_TO_COVERAGE

void main() {
    const vec2 moments = computeMoments();
#if defined(USE_ALPHA_DISCARD)
    const NodeMaterialData data = dvd_Materials[MATERIAL_IDX];
    const float coverage = alphaTestCoverage(getAlpha(data, vec3(VAR._texCoord, 0)));
    if (coverage <= 0.f) {
        discard;
    }
#else //USE_ALPHA_DISCARD
    const float coverage = 1.f;
#endif //USE_ALPHA_DISCARD

#if defined(ALPHA_TO_COVERAGE)
    _colourOut = vec4(moments, 0.f, coverage);
#else //ALPHA_TO_COVERAGE
    _colourOut = moments;
#endif //ALPHA_TO_COVERAGE
}

--Fragment.LineariseDepthBuffer

#include "utility.frag"

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texDepth;

//linear depth
layout(location = 0) out float _output;

void main() {
    const float depth = texture(texDepth, VAR._texCoord).r;
    _output = ToLinearDepth(depth, _zPlanes);
}
