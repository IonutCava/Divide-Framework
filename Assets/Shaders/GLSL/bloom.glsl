-- Fragment.BloomApply

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texScreen;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 1) uniform sampler2D texBloom;

layout(location = 0) out vec4 _colourOut;

void main() {
    vec3 hdrColour = texture(texScreen, VAR._texCoord).rgb;
    const vec3 bloom = texture(texBloom, VAR._texCoord).rgb;
    _colourOut.rgb = hdrColour + bloom; //additive blending
}

-- Fragment.BloomCalc

layout(location = 0) out vec4 _bloomOut;

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texScreen;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 1) uniform sampler2D texAvgLuminance;

void main() {
    const float luminanceBias = 0.8f;
    vec4 screenColour = texture(texScreen, VAR._texCoord);
    float avgLuminance = texture(texAvgLuminance, VAR._texCoord ).r;
    if (dot(screenColour.rgb, vec3(0.2126f, 0.7152f, 0.0722f)) > avgLuminance + Saturate( luminanceBias ))
    {
        _bloomOut = screenColour;
    }
    else
    {
        _bloomOut = vec4(0.f);
    }
}


-- Fragment.BloomDownscale

#include "utility.frag"

//ref: https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom

// This shader performs downsampling on a texture,
// as taken from Call Of Duty method, presented at ACM Siggraph 2014.
// This particular method was customly designed to eliminate
// "pulsating artifacts and temporal stability issues".

// Remember to add bilinear minification filter for this texture!
// Remember to use a floating-point texture format (for HDR)!
// Remember to use edge clamping for this texture!
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D srcTexture;

layout(location = 0) out vec3 _downsample;

float RGBToLuminance(vec3 col)
{
    return dot(col, vec3(0.2126f, 0.7152f, 0.0722f));
}

float KarisAverage(vec3 col)
{
    // Formula is 1 / (1 + luma)
    float luma = RGBToLuminance(ToSRGB(col)) * 0.25f;
    return 1.0f / (1.0f + luma);
}

void main()
{
    const float x = invSrcResolution.x;
    const float y = invSrcResolution.y;

    // Take 13 samples around current texel:
    // a - b - c
    // - j - k -
    // d - e - f
    // - l - m -
    // g - h - i
    // === ('e' is the current texel) ===
    vec3 a = texture(srcTexture, vec2(VAR._texCoord.x - 2 * x, VAR._texCoord.y + 2 * y)).rgb;
    vec3 b = texture(srcTexture, vec2(VAR._texCoord.x, VAR._texCoord.y + 2 * y)).rgb;
    vec3 c = texture(srcTexture, vec2(VAR._texCoord.x + 2 * x, VAR._texCoord.y + 2 * y)).rgb;

    vec3 d = texture(srcTexture, vec2(VAR._texCoord.x - 2 * x, VAR._texCoord.y)).rgb;
    vec3 e = texture(srcTexture, vec2(VAR._texCoord.x, VAR._texCoord.y)).rgb;
    vec3 f = texture(srcTexture, vec2(VAR._texCoord.x + 2 * x, VAR._texCoord.y)).rgb;

    vec3 g = texture(srcTexture, vec2(VAR._texCoord.x - 2 * x, VAR._texCoord.y - 2 * y)).rgb;
    vec3 h = texture(srcTexture, vec2(VAR._texCoord.x, VAR._texCoord.y - 2 * y)).rgb;
    vec3 i = texture(srcTexture, vec2(VAR._texCoord.x + 2 * x, VAR._texCoord.y - 2 * y)).rgb;

    vec3 j = texture(srcTexture, vec2(VAR._texCoord.x - x, VAR._texCoord.y + y)).rgb;
    vec3 k = texture(srcTexture, vec2(VAR._texCoord.x + x, VAR._texCoord.y + y)).rgb;
    vec3 l = texture(srcTexture, vec2(VAR._texCoord.x - x, VAR._texCoord.y - y)).rgb;
    vec3 m = texture(srcTexture, vec2(VAR._texCoord.x + x, VAR._texCoord.y - y)).rgb;

    // Apply weighted distribution:
    // 0.5 + 0.125 + 0.125 + 0.125 + 0.125 = 1
    // a,b,d,e * 0.125
    // b,c,e,f * 0.125
    // d,e,g,h * 0.125
    // e,f,h,i * 0.125
    // j,k,l,m * 0.5
    // This shows 5 square areas that are being sampled. But some of them overlap,
    // so to have an energy preserving downsample we need to make some adjustments.
    // The weights are the distributed, so that the sum of j,k,l,m (e.g.)
    // contribute 0.5 to the final color output. The code below is written
    // to effectively yield this sum. We get:
    // 0.125*5 + 0.03125*4 + 0.0625*4 = 1

    if (performKarisAverage)
    {
        vec3 groups[5];
        // We are writing to mip 0, so we need to apply Karis average to each block
        // of 4 samples to prevent fireflies (very bright subpixels, leads to pulsating artifacts).
        groups[0] = (a + b + d + e) * (0.125f / 4.0f);
        groups[1] = (b + c + e + f) * (0.125f / 4.0f);
        groups[2] = (d + e + g + h) * (0.125f / 4.0f);
        groups[3] = (e + f + h + i) * (0.125f / 4.0f);
        groups[4] = (j + k + l + m) * (0.5f / 4.0f);
        groups[0] *= KarisAverage(groups[0]);
        groups[1] *= KarisAverage(groups[1]);
        groups[2] *= KarisAverage(groups[2]);
        groups[3] *= KarisAverage(groups[3]);
        groups[4] *= KarisAverage(groups[4]);
        _downsample = groups[0] + groups[1] + groups[2] + groups[3] + groups[4];
    }
    else
    {
        _downsample = e * 0.125;
        _downsample += (a + c + g + i) * 0.03125;
        _downsample += (b + d + f + h) * 0.0625;
        _downsample += (j + k + l + m) * 0.125;
        _downsample = max(_downsample, M_EPSILON);
    }
}

-- Fragment.BloomUpscale
//ref: https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom

// This shader performs upsampling on a texture,
// as taken from Call Of Duty method, presented at ACM Siggraph 2014.

// Remember to add bilinear minification filter for this texture!
// Remember to use a floating-point texture format (for HDR)!
// Remember to use edge clamping for this texture!
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D srcTexture;

layout(location = 0) out vec3 _upsample;

void main()
{
    // The filter kernel is applied with a radius, specified in texture
    // coordinates, so that the radius will vary across mip resolutions.
    const float x = filterRadius;
    const float y = filterRadius;

    // Take 9 samples around current texel:
    // a - b - c
    // d - e - f
    // g - h - i
    // === ('e' is the current texel) ===
    vec3 a = texture(srcTexture, vec2(VAR._texCoord.x - x, VAR._texCoord.y + y)).rgb;
    vec3 b = texture(srcTexture, vec2(VAR._texCoord.x, VAR._texCoord.y + y)).rgb;
    vec3 c = texture(srcTexture, vec2(VAR._texCoord.x + x, VAR._texCoord.y + y)).rgb;

    vec3 d = texture(srcTexture, vec2(VAR._texCoord.x - x, VAR._texCoord.y)).rgb;
    vec3 e = texture(srcTexture, vec2(VAR._texCoord.x, VAR._texCoord.y)).rgb;
    vec3 f = texture(srcTexture, vec2(VAR._texCoord.x + x, VAR._texCoord.y)).rgb;

    vec3 g = texture(srcTexture, vec2(VAR._texCoord.x - x, VAR._texCoord.y - y)).rgb;
    vec3 h = texture(srcTexture, vec2(VAR._texCoord.x, VAR._texCoord.y - y)).rgb;
    vec3 i = texture(srcTexture, vec2(VAR._texCoord.x + x, VAR._texCoord.y - y)).rgb;

    // Apply weighted distribution, by using a 3x3 tent filter:
    //  1   | 1 2 1 |
    // -- * | 2 4 2 |
    // 16   | 1 2 1 |
    _upsample = e * 4.0;
    _upsample += (b + d + f + h) * 2.0;
    _upsample += (a + c + g + i);
    _upsample *= 1.0 / 16.0;
}