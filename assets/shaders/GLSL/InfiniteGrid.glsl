-- Vertex

#include "nodeDataInput.cmn"

//ref: http://asliceofrendering.com/scene%20helper/2020/01/05/InfiniteGrid/
layout(location = ATTRIB_POSITION) in vec3 inVertexData;

layout(location = ATTRIB_FREE_START + 0) out vec3 nearPoint;
layout(location = ATTRIB_FREE_START + 1) out vec3 farPoint;

vec3 UnprojectPoint(in float x, in  float y, in  float z) {
    const vec4 transformedPoint = dvd_InverseViewMatrix * dvd_InverseProjectionMatrix * vec4(x, y, z, 1.f);
    return Homogenize(transformedPoint);
}

void main()
{
    nearPoint = UnprojectPoint(inVertexData.x, inVertexData.y, 0.f);
    farPoint = UnprojectPoint(inVertexData.x, inVertexData.y, 1.f);
    gl_Position = vec4(inVertexData, 1.f);
}

-- Fragment

//ref: http://asliceofrendering.com/scene%20helper/2020/01/05/InfiniteGrid/
//ref: https://github.com/martin-pr/possumwood/wiki/Infinite-ground-plane-using-GLSL-shaders
//ref: https://github.com/ToniPlays/Hazard/blob/db44cf26bcb128231f8ff3189594ba4a25f25eca/HazardEditor/res/Shaders/Grid.glsl

#include "utility.frag"
#include "output.frag"
layout(location = ATTRIB_FREE_START + 0) in vec3 nearPoint;
layout(location = ATTRIB_FREE_START + 1) in vec3 farPoint;

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


void main()
{
    const float t = -nearPoint.y / (farPoint.y - nearPoint.y);
    const vec3 fragPos3D = nearPoint + t * (farPoint - nearPoint);
    float fade_factor = length(dvd_CameraPosition.xz - fragPos3D.xz) * 10.f;
    fade_factor = Saturate(1.f - (fade_factor / dvd_ZPlanes.y));

    vec4 outColour = grid(fragPos3D, gridScale) * float(t > 0); 
    outColour.a *= fade_factor;
    gl_FragDepth = computeDepth(dvd_ViewMatrix * vec4(fragPos3D, 1.f), DEPTH_RANGE);
    writeScreenColour(outColour);
}