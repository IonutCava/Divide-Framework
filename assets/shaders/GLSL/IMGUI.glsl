-- Vertex

#include "nodeDataInput.cmn"

layout(location = ATTRIB_POSITION) in vec3 inVertexData;
layout(location = ATTRIB_TEXCOORD) in vec2 inTexCoordData;
layout(location = ATTRIB_COLOR)    in vec4 inColourData;
layout(location = ATTRIB_GENERIC)  in vec2 inGenericData;

layout(location = 0) out vec2 Frag_UV;
layout(location = 1) out vec4 Frag_Color;

void main()
{
    Frag_UV = inTexCoordData;
    Frag_Color = inColourData;
    gl_Position = dvd_ViewProjectionMatrix * vec4(inGenericData,0,1);
};

-- Fragment

#include "utility.frag"

layout(binding = TEXTURE_UNIT0) uniform sampler2D Texture;
layout(binding = TEXTURE_UNIT1) uniform sampler2DArray Texture2;

layout(location = 0) in vec2 Frag_UV;
layout(location = 1) in vec4 Frag_Color;

out vec4 Out_Color;

uniform ivec4 toggleChannel;
uniform vec2 depthRange;
uniform uint layer = 0u;
uniform uint mip = 0u;
uniform uint textureType = 0u;
uniform bool depthTexture;
uniform bool flip = false;

void main()
{
    Out_Color = Frag_Color;
    vec2 uv = Frag_UV.st;
    if (flip) {
        uv.t = 1.f - uv.t;
    }

    const vec2 zPlanes = dvd_zPlanes * depthRange;

    vec4 texColor = vec4(0.f);
    if (textureType == 0u) {
        texColor = textureLod(Texture, uv, mip * 1.f);
    } else {
        texColor = textureLod(Texture2, vec3(uv, float(layer)), mip * 1.f);
    }

    if (depthTexture) {
        texColor = vec4((ToLinearDepth(texColor.r, zPlanes) / zPlanes.y) * toggleChannel[0]);
    } else {
        Out_Color.xyz *= toggleChannel.xyz;
        if (toggleChannel.w == 0) {
            Out_Color.a = 1.f;
            texColor.a = 1.f;
        }
    }

    Out_Color *= texColor;
};
