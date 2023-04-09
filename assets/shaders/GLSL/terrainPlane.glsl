-- Vertex

#include "vbInputData.vert"

#if !defined(PRE_PASS)
layout(location = ATTRIB_FREE_START + 0) out vec4 _scrollingUV;
#endif //!PRE_PASS

void main(void) {
    const vec4 vertexWVP = computeData(fetchInputData());
    setClipPlanes();

#if !defined(PRE_PASS)
    const float time2 = MSToSeconds(dvd_GameTimeMS) * 0.001f;
    _scrollingUV = vec4(VAR._texCoord + time2.xx, 
                        VAR._texCoord + vec2(-time2, time2)) * 25.f;
#endif //!PRE_PASS

    gl_Position = vertexWVP;
}

--Fragment

#if !defined(PRE_PASS)
layout(early_fragment_tests) in;
#endif //!PRE_PASS

#if defined(PRE_PASS)
#include "prePass.frag"
#else //PRE_PASS
layout(location = ATTRIB_FREE_START + 0) in vec4 _scrollingUV;

#include "nodeBufferedInput.cmn"
#include "output.frag"
#include "utility.frag"
#endif //PRE_PASS

void main(void) {
#if defined(PRE_PASS)
    writeGBuffer(VAR._normalWV, 1.f);
#else //PRE_PASS
    const vec3 albedo = overlayVec(texture(texDiffuse0, vec3(_scrollingUV.st, 0)).rgb,
                                   texture(texDiffuse0, vec3(_scrollingUV.pq, 0)).rgb);

    writeScreenColour(vec4(albedo, 1.f), VAR._normalWV);
#endif //PRE_PASS
}