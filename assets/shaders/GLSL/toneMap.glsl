-- Fragment.Apply.FOG.SSAO.SSR

#include "sceneData.cmn"
#include "utility.frag"
layout(binding = TEXTURE_UNIT0)         uniform sampler2D texScreen;
layout(binding = TEXTURE_OPACITY)       uniform sampler2D texSSAO;
layout(binding = TEXTURE_PROJECTION)    uniform sampler2D texSSR;
layout(binding = TEXTURE_SCENE_NORMALS) uniform sampler2D texNormalsAndMatData;
layout(binding = TEXTURE_DEPTH)         uniform sampler2D texDepth;

uniform mat4 invProjectionMatrix;
uniform mat4 invViewMatrix;
uniform vec3 cameraPosition;
uniform bool enableFog;

out vec4 _colourOut;

//https://iquilezles.org/www/articles/fog/fog.htm
vec3 applyFog(in vec3  rgb,      // original color of the pixel
              in float distance, // camera to point distance
              in vec3  rayOri,   // camera position
              in vec3  rayDir)   // camera to point vector
{
    const float c = dvd_fogDetails._colourSunScatter.a;
    const float b = dvd_fogDetails._colourAndDensity.a;
    const float fogAmount = c * exp(-rayOri.y * b) * (1.f - exp(-distance * rayDir.y * b)) / rayDir.y;
    return mix(rgb, dvd_fogDetails._colourAndDensity.rgb, fogAmount);
}

vec3 computeFresnel(in vec3 V, in vec3 N) {
    const vec3 H = normalize(N + V);
    #define fresnelExp 5.0
    float cosine = dot(H, V);
    float product = max(cosine, 0.0);
    float factor = 1.0 - pow(product, fresnelExp);

    return vec3(factor);
}

vec3 computeFresnelSchlickRoughness(in vec3 H, in vec3 V, in vec3 F0, in float roughness) {
    const float cosTheta = saturate(dot(H, V));
    return F0 + (max(vec3(1.f - roughness), F0) - F0) * pow(1.f - cosTheta, 5.f);
}

void main() {
    vec4 albedo = texture(texScreen, VAR._texCoord);

    const float depth = texture(texDepth, VAR._texCoord).r;
    const vec3 vsPos = ViewSpacePos(VAR._texCoord, depth, invProjectionMatrix);
    const vec3 worldPos = (invViewMatrix * vec4(vsPos.xyz, 1.f)).xyz;

    const vec4 matData = texture(texNormalsAndMatData, VAR._texCoord);
    const uint materialFlags = uint(unpackVec2(matData.a).y);

    vec3 ambientIBL = albedo.rgb;
    if (!BitCompare(materialFlags, FLAG_NO_REFLECTIONS)) {
        const vec2 MR = unpackVec2(matData.b);
        const vec3 normalWV = normalize(unpackNormal(matData.rg));
        const vec3 N = (invViewMatrix * vec4(normalWV, 0.f)).xyz;
        const vec3 V = normalize(cameraPosition - worldPos);

        const float metalness = MR.x;
#if 0
        const vec3 kS = computeFresnel(V, N);
#else 
        const float roughness = MR.y;
        const vec3 dielectricSpecular = vec3(0.04f);
        const vec3 F0 = mix(dielectricSpecular, albedo.rgb, metalness);
        const vec3 H = normalize(N + V);
        const vec3 kS = computeFresnelSchlickRoughness(H, V, F0, roughness);
#endif

        // Energy conservation
        vec3 kD = vec3(1.f) - kS;
        kD *= 1.f - metalness;

        // Diffuse irradience computation
        const vec3 diffuseIrradiance = albedo.rgb;

        // Specular radiance computation
        const vec3 reflection = texture(texSSR, VAR._texCoord).rgb;
        const vec3 specularRadiance = reflection * kS;
        ambientIBL = (kD * diffuseIrradiance + specularRadiance);
    }

    const float ssao = texture(texSSAO, VAR._texCoord).r;
    albedo.rgb = ssao * ambientIBL;

    if (enableFog && !BitCompare(materialFlags, FLAG_NO_FOG)) {
        albedo.rgb = applyFog(albedo.rgb,
                              distance(worldPos, cameraPosition),
                              cameraPosition,
                              normalize(worldPos - cameraPosition));
    }

    _colourOut = vec4(albedo.rgb, 1.f);
}

-- Fragment

#include "utility.frag"

//ref: https://github.com/BruOp/bae

layout(binding = TEXTURE_UNIT0) uniform sampler2D texScreen;
layout(binding = TEXTURE_UNIT1) uniform sampler2D texExposure;

uniform float manualExposureFactor;
uniform int mappingFunction;
uniform bool useAdaptiveExposure;
uniform bool skipToneMapping;

out vec4 _colourOut;

float Reinhard(float x) {
    return x / (1.0f + x);
}

float Reinhard2(float x) {
    const float L_white = 4.0f;
    return (x * (1.0f + x / (L_white * L_white))) / (1.0 + x);
}

float Tonemap_ACES(float x) {
    // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}

float Tonemap_Unreal(float x) {
    // Unreal 3, Documentation: "Color Grading"
    // Adapted to be close to Tonemap_ACES, with similar range
    // Gamma 2.2 correction is baked in, don't use with sRGB conversion!
    return x / (x + 0.155) * 1.019;
}

float Tonemap_Uchimura(float x, float P, float a, float m, float l, float c, float b) {
    // Uchimura 2017, "HDR theory and practice"
    // Math: https://www.desmos.com/calculator/gslcdxvipg
    // Source: https://www.slideshare.net/nikuque/hdr-theory-and-practicce-jp
    float l0 = ((P - m) * l) / a;
    float L0 = m - m / a;
    float L1 = m + (1.0 - m) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;
    float w0 = 1.0 - smoothstep(0.0, m, x);
    float w2 = step(m + l0, x);
    float w1 = 1.0 - w0 - w2;
    float T = m * pow(x / m, c) + b;
    float S = P - (P - S1) * exp(CP * (x - S0));
    float L = m + a * (x - m);
    return T * w0 + L * w1 + S * w2;
}

float Tonemap_Uchimura(float x) {
    const float P = 1.0f;  // max display brightness
    const float a = 1.0f;  // contrast
    const float m = 0.22f; // linear section start
    const float l = 0.4f;  // linear section length
    const float c = 1.33f; // black
    const float b = 0.0f;  // pedestal

    return Tonemap_Uchimura(x, P, a, m, l, c, b);
}

float Tonemap_Lottes(float x) {
    // Lottes 2016, "Advanced Techniques and Optimization of HDR Color Pipelines"
    const float a = 1.6f;
    const float d = 0.977f;
    const float hdrMax = 8.0f;
    const float midIn = 0.18f;
    const float midOut = 0.267f;
    // Can be precomputed
    const float b =
        (-pow(midIn, a) + pow(hdrMax, a) * midOut) /
        ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    const float c =
        (pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) /
        ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    return pow(x, a) / (pow(x, a * d) * b + c);
}

float Uncharted2(float x)
{
    const float A = 0.15f;
    const float B = 0.50f;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.02;
    const float F = 0.30f;
    const float W = 11.2f;

    x =  ((x*(A*x + C*B) + D*E) / (x*(A*x + B) + D*F)) - E / F;
    float white = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
    
    return x / white;
}

vec3 convertRGB2Yxy(vec3 _rgb) {
    // Reference:
    // RGB/XYZ Matrices
    // http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
    vec3 xyz;
    xyz.x = dot(vec3(0.4124564f, 0.3575761f, 0.1804375f), _rgb);
    xyz.y = dot(vec3(0.2126729f, 0.7151522f, 0.0721750f), _rgb);
    xyz.z = dot(vec3(0.0193339f, 0.1191920f, 0.9503041f), _rgb);

    // Reference:
    // http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_xyY.html
    float inv = 1.0f / dot(xyz, vec3(1.0f));
    return vec3(xyz.y, xyz.x * inv, xyz.y * inv);
}

vec3 convertYxy2RGB(vec3 _Yxy) {
    // Reference:
    // http://www.brucelindbloom.com/index.html?Eqn_xyY_to_XYZ.html
    vec3 xyz;
    xyz.x = _Yxy.x * _Yxy.y / _Yxy.z;
    xyz.y = _Yxy.x;
    xyz.z = _Yxy.x * (1.0f - _Yxy.y - _Yxy.z) / _Yxy.z;

    vec3 rgb;
    rgb.x = dot(vec3( 3.2404542f, -1.5371385f, -0.4985314f), xyz);
    rgb.y = dot(vec3(-0.9692660f,  1.8760108f,  0.0415560f), xyz);
    rgb.z = dot(vec3( 0.0556434f, -0.2040259f,  1.0572252f), xyz);
    return rgb;
}

void main() {
    const vec4 inputColour = texture(texScreen, VAR._texCoord);

    vec3 screenColour = inputColour.rgb;

    if (!skipToneMapping && mappingFunction != NONE) {
        vec3 Yxy = vec3(0.f);

        if (useAdaptiveExposure) {
            Yxy = convertRGB2Yxy(screenColour);
            const float avgLuminance = texture(texExposure, VAR._texCoord).r;
            Yxy.x = (Yxy.x / (9.6f * avgLuminance + 0.0001f)) * manualExposureFactor;
        } else {
            Yxy = convertRGB2Yxy(screenColour * manualExposureFactor);
        }

        if (mappingFunction == REINHARD)     {
            Yxy.x = Reinhard(Yxy.x);
        } else if (mappingFunction == REINHARD_MODIFIED)     {
            Yxy.x = Reinhard2(Yxy.x);
        } else if (mappingFunction == GT)     {
            Yxy.x = Tonemap_Uchimura(Yxy.x);
        } else if (mappingFunction == ACES)     {
            Yxy.x = Tonemap_ACES(Yxy.x);
        } else if (mappingFunction == UNREAL_ACES)     {
            Yxy.x = Tonemap_Unreal(Yxy.x);
        } else if (mappingFunction == AMD_ACES)     {
            Yxy.x = Tonemap_Lottes(Yxy.x);
        } else if (mappingFunction == AMD_ACES)     {
            Yxy.x = Tonemap_Uchimura(Yxy.x, 1.0f, 1.7f, 0.1f, 0.0f, 1.33f, 0.0f);
        } else if (mappingFunction == UNCHARTED_2)     {
            Yxy.x = Uncharted2(Yxy.x);
        }

        screenColour = convertYxy2RGB(Yxy);
    }

    //Do not apply gamma correction here as edge-detection in subsequent passes work best in linear space
#if 0
    _colourOut.rgb = ToSRGBAccurate(screenColour);
#else
    _colourOut.rgb = screenColour;
#endif
    _colourOut.a = Luminance(_colourOut.rgb);
}