-- Fragment

#include "utility.frag"

layout(location = 0) out vec4 _colourOut;

uniform uint channelCount;
uniform uint startChannel;
uniform float lodLevel;
uniform float multiplier;
uniform uint channelsArePacked;
uniform uint scaleAndBias;
uniform uint normalizeOutput;

DESCRIPTOR_SET_RESOURCE(PER_DRAW_SET, TEXTURE_UNIT0) uniform sampler2D texDiffuse0;

vec3 error() {
    const float total = floor(VAR._texCoord.x * float(dvd_ScreenDimensions.x)) +
                        floor(VAR._texCoord.y * float(dvd_ScreenDimensions.y));
    return (mod(total, 2.f) == 0.f ? vec3(1.f, 0.f, 0.f) : vec3(0.f, 1.f, 0.f));
}

void main()
{
    const vec4 colourIn = textureLod(texDiffuse0, VAR._texCoord, lodLevel);
    
    if (channelsArePacked != 0u) {
        if (channelCount == 1) {
            _colourOut.rgb = vec3(unpackVec2(colourIn[startChannel]), 0.f);
        } else if (channelCount == 2) {
            _colourOut.rgb = unpackNormal(vec2(colourIn[startChannel + 0],
                                               colourIn[startChannel + 1]));
        } else {
            _colourOut.rgb = error();
        }
    } else {
        if (channelCount == 1) {
            _colourOut.rgb = vec3(colourIn[startChannel]);
        } else if (channelCount == 2) {
            _colourOut.rgb = vec3(colourIn[startChannel + 0],
                                  colourIn[startChannel + 1],
                                  0.f);
        } else if (channelCount == 3) {
            _colourOut.rgb = vec3(colourIn[startChannel + 0],
                                  colourIn[startChannel + 1],
                                  colourIn[startChannel + 2]);
        } else if (channelCount == 4) {
            _colourOut = colourIn;
        } else {
            _colourOut.rgb = error();
        }
    }
    if (normalizeOutput != 0u) {
        _colourOut.rgb = normalize(_colourOut.rgb);
    }
    if (scaleAndBias != 0u) {
        _colourOut.rgb = 0.5f * _colourOut.rgb + 0.5f;
    }
    _colourOut.rgb *= multiplier;
}

-- Fragment.LinearDepth

#include "utility.frag"

layout(location = 0) out vec4 _colourOut;

uniform vec2 _zPlanes;
uniform float lodLevel;

DESCRIPTOR_SET_RESOURCE(PER_DRAW_SET, TEXTURE_UNIT0) uniform sampler2D texDiffuse0;

void main()
{
    const float linearDepth = ToLinearDepth(textureLod(texDiffuse0, VAR._texCoord, lodLevel).r, _zPlanes);
    /// Map back to [0 ... 1] range
    _colourOut = vec4(vec3(linearDepth / _zPlanes.y), 1.0);
}

-- Fragment.Layered

layout(location = 0) out vec4 _colourOut;

DESCRIPTOR_SET_RESOURCE(PER_DRAW_SET, TEXTURE_UNIT0) uniform sampler2DArray texDiffuse0;

uniform float lodLevel;
uniform int layer;

void main()
{
    _colourOut = textureLod(texDiffuse0, vec3(VAR._texCoord, layer), lodLevel);
    _colourOut.a = 1.f;
}

--Fragment.Layered.LinearDepth

#include "utility.frag"

layout(location = 0) out vec4 _colourOut;

DESCRIPTOR_SET_RESOURCE(PER_DRAW_SET, TEXTURE_UNIT0) uniform sampler2DArray texDiffuse0;

uniform int layer;
uniform float lodLevel;

void main()
{
    float linearDepth = textureLod(texDiffuse0, vec3(VAR._texCoord, layer), lodLevel).r;
    _colourOut = vec4(vec3(linearDepth), 1.f);
}

--Fragment.Cube

#include "utility.frag"

layout(location = 0) out vec4 _colourOut;

DESCRIPTOR_SET_RESOURCE(PER_DRAW_SET, TEXTURE_UNIT0) uniform samplerCubeArray texDiffuse0;

uniform int layer;
uniform int face;
uniform float lodLevel;

void main() {
    const vec2 uv_cube = 2.f * VAR._texCoord - 1.f;
    vec3 vertex = vec3(0);
    switch (face) {
        case 0: vertex.xyz = vec3( 1.f,       uv_cube.y,  uv_cube.x); break;
        case 1: vertex.xyz = vec3(-1.f,       uv_cube.y, -uv_cube.x); break;
        case 2: vertex.xyz = vec3( uv_cube.x,  1.f,       uv_cube.y); break;
        case 3: vertex.xyz = vec3( uv_cube.x, -1.f,      -uv_cube.y); break;
        case 4: vertex.xyz = vec3(-uv_cube.x, uv_cube.y,  1.f);       break;
        case 5: vertex.xyz = vec3( uv_cube.x, uv_cube.y, -1.f);       break;
    };
    const vec4 coords = vec4(vertex, layer);
#if defined(SPLAT_R_CHANNEL)
    _colourOut = vec4(vec3(textureLod(texDiffuse0, coords, lodLevel).r), 1.f);
#else //SPLAT_R_CHANNEL
    _colourOut = textureLod(texDiffuse0, coords, lodLevel);
#endif //SPLAT_R_CHANNEL
}