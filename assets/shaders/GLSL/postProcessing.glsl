-- Fragment

#include "utility.frag"
#include "sceneData.cmn"
layout(location = TARGET_ALBEDO) out vec4 _colourOut;

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texScreen;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 1) uniform sampler2D texVignette;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 2) uniform sampler2D texNoise;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 3) uniform sampler2D texWaterNoiseNM;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 4) uniform sampler2D texLinearDepth;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 5) uniform sampler2D texDepth;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 6) uniform sampler2D texSSR;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 7) uniform sampler2D texSceneData;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 8) uniform sampler2D texSceneVelocity;

uniform vec4 _fadeColour;
uniform mat4 _invProjectionMatrix;
uniform vec2 _zPlanes;
uniform float _noiseTile;
uniform float _noiseFactor;
uniform float randomCoeffNoise;
uniform float randomCoeffFlash;
uniform float _fadeStrength;
uniform uint vignetteEnabled;
uniform uint noiseEnabled;
uniform uint underwaterEnabled;
uniform uint lutCorrectionEnabled;
uniform uint _fadeActive;

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

    vec4 colour = underwaterEnabled != 0u? Underwater() : texture(texScreen, VAR._texCoord);

    if (noiseEnabled != 0u) {
        colour = Noise(colour);
    }
    if (vignetteEnabled != 0u) {
        colour = Vignette(colour);
    }
    if (lutCorrectionEnabled != 0u) {
        colour = LUTCorrect(colour);
    }

    if (_fadeActive != 0u) {
        colour = mix(colour, _fadeColour, _fadeStrength);
    }

    _colourOut = colour;
}
