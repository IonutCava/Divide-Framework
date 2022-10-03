#ifndef _UTILITY_FRAG_
#define _UTILITY_FRAG_

#include "nodeDataInput.cmn"

#define overlay(X, Y) (((X) < 0.5f) ? (2.f * (X) * (Y)) : (1.f - 2.f * (1.f - (X)) * (1.f - (Y))))

vec3 overlayVec(in vec3 base, in vec3 blend)
{
    return vec3(overlay(base.r, blend.r),
                overlay(base.g, blend.g),
                overlay(base.b, blend.b));
}

vec3 WorldSpacePos(in vec2 texCoords, in float depthIn, in mat4 invProjMatrix, in mat4 invView) {
    return (invView * vec4( ViewSpacePos( texCoords, depthIn, invProjMatrix ), 1.f)).xyz;
}

#define Luminance(RGB) max(dot((RGB), vec3(0.299f, 0.587f, 0.114f)), 0.0001f)
#define LevelOfGrey(C) vec4(C.rgb * vec3(0.299f, 0.587f, 0.114f), C.a)

#define detail__gamma 2.2f
#define detail__gamma__inv  1.0f / detail__gamma

#define ToLinear(SRGB) pow((SRGB).rgb, vec3(detail__gamma))
#define ToSRGB(RGB) pow((RGB).rgb, vec3(detail__gamma__inv))
#define dvd_screenPositionNormalised (gl_FragCoord.xy / dvd_ViewPort.zw)

// Accurate variants from Frostbite notes:
// https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
vec3 ToLinearAccurate(in vec3 sRGBCol) {
    const vec3 linearRGBLo = sRGBCol / 12.92f; 
    const vec3 linearRGBHi = pow((sRGBCol + 0.055f) / 1.055f, vec3(2.4f));
    
    return vec3((sRGBCol.r <= 0.04045f) ? linearRGBLo.r : linearRGBHi.r,
                (sRGBCol.g <= 0.04045f) ? linearRGBLo.g : linearRGBHi.g,
                (sRGBCol.b <= 0.04045f) ? linearRGBLo.b : linearRGBHi.b);
}

vec3 ToSRGBAccurate(in vec3 linearCol) {
    const vec3 sRGBLo = linearCol * 12.92f;
    const vec3 sRGBHi = (pow(abs(linearCol), vec3(1.f / 2.4f)) * 1.055f) - 0.055f;

    return vec3((linearCol.r <= 0.0031308f) ? sRGBLo.r : sRGBHi.r,
                (linearCol.g <= 0.0031308f) ? sRGBLo.g : sRGBHi.g,
                (linearCol.b <= 0.0031308f) ? sRGBLo.b : sRGBHi.b);
}

float computeDepth(in vec4 posWV, in mat4 projMatrix, in vec2 zPlanes) {
    const vec4 clip_space_pos = projMatrix * posWV;
    return (((zPlanes.y - zPlanes.x) * (clip_space_pos.z / clip_space_pos.w)) + zPlanes.x + zPlanes.y) * 0.5f;
}

#define IsInScreenRect(COORDS) all(bvec4(COORDS.x >= 0.f, COORDS.x <= 1.f, COORDS.y >= 0.f, COORDS.y <= 1.f))
#define IsInFrustum(COORDS)  (COORDS.z <= 1.f && IsInScreenRect(COORDS.xy))

#define packVec2(X, Y) uintBitsToFloat(packHalf2x16(vec2(x, y)))
vec2 unpackVec2(in uint pckd)  { return unpackHalf2x16(pckd); }
vec2 unpackVec2(in float pckd) { return unpackHalf2x16(floatBitsToUint(pckd)); }

//https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
//https://www.shadertoy.com/view/Mtfyzl#
vec2 packNormal(in vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = (n.z >= 0.f) ? n.xy : (1.f - abs(n.yx)) * sign(n.xy);
    n.xy = 0.5f * n.xy + 0.5f;
    return n.xy;
}

vec3 unpackNormal(in vec2 enc) {
    vec2 f = 2.f * enc - 1.f;

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    vec3 n = vec3(f, 1.f - abs(f.x) - abs(f.y));
    const float t = max(-n.z, 0.f);
    n.x += (n.x >= 0.f ? -t : t);
    n.y += (n.y >= 0.f ? -t : t);
    return normalize(n);
}

#endif //_UTILITY_FRAG_
