--Geometry

layout(points, invocations = MAX_CSM_SPLITS_PER_LIGHT) in;
layout(triangle_strip, max_vertices = 4) out;

uniform int layerCount;
uniform int layerOffsetWrite;

layout(location = ATTRIB_FREE_START) out flat int layerIndex;

void main() {
    layerIndex = gl_InvocationID;

    if (gl_InvocationID < layerCount) {
        gl_Layer = gl_InvocationID + layerOffsetWrite;
        gl_Position = vec4(1.0, 1.0, 0.0, 1.0);
        _out._texCoord = vec2(1.0, 1.0);
        EmitVertex();

        gl_Layer = gl_InvocationID + layerOffsetWrite;
        gl_Position = vec4(-1.0, 1.0, 0.0, 1.0);
        _out._texCoord = vec2(0.0, 1.0f);
        EmitVertex();

        gl_Layer = gl_InvocationID + layerOffsetWrite;
        gl_Position = vec4(1.0, -1.0, 0.0, 1.0);
        _out._texCoord = vec2(1.0, 0.0);
        EmitVertex();

        gl_Layer = gl_InvocationID + layerOffsetWrite;
        gl_Position = vec4(-1.0, -1.0, 0.0, 1.0);
        _out._texCoord = vec2(0.0, 0.0);
        EmitVertex();

        EndPrimitive();
    }
}

--Fragment

uniform int layerOffsetRead;

layout(location = ATTRIB_FREE_START) in flat int layerIndex;

layout(location = 0) out vec2 _outColour;

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2DArray texDepth;

#include "vsm.frag"

void main(void)
{
    const float depth = texture(texDepth, vec3(VAR._texCoord.x, VAR._texCoord.y, layerOffsetRead + layerIndex)).r;

    _outColour = computeMoments(depth);
}

--Fragment.MSAA

uniform vec2 TextureSize;
uniform int layerOffsetRead;
uniform int NumSamples;

layout(location = ATTRIB_FREE_START) in flat int layerIndex;

layout(location = 0) out vec2 _outColour;

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2DMSArray texDepth;

#include "vsm.frag"

void main(void)
{
    const vec2 TextureSpaceCoords = vec2(VAR._texCoord.x * TextureSize.x, VAR._texCoord.y * TextureSize.y);
    const ivec3 TexCoords = ivec3(TextureSpaceCoords.x, TextureSpaceCoords.y, layerOffsetRead + layerIndex);

    float depth = 0.0f;
    for (int i = 0; i < NumSamples; ++i) {
        depth += texelFetch(texDepth, TexCoords, i).r;
    }

    _outColour = computeMoments(depth / NumSamples);
}