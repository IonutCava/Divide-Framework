-- Fragment.BloomApply

layout(binding = TEXTURE_UNIT0) uniform sampler2D texScreen;
layout(binding = TEXTURE_UNIT1) uniform sampler2D texBloom;

out vec4 _colourOut;

void main() {
    vec3 hdrColour = texture(texScreen, VAR._texCoord).rgb;
    const vec3 bloom = texture(texBloom, VAR._texCoord).rgb;
    _colourOut.rgb = hdrColour + bloom; //additive blending
}

-- Fragment.BloomCalc

out vec4 _bloomOut;

uniform float luminanceThreshold;

layout(binding = TEXTURE_UNIT0) uniform sampler2D texScreen;

void main() {    
    vec4 screenColour = texture(texScreen, VAR._texCoord);
    if (dot(screenColour.rgb, vec3(0.2126f, 0.7152f, 0.0722f)) > luminanceThreshold) {
        _bloomOut = screenColour;
    } else {
        _bloomOut = vec4(0.f);
    }
}
