--Vertex.PrePass

#include "vbInputData.vert"

layout(location = 0) out vec4 _scrollingUV;

void main(void) {
    const vec4 vertexWVP = computeData(fetchInputData());
    _scrollingUV = vec4(0.f);
    setClipPlanes();
    gl_Position = vertexWVP;
}

-- Vertex.Colour

#include "vbInputData.vert"

layout(location = 0) out vec4 _scrollingUV;

void main(void) {
    const vec4 vertexWVP = computeData(fetchInputData());
    setClipPlanes();

    const float time2 = MSToSeconds(dvd_time) * 0.01f;
    _scrollingUV = vec4(VAR._texCoord + time2.xx, 
                        VAR._texCoord + vec2(-time2, time2)) * 25.f;

    gl_Position = vertexWVP;
}

--Fragment.Colour

layout(early_fragment_tests) in;

layout(location = 0) in vec4 _scrollingUV;

#include "output.frag"

layout(binding = TEXTURE_UNIT0) uniform sampler2D texWaterCaustics;


void main(void) {
    const vec3 colourOut = overlayVec(texture(texWaterCaustics, _scrollingUV.st).rgb,
                                      texture(texWaterCaustics, _scrollingUV.pq).rgb);

    writeScreenColour(vec4(colourOut, 1.f), VAR._normalWV);
}