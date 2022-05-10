-- Fragment

#include "utility.frag"

//ref: https://github.com/BruOp/bae

DESCRIPTOR_SET_RESOURCE(PER_DRAW_SET, TEXTURE_UNIT0) uniform sampler2D texScreen;
DESCRIPTOR_SET_RESOURCE(PER_DRAW_SET, TEXTURE_UNIT1) uniform sampler2D texExposure;

uniform float manualExposureFactor;
uniform int mappingFunction;
uniform uint useAdaptiveExposure;
uniform uint skipToneMapping;

layout(location = 0) out vec4 _colourOut;

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

    if (skipToneMapping == 0u && mappingFunction != NONE) {
        vec3 Yxy = vec3(0.f);

        if (useAdaptiveExposure != 0u) {
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