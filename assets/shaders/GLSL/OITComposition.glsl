--Fragment

#include "utility.frag"

#if defined(USE_MSAA_TARGET)
#define SamplerType sampler2DMS
#define SampleID gl_SampleID
#else //USE_MSAA_TARGET
#define SamplerType sampler2D
#define SampleID 0
#endif //USE_MSAA_TARGET

/* sum(rgb * a, a) */
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform SamplerType accumTexture;
/* prod(1 - a) */
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 1) uniform SamplerType revealageTexture;

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 2) uniform SamplerType nornmalsTexture;

layout(location = TARGET_ALBEDO)   out vec4 _colourOut;
layout(location = TARGET_NORMALS)  out vec3 _normalsOut;

void main() {
    const ivec2 C = ivec2(gl_FragCoord.xy);

    const float revealage = texelFetch(revealageTexture, C, SampleID).r;
    if (revealage >= ALPHA_DISCARD_THRESHOLD) {
        // Save the blending and color texture fetch cost
        discard;
    }

    vec4 accum = texelFetch(accumTexture, C, SampleID);
    vec3 averageColor  = accum.rgb / max(accum.a, M_EPSILON);
    // dst' =  (accum.rgb / accum.a) * (1 - revealage) + dst
    // [dst has already been modulated by the transmission colors and coverage and the blend mode inverts revealage for us] 
    _colourOut = vec4(averageColor, 1.0f - revealage);
    if ( revealage < 0.5f)
    {
        _normalsOut.rg = texelFetch( nornmalsTexture, C, SampleID ).rg;
    }
}


