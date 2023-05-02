-- Vertex

layout(location = ATTRIB_POSITION)  in vec3 inVertexData;
layout(location = ATTRIB_COLOR)     in vec4 inColourData;
layout(location = ATTRIB_NORMAL)    in vec4 particleNormalData;

// Output data will be interpolated for each fragment.
layout(location = ATTRIB_FREE_START + 0) out vec4 particleColour;

void main()
{
    vec3 camRighW = dvd_ViewMatrix[0].xyz;
    vec3 camUpW = dvd_ViewMatrix[1].xyz;

    float spriteSize = particleNormalData.w;
    vec3 vertexPositionWV = (dvd_ViewMatrix * vec4( particleNormalData.xyz + 
                                                   (camRighW * inVertexData.x * spriteSize) +
                                                   (camUpW * inVertexData.y * spriteSize), 1.0f)).xyz;
    // Output position of the vertex
    // Even though the variable ends with WV, we'll store WVP to skip adding a new varying variable

    VAR._vertexWV = vec4(vertexPositionWV, 1.0f);
    VAR._normalWV = mat3(dvd_ViewMatrix) * vec3(0.0, 0.0f, 1.0);
    gl_Position = dvd_ProjectionMatrix * VAR._vertexWV;
    
    // UV of the vertex. No special space for this one.
    VAR._texCoord = inVertexData.xy + vec2(0.5, 0.5);

    particleColour = inColourData;
}

--Fragment.Shadow.VSM

#include "nodeDataInput.cmn"
#ifdef HAS_TEXTURE
#include "vsm.frag"
layout(location = 0) out vec2 _colourOut;

layout(location = ATTRIB_FREE_START + 0) in vec4 particleColour;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2DArray texDiffuse0;

void main() {
    if (texture(texDiffuse0, vec3(VAR._texCoord,0)).a < ALPHA_DISCARD_THRESHOLD) {
        discard;
    }

    _colourOut = computeMoments();
}
#endif

-- Fragment

#if defined(PRE_PASS)
#include "utility.frag"
#include "prePass.frag"
#else
#include "output.frag"
#endif
// Interpolated values from the vertex shaders
layout(location = ATTRIB_FREE_START + 0) in vec4 particleColour;

#ifdef HAS_TEXTURE
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2DArray texDiffuse0;
#endif

void main(){
   
#ifdef HAS_TEXTURE
    vec4 texColour = texture(texDiffuse0, vec3(VAR._texCoord, 0));

    vec4 colour = particleColour * texColour;
#else
    vec4 colour = particleColour;
#endif

    float d = texture(texDepth, gl_FragCoord.xy * ivec2(dvd_ViewPort.zw)).r - gl_FragCoord.z;
    float softness = pow(1.f - min(1.f, 200.f * d), 2.f);
    colour.a *= max(0.1f, 1.f - pow(softness, 2.f));

#if defined(PRE_PASS)
    if (colour.a <= ALPHA_DISCARD_THRESHOLD) {
        discard;
    }
    const NodeMaterialData data = dvd_Materials[MATERIAL_IDX];
    writeGBuffer(data);
#else //PRE_PASS
    float normalVariation = 0.f;
    const NodeMaterialData data = dvd_Materials[MATERIAL_IDX];
    const vec3 normalWV = getNormalWV( data, vec3( VAR._texCoord, 0 ), normalVariation );

    writeScreenColour(colour, vec3( 0.f ), normalWV );
#ensif //PRE_PASS

}