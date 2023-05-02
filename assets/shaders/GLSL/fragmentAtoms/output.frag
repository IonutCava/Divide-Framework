#ifndef _OUTPUT_FRAG_
#define _OUTPUT_FRAG_

#if !defined(OIT_PASS)

layout(location = TARGET_ALBEDO) out vec4 _colourOut;

#define writeScreenColour(COLOUR, TRANSMIT, NORMAL) _colourOut = COLOUR

#else //OIT_PASS

#include "utility.frag"

layout(location = TARGET_ACCUMULATION) out vec4  _accum;
layout(location = TARGET_REVEALAGE) out float _revealage;
layout(location = TARGET_NORMALS) out vec4 _normalsOut;
layout(location = TARGET_MODULATE) out vec4  _modulate;

DESCRIPTOR_SET_RESOURCE(PER_PASS, 1) uniform sampler2D texDepth;

// Shameless copy-paste from http://casual-effects.blogspot.co.uk/2015/03/colored-blended-order-independent.html
void writePixel(in vec4 colourIn, in vec3 transmissionCoefficient)
{
    vec4 premultipliedReflect = vec4( colourIn.rgb * colourIn.a, colourIn.a );

    // NEW: Perform this operation before modifying the coverage to account for transmission.
    _modulate.rgb = premultipliedReflect.a * (vec3(1.f) - transmissionCoefficient);

    //Modulate the net coverage for composition by the transmission. This does not affect the color channels of the
    //transparent surface because the caller's BSDF model should have already taken into account if transmission modulates
    //reflection. See

    //McGuire and Enderton, Colored Stochastic Shadow Maps, ACM I3D, February 2011
    //http://graphics.cs.williams.edu/papers/CSSM/

    //for a full explanation and derivation.
    premultipliedReflect.a *= 1.0f - Saturate((transmissionCoefficient.r + transmissionCoefficient.g + transmissionCoefficient.b) * 0.33f);
    
    // You may need to adjust the w function if you have a very large or very small view volume; see the paper and
    // presentation slides at http://jcgt.org/published/0002/02/09/ 
    // Intermediate terms to be cubed
#if 1
    const float a = min(1.f, premultipliedReflect.a) * 8.f + 0.01f;
    float b = 1.f - gl_FragCoord.z * 0.95f;
    const float max = 3e2;
    // If your scene has a lot of content very close to the far plane, then include this line (one rsqrt instruction):
    /* const float viewSpaceZ = ViewSpaceZ(texture(texDepth, dvd_screenPositionNormalised).r, inverse(dvd_ProjectionMatrix));
    b /= sqrt(1e4 * abs(viewSpaceZ));*/
#else //https://learnopengl.com/Guest-Articles/2020/OIT/Weighted-Blended#:~:text=Weighted%2C%20Blended%20is%20an%20approximate,class%20of%20then%20gaming%20platforms.

    const float a = min( 1.f, premultipliedReflect.a) * 10.f + 0.01f;
    const float b = 1.f - gl_FragCoord.z * 0.9f;
    const float max = 3e3;
#endif

   // weight function
    const float weight = clamp(pow(a, 3) * 1e8 * pow(b, 3), 1e-2, max );

    // store pixel color accumulation
    _accum = premultipliedReflect * weight;
    // store pixel revealage threshold
    _revealage = premultipliedReflect.a;
}

void writeScreenColour(in vec4 colour, in vec3 transmissionCoefficient, in vec3 normalWV)
{
    writePixel(colour, transmissionCoefficient);

    _normalsOut.rg = packNormal(normalWV);
    _normalsOut.b = 0.f;
    _normalsOut.a = 1.f;
}
#endif //OIT_PASS

#endif //_OUTPUT_FRAG_
