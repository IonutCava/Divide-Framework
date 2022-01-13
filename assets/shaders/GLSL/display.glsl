-- Fragment

#if !defined(DEPTH_ONLY)
#include "utility.frag"
#endif

layout(binding = TEXTURE_UNIT0) uniform sampler2D tex;
#if !defined(DEPTH_ONLY)
uniform bool convertToSRGB;
out vec4 _colourOut;
#endif

void main(void){
#if !defined(DEPTH_ONLY)
    const vec4 colour = texture(tex, VAR._texCoord);

    _colourOut = convertToSRGB ? ToSRGBAccurate(colour) : colour;
#else
    gl_FragDepth = texture(tex, VAR._texCoord).r;
#endif
}


-- Fragment.ResolveGBuffer

#include "utility.frag"

layout(binding = TEXTURE_UNIT0) uniform sampler2DMS velocityTex;
layout(binding = TEXTURE_UNIT1) uniform sampler2DMS normalsDataTex;

layout(location = TARGET_VELOCITY) out vec2 _velocityOut;
layout(location = TARGET_NORMALS)  out vec3 _normalDataOut;

//ToDo: Move this to a compute shader! -Ionut
void main() {
    const ivec2 C = ivec2(gl_FragCoord.xy);
    const int sampleCount = gl_NumSamples;

    { // Average colour and velocity
        vec2 avgVelocity = vec2(0.f);
        for (int s = 0; s < sampleCount; ++s) {
            avgVelocity += texelFetch(velocityTex, C, s).rg;
        }
        _velocityOut = avgVelocity / sampleCount;
    }
    { // Average material data with special consideration for packing and clamping
        vec3 avgNormalData = vec3(0.f);
        float avgRoughness = 0.f;
        for (int s = 0; s < sampleCount; ++s) {
            const vec3 normalsIn = texelFetch(normalsDataTex, C, s).rgb;
            avgNormalData += unpackNormal(normalsIn.rg);
            avgRoughness += normalsIn.b;
        }
        _normalDataOut.rg = packNormal(avgNormalData / sampleCount);
        _normalDataOut.b = saturate(avgRoughness / sampleCount);
    }
}
