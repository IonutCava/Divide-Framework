#ifndef _UTILITY_FRAG_
#define _UTILITY_FRAG_

#include "nodeDataInput.cmn"

#define overlay(X, Y) ((X < 0.5f) ? (2.f * X * Y) : (1.f - 2.f * (1.f - X) * (1.f - Y)))

vec3 overlayVec(in vec3 base, in vec3 blend) {
    return vec3(overlay(base.r, blend.r),
                overlay(base.g, blend.g),
                overlay(base.b, blend.b));
}

#define scaledTextureCoords(UV, SCALE) (UV * SCALE)

float ToLinearDepth(in float depth, in vec2 depthRangeIn) {
    return (2.f * depthRangeIn.y * depthRangeIn.x / (depthRangeIn.y + depthRangeIn.x + (2.f * depth - 1.f) * (depthRangeIn.x - depthRangeIn.y)));
}

float ToLinearDepth(in float D, in mat4 projMatrix) { 
    return projMatrix[3][2] / (D - projMatrix[2][2]);
}

float ViewSpaceZ(in float depthIn, in mat4 invProjMatrix) {
    return -1.0f / (invProjMatrix[2][3] * (depthIn * 2.f - 1.f) + invProjMatrix[3][3]);
}

vec3 ClipSpacePos(in vec2 texCoords, in float depthIn) {
    return vec3(texCoords.s  * 2.f - 1.f,
                texCoords.t * 2.f - 1.f,
                depthIn     * 2.f - 1.f);
}

vec3 ViewSpacePos(in vec2 texCoords, in float depthIn, in mat4 invProjMatrix) {
    const vec4 viewSpacePos = invProjMatrix * vec4(ClipSpacePos(texCoords, depthIn), 1.f);
    return Homogenize(viewSpacePos);
}

vec3 WorldSpacePos(in vec2 texCoords, in float depthIn, in mat4 invProjMatrix, in mat4 invView) {
    const vec3 viewSpacePos = ViewSpacePos(texCoords, depthIn, invProjMatrix);
    return (invView * vec4(viewSpacePos, 1.f)).xyz;
}

// Utility function that maps a value from one range to another. 
float ReMap(const float V, const float Min0, const float Max0, const float Min1, const float Max1) {
    return (Min1 + (((V - Min0) / (Max0 - Min0)) * (Max1 - Min1)));
}

#define InRangeExclusive(V, MIN, MAX) (VS > MIN && V < MAX)
#define LinStep(LOW, HIGH, V) Saturate((V - LOW) / (HIGH - LOW))
#define Luminance(RGB) max(dot(RGB, vec3(0.299f, 0.587f, 0.114f)), 0.0001f)
#define LevelOfGrey(C) vec4(C.rgb * vec3(0.299f, 0.587f, 0.114f), C.a)

#define detail__gamma 2.2f
#define detail__gamma__inv  1.0f / detail__gamma

#define _ToLinear(SRGB) pow(SRGB, vec3(detail__gamma))
#define _ToSRGB(RGB) pow(RGB, vec3(detail__gamma__inv))

vec3 ToLinear(in vec3 sRGBCol) { return _ToLinear(sRGBCol); }
vec4 ToLinear(in vec4 sRGBCol) { return vec4(_ToLinear(sRGBCol.rgb), sRGBCol.a); }
vec3 ToSRGB(in vec3 linearCol) { return _ToSRGB(linearCol); }
vec4 ToSRGB(in vec4 linearCol) { return vec4(_ToSRGB(linearCol.rgb), linearCol.a); }

// Accurate variants from Frostbite notes:
// https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
vec3 ToLinearAccurate(in vec3 sRGBCol) {
    const vec3 linearRGBLo = sRGBCol / 12.92f; 
    const vec3 linearRGBHi = pow((sRGBCol + 0.055f) / 1.055f, vec3(2.4f));
    
    return vec3((sRGBCol.r <= 0.04045f) ? linearRGBLo.r : linearRGBHi.r,
                (sRGBCol.g <= 0.04045f) ? linearRGBLo.g : linearRGBHi.g,
                (sRGBCol.b <= 0.04045f) ? linearRGBLo.b : linearRGBHi.b);
}

vec4 ToLinearAccurate(in vec4 sRGBCol) {
    return vec4(ToLinearAccurate(sRGBCol.rgb), sRGBCol.a);
}

vec3 ToSRGBAccurate(in vec3 linearCol) {
    const vec3 sRGBLo = linearCol * 12.92f;
    const vec3 sRGBHi = (pow(abs(linearCol), vec3(1.f / 2.4f)) * 1.055f) - 0.055f;

    return vec3((linearCol.r <= 0.0031308f) ? sRGBLo.r : sRGBHi.r,
                (linearCol.g <= 0.0031308f) ? sRGBLo.g : sRGBHi.g,
                (linearCol.b <= 0.0031308f) ? sRGBLo.b : sRGBHi.b);
}

vec4 ToSRGBAccurate(in vec4 linearCol) {
    return vec4(ToSRGBAccurate(linearCol.rgb), linearCol.a);
}

float computeDepth(in vec4 posWV, in mat4 projMatrix, in vec2 zPlanes) {
    const vec4 clip_space_pos = projMatrix * posWV;
    return (((zPlanes.y - zPlanes.x) * (clip_space_pos.z / clip_space_pos.w)) + zPlanes.x + zPlanes.y) * 0.5f;
}

float computeDepth(in vec4 posWV, in vec2 zPlanes) {
    return computeDepth(posWV, dvd_ProjectionMatrix, zPlanes);
}

#define dvd_screenPositionNormalised (gl_FragCoord.xy / dvd_ViewPort.zw)

bool isInScreenRect(in vec2 coords) {
    return all(bvec4(coords.x >= 0.f, coords.x <= 1.f, coords.y >= 0.f, coords.y <= 1.f));
}

bool isInFrustum(in vec3 coords) {
    return coords.z <= 1.f && isInScreenRect(coords.xy);
}

#define packVec2(X, Y) uintBitsToFloat(packHalf2x16(vec2(x, y)))
vec2 unpackVec2(in uint pckd)  { return unpackHalf2x16(pckd); }
vec2 unpackVec2(in float pckd) { return unpackHalf2x16(floatBitsToUint(pckd)); }

//https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
//https://www.shadertoy.com/view/Mtfyzl#
vec2 packNormal(in vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = (n.z >= 0.f) ? n.xy : (1.f - abs(n.yx)) * sign(n.xy);
    n.xy = n.xy * 0.5f + 0.5f;
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
