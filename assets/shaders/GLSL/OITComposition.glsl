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
layout(binding = TEXTURE_UNIT0) uniform SamplerType accumTexture;
/* prod(1 - a) */
layout(binding = TEXTURE_UNIT1) uniform SamplerType revealageTexture;

layout(location = TARGET_ALBEDO) out vec4 _colourOut;

void main() {
    const ivec2 C = ivec2(gl_FragCoord.xy);

    const float revealage = texelFetch(revealageTexture, C, SampleID).r;
    if (revealage >= ALPHA_DISCARD_THRESHOLD) {
        // Save the blending and color texture fetch cost
        discard;
    }

    vec4 accum = texelFetch(accumTexture, C, SampleID);
    vec3 averageColor  = accum.rgb / clamp(accum.a, 1e-4f, 5e4f);
    // dst' =  (accum.rgb / accum.a) * (1 - revealage) + dst
    // [dst has already been modulated by the transmission colors and coverage and the blend mode inverts revealage for us] 
    _colourOut = vec4(averageColor, 1.0f - revealage);
}


