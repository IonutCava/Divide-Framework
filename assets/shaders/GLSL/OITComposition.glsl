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

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 2) uniform SamplerType normalsTexture;

layout(location = TARGET_ALBEDO)   out vec4 _colourOut;
layout(location = TARGET_NORMALS)  out vec4 _normalsOut;

// calculate floating point numbers equality accurately
bool isApproximatelyEqual( in float a, in float b )
{
    return abs( a - b ) <= (abs( a ) < abs( b ) ? abs( b ) : abs( a )) * M_EPSILON;
}

// get the max value between three values
float max3( vec3 v )
{
    return max( max( v.x, v.y ), v.z );
}

// Updated from https://learnopengl.com/Guest-Articles/2020/OIT/Weighted-Blended#:~:text=Weighted%2C%20Blended%20is%20an%20approximate,class%20of%20then%20gaming%20platforms.
void main()
{
    const ivec2 C = ivec2(gl_FragCoord.xy);

    const float revealage = texelFetch(revealageTexture, C, SampleID).r;

    // save the blending and color texture fetch cost if there is not a transparent fragment
    if ( isApproximatelyEqual( revealage, 1.f ) )
    {
        discard;
    }
    // fragment color
    vec4 accumulation = texelFetch(accumTexture, C, SampleID);
    // suppress overflow
    if ( isinf( max3( abs( accumulation.rgb ) ) ) )
    {
        accumulation.rgb = vec3( accumulation.a );
    }

    vec3 averageColor  = accumulation.rgb / max( accumulation.a, M_EPSILON);
    // dst' =  (accumulation.rgb / accumulation.a) * (1 - revealage) + dst
    // [dst has already been modulated by the transmission colors and coverage and the blend mode inverts revealage for us] 

    // blend pixels
    _colourOut = vec4(averageColor, 1.f - revealage);
    if ( revealage < 0.5f)
    {
        _normalsOut.rg = texelFetch( normalsTexture, C, SampleID ).rg;
    }
}


