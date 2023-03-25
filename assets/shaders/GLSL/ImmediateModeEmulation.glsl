-- Vertex

#include "nodeDataInput.cmn"

layout(location = ATTRIB_POSITION) in vec3 inVertexData;
#if !defined(NO_TEXTURE)
layout(location = ATTRIB_TEXCOORD) in vec2 inTexCoordData;
#endif //!NO_TEXTURE
layout(location = ATTRIB_COLOR)    in vec4 inColourData;
layout(location = ATTRIB_GENERIC)  in vec2 inLineWidth; //x = start, y = end

uniform mat4 dvd_WorldMatrix;
uniform uint useTexture;

layout(location = ATTRIB_FREE_START + 0) out vec4 _colour;
layout(location = ATTRIB_FREE_START + 1) out vec2 _lineWidth;

void main(){
#if !defined(NO_TEXTURE)
  VAR._texCoord = inTexCoordData;
#endif //!NO_TEXTURE
  _colour = inColourData;
  _lineWidth = inLineWidth;
  gl_Position = dvd_ViewProjectionMatrix * dvd_WorldMatrix * vec4(inVertexData, 1.f);
} 

-- Fragment

#if defined(WORLD_PASS)
#define NO_POST_FX //ToDo: control this via uniforms -Ionut
#include "output.frag"
#else //WORLD_PASS
#include "utility.frag"
layout(location = TARGET_ALBEDO) out vec4 _colourOut;
#endif //WORLD_PASS

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texDiffuse0;

uniform mat4 dvd_WorldMatrix;
uniform uint useTexture;

layout(location = ATTRIB_FREE_START + 0) in vec4 _colour;
layout(location = ATTRIB_FREE_START + 1) in vec2 _lineWidth;

void main(){
#if defined(NO_TEXTURE)
    const vec4 colourTemp = _colour;
#else //NO_TEXTURE
    vec4 colourTemp;
    if (useTexture == 0u) {
        colourTemp = _colour;
    } else {
        colourTemp = texture(texDiffuse0, VAR._texCoord);
        colourTemp.rgb += _colour.rgb;
    }
#endif //NO_TEXTURE
#if defined(WORLD_PASS)
    writeScreenColour(colourTemp);
#else //WORLD_PASS
    _colourOut = colourTemp;
#endif //WORLD_PASS
}

--Vertex.GUI

#include "nodeDataInput.cmn"

layout(location = ATTRIB_POSITION) in vec2 inVertexData;
layout(location = ATTRIB_TEXCOORD) in vec2 inTexCoordData;
layout(location = ATTRIB_COLOR) in vec4 inColourData;

layout(location = ATTRIB_FREE_START + 0) out vec4 _colour;

void main() {
    VAR._texCoord = inTexCoordData;
    _colour = inColourData;
    gl_Position = dvd_ViewProjectionMatrix * vec4(inVertexData, 1.0, 1.0);
}

-- Fragment.GUI

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texDiffuse0;

layout(location = ATTRIB_FREE_START + 0) in  vec4 _colour;

layout(location = 0) out vec4 _colourOut;

void main(){
    _colourOut = vec4(_colour.rgb, texture(texDiffuse0, VAR._texCoord).r);
}
