--Vertex

#define NEED_SCENE_DATA
#if !defined(DEPTH_PASS)
#define NEED_TANGENT
#endif //DEPTH_PASS

#include "vbInputData.vert"
#include "vegetationData.cmn"
#include "sceneData.cmn"
#include "lightingDefaults.vert"

layout(location = 0) flat out uint  _layer;
layout(location = 1) flat out uint  _instanceID;
layout(location = 2)      out float _alphaFactor;
layout(location = 3)      out mat3  _tbnWV;

#define GRASS_DISPLACEMENT_DISTANCE 50.0f
#define GRASS_DISPLACEMENT_MAGNITUDE 0.5f

void smallScaleMotion(inout vec4 vertexW, in float heightExtent, in float time) {
    const float disp = 0.064f * sin(2.65f * (vertexW.x + vertexW.y + vertexW.z + (time * 3)));
    vertexW.xz += heightExtent * vec2(disp, disp);
}

void largetScaleMotion(inout vec4 vertexW, in vec4 vertex, in float heightExtent, in float time) {
    const float X = (2.f * sin(1.f * (vertex.x + vertex.y + vertex.z + time))) + 1.0f;
    const float Z = (1.f * sin(2.f * (vertex.x + vertex.y + vertex.z + time))) + 0.5f;

    vertexW.xz += heightExtent * vec2(X * dvd_windDetails.x, Z * dvd_windDetails.z);
    smallScaleMotion(vertexW, heightExtent, time * 1.25f);
}

void main() {
    const NodeTransformData nodeData = fetchInputData();

    VegetationData data = GrassData(gl_InstanceID);
    _instanceID = gl_InstanceID;

#if defined(USE_CULL_DISTANCE)
    if (data.data.z > 2.5f) {
        gl_CullDistance[0] = -1.0f;
    }
    float scale = data.positionAndScale.w;
#else //USE_CULL_DISTANCE
    float scale = data.data.z > 2.5f ? 0.0f : data.positionAndScale.w * saturate(data.data.z);
#endif //USE_CULL_DISTANCE

    _alphaFactor = saturate(data.data.z);

    _layer = uint(data.data.x);
    VAR._LoDLevel = uint(data.data.y);

    const float timeGrass = dvd_windDetails.w * MSToSeconds(dvd_time) * 0.5f;
    const bool animate = VAR._LoDLevel < 2u && dvd_Vertex.y > 0.5f;

    const float height = dvd_Vertex.y;
    dvd_Vertex.xyz *= scale;
    dvd_Vertex.y -= (1.f - scale) * 0.25f;
    dvd_Vertex.xyz = QUATERNION_ROTATE(dvd_Vertex.xyz, data.orientationQuad);
    VAR._vertexW = dvd_Vertex + vec4(data.positionAndScale.xyz, 0.0f);

    if (animate) {
        largetScaleMotion(VAR._vertexW, dvd_Vertex, dvd_Vertex.y * 0.075f, timeGrass);

#       if 1
            const vec2 viewDirection = normalize(VAR._vertexW.xz - dvd_cameraPosition.xz);
#       else
            const vec2 viewDirection = normalize(mat3(dvd_InverseViewMatrix) * vec3(0.f, 0.f, -1.f)).xz;
#       endif

        const float displacement = (GRASS_DISPLACEMENT_MAGNITUDE * (1.f - smoothstep(GRASS_DISPLACEMENT_DISTANCE, GRASS_DISPLACEMENT_DISTANCE * 1.5f, data.data.w)));
        VAR._vertexW.xz += viewDirection * displacement;
    }

#if !defined(SHADOW_PASS)
    setClipPlanes();
#endif //!SHADOW_PASS

    VAR._vertexWV = dvd_ViewMatrix * VAR._vertexW;

#if !defined(DEPTH_PASS)
    computeViewDirectionWV(nodeData);

    dvd_Normal = normalize(QUATERNION_ROTATE(dvd_Normal, data.orientationQuad));
    _tbnWV = computeTBN(dvd_NormalMatrixW(nodeData));
    VAR._normalWV = _tbnWV[2];
#else
    _tbnWV = mat3(1.f);
#endif //!DEPTH_PASS

    gl_Position = dvd_ProjectionMatrix * VAR._vertexWV;
}

-- Fragment.Colour

layout(early_fragment_tests) in;

#define NO_IBL
#define USE_CUSTOM_TBN
#define MAX_SHADOW_MAP_LOD 1
//#define DEBUG_LODS

#include "BRDF.frag"
#include "output.frag"

layout(location = 0) flat in uint  _layer;
layout(location = 1) flat in uint  _instanceID;
layout(location = 2)      in float _alphaFactor;
layout(location = 3)      in mat3  _tbnWV;

mat3 getTBNWV() {
    return _tbnWV;
}

void main (void){
    NodeMaterialData data = dvd_Materials[MATERIAL_IDX];

    vec4 albedo = texture(texDiffuse0, vec3(VAR._texCoord, _layer));

    if (_instanceID % 3 == 0) {
        albedo.rgb = overlayVec(albedo.rgb, vec3(0.9f, 0.85f, 0.55f));
    } else if (_instanceID % 4 == 0) {
        albedo.rgb = overlayVec(albedo.rgb, vec3(0.75f, 0.65f, 0.45f));
    }

#if defined(DEBUG_LODS)
    if (VAR._LoDLevel == 0) {
        albedo.rgb = vec3(1.f, 0.f, 0.f);
    } else if (VAR._LoDLevel == 1) {
        albedo.rgb = vec3(0.f, 1.f, 0.f);
    } else if (VAR._LoDLevel == 2) {
        albedo.rgb = vec3(0.f, 0.f, 1.f);
    } else if (VAR._LoDLevel == 3) {
        albedo.rgb = vec3(1.f, 0.f, 1.f);
    } else {
        albedo.rgb = vec3(0.f, 1.f, 1.f);
    }
#endif //DEBUG_LODS

    float normalVariation = 0.f;
    const vec3 normalWV = getNormalWV(data, vec3(VAR._texCoord, 0), normalVariation);

    vec4 colour = vec4(albedo.rgb, min(albedo.a, _alphaFactor));
    vec3 MetalnessRoughnessProbeID = vec3(0.f, 1.f, 0.f);
    if (albedo.a >= Z_TEST_SIGMA) {
        colour = getPixelColour(albedo, data, normalWV, normalVariation, VAR._texCoord, MetalnessRoughnessProbeID);
    }
    writeScreenColour(colour, normalWV, MetalnessRoughnessProbeID);
}

--Fragment.PrePass

#include "prePass.frag"

layout(location = 0) flat in uint  _layer;
layout(location = 1) flat in uint  _instanceID;
layout(location = 2)      in float _alphaFactor;
layout(location = 3)      in mat3  _tbnWV;

void main() {
    const float albedoAlpha = texture(texDiffuse0, vec3(VAR._texCoord, _layer)).a * _alphaFactor;
    writeGBuffer(albedoAlpha);
}

--Fragment.Shadow.VSM

layout(location = 0) flat in uint  _layer;
layout(location = 1) flat in uint  _instanceID;
layout(location = 2)      in float _alphaFactor;
layout(location = 3)      in mat3  _tbnWV;

layout(binding = TEXTURE_UNIT0) uniform sampler2DArray texDiffuse0;

#include "vsm.frag"
out vec2 _colourOut;

void main(void) {
    const float albedoAlpha = texture(texDiffuse0, vec3(VAR._texCoord, _layer)).a;
    // Only discard alhpa == 0
    if (albedoAlpha < Z_TEST_SIGMA) {
        discard;
    }

    _colourOut = computeMoments();
}