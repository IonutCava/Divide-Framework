-- Vertex

#include "vertexDefault.vert"

layout(location = 13) in vec4 particleNormalData;
// Output data will be interpolated for each fragment.
layout(location = 0) out vec4 particleColour;

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

#ifdef HAS_TEXTURE
#include "vsm.frag"
out vec2 _colourOut;

layout(location = 0) in vec4 particleColour;
layout(binding = TEXTURE_UNIT0) uniform sampler2D texDiffuse0;

void main() {
    const float albedo = (particleColour * texture(texDiffuse0, VAR._texCoord)).a;
    if (albedo < INV_Z_TEST_SIGMA) {
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
layout(location = 0) in vec4 particleColour;

#ifdef HAS_TEXTURE
layout(binding = TEXTURE_UNIT0) uniform sampler2D texDiffuse0;
#endif

layout(binding = TEXTURE_DEPTH) uniform sampler2D texDepthMap;

void main(){
   
#ifdef HAS_TEXTURE
    vec4 colour = particleColour * texture(texDiffuse0, VAR._texCoord);
#else
    vec4 colour = particleColour;
#endif

    float d = texture(texDepthMap, gl_FragCoord.xy * ivec2(dvd_ViewPort.zw)).r - gl_FragCoord.z;
    float softness = pow(1.0 - min(1.0, 200.0 * d), 2.0);
    colour.a *= max(0.1, 1.0 - pow(softness, 2.0));

#if defined(PRE_PASS)
    writeGBuffer(colour.a);
#else //PRE_PASS
    writeScreenColour(colour, VAR._normalWV);
#ensif //PRE_PASS

}