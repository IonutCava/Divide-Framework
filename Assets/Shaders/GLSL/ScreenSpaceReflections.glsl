--Fragment

// Copyright (c) 2026 DIVIDE-Studio

#define DEBUG_NAN_VALUES

#include "utility.frag"
#include "sceneData.cmn"

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texScreen;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 1) uniform sampler2D texDepth;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 2) uniform sampler2D texNormal;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 3) uniform sampler2D texPreviousDepth;

// A projection matrix that maps to pixel coordinates (not [-1, +1] normalized device coordinates)
#define projToPixel PushData0
#define projectionMatrix PushData1

uniform mat4 invProjectionMatrix;
uniform mat4 invViewMatrix;
uniform mat4 previousProjectionMatrix;
uniform mat4 previousViewProjectionMatrix;
uniform uint previousDepthValid = 0u;
uniform vec2 _zPlanes;
uniform vec2 screenDimensions;
uniform uint maxScreenMips = 5u;
uniform float maxSteps = 256;
uniform float binarySearchIterations = 4;
uniform float jitterAmount = 1.f;
uniform float maxDistance = 200.f;
uniform float stride = 8.f;
uniform float zThickness = 1.5f;
uniform float strideZCutoff = 100.f;
uniform float screenEdgeFadeStart = 0.75f;
uniform float eyeFadeStart = 0.5f;
uniform float eyeFadeEnd = 1.f;


layout(location = 0) out vec4 _colourOut;

bool PreviousFrameDepthMatches(in vec2 uv, in float expectedDepth) {
    if (previousDepthValid == 0u) {
        return false;
    }

    const float previousDepth = textureLod(texPreviousDepth, uv, 0.f).r;
    if (previousDepth >= INV_Z_TEST_SIGMA) {
        return false;
    }

    const float expectedViewDepth = ViewDistanceFromDepth(expectedDepth, previousProjectionMatrix);
    const float storedViewDepth = ViewDistanceFromDepth(previousDepth, previousProjectionMatrix);
    const float depthTolerance = max(0.05f, expectedViewDepth * 0.01f);
    return expectedViewDepth > 0.f && abs(storedViewDepth - expectedViewDepth) <= depthTolerance;
}

#define DistanceSquared(A, B) dot((A-B),(A-B))

bool IntersectsDepthSlab(in float z0, in float z1, in float sceneZ) {
    return max(z0, z1) >= sceneZ - zThickness && min(z0, z1) <= sceneZ;
}

//ref: http://casual-effects.blogspot.com/2014/08/screen-space-ray-tracing.html
//ref: https://github.com/theFrenchDutch/synthese_image/blob/5fb8264447f555a4b498a125f931060302345839/Shaders/deferred_SSR.glsl
bool FindSSRHit(in vec3 csOrig,       // Camera-space ray origin, which must be within the view volume
                in vec3 csDir,        // Unit length camera-space ray direction
                in float jitter,      // Number between 0 and 1 for how far to bump the ray in stride units to conceal banding artifacts
                out vec2 hitPixel,    // Pixel coordinates of the first intersection with the scene
                out vec3 hitPoint,    // Camera space location of the ray hit
                out float iterations)
{
    // Clip to the near plane
    const float rayLength = ((csOrig.z + csDir.z * maxDistance) > -_zPlanes.x)
                                     ? (-_zPlanes.x - csOrig.z) / csDir.z
                                     : maxDistance;
    hitPixel = vec2(0.f);
    hitPoint = csOrig;
    iterations = 0.f;
    if (rayLength <= 0.f) {
        return false;
    }

    const vec3 csEndPoint = csOrig + csDir * rayLength;

    // Project into homogeneous clip space
    const vec4 H0 = projToPixel * vec4(csOrig, 1.f);
    const vec4 H1 = projToPixel * vec4(csEndPoint, 1.f);
    float k0 = 1.f / H0.w, k1 = 1.f / H1.w;

    // The interpolated homogeneous version of the camera-space points  
    vec3 Q0 = csOrig * k0, Q1 = csEndPoint * k1;

    // Screen-space endpoints
    vec2 P0 = H0.xy * k0, P1 = H1.xy * k1;

    // If the line is degenerate, make it cover at least one pixel
    // to avoid handling zero-pixel extent as a special case later
    P1 += vec2((DistanceSquared(P0, P1) < 0.0001f) ? 0.01f : 0.f);
    vec2 delta = P1 - P0;

    // Permute so that the primary iteration is in x to collapse
    // all quadrant-specific DDA cases later
    bool permute = false;
    if (abs(delta.x) < abs(delta.y)) {
        // This is a more-vertical line
        permute = true;
        delta = delta.yx;
        P0 = P0.yx;
        P1 = P1.yx;
    }

    const float stepDir = sign(delta.x);
    const float invdx = stepDir / delta.x;

    // Track the derivatives of Q and k
    vec3  dQ = (Q1 - Q0) * invdx;
    float dk = (k1 - k0) * invdx;
    vec2  dP = vec2(stepDir, delta.y * invdx);

    const float strideScaler = 1.f - min(1.f, -csOrig.z / strideZCutoff);
    const float pixelStride = 1.f + strideScaler * stride;

    // Scale derivatives by the desired pixel stride, keeping at least one
    // segment of short rays after applying the jitter.
    dP *= pixelStride; dQ *= pixelStride; dk *= pixelStride;
    const float jitterSteps = clamp(jitter, 0.f, max(0.f, abs(delta.x) / pixelStride - 1.f));
    P0 += dP * jitterSteps; Q0 += dQ * jitterSteps; k0 += dk * jitterSteps;

    // Adjust end condition for iteration direction
    const float end = P1.x * stepDir;

    const vec2 pixelStart = permute ? P0.yx : P0.xy;
    const vec2 pixelStep = permute ? dP.yx : dP.xy;
    const vec2 startUV = pixelStart / screenDimensions;
    if (!IsInScreenRect(startUV)) {
        return false;
    }
    float progressLimit = (end - P0.x * stepDir) / pixelStride;
    for (int axis = 0; axis < 2; ++axis) {
        if (pixelStep[axis] > 0.f) {
            progressLimit = min(progressLimit, (screenDimensions[axis] - pixelStart[axis]) / pixelStep[axis]);
        } else if (pixelStep[axis] < 0.f) {
            progressLimit = min(progressLimit, -pixelStart[axis] / pixelStep[axis]);
        }
    }

    vec4 pqk = vec4(P0, Q0.z, k0);
    const vec4 dPQK = vec4(dP, dQ.z, dk);
    float progress = 0.f;
    vec4 lower = pqk;
    float lowerProgress = 0.f;
    float upperSceneZ = 0.f;
    bool intersect = false;
    for (float i = 0.f; i < maxSteps && !intersect; ++i) {
        const float stepFraction = min(1.f, progressLimit - progress);
        if (stepFraction <= 0.f) {
            break;
        }

        const vec4 next = pqk + dPQK * stepFraction;
        const vec2 nextPixel = clamp((permute ? next.yx : next.xy) / screenDimensions, 0.f, 1.f);
        const float sceneDepth = texture(texDepth, nextPixel).r;
        upperSceneZ = -ViewDistanceFromDepth(sceneDepth, projectionMatrix);
        intersect = sceneDepth < INV_Z_TEST_SIGMA &&
                    IntersectsDepthSlab(pqk.z / pqk.w, next.z / next.w, upperSceneZ);
        lower = pqk;
        lowerProgress = progress;
        pqk = next;
        progress += stepFraction;
        iterations += 1.f;
    }

    // Binary search refinement
    if (pixelStride > 1.f && intersect) {
        vec4 upper = pqk;
        float upperProgress = progress;
        for (float j = 0.f; j < binarySearchIterations; ++j) {
            const vec4 midpoint = (lower + upper) * 0.5f;
            const float midpointProgress = (lowerProgress + upperProgress) * 0.5f;
            const vec2 midpointPixel = clamp((permute ? midpoint.yx : midpoint.xy) / screenDimensions, 0.f, 1.f);
            const float midpointDepth = texture(texDepth, midpointPixel).r;
            const float midpointSceneZ = -ViewDistanceFromDepth(midpointDepth, projectionMatrix);
            if (midpointDepth < INV_Z_TEST_SIGMA &&
                IntersectsDepthSlab(lower.z / lower.w, midpoint.z / midpoint.w, midpointSceneZ)) {
                upper = midpoint;
                upperProgress = midpointProgress;
                upperSceneZ = midpointSceneZ;
            } else if (IntersectsDepthSlab(midpoint.z / midpoint.w, upper.z / upper.w, upperSceneZ)) {
                lower = midpoint;
                lowerProgress = midpointProgress;
            } else {
                intersect = false;
                break;
            }
        }
        pqk = upper;
        progress = upperProgress;
    }

    hitPixel = (permute ? pqk.yx : pqk.xy) / screenDimensions;
    Q0.xy += dQ.xy * progress;
    Q0.z = pqk.z;
    hitPoint = Q0 / pqk.w;
    
    return intersect;
}

float ComputeBlendFactorForIntersection(in float iterationCount,
                                        in vec2 hitPixel,
                                        in vec3 hitPoint,
                                        in vec3 vsRayOrigin,
                                        in vec3 vsRayDirection)
{
    float alpha = 1.f;
    {// Fade ray hits that approach the maximum iterations
        alpha *= 1.f - pow(iterationCount / maxSteps, 8.f);
    }
    {// Fade ray hits that approach the screen edge
        const float screenFade = clamp(screenEdgeFadeStart, 0.f, 1.f);

        const vec2 hitPixelNDC = 2.f * hitPixel - 1.f;
        const float maxDimension = min(1.f, max(abs(hitPixelNDC.x), abs(hitPixelNDC.y)));
        alpha *= screenFade < 1.f
            ? 1.f - (max(0.f, maxDimension - screenFade) / (1.f - screenFade))
            : (maxDimension < 1.f ? 1.f : 0.f);
    }
    {// Fade ray hits base on how much they face the camera
        if (eyeFadeEnd > eyeFadeStart) {
            const float eyeDirection = clamp(vsRayDirection.z, eyeFadeStart, eyeFadeEnd);
            alpha *= 1.f - ((eyeDirection - eyeFadeStart) / (eyeFadeEnd - eyeFadeStart));
        } else {
            alpha *= vsRayDirection.z < eyeFadeStart ? 1.f : 0.f;
        }
    }
    {// Fade ray hits based on distance from ray origin
        alpha *= 1.f - Saturate(distance(vsRayOrigin, hitPoint) / maxDistance);
    }

    //return Saturate(alpha);
    return alpha;
}

bool PreviousFrameUV(in vec4 clip, out vec2 uv) {
    uv = vec2(0.f);
    if (!(clip.w > 0.f)) {
        return false;
    }
    const vec3 ndc = clip.xyz / clip.w;
    uv = 0.5f * ndc.xy + 0.5f;
    return IsInFrustum(vec3(uv, ndc.z));
}

vec3 CameraIncidentDirection(in vec3 viewPosition) {
    return projectionMatrix[3][3] == 0.f ? normalize(viewPosition) : vec3(0.f, 0.f, -1.f);
}

void main() {
    float reflBlend = 0.f;
    vec3 ambientReflected = vec3(0.f);

    if (dvd_MaterialDebugFlag == DEBUG_NONE ||
        dvd_MaterialDebugFlag == DEBUG_SSR ||
        dvd_MaterialDebugFlag == DEBUG_SSR_BLEND)
    {
        const float depth = texture(texDepth, VAR._texCoord).r;
        if (depth < INV_Z_TEST_SIGMA) {
            const vec3 dataIn = texture(texNormal, VAR._texCoord).rgb;
            const float roughness = dataIn.b;
            if (roughness < INV_Z_TEST_SIGMA) {
                const vec3 vsNormal = normalize(unpackNormal(dataIn.rg));
                const vec3 vsPos = ViewSpacePos(VAR._texCoord, depth, invProjectionMatrix);
                const vec3 vsRayDir = CameraIncidentDirection(vsPos);
                const vec3 vsReflect = reflect(vsRayDir, vsNormal);
                const vec2 uv2 = VAR._texCoord * screenDimensions;
                const float jitter = mod((uv2.x + uv2.y) * 0.25f, 1.f);

                vec2 hitPixel = vec2(0.f);
                vec3 hitPoint = vec3(0.f);
                float iterations = 0;
                if (FindSSRHit(vsPos, vsReflect, jitter * jitterAmount, hitPixel, hitPoint, iterations)) {
                    const ivec2 depthSize = textureSize(texDepth, 0);
                    const ivec2 hitTexel = clamp(ivec2(hitPixel * vec2(depthSize)), ivec2(0), depthSize - 1);
                    const float hitDepth = texelFetch(texDepth, hitTexel, 0).r;
                    const vec2 hitUV = (vec2(hitTexel) + 0.5f) / vec2(depthSize);
                    const vec4 hitWorld = invViewMatrix * vec4(ViewSpacePos(hitUV, hitDepth, invProjectionMatrix), 1.f);
                    const vec4 prevClip = previousViewProjectionMatrix * hitWorld;
                    vec2 prevUV;
                    if (hitDepth < INV_Z_TEST_SIGMA && PreviousFrameUV(prevClip, prevUV) && PreviousFrameDepthMatches(prevUV, prevClip.z / prevClip.w)) {
                        // The material pass applies this confidence to the unweighted colour.
                        reflBlend = ComputeBlendFactorForIntersection(iterations, hitPixel, hitPoint, vsPos, vsReflect);
                        const vec3 ssr = textureLod(texScreen, prevUV, roughness * maxScreenMips).rgb;
                        ambientReflected = ssr;
#if defined(DEBUG_NAN_VALUES)
                        if (dvd_MaterialDebugFlag == DEBUG_SSR)
                        {
                            if (isnan(reflBlend)) {
                                ambientReflected = vec3(0.f, 0.f, 1.f);
                                reflBlend = 0.f;
                            } else if(isnan(ssr.r) || isnan(ssr.g) || isnan(ssr.b)) {
                                ambientReflected = vec3(1.f, 0.f, 0.f);
                                reflBlend = 0.f;
                            }
                        }
#endif //DEBUG_NAN_VALUES
                    }
                }
            }
        }
    }

    _colourOut = vec4(ambientReflected, reflBlend);
}
