#ifndef _VERTEX_DEFAULT_VERT_
#define _VERTEX_DEFAULT_VERT_

layout(location = ATTRIB_POSITION)    in vec3 inVertexData;
layout(location = ATTRIB_TEXCOORD)    in vec2 inTexCoordData;
layout(location = ATTRIB_NORMAL)      in float inNormalData;
#if defined(ENABLE_TBN)
layout(location = ATTRIB_TANGENT)     in float inTangentData;
#endif //ENABLE_TBN
#if !defined(DEPTH_PASS) && defined(HAS_COLOR_ATTRIBUTE)
layout(location = ATTRIB_COLOR)       in vec4 inColourData;
#endif //!DEPTH_PASS && HAS_COLOR_ATTRIBUTE
#if defined(USE_GPU_SKINNING)
layout(location = ATTRIB_BONE_WEIGHT) in vec4 inBoneWeightData;
layout(location = ATTRIB_BONE_INDICE) in uvec4 inBoneIndiceData;
#endif //USE_GPU_SKINNING
#if defined(USE_LINE_WIDTH)
layout(location = ATTRIB_WIDTH)       in uint inLineWidthData;
#endif //USE_LINE_WIDTH
#if defined(USE_GENERIC_ATTRIB)
layout(location = ATTRIB_GENERIC)     in vec2 inGenericData;
#endif //USE_GENERIC_ATTRIB
vec4   dvd_Vertex;
vec3   dvd_Normal;
#if defined(ENABLE_TBN)
vec3   dvd_Tangent;
#endif //ENABLE_TBN
#if !defined(DEPTH_PASS)
vec4   dvd_Colour;
#endif //DEPTH_PASS

#endif //_VERTEX_DEFAULT_VERT_
