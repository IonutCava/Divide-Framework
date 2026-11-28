// Copyright (c) 2026 DIVIDE-Studio

#ifndef _OUTPUT_FRAG_
#define _OUTPUT_FRAG_

#if !defined(OIT_PASS)

layout(location = TARGET_ALBEDO) out vec4 _colourOut;

#if defined(ALPHA_TEST_PASS) && defined(MAIN_DISPLAY_PASS)

#include "utility.frag"
#include "velocityCalc.frag"

// Alpha-tested geometry is not in the depth pre-pass, so it writes its own velocity
layout(location = TARGET_VELOCITY) out vec2 _velocityOut;

void writeScreenColour(in vec4 colour, in vec3 transmissionCoefficient, in vec3 normalWV)
{
    _colourOut = colour;
    _velocityOut = velocityCalc();
}

#else //ALPHA_TEST_PASS && MAIN_DISPLAY_PASS

#define writeScreenColour(COLOUR, TRANSMIT, NORMAL) _colourOut = COLOUR

#endif //ALPHA_TEST_PASS && MAIN_DISPLAY_PASS

#else //OIT_PASS

#include "utility.frag"

layout(location = TARGET_ACCUMULATION) out vec4  _accum;
layout(location = TARGET_NORMALS) out vec4 _normalsOut;
layout(location = TARGET_MODULATE) out vec4  _modulate;

DESCRIPTOR_SET_RESOURCE(PER_PASS, 1) uniform sampler2D texDepth;

// Shameless copy-paste from http://casual-effects.blogspot.co.uk/2015/03/colored-blended-order-independent.html
float writePixel(in vec4 colourIn, in vec3 transmissionCoefficient)
{
    vec4 premultipliedReflect = vec4( colourIn.rgb * colourIn.a, colourIn.a );

    // NEW: Perform this operation before modifying the coverage to account for transmission.
    _modulate = vec4(premultipliedReflect.a * (vec3(1.f) - transmissionCoefficient), 0.f);

    //Modulate the net coverage for composition by the transmission. This does not affect the color channels of the
    //transparent surface because the caller's BSDF model should have already taken into account if transmission modulates
    //reflection. See

    //McGuire and Enderton, Colored Stochastic Shadow Maps, ACM I3D, February 2011
    //http://graphics.cs.williams.edu/papers/CSSM/

    //for a full explanation and derivation.
    premultipliedReflect.a *= 1.0f - Saturate((transmissionCoefficient.r + transmissionCoefficient.g + transmissionCoefficient.b) * 0.33f);

    // Weight by linear view depth so nearer layers dominate the average (McGuire and Bavoil 2013, eq. 8, http://jcgt.org/published/0002/02/09/).
    // Window-space depth is too non-linear for this: with a distant far plane it saturates the weight at every depth.
    // The upper clamp keeps the summed HDR colour within half-float range.
    const float viewDepth = 1.f / gl_FragCoord.w;
    const float nearTerm = viewDepth * 0.1f;
    const float farTerm = viewDepth * 0.005f;
    const float farTerm3 = farTerm * farTerm * farTerm;
    const float depthWeight = 10.f / (1e-5f + nearTerm * nearTerm * nearTerm + farTerm3 * farTerm3);
    const float weight = premultipliedReflect.a * clamp(depthWeight, 1e-2f, 3e2f);

    // store pixel color accumulation
    _accum = premultipliedReflect * weight;
    // store pixel revealage threshold
    return premultipliedReflect.a;
}

void writeScreenColour(in vec4 colour, in vec3 transmissionCoefficient, in vec3 normalWV)
{
    const float coverage = writePixel(colour, transmissionCoefficient);
    _normalsOut = vec4(normalWV * coverage, coverage);
}
#endif //OIT_PASS

#endif //_OUTPUT_FRAG_
