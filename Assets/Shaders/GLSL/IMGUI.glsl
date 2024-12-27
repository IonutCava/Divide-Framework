-- Vertex

#include "nodeDataInput.cmn"

layout(location = ATTRIB_TEXCOORD) in vec2 inTexCoordData;
layout(location = ATTRIB_COLOR)    in vec4 inColourData;
layout(location = ATTRIB_GENERIC)  in vec2 inGenericData;

layout(location = ATTRIB_FREE_START + 0) out vec2 Frag_UV;
layout(location = ATTRIB_FREE_START + 1) out vec4 Frag_Color;

void main()
{
    Frag_UV = inTexCoordData;
    Frag_Color = inColourData;
    gl_Position = dvd_ViewProjectionMatrix * vec4(inGenericData,0,1);
}

-- Fragment

#include "utility.frag"

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D Texture;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 1) uniform sampler2DArray Texture2;

layout(location = ATTRIB_FREE_START + 0) in vec2 Frag_UV;
layout(location = ATTRIB_FREE_START + 1) in vec4 Frag_Color;

layout(location = 0) out vec4 Out_Color;

void main()
{
    Out_Color = Frag_Color;
    vec2 uv = Frag_UV.st;
    if (flip != 0u) {
        uv.t = 1.f - uv.t;
    }

    const vec2 zPlanes = dvd_ZPlanes * depthRange;
    
    vec4 texColor = vec4(0.f, 0.f, 0.f, 1.f);
    if (textureType == 0u) {
        texColor = textureLod(Texture, uv, mip * 1.f);
    } else {
        texColor = textureLod(Texture2, vec3(uv, float(layer)), mip * 1.f);
    }

    if (depthTexture != 0u) {
        texColor.rgb = vec3((ToLinearDepth(texColor.r, zPlanes) / zPlanes.y) * toggleChannel[0]);
    } else {
        Out_Color.rgb *= toggleChannel.rgb;
        if (toggleChannel.w == 0)
        {
            Out_Color.a = 1.f;
            texColor.a = 1.f;
        }
    }

    Out_Color *= texColor;
    if ( !convertToSRGB )
    {
        Out_Color.rgb = ToLinearAccurate( Out_Color.rgb );
    }
}
