#ifndef _VELOCITY_CALC_FRAG_
#define _VELOCITY_CALC_FRAG_

//ref: https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-27-motion-blur-post-processing-effect
#if defined(HAS_VELOCITY)
vec2 velocityCalc() {
    const vec4 vertexWVP = dvd_ProjectionMatrix * VAR._vertexWV;

    const vec2 a = 0.5f * (vertexWVP.xy / vertexWVP.w) + 0.5f;
    const vec2 b = 0.5f * Homogenize(VAR._prevVertexWVP).xy + 0.5f;
    return a - b;
}

#else //HAS_VELOCITY
#define velocityCalc() vec2(0.0f)
#endif //HAS_VELOCITY

#endif //_VELOCITY_CALC_FRAG_
