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


-- Fragment.ResolveScreen

#include "utility.frag"

layout(binding = TEXTURE_UNIT0)     uniform sampler2DMS colourTex;
layout(binding = TEXTURE_NORMALMAP) uniform sampler2DMS velocityTex;
layout(binding = TEXTURE_HEIGHTMAP) uniform sampler2DMS matDataTex;

layout(location = TARGET_ALBEDO)                    out vec4 _colourOut;
layout(location = TARGET_VELOCITY)                  out vec2 _velocityOut;
layout(location = TARGET_NORMALS_AND_MATERIAL_DATA) out vec4 _matDataOut;

//ToDo: Move this to a compute shader! -Ionut
void main() {
    const ivec2 C = ivec2(gl_FragCoord.xy);
    const int sampleCount = gl_NumSamples;

    { // Average colour and velocity
        vec4 avgColour = vec4(0.f);
        vec2 avgVelocity = vec2(0.f);
        for (int s = 0; s < sampleCount; ++s) {
            avgColour += texelFetch(colourTex, C, s);
            avgVelocity += texelFetch(velocityTex, C, s).rg;
        }
        _colourOut = avgColour / sampleCount;
        _velocityOut = avgVelocity / sampleCount;
    }
    { // Average material data with special consideration for packing and clamping
        vec2 avgNormalData = vec2(0.f);
        vec2 avgMetalnessRoughness = vec2(0.f);
        uint bestProbeID = 0u;
        float bestSign = 0.f;
        for (int s = 0; s < sampleCount; ++s) {
            const vec4 dataIn = texelFetch(matDataTex, C, s);
            const int probeID = int(dataIn.a);
            avgNormalData += dataIn.rg;
            avgMetalnessRoughness += unpackVec2(dataIn.b);
            if (abs(probeID) > bestProbeID) {
                bestProbeID = probeID;
                bestSign = sign(probeID);
            }
        }
        _matDataOut.rg = avgNormalData / sampleCount;
        _matDataOut.b = packVec2(avgMetalnessRoughness / sampleCount);
        _matDataOut.a = bestProbeID * bestSign;
    }
}
