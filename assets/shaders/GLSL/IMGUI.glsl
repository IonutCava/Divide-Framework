-- Vertex

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

layout(binding = TEXTURE_UNIT0) uniform sampler2DArray Texture;

layout(location = 0) in vec2 Frag_UV;
layout(location = 1) in vec4 Frag_Color;

out vec4 Out_Color;

uniform ivec4 toggleChannel;
uniform vec2 depthRange;
uniform int depthTexture;
uniform uint layer = 0u;
uniform int flip;

void main()
{
    Out_Color = Frag_Color;
    vec2 uv = Frag_UV.st;
    if (flip == 1) {
        uv.t = 1.0f - uv.t;
    }

    const vec2 zPlanes = dvd_zPlanes * depthRange;
#if 0
    vec4 texColor = texture( Texture, vec3(uv, float(layer)) );
    if (depthTexture == 1) {
        texColor = vec4((ToLinearDepth(texColor.r, zPlanes) / zPlanes.y) * toggleChannel[0]);
        Out_Color *= texColor;
    } else {
        if (toggleChannel.r == 1 && toggleChannel.gba == ivec3(0)) {
            Out_Color.rgb *= vec3(texColor.r);
        } else if (toggleChannel.g == 1 && toggleChannel.rba == ivec3(0)) {
            Out_Color.rgb *= vec3(texColor.g);
        } else if (toggleChannel.b == 1 && toggleChannel.rga == ivec3(0)) {
            Out_Color.rgb *= vec3(texColor.b);
        } else if (toggleChannel.rgb == ivec3(0)) {
            Out_Color.rgb *= vec3(texColor.a);
        } else {
            Out_Color.rgb *= toggleChannel.xyz;
            if (toggleChannel.w == 0) {
                Out_Color.w = 1.0f;
            }
            Out_Color *= texColor;
        }
    }
#else 
    vec4 texColor = texture(Texture, vec3(uv,float(layer)));
    if (depthTexture == 1) {
        texColor = vec4((ToLinearDepth(texColor.r, zPlanes) / zPlanes.y) * toggleChannel[0]);
    } else {
        Out_Color.xyz *= toggleChannel.xyz;
        if (toggleChannel.w == 0) {
            Out_Color.w = 1.0f;
        }
    }

    Out_Color *= texColor;
#endif
};