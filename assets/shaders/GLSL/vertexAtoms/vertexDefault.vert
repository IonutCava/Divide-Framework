#ifndef _VERTEX_DEFAULT_VERT_
#define _VERTEX_DEFAULT_VERT_

#if defined(HAS_POSITION_ATTRIBUTE)
layout(location = ATTRIB_POSITION)    in vec3 inVertexData;
#endif //HAS_POSITION_ATTRIBUTE
#if defined(HAS_TEXCOORD_ATTRIBUTE)
layout(location = ATTRIB_TEXCOORD)    in vec2 inTexCoordData;
#endif //HAS_TEXCOORD_ATTRIBUTE
#if defined(HAS_NORMAL_ATTRIBUTE)
layout(location = ATTRIB_NORMAL)      in float inNormalData;
#endif //HAS_NORMAL_ATTRIBUTE
#if defined(ENABLE_TBN) && defined(HAS_TANGENT_ATTRIBUTE)
layout(location = ATTRIB_TANGENT)     in float inTangentData;
#endif //ENABLE_TBN && HAS_TANGENT_DATA
#if !defined(DEPTH_PASS) && defined(HAS_COLOR_ATTRIBUTE)
layout(location = ATTRIB_COLOR)       in vec4 inColourData;
#endif //!DEPTH_PASS && HAS_COLOR_ATTRIBUTE
#if defined(USE_GPU_SKINNING)
#if defined(HAS_BONE_WEIGHT_ATTRIBUTE)
layout(location = ATTRIB_BONE_WEIGHT) in vec4 inBoneWeightData;
#endif //HAS_BONE_WEIGHT_ATTRIBUTE
#if defined(HAS_BONE_INDICE_ATTRIBUTE)
layout(location = ATTRIB_BONE_INDICE) in uvec4 inBoneIndiceData;
#endif //HAS_BONE_INDICE_ATTRIBUTE
#endif //USE_GPU_SKINNING
#if defined(USE_LINE_WIDTH) && defined(HAS_WIDTH_ATTRIBUTE)
layout(location = ATTRIB_WIDTH)       in uint inLineWidthData;
#endif //USE_LINE_WIDTH && HAS_WIDTH_ATTRIBUTE
#if defined(USE_GENERIC_ATTRIB) && defined(HAS_GENERIC_ATTRIBUTE)
layout(location = ATTRIB_GENERIC)     in vec2 inGenericData;
#endif //USE_GENERIC_ATTRIB && HAS_GENERIC_ATTRIBUTE
vec4   dvd_Vertex;
vec3   dvd_Normal;
#if defined(ENABLE_TBN)
vec3   dvd_Tangent;
#endif //ENABLE_TBN
#if !defined(DEPTH_PASS)
vec4   dvd_Colour;
#endif //DEPTH_PASS

#endif //_VERTEX_DEFAULT_VERT_
