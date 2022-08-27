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

uniform float luminanceThreshold;

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texScreen;

void main() {
    vec4 screenColour = texture(texScreen, VAR._texCoord);
    if (dot(screenColour.rgb, vec3(0.2126f, 0.7152f, 0.0722f)) > luminanceThreshold) {
        _bloomOut = screenColour;
    } else {
        _bloomOut = vec4(0.f);
    }
}
