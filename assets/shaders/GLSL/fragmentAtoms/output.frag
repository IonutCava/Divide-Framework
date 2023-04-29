#ifndef _OUTPUT_FRAG_
#define _OUTPUT_FRAG_

#if !defined(OIT_PASS)

layout(location = TARGET_ALBEDO) out vec4 _colourOut;

#define writeScreenColour(COL, NORM) _colourOut = COL

#else //OIT_PASS

#include "utility.frag"

layout(location = TARGET_ACCUMULATION) out vec4  _accum;
layout(location = TARGET_REVEALAGE) out float _revealage;
layout(location = TARGET_NORMALS) out vec4 _normalsOut;

DESCRIPTOR_SET_RESOURCE(PER_PASS, 1) uniform sampler2D texDepth;

#if defined(USE_COLOURED_WOIT)
layout(location = TARGET_MODULATE) out vec4  _modulate;
#endif

//DESCRIPTOR_SET_RESOURCE(PER_PASS, 2) uniform sampler2D texTransmitance;

// Shameless copy-paste from http://casual-effects.blogspot.co.uk/2015/03/colored-blended-order-independent.html
void writePixel(in vec4 colourIn)
{
#if 0

    const vec3 transmit = vec3( 0.f );//texture(texTransmitance, dvd_screenPositionNormalised).rgb;
#if defined(USE_COLOURED_WOIT)
    // NEW: Perform this operation before modifying the coverage to account for transmission.
    _modulate.rgb = alpha * (vec3(1.f) - transmit);
#endif

    //Modulate the net coverage for composition by the transmission. This does not affect the color channels of the
    //transparent surface because the caller's BSDF model should have already taken into account if transmission modulates
    //reflection. See

    //McGuire and Enderton, Colored Stochastic Shadow Maps, ACM I3D, February 2011
    //http://graphics.cs.williams.edu/papers/CSSM/

    vec4 premultipliedReflect = vec4( colour.rgb * colour.a, colour.a );
    //for a full explanation and derivation.
    //premultipliedReflect.a *= 1.0f - Saturate((transmit.r + transmit.g + transmit.b) * 0.33f);

    // You may need to adjust the w function if you have a very large or very small view volume; see the paper and
    // presentation slides at http://jcgt.org/published/0002/02/09/ 
    // Intermediate terms to be cubed
    const float a = min(1.f, premultipliedReflect.a) * 8.f + 0.01f;
    float b = -gl_FragCoord.z * 0.95f + 1.f;

    // If your scene has a lot of content very close to the far plane, then include this line (one rsqrt instruction):
    const float viewSpaceZ = ViewSpaceZ( texture( texDepth, dvd_screenPositionNormalised ).r, inverse( dvd_ProjectionMatrix ) );
    b /= sqrt(1e4 * abs(viewSpaceZ));

    const float w = clamp(a * a * a * 1e3 * b * b * b, 1e-2, 3e2);

    _accum = premultipliedReflect * w;
    _revealage = premultipliedReflect.a;
#else //https://learnopengl.com/Guest-Articles/2020/OIT/Weighted-Blended#:~:text=Weighted%2C%20Blended%20is%20an%20approximate,class%20of%20then%20gaming%20platforms.
   // weight function
    const float weight = clamp( pow( min( 1.f, colourIn.a * 10.f ) + 0.01f, 3.f ) * 1e8 *
                                pow( 1.f - gl_FragCoord.z * 0.9f, 3.f ), 1e-2, 3e3 );

    // store pixel color accumulation
    _accum = vec4( colourIn.rgb * colourIn.a, colourIn.a ) * weight;

    // store pixel revealage threshold
    _revealage = colourIn.a;
#endif
}

void writeScreenColour(in vec4 colour, in vec3 normalWV)
{
    writePixel(colour);

    _normalsOut.rg = packNormal(normalWV);
    _normalsOut.b = 0.f;
    _normalsOut.a = 1.f;
}
#endif //OIT_PASS

#endif //_OUTPUT_FRAG_
