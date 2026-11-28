-- Fragment.Occlusion

#include "utility.frag"

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texDepth;

layout(location = 0) out vec3 _colourOut;

void main()
{
    const float depth = texture(texDepth, VAR._texCoord).r;
    // Far-plane / sky samples contribute shafts; geometry occludes them.
    const float mask = depth >= dvd_depthThreshold ? 1.f : 0.f;
    _colourOut = dvd_lightColour.rgb * mask;
}

-- Fragment.RadialBlur

#include "utility.frag"

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texOcclusion;

layout(location = 0) out vec3 _colourOut;

void main()
{
    vec2 texCoord = VAR._texCoord;
    const vec2 delta = (dvd_lightScreenPos.xy - texCoord) * dvd_density * (1.f / float(dvd_sampleCount));

    vec3 colour = vec3(0.f);
    float illuminationDecay = 1.f;

    for (uint i = 0u; i < dvd_sampleCount; ++i)
    {
        texCoord += delta;
        const vec3 sampleColour = texture(texOcclusion, clamp(texCoord, vec2(0.f), vec2(1.f))).rgb;
        colour += sampleColour * illuminationDecay * dvd_weight;
        illuminationDecay *= dvd_decay;
    }

    _colourOut = colour * dvd_exposure;
}
