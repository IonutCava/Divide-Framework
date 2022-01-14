-- Fragment

#include "utility.frag"

#define NEED_SCENE_DATA
#include "sceneData.cmn"
layout(location = TARGET_ALBEDO) out vec4 _colourOut;

layout(binding = TEX_BIND_POINT_SCREEN)         uniform sampler2D texScreen;
layout(binding = TEX_BIND_POINT_NOISE)          uniform sampler2D texNoise;
layout(binding = TEX_BIND_POINT_BORDER)         uniform sampler2D texVignette;
layout(binding = TEX_BIND_POINT_UNDERWATER)     uniform sampler2D texWaterNoiseNM;
layout(binding = TEX_BIND_POINT_LINDEPTH)       uniform sampler2D texLinearDepth;
layout(binding = TEX_BIND_POINT_DEPTH)          uniform sampler2D texDepth;
layout(binding = TEX_BIND_POINT_SSR)            uniform sampler2D texSSR;
layout(binding = TEX_BIND_POINT_SCENE_DATA)     uniform sampler2D texSceneData;
layout(binding = TEX_BIND_POINT_SCENE_VELOCITY) uniform sampler2D texSceneVelocity;

uniform vec4 _fadeColour;
uniform mat4 _invProjectionMatrix;
uniform vec2 _zPlanes;
uniform float _noiseTile;
uniform float _noiseFactor;
uniform float randomCoeffNoise;
uniform float randomCoeffFlash;
uniform float _fadeStrength;
uniform bool vignetteEnabled;
uniform bool noiseEnabled;
uniform bool underwaterEnabled;
uniform bool lutCorrectionEnabled;
uniform bool _fadeActive;

vec4 Vignette(in vec4 colourIn){
    const vec4 colourOut = colourIn - (vec4(1.f,1.f,1.f,2.f) - texture(texVignette, VAR._texCoord));
    return vec4(Saturate(colourOut.rgb), colourOut.a);
}

vec4 LUTCorrect(in vec4 colourIn) {
    //ToDo: Implement this -Ionut
    return colourIn;
}

vec4 Noise(in vec4 colourIn){
    return mix(texture(texNoise, VAR._texCoord + vec2(randomCoeffNoise, randomCoeffNoise)),
               vec4(1.0), randomCoeffFlash) / 3.f + 2.f * LevelOfGrey(colourIn) / 3.f;
}

vec4 Underwater() {
    //ToDo: Move this code, and the one in water.glsl, to a common header (like utility.frag) in a function that both shaders can use
    const float time2 = MSToSeconds(dvd_TimeMS) * 0.015f;
    const vec2 uvNormal0 = (VAR._texCoord * _noiseTile) + vec2( time2, time2);
    const vec2 uvNormal1 = (VAR._texCoord * _noiseTile) + vec2(-time2, time2);

    const vec3 normal0 = 2.f * texture(texWaterNoiseNM, uvNormal0).rgb - 1.f;
    const vec3 normal1 = 2.f * texture(texWaterNoiseNM, uvNormal1).rgb - 1.f;
    const vec3 normal = normalize((normal0 + normal1) * 0.5f);

    const vec2 coords = VAR._texCoord + (normal.xy * _noiseFactor);
    return Saturate(texture(texScreen, coords) * vec4(0.35f));
}

void main(void){
    if (dvd_MaterialDebugFlag == DEBUG_DEPTH) {
        _colourOut = vec4(vec3(texture(texLinearDepth, VAR._texCoord).r / _zPlanes.y + _zPlanes.x), 1.0f);
        return;
    } else if (dvd_MaterialDebugFlag == DEBUG_SSR) {
        const vec3 ssr = texture(texSSR, VAR._texCoord).rgb;
        _colourOut = vec4(ssr, 1.f);
        return;
    } else if (dvd_MaterialDebugFlag == DEBUG_SSR_BLEND) {
        const float blend = texture(texSSR, VAR._texCoord).a;
        if (isnan(blend)) {
            _colourOut = vec4(vec3(1.f, 0.f, 0.f), 1.f);
        } else if (isinf(blend)) {
            _colourOut = vec4(vec3(0.f, 1.f, 0.f), 1.f);
        } else {
            _colourOut = vec4(vec3(blend), 1.f);
        }
        return;
    } else if (dvd_MaterialDebugFlag != DEBUG_NONE) {
        _colourOut = texture(texScreen, VAR._texCoord);
        return;
    }

    vec4 colour = underwaterEnabled ? Underwater() : texture(texScreen, VAR._texCoord);

    if (noiseEnabled) {
        colour = Noise(colour);
    }
    if (vignetteEnabled) {
        colour = Vignette(colour);
    }
    if (lutCorrectionEnabled) {
        colour = LUTCorrect(colour);
    }

    if (_fadeActive) {
        colour = mix(colour, _fadeColour, _fadeStrength);
    }

    _colourOut = colour;
}
