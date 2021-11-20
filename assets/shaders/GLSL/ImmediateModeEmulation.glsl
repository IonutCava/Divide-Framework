-- Vertex

layout(location = ATTRIB_POSITION) in vec3 inVertexData;
layout(location = ATTRIB_TEXCOORD) in vec2 inTexCoordData;
layout(location = ATTRIB_COLOR)    in vec4 inColourData;

uniform mat4 dvd_WorldMatrix;
uniform bool useTexture;

layout(location = 0) out vec4 _colour;


void main(){
  VAR._texCoord = inTexCoordData;
  _colour = inColourData;
  gl_Position = dvd_ViewProjectionMatrix * dvd_WorldMatrix * vec4(inVertexData,1.0);
} 

-- Fragment

#include "utility.frag"

layout(binding = TEXTURE_UNIT0) uniform sampler2D texDiffuse0;

uniform mat4 dvd_WorldMatrix;
uniform bool useTexture;

layout(location = 0) in  vec4 _colour;
layout(location = TARGET_ALBEDO) out vec4 _colourOut;

void main(){
    if(!useTexture){
        _colourOut = _colour;
    }else{
        _colourOut = texture(texDiffuse0, VAR._texCoord);
        _colourOut.rgb += _colour.rgb;
    }
}

--Vertex.GUI

layout(location = ATTRIB_POSITION) in vec3 inVertexData;
layout(location = ATTRIB_TEXCOORD) in vec2 inTexCoordData;
layout(location = ATTRIB_COLOR) in vec4 inColourData;

layout(location = 0) out vec4 _colour;

void main() {
    VAR._texCoord = inTexCoordData;
    _colour = inColourData;
    gl_Position = dvd_ViewProjectionMatrix * vec4(inVertexData, 1.0);
}

-- Fragment.GUI

layout(binding = TEXTURE_UNIT0) uniform sampler2D texDiffuse0;

layout(location = 0) in  vec4 _colour;

out vec4 _colourOut;

void main(){
    _colourOut = vec4(_colour.rgb, texture(texDiffuse0, VAR._texCoord).r);
}

--Vertex.EnvironmentProbe

#include "nodeBufferedInput.cmn"

uniform mat4 dvd_WorldMatrixOverride;
uniform uint dvd_LayerIndex;

void main(void) {
    vec3 dvd_Normal = UNPACK_VEC3(inNormalData);
    VAR._vertexW = dvd_WorldMatrixOverride * vec4(inVertexData, 1.0);
    VAR._vertexWV = dvd_ViewMatrix * VAR._vertexW;
    VAR._viewDirectionWV = mat3(dvd_ViewMatrix) * normalize(dvd_cameraPosition.xyz - VAR._vertexW.xyz);
    NodeTransformData nodeData = dvd_Transforms[DVD_GL_BASE_INSTANCE];
    mat3 normalMatrixWV = mat3(dvd_ViewMatrix) * dvd_NormalMatrixW(nodeData);
    VAR._normalWV = normalize(normalMatrixWV * dvd_Normal);
    //Compute the final vert position
    gl_Position = dvd_ProjectionMatrix * VAR._vertexWV;
}

--Fragment.EnvironmentProbe

#include "output.frag"

uniform mat4 dvd_WorldMatrixOverride;
uniform uint dvd_LayerIndex;

layout(binding = TEXTURE_REFLECTION_CUBE) uniform samplerCube texEnvironmentCube;

void main() {
    vec3 reflectDirection = reflect(-VAR._viewDirectionWV, VAR._normalWV);
    vec4 colour = vec4(texture(texEnvironmentCube, vec3(reflectDirection).rgb, 1.0);

    writeScreenColour(colour, VAR._normalWV);
}