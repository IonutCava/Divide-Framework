--Fragment

#define DEBUG_NAN_VALUES

#include "utility.frag"
#include "sceneData.cmn"

DESCRIPTOR_SET_RESOURCE(PER_DRAW_SET, TEXTURE_UNIT0)     uniform sampler2D texScreen;
DESCRIPTOR_SET_RESOURCE(PER_DRAW_SET, TEXTURE_UNIT1)     uniform sampler2D texDepth;
DESCRIPTOR_SET_RESOURCE(PER_DRAW_SET, TEXTURE_NORMALMAP) uniform sampler2D texNormal;

uniform mat4 projToPixel; // A projection matrix that maps to pixel coordinates (not [-1, +1] normalized device coordinates)
uniform mat4 projectionMatrix;
uniform mat4 invProjectionMatrix;
uniform mat4 previousViewProjection;
uniform mat4 invViewMatrix;
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

#define DistanceSquared(A, B) dot((A-B),(A-B))

float Linear01Depth(in float z) {
    const float temp = _zPlanes.y / _zPlanes.x;
    return 1.f / ((1.f - temp) * z + temp);
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

    const vec3 csEndPoint = csOrig + csDir * rayLength;
    //if(csEndPoint.z > csOrig.z)
    // return false;

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
    const float end = P1.x * stepDir;

    float i, zA = csOrig.z, zB = csOrig.z;
    vec4 pqk = vec4(P0, Q0.z, k0);
    vec4 dPQK = vec4(dP, dQ.z, dk);
    bool intersect = false;
    for (i = 0; i < maxSteps && intersect == false && pqk.x * stepDir <= end; i++) 	{
        pqk += dPQK;

        zA = zB;
        zB = (dPQK.z * 0.5f + pqk.z) / (dPQK.w * 0.5f + pqk.w);

        hitPixel = permute ? pqk.yx : pqk.xy;
        hitPixel = hitPixel / screenDimensions;
        const float currentZ = Linear01Depth(texture(texDepth, hitPixel).r) * -_zPlanes.y;
        intersect = zA >= currentZ - zThickness && zB <= currentZ;
    }

    // Binary search refinement
    float addDQ = 0.f;
    if (pixelStride > 1.f && intersect) {
        pqk -= dPQK;
        dPQK /= pixelStride;
        float originalStride = pixelStride * 0.5;
        float strideTemp = originalStride;
        zA = pqk.z / pqk.w;
        zB = zA;
        for (float j = 0; j < binarySearchIterations; j++) {
            pqk += dPQK * strideTemp;
            addDQ += strideTemp;

            zA = zB;
            zB = (dPQK.z * 0.5f + pqk.z) / (dPQK.w * 0.5f + pqk.w);

            hitPixel = permute ? pqk.yx : pqk.xy;
            hitPixel = hitPixel / screenDimensions;
            const float currentZ = Linear01Depth(texture(texDepth, hitPixel).r) * -_zPlanes.y;
            bool intersect2 = zA >= currentZ - zThickness && zB <= currentZ;

            originalStride *= 0.5f;
            strideTemp = intersect2 ? -originalStride : originalStride;
        }
    }

    // Advance Q based on the number of steps
    Q0.xy += dQ.xy * (i - 1) + (dQ.xy / pixelStride) * addDQ;
    Q0.z = pqk.z;
    hitPoint = Q0 / pqk.w;
    iterations = i;
    
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
        const float screenFade = screenEdgeFadeStart;

        const vec2 hitPixelNDC = 2.f * hitPixel - 1.f;
        const float maxDimension = min(1.f, max(abs(hitPixelNDC.x), abs(hitPixelNDC.y)));
        alpha *= 1.f - (max(0.f, maxDimension - screenFade) / (1.f - screenFade));
    }
    {// Fade ray hits base on how much they face the camera
        const float eyeDirection = clamp(vsRayDirection.z, eyeFadeStart, eyeFadeEnd);
        alpha *= 1.f - ((eyeDirection - eyeFadeStart) / (eyeFadeEnd - eyeFadeStart));
    }
    {// Fade ray hits based on distance from ray origin
        alpha *= 1.f - Saturate(distance(vsRayOrigin, hitPoint) / maxDistance);
    }

    //return Saturate(alpha);
    return alpha;
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
                const vec3 vsRayDir = normalize(vsPos);
                const vec3 vsReflect = reflect(vsRayDir, vsNormal);
                const vec3 worldReflect = (invViewMatrix * vec4(vsReflect.xyz, 0)).xyz;

                const vec2 uv2 = VAR._texCoord * screenDimensions;
                const float jitter = mod((uv2.x + uv2.y) * 0.25f, 1.f);

                vec2 hitPixel = vec2(0.f);
                vec3 hitPoint = vec3(0.f);
                float iterations = 0;
                if (FindSSRHit(vsPos, vsReflect, jitter * jitterAmount, hitPixel, hitPoint, iterations)) {
                    // Sample reflection in previous frame with temporal reprojection
#if 0
                    vec4 prevHit = invViewMatrix * vec4(hitPoint.xyz, 1);
                    prevHit = dvd_PreviousViewMatrix * prevHit;
                    prevHit = dvd_PreviousProjectionMatrix * prevHit;
                    prevHit.xyz /= prevHit.w;
#else
#if 0
                    const vec3 prevHit = Homogenize(previousViewProjection * (invViewMatrix * vec4(hitPoint, 1.f)));
#else
                    const vec3 prevHit = Homogenize(dvd_PreviousViewProjectionMatrix * (invViewMatrix * vec4(hitPoint, 1.f)));
#endif
#endif
                    // Blend between reprojected SSR sample and IBL
                    reflBlend = ComputeBlendFactorForIntersection(iterations, hitPixel, hitPoint, vsPos, vsReflect);
                    const vec3 ssr = textureLod(texScreen, 0.5f * prevHit.xy + 0.5f, roughness * maxScreenMips).rgb;
                    ambientReflected = mix(ambientReflected, ssr, reflBlend);
#if defined(DEBUG_NAN_VALUES)
                    if (dvd_MaterialDebugFlag == DEBUG_SSR)
                    {
                       if (isnan(prevHit.r) || isnan(prevHit.g)) {
                            ambientReflected = vec3(1.f, 0.f, 1.f);
                            reflBlend = 0.f;
                        } else if (isnan(reflBlend)) {
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

    _colourOut = vec4(ambientReflected, reflBlend);
}
