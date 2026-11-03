-- Vertex

#include "nodeBufferedInput.cmn"

layout(location = ATTRIB_POSITION)  in vec3 inVertexData;
layout(location = ATTRIB_COLOR)     in vec4 inColourData;
layout(location = ATTRIB_NORMAL)    in vec4 particleNormalData;

// Output data will be interpolated for each fragment.
layout(location = ATTRIB_FREE_START + 0) out vec4 particleColour;

void main()
{
    ComputeIndirectionData();

    const vec3 centreWV = (dvd_ViewMatrix * vec4(particleNormalData.xyz, 1.f)).xyz;
    VAR._vertexWV = vec4(centreWV + vec3(inVertexData.xy * particleNormalData.w, 0.f), 1.f);
    VAR._vertexW = dvd_InverseViewMatrix * VAR._vertexWV;
    VAR._normalWV = vec3(0.f, 0.f, 1.f);
#if defined(HAS_VELOCITY)
    // Per-particle previous positions are not buffered, so retain camera motion only.
    VAR._prevVertexWVP = dvd_PrevViewProjectionMatrix * VAR._vertexW;
#endif //HAS_VELOCITY
    gl_Position = dvd_ProjectionMatrix * VAR._vertexWV;
    
    // UV of the vertex. No special space for this one.
    VAR._texCoord = inVertexData.xy + vec2(0.5, 0.5);

    particleColour = inColourData;
}

--Fragment.Shadow.VSM

#include "nodeBufferedInput.cmn"
#include "vsm.frag"
layout(location = 0) out vec2 _colourOut;

layout(location = ATTRIB_FREE_START + 0) in vec4 particleColour;

void main() {
    float alpha = particleColour.a;
#ifdef HAS_TEXTURE
    alpha *= texture(texDiffuse0, vec3(VAR._texCoord, 0)).a;
#endif //HAS_TEXTURE
    if (alpha <= ALPHA_DISCARD_THRESHOLD) {
        discard;
    }

    _colourOut = computeMoments();
}

-- Fragment

#if defined(PRE_PASS)
#include "prePass.frag"
#else
#include "materialData.frag"
#include "output.frag"
#if defined(MAIN_DISPLAY_PASS) && !defined(OIT_PASS)
DESCRIPTOR_SET_RESOURCE(PER_PASS, 1) uniform sampler2D texDepth;
#endif //MAIN_DISPLAY_PASS && !OIT_PASS
#endif
// Interpolated values from the vertex shaders
layout(location = ATTRIB_FREE_START + 0) in vec4 particleColour;

void main(){
   
#ifdef HAS_TEXTURE
    vec4 texColour = texture(texDiffuse0, vec3(VAR._texCoord, 0));

    vec4 colour = particleColour * texColour;
#else
    vec4 colour = particleColour;
#endif

#if defined(MAIN_DISPLAY_PASS) && !defined(PRE_PASS)
    float d = texture(texDepth, gl_FragCoord.xy / vec2(textureSize(texDepth, 0))).r - gl_FragCoord.z;
    float softness = pow(1.f - min(1.f, 200.f * d), 2.f);
    colour.a *= max(0.1f, 1.f - pow(softness, 2.f));
#endif //MAIN_DISPLAY_PASS && !PRE_PASS

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
#endif //PRE_PASS

}
