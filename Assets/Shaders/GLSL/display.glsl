--Fragment

#if !defined(DEPTH_ONLY)
#include "utility.frag"
#endif

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D tex;
#if defined(DEPTH_ONLY)
#else //DEPTH_ONLY
layout(location = 0) out vec4 _colourOut;
#endif//DEPTH_ONLY

void main(void){
#if !defined(DEPTH_ONLY)
    const vec4 colour = texture(tex, VAR._texCoord);

    _colourOut = convertToSRGB != 0u ? vec4(ToSRGBAccurate(colour.rgb), colour.a) : colour;
#else
    gl_FragDepth = texture(tex, VAR._texCoord).r;
#endif
}

-- Fragment.ResolveGBuffer

#include "utility.frag"

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2DMS normalsDataTex;

layout(location = 0) out vec3 _normalDataOut;

//ToDo: Move this to a compute shader! -Ionut
void main() {
    const ivec2 C = ivec2(gl_FragCoord.xy);

    vec3 avgNormalData = vec3(0.f);
    float avgRoughness = 0.f;
    for (int s = 0; s < NUM_SAMPLES; ++s)
    {
        const vec3 normalsIn = texelFetch(normalsDataTex, C, s).rgb;
        avgNormalData += unpackNormal(normalsIn.rg);
        avgRoughness += normalsIn.b;
    }
    _normalDataOut.rg = packNormal(avgNormalData / NUM_SAMPLES);
    _normalDataOut.b = Saturate(avgRoughness / NUM_SAMPLES);
}
