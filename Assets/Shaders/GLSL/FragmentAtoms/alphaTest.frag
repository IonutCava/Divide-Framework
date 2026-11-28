// Copyright (c) 2026 DIVIDE-Studio

#ifndef _ALPHA_TEST_FRAG_
#define _ALPHA_TEST_FRAG_

// Coverage in [0, 1] for an alpha-tested fragment. Zero means the fragment is cut out.
// With alpha-to-coverage, alpha is rescaled around the cutoff so the edge spans about one pixel and the hardware turns the
// remaining fraction into a sample mask. Without it, this is a plain alpha test. Call it before any discard: it uses derivatives.
float alphaTestCoverage(in float alpha)
{
#if defined(ALPHA_TO_COVERAGE)
    return Saturate((alpha - ALPHA_DISCARD_THRESHOLD) / max(fwidth(alpha), 1e-4f) + 0.5f);
#else //ALPHA_TO_COVERAGE
    return alpha < ALPHA_DISCARD_THRESHOLD ? 0.f : 1.f;
#endif //ALPHA_TO_COVERAGE
}

// Fades alpha-tested coverage without moving the cutoff edge.
// Alpha-to-coverage turns the fraction into partial sample masks; otherwise a screen-space dither decides.
float alphaTestFade(in float coverage, in float fade)
{
#if defined(ALPHA_TO_COVERAGE)
    return coverage * fade;
#else //ALPHA_TO_COVERAGE
    // Interleaved gradient noise (Jimenez 2014)
    const float dither = fract(52.9829189f * fract(dot(gl_FragCoord.xy, vec2(0.06711056f, 0.00583715f))));
    return fade > dither ? coverage : 0.f;
#endif //ALPHA_TO_COVERAGE
}

#endif //_ALPHA_TEST_FRAG_
