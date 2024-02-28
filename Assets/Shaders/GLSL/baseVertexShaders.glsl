-- Vertex.FullScreenQuad

#include "nodeDataInput.cmn"

void main(void)
{
    vec2 uv = vec2(0, 0);
    if ((dvd_VertexIndex & 1) != 0)uv.x = 1;
    if ((dvd_VertexIndex & 2) != 0)uv.y = 1;

    VAR._texCoord = uv * 2;
    gl_Position.xy = uv * 4 - 1;
    gl_Position.zw = vec2(0, 1);

#if defined(TARGET_VULKAN)
    VAR._texCoord.y = 1.f - VAR._texCoord.y;
#endif //TARGET_VULKAN
}

-- Vertex.Dummy

void main(void)
{
}

-- Vertex.BasicData

#include "vbInputData.vert"

void main(void)
{
    const vec4 vertexWVP = computeData(fetchInputData());
    setClipPlanes();
    gl_Position = vertexWVP;
}

--Vertex.BasicLightData

#include "vbInputData.vert"
#include "lightingDefaults.vert"

void main() {
    const NodeTransformData data = fetchInputData();
    const vec4 vertexWVP = computeData(data);
    setClipPlanes();
    computeLightVectors(data);
    gl_Position = vertexWVP;
}
