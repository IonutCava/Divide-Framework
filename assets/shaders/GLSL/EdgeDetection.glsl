//ref: https://github.com/dmnsgn/glsl-smaa

-- Vertex

#include "nodeDataInput.cmn"

layout(location = 0) out vec4 vOffset[3];

void main(void)
{
    vec2 resolution = dvd_ViewPort.zw;

    vec4 RT_METRICS = vec4(1.0f / resolution.x, 1.0f / resolution.y, resolution.x, resolution.y);

    vec2 uv = vec2(0, 0);
    if ((gl_VertexID & 1) != 0)uv.x = 1;
    if ((gl_VertexID & 2) != 0)uv.y = 1;

    VAR._texCoord = uv * 2;

    vOffset[0] = mad(RT_METRICS.xyxy, vec4(-1.0, 0.0, 0.0, -1.0), VAR._texCoord.xyxy);
    vOffset[1] = mad(RT_METRICS.xyxy, vec4(1.0, 0.0, 0.0, 1.0), VAR._texCoord.xyxy);
    vOffset[2] = mad(RT_METRICS.xyxy, vec4(-2.0, 0.0, 0.0, -2.0), VAR._texCoord.xyxy);

    gl_Position.xy = uv * 4 - 1;
    gl_Position.zw = vec2(0, 1);
}

--Fragment.Depth

uniform float dvd_edgeThreshold;

#ifndef DEPTH_THRESHOLD
#define DEPTH_THRESHOLD (0.1 * dvd_edgeThreshold)
#endif

layout(location = 0) in vec4 vOffset[3];

out vec2 _colourOut;

layout(binding = TEXTURE_DEPTH) uniform sampler2D texDepth;

/**
 * Gathers current pixel, and the top-left neighbors.
 */
vec3 GatherNeighbours(vec2 texcoord, vec4 offset[3], sampler2D tex) {
    float P = texture2D(tex, texcoord).r;
    float Pleft = texture2D(tex, offset[0].xy).r;
    float Ptop = texture2D(tex, offset[0].zw).r;

    return vec3(P, Pleft, Ptop);
}

void main(){
    vec3 neighbours = GatherNeighbours(VAR._texCoord, vOffset, texDepth);
    vec2 delta = abs(neighbours.xx - vec2(neighbours.y, neighbours.z));
    vec2 edges = step(DEPTH_THRESHOLD, delta);

    if (dot(edges, vec2(1.f, 1.f)) == 0.f) {
        discard;
    }

    _colourOut = edges;
}


-- Fragment.Luma

#ifndef _LOCAL_CONTRAST_ADAPTATION_FACTOR
#define _LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0
#endif

layout(location = 0) in vec4 vOffset[3];
uniform float dvd_edgeThreshold;

out vec2 _colourOut;

layout(binding = TEXTURE_UNIT0) uniform sampler2D texScreen;

void main() {
    vec2 vTexCoord0 = VAR._texCoord;
    vec2 threshold = vec2(dvd_edgeThreshold);

    // Calculate lumas:
    vec3 weights = vec3(0.2126, 0.7152, 0.0722);
    float L = dot(texture(texScreen, vTexCoord0).rgb, weights);

    float Lleft = dot(texture(texScreen, vOffset[0].xy).rgb, weights);
    float Ltop = dot(texture(texScreen, vOffset[0].zw).rgb, weights);

    // We do the usual threshold:
    vec4 delta;
    delta.xy = abs(L - vec2(Lleft, Ltop));
    vec2 edges = step(threshold, delta.xy);

    // Then discard if there is no edge:
    if (dot(edges, vec2(1.f, 1.f)) == 0.f)
        discard;

    // Calculate right and bottom deltas:
    float Lright = dot(texture(texScreen, vOffset[1].xy).rgb, weights);
    float Lbottom = dot(texture(texScreen, vOffset[1].zw).rgb, weights);
    delta.zw = abs(L - vec2(Lright, Lbottom));

    // Calculate the maximum delta in the direct neighborhood:
    vec2 maxDelta = max(delta.xy, delta.zw);

    // Calculate left-left and top-top deltas:
    float Lleftleft = dot(texture(texScreen, vOffset[2].xy).rgb, weights);
    float Ltoptop = dot(texture(texScreen, vOffset[2].zw).rgb, weights);
    delta.zw = abs(vec2(Lleft, Ltop) - vec2(Lleftleft, Ltoptop));

    // Calculate the final maximum delta:
    maxDelta = max(maxDelta.xy, delta.zw);
    float finalDelta = max(maxDelta.x, maxDelta.y);

    // Local contrast adaptation:
    edges.xy *= step(finalDelta, _LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);

    _colourOut = edges;
}

--Fragment.Colour

uniform float dvd_edgeThreshold;

#ifndef _LOCAL_CONTRAST_ADAPTATION_FACTOR
#define _LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0
#endif

layout(location = 0) in vec4 vOffset[3];

out vec2 _colourOut;

layout(binding = TEXTURE_UNIT0) uniform sampler2D texScreen;

void main() {
    // Calculate the threshold:
    vec2 vTexCoord0 = VAR._texCoord;
    vec2 threshold = vec2(dvd_edgeThreshold);

    // Calculate color deltas:
    vec4 delta;
    vec3 c = texture(texScreen, vTexCoord0).rgb;

    vec3 cLeft = texture(texScreen, vOffset[0].xy).rgb;
    vec3 t = abs(c - cLeft);
    delta.x = max(max(t.r, t.g), t.b);

    vec3 cTop = texture(texScreen, vOffset[0].zw).rgb;
    t = abs(c - cTop);
    delta.y = max(max(t.r, t.g), t.b);

    // We do the usual threshold:
    vec2 edges = step(threshold, delta.xy);

    // Then discard if there is no edge:
    if (dot(edges, vec2(1.f, 1.f)) == 0.f)
        discard;

    // Calculate right and bottom deltas:
    vec3 cRight = texture(texScreen, vOffset[1].xy).rgb;
    t = abs(c - cRight);
    delta.z = max(max(t.r, t.g), t.b);

    vec3 cBottom = texture(texScreen, vOffset[1].zw).rgb;
    t = abs(c - cBottom);
    delta.w = max(max(t.r, t.g), t.b);

    // Calculate the maximum delta in the direct neighborhood:
    vec2 maxDelta = max(delta.xy, delta.zw);

    // Calculate left-left and top-top deltas:
    vec3 cLeftLeft = texture(texScreen, vOffset[2].xy).rgb;
    t = abs(c - cLeftLeft);
    delta.z = max(max(t.r, t.g), t.b);

    vec3 cTopTop = texture(texScreen, vOffset[2].zw).rgb;
    t = abs(c - cTopTop);
    delta.w = max(max(t.r, t.g), t.b);

    // Calculate the final maximum delta:
    maxDelta = max(maxDelta.xy, delta.zw);
    float finalDelta = max(maxDelta.x, maxDelta.y);

    // Local contrast adaptation:
    edges.xy *= step(finalDelta, _LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);

    _colourOut = edges;
}


