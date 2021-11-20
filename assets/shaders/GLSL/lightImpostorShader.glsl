-- Vertex

#include "lightInput.cmn"

layout(location = 0) out flat vec4 colourAndSize;
layout(location = 1) out flat vec2 texCoordOffset;

void main()
{
    const Light light = dvd_LightSource[gl_VertexID];

    switch (light._TYPE) {
        case LIGHT_DIRECTIONAL:     texCoordOffset = vec2(0.f, 0.f); break;
        case LIGHT_OMNIDIRECTIONAL: texCoordOffset = vec2(0.f, 1.f); break;
        case LIGHT_SPOT:            texCoordOffset = vec2(1.f, 1.f); break;
    }

    const vec4 positionAndSize = getPositionAndRangeForLight(light);

    colourAndSize = vec4(light._colour.rgb, positionAndSize.w);
    gl_Position = vec4(positionAndSize.xyz, 1.f);
}

-- Geometry

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

layout(location = 0) in flat vec4 colourAndSize[];
layout(location = 1) in flat vec2 texCoordOffset[];

layout(location = 0) out flat vec3 lightColour;

void main()
{
    const vec4 pos = gl_in[0].gl_Position;

    lightColour = colourAndSize[0].xyz;
    const float size = colourAndSize[0].w;

    // a: left-bottom 
    vec2 va = pos.xy + vec2(-0.5, -0.5) * size;
    gl_Position = dvd_ProjectionMatrix * vec4(va, pos.zw);
    _out._texCoord = vec2(0.0 + (texCoordOffset[0].x * 0.5),
                         (0.0 + (texCoordOffset[0].y * 0.5)));
    EmitVertex();

    // d: right-bottom
    vec2 vd = pos.xy + vec2(0.5, -0.5) * size;
    gl_Position = dvd_ProjectionMatrix * vec4(vd, pos.zw);
    _out._texCoord = vec2(0.5 + (texCoordOffset[0].x * 0.5),
                         (0.0 + (texCoordOffset[0].y * 0.5)));
    EmitVertex();

    // b: left-top
    vec2 vb = pos.xy + vec2(-0.5, 0.5) * size;
    gl_Position = dvd_ProjectionMatrix * vec4(vb, pos.zw);
    _out._texCoord = vec2(0.0 + (texCoordOffset[0].x * 0.5), 
                         (0.5 + (texCoordOffset[0].y * 0.5)));
    EmitVertex();
    
    // c: right-top
    vec2 vc = pos.xy + vec2(0.5, 0.5) * size;
    gl_Position = dvd_ProjectionMatrix * vec4(vc, pos.zw);
    _out._texCoord = vec2(0.5 + (texCoordOffset[0].x * 0.5),
                         (0.5 + (texCoordOffset[0].y * 0.5)));
    EmitVertex();

    EndPrimitive();
}

-- Fragment

#define COLOUR_OUTPUT_ONLY

layout(location = 0) in flat vec3 lightColour;

layout(binding = TEXTURE_UNIT0) uniform sampler2D texDiffuse0;

layout(location = TARGET_ALBEDO) out vec4 _colourOut;

void main()
{
    if (texture(texDiffuse0, VAR._texCoord).a < INV_Z_TEST_SIGMA) {
        discard;
    }
    
    _colourOut = vec4(lightColour, 1.f);
}