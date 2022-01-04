--Fragment

#include "utility.frag"


layout(binding = TEXTURE_UNIT0) uniform sampler2D texScreen;
layout(binding = TEXTURE_UNIT1) uniform sampler2D texDepth;
layout(binding = TEXTURE_SCENE_NORMALS) uniform sampler2D texNormal;

uniform mat4 projToPixel; // A projection matrix that maps to pixel coordinates (not [-1, +1] normalized device coordinates)
uniform mat4 projectionMatrix;
uniform mat4 invProjectionMatrix;
uniform mat4 invViewMatrix;
uniform mat4 prevViewProjectionMatrix;
uniform vec2 zPlanes;
uniform uint maxScreenMips;
uniform float maxSteps;
uniform float binarySearchIterations;
uniform float jitterAmount;
uniform float maxDistance;
uniform float stride;
uniform float zThickness;
uniform float strideZCutoff;
uniform float screenEdgeFadeStart;
uniform float eyeFadeStart;
uniform float eyeFadeEnd;
uniform bool ssrEnabled;

out vec4 _colourOut;

#define DistanceSquared(A, B) dot((A-B),(A-B))

bool FindSSRHit(in vec3 csOrig,       // Camera-space ray origin, which must be within the view volume
                in vec3 csDir,        // Unit length camera-space ray direction
                in float jitter,      // Number between 0 and 1 for how far to bump the ray in stride units to conceal banding artifacts
                out vec2 hitPixel,    // Pixel coordinates of the first intersection with the scene
                out vec3 hitPoint,    // Camera space location of the ray hit
                out float iterations)
{
    // Clip to the near plane
    const float rayLength = ((csOrig.z + csDir.z * maxDistance) > -zPlanes.x) 
                                     ? (-zPlanes.x - csOrig.z) / csDir.z
                                     : maxDistance;

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

    // Scale derivatives by the desired pixel stride and then
    // offset the starting values by the jitter fraction
    dP *= pixelStride; dQ *= pixelStride; dk *= pixelStride;
    P0 += dP * jitter; Q0 += dQ * jitter; k0 += dk * jitter;

    // Adjust end condition for iteration direction
    const float  end = P1.x * stepDir;

    float i, zA = csOrig.z, zB = csOrig.z;
    vec4 pqk = vec4(P0, Q0.z, k0);
    vec4 dPQK = vec4(dP, dQ.z, dk);
    bool intersect = false;
    for (i = 0; i < maxSteps && intersect == false && pqk.x * stepDir <= end; i++) 	{
        pqk += dPQK;

        zA = zB;
        zB = (dPQK.z * 0.5f + pqk.z) / (dPQK.w * 0.5f + pqk.w);

        hitPixel = permute ? pqk.yx : pqk.xy;
        hitPixel = hitPixel / dvd_screenDimensions;
        const float currentZ = ViewSpaceZ(texture(texDepth, hitPixel).r, invProjectionMatrix);
        intersect = zA >= currentZ - zThickness && zB <= currentZ;
    }

    // Binary search refinement
    float addDQ = 0.f;
    if (pixelStride > 1.f && intersect) {
        pqk -= dPQK;
        dPQK /= pixelStride;
        float originalStride = pixelStride * 0.5;
        float stride = originalStride;
        zA = pqk.z / pqk.w;
        zB = zA;
        for (float j = 0; j < binarySearchIterations; j++) {
            pqk += dPQK * stride;
            addDQ += stride;

            zA = zB;
            zB = (dPQK.z * 0.5f + pqk.z) / (dPQK.w * 0.5f + pqk.w);

            hitPixel = permute ? pqk.yx : pqk.xy;
            hitPixel = hitPixel / dvd_screenDimensions;
            const float currentZ = ViewSpaceZ(texture(texDepth, hitPixel).r, invProjectionMatrix);
            bool intersect2 = zA >= currentZ - zThickness && zB <= currentZ;

            originalStride *= 0.5f;
            stride = intersect2 ? -originalStride : originalStride;
        }
    }

    // Advance Q based on the number of steps
    Q0.xy += dQ.xy * (i - 1) + (dQ.xy / pixelStride) * addDQ;
    Q0.z = pqk.z;
    hitPoint = Q0 / pqk.w;
    iterations = i;
    
    return intersect && isInScreenRect(hitPixel);
}

float ComputeBlendFactorForIntersection(in float iterationCount,
                                        in vec2 hitPixel,
                                        in vec3 hitPoint,
                                        in vec3 vsRayOrigin,
                                        in vec3 vsRayDirection)
{
    float alpha = 1.f;
    {// Fade ray hits that approach the maximum iterations
        alpha -= pow(iterationCount / maxSteps, 8.f);
    }
    {// Fade ray hits that approach the screen edge
        const vec2 hitPixelNDC = 2.f * hitPixel - 1.f;
        const float maxDimension = min(1.f, max(abs(hitPixelNDC.x), abs(hitPixelNDC.y)));
        const float screenFade = screenEdgeFadeStart;
        alpha *= 1.f - (max(0.f, maxDimension - screenFade) / (1.f - screenFade));
    }
    {// Fade ray hits base on how much they face the camera
        const float eyeDirection = clamp(vsRayDirection.z, eyeFadeStart, eyeFadeEnd);
        alpha *= 1.f - ((eyeDirection - eyeFadeStart) / (eyeFadeEnd - eyeFadeStart));
    }
    {// Fade ray hits based on distance from ray origin
        alpha *= 1.f - saturate(distance(vsRayOrigin, hitPoint) / maxDistance);
    }

    return alpha;
}

vec4 GetReflectionColour() {
    if (!ssrEnabled) {
        return vec4(0.f);
    }

    float reflBlend = 0.f;
    vec3 ambientReflected = vec3(0.f);

    const float depth = texture(texDepth, VAR._texCoord).r;
    if (depth < INV_Z_TEST_SIGMA) {
        const vec3 dataIn = texture(texNormal, VAR._texCoord).rgb;
        const vec3 normal = unpackNormal(dataIn.rg);
        const float roughness = dataIn.b;

        if (roughness < INV_Z_TEST_SIGMA) 
        {
            const vec3 vsNormal = normalize(normal);
            const vec3 vsPos = ViewSpacePos(VAR._texCoord, depth, invProjectionMatrix);
            const vec3 vsReflect = reflect(normalize(vsPos), vsNormal);

            const vec2 uv2 = VAR._texCoord * dvd_screenDimensions;
            const float jitter = mod((uv2.x + uv2.y) * 0.25f, 1.f) * jitterAmount;

            vec3 hitPointWV = vec3(0.f);
            vec2 hitPixel = vec2(0.f);
            float iterations = 0;
            if (FindSSRHit(vsPos, vsReflect, jitter, hitPixel, hitPointWV, iterations)) {
                const vec4 hitPointW = invViewMatrix * vec4(hitPointWV, 1.f);
                const vec4 hitPoint = prevViewProjectionMatrix * hitPointW;

                const vec3 ndc = homogenize(hitPoint);
                const vec2 previousUV = vec2(ndc.xy) * 0.5f + 0.5f;
                reflBlend = ComputeBlendFactorForIntersection(iterations, previousUV, hitPointW.xyz, vsPos, vsReflect);
                ambientReflected = mix(ambientReflected,
                                        textureLod(texScreen, previousUV, roughness * maxScreenMips).rgb,
                                        reflBlend);
            }
        }
    }

    return vec4(ambientReflected, reflBlend);
}

void main() {
    if (dvd_materialDebugFlag == DEBUG_NONE || dvd_materialDebugFlag == DEBUG_SSR) {
        //_colourOut.a = refBlend
        _colourOut = GetReflectionColour();
        return;
    }

    _colourOut = vec4(0.f);
}
