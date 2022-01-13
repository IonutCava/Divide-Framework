-- Vertex

#include "nodeDataInput.cmn"

//ref: http://asliceofrendering.com/scene%20helper/2020/01/05/InfiniteGrid/
layout(location = ATTRIB_POSITION) in vec3 inVertexData;

layout(location = 10) out vec3 nearPoint;
layout(location = 11) out vec3 farPoint;

vec3 UnprojectPoint(in float x, in  float y, in  float z) {
    const vec4 transformedPoint = dvd_InverseViewMatrix * dvd_InverseProjectionMatrix * vec4(x, y, z, 1.f);
    return Homogenize(transformedPoint);
}

void main()
{
    nearPoint = UnprojectPoint(inVertexData.x, inVertexData.y, 0.f);
    farPoint = UnprojectPoint(inVertexData.x, inVertexData.y, 1.f);
    gl_Position = vec4(inVertexData, 1.f);
};

-- Fragment

//ref: http://asliceofrendering.com/scene%20helper/2020/01/05/InfiniteGrid/
//ref: https://github.com/martin-pr/possumwood/wiki/Infinite-ground-plane-using-GLSL-shaders
//ref: https://github.com/ToniPlays/Hazard/blob/db44cf26bcb128231f8ff3189594ba4a25f25eca/HazardEditor/res/Shaders/Grid.glsl
#include "output.frag"

layout(location = 10) in vec3 nearPoint;
layout(location = 11) in vec3 farPoint;

uniform float axisWidth = 2.f;
uniform float gridScale = 1.f;

vec4 grid(vec3 fragPos3D, float scale) {
    vec2 coord = fragPos3D.xz * scale;
    vec2 derivative = fwidth(coord);
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;
    float line = min(grid.x, grid.y);
    float minimumz = min(derivative.y, 1);
    float minimumx = min(derivative.x, 1);
    vec4 color = vec4(0.2, 0.2, 0.2, 1.0 - min(line, 1.0));
    // z axis
    if (fragPos3D.x > -axisWidth * minimumx && fragPos3D.x < axisWidth * minimumx) {
        color = vec4(0.0, 0.5, 1.0, 0.8);
    }
    // x axis
    if (fragPos3D.z > -axisWidth * minimumz && fragPos3D.z < axisWidth * minimumz) {
        color = vec4(1.0, 0.0, 0.0, 0.8);
    }
    return color;
}

float computeDepth(vec3 pos) {
    vec4 clip_space_pos = dvd_ViewProjectionMatrix * vec4(pos.xyz, 1.0);
    float clip_space_depth = clip_space_pos.z / clip_space_pos.w;

    float far = gl_DepthRange.far;
    float near = gl_DepthRange.near;

    float depth = (((far - near) * clip_space_depth) + near + far) / 2.0;

    return depth;
}

void main()
{
    const float t = -nearPoint.y / (farPoint.y - nearPoint.y);
    const vec3 fragPos3D = nearPoint + t * (farPoint - nearPoint);
    float fade_factor = length(dvd_cameraPosition.xz - fragPos3D.xz);
    fade_factor = Saturate(1.f - (fade_factor / dvd_zPlanes.y));

    vec4 outColour = grid(fragPos3D, gridScale) * float(t > 0);
    outColour.a *= fade_factor;
    gl_FragDepth = computeDepth(fragPos3D);
    writeScreenColour(outColour);
};