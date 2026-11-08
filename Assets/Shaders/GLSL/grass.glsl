--Vertex

// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#define COMPUTE_TBN
#include "vbInputData.vert"
#define VEGETATION_GRASS_DRAW
#include "vegetationData.cmn"
#include "grassInstanceMetadata.cmn"
#include "sceneData.cmn"
#include "lightingDefaults.vert"

VegetationData GrassData(in uint instanceID) {
    return grassData[dvd_instanceDataIndex + instanceID];
}

layout(location = ATTRIB_FREE_START + 0) flat out uint  _layer;
layout(location = ATTRIB_FREE_START + 1) flat out uint  _instanceID;
layout(location = ATTRIB_FREE_START + 2)      out float _alphaFactor;

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
    VegetationData data = GrassData(VEGETATION_INSTANCE_INDEX);
    if (data.data.z < 0.f) {
        //gl_CullDistance[0] = -1.0f;
        gl_Position = vec4(2.f, 2.f, 2.f, 1.f);
        return;
    }

    const NodeTransformData nodeData = fetchInputData();
    VAR._SelectionFlag = dvd_SelectionFlag(nodeData);
    _instanceID = GRASS_METADATA_TINT(uint(data.data.y));
    float scale = data.positionAndScale.w * Saturate(data.data.z);

    _alphaFactor = Saturate(data.data.z);

    _layer = uint(data.data.x);
    VAR._LoDLevel = GRASS_METADATA_LOD(uint(data.data.y));

    const float timeGrass = dvd_windDetails.w * MSToSeconds(dvd_GameTimeMS) * 0.5f;
    const bool animate = VAR._LoDLevel < 2u && dvd_Vertex.y > 0.5f;

    dvd_Vertex.xyz *= scale;
    dvd_Vertex.y -= (1.f - scale) * 0.25f;
    dvd_Vertex.xyz = QuaternionRotate(data.orientationQuad, dvd_Vertex.xyz);
    VAR._vertexW = dvd_Vertex + vec4(data.positionAndScale.xyz, 0.0f);

    if (animate) {
        largetScaleMotion(VAR._vertexW, dvd_Vertex, dvd_Vertex.y * 0.075f, timeGrass);

#       if 1
            const vec2 viewDirection = normalize(VAR._vertexW.xz - dvd_CameraPosition.xz);
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
    computeLightVectors(data.orientationQuad, vec3(scale));

    const vec4 clipPosition = dvd_ProjectionMatrix * VAR._vertexWV;
    gl_Position = clipPosition;
#if defined(HAS_VELOCITY)
    // STUBBED: TODO: Implement vegetation velocity support with previous instance and deformation state.
    VAR._prevVertexWVP = dvd_PrevViewProjectionMatrix * VAR._vertexW;
#endif //HAS_VELOCITY
}

-- Fragment.Colour

#if !defined(PRE_PASS)
layout(early_fragment_tests) in;
#endif //!PRE_PASS

#define NO_REFLECTIONS
//#define DEBUG_LODS

#include "BRDF.frag"
#include "output.frag"
#include "transparentCoverage.cmn"

layout(location = ATTRIB_FREE_START + 0) flat in uint  _layer;
layout(location = ATTRIB_FREE_START + 1) flat in uint  _instanceID;
layout(location = ATTRIB_FREE_START + 2)      in float _alphaFactor;

void main (void){
    const vec4 positionDx = dFdx(VAR._vertexW);
    const vec4 positionDy = dFdy(VAR._vertexW);
    NodeMaterialData data = dvd_Materials[MATERIAL_IDX];

    vec4 albedo = texture(texDiffuse0, vec3(VAR._texCoord, _layer));
    albedo.a *= _alphaFactor;
    if (!TRANSPARENT_HAS_COVERAGE(albedo.a))
    {
        discard;
    }

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

    const vec3 normalWV = getNormalWV(data, vec3(VAR._texCoord, 0));
    const vec3 viewVec = computeViewDirectionWV();
    const float NdotV = max(dot(normalWV, viewVec), 0.f);
    const PBRMaterial material = initMaterialProperties(data, albedo.rgb, VAR._texCoord, normalWV, NdotV);
    const IBLGradients gradients = IBL_GRADIENTS(vec3(0.f), vec2(0.f));

    writeScreenColour(getPixelColour(albedo, data, normalWV, material, viewVec, gradients, positionDx, positionDy), vec3(0.f), normalWV);
}

--Fragment.PrePass

#include "prePass.frag"
#include "texturing.frag"

layout(location = ATTRIB_FREE_START + 0) flat in uint  _layer;
layout(location = ATTRIB_FREE_START + 1) flat in uint  _instanceID;
layout(location = ATTRIB_FREE_START + 2)      in float _alphaFactor;

void main() {
    if (texture(texDiffuse0, vec3(VAR._texCoord, _layer)).a * _alphaFactor < ALPHA_DISCARD_THRESHOLD) {
        discard;
    }
    const NodeMaterialData data = dvd_Materials[MATERIAL_IDX];
    writeGBuffer(data);
}

--Fragment.Shadow.VSM

layout(location = ATTRIB_FREE_START + 0) flat in uint  _layer;
layout(location = ATTRIB_FREE_START + 1) flat in uint  _instanceID;
layout(location = ATTRIB_FREE_START + 2)      in float _alphaFactor;

#include "texturing.frag"
#include "vsm.frag"

layout(location = 0) out vec2 _colourOut;

void main(void) {
    const vec2 moments = computeMoments();
    // Only discard alpha == 0
    if (texture(texDiffuse0, vec3(VAR._texCoord, _layer)).a < ALPHA_DISCARD_THRESHOLD) {
        discard;
    }

    _colourOut = moments;
}
