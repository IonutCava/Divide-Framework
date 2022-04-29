--Vertex

#define NO_CLIP_CULL_IN
#define NO_CLIP_CULL_OUT

#include "terrainUtils.cmn"

layout(location = ATTRIB_POSITION) in vec4 inVertexData;
layout(location = ATTRIB_COLOR)    in vec4 inColourData;

layout(location = ATTRIB_FREE_START + 1) out vec4 vtx_adjancency;
layout(location = ATTRIB_FREE_START + 2) out float vtx_tileSize;
layout(location = ATTRIB_FREE_START + 3) out flat uint vtx_ringID;

void main(void)
{
    ComputeIndirectionData();

    // Calculate texture coordinates (u,v) relative to entire terrain
    const float iv = floor(gl_VertexID * INV_CONTROL_VTX_PER_TILE_EDGE);
    const float iu = gl_VertexID - iv * CONTROL_VTX_PER_TILE_EDGE;
    const float u = iu / (CONTROL_VTX_PER_TILE_EDGE - 1.0f);
    const float v = iv / (CONTROL_VTX_PER_TILE_EDGE - 1.0f);

    vtx_tileSize = inVertexData.z;
    vtx_adjancency = inColourData;
    vtx_ringID = uint(inVertexData.w);

    const vec2 posXZ = vec2(u, v) * vtx_tileSize + inVertexData.xy;

    gl_Position = vec4(posXZ.x, GetHeight(WorldXZtoHeightUV(posXZ)), posXZ.y, 1.f);
}

--TessellationC

#define NO_CLIP_CULL_IN
#define NO_CLIP_CULL_OUT

#include "terrainUtils.cmn"

// Most of the stuff here is from nVidia's DX11 terrain tessellation sample
layout(location = ATTRIB_FREE_START + 1) in vec4 vtx_adjancency[];
layout(location = ATTRIB_FREE_START + 2) in float vtx_tileSize[];
layout(location = ATTRIB_FREE_START + 3) in flat uint vtx_ringID[];

// Outputs
layout(vertices = TESSELLATION_OUTPUT_VERTICES) out;
layout(location = ATTRIB_FREE_START + 1) out float tcs_tileSize[];
layout(location = ATTRIB_FREE_START + 2) out flat uint tcs_ringID[];

#if defined(TOGGLE_DEBUG) || defined(TOGGLE_TESS_LEVEL)
layout(location = ATTRIB_FREE_START + 4) out vec3[5] tcs_debugColour[];
#if defined(TOGGLE_DEBUG)
#if defined(TOGGLE_NORMALS)
#undef MAX_TESS_LEVEL
#define MAX_TESS_LEVEL 1
#endif //TOGGLE_NORMALS
#endif //TOGGLE_DEBUG
#endif //TOGGLE_DEBUG || TOGGLE_TESS_LEVEL

#if !defined(MAX_TESS_LEVEL)
#define MAX_TESS_LEVEL 64
#endif //MAX_TESS_LEVEL

float SphereToScreenSpaceTessellation(in vec3 w0, in vec3 w1, in float diameter, in mat4 worldViewMat)
{
    const vec4 view0 = worldViewMat * vec4((w0 + w1) * 0.5f, 1.f);
    const vec4 view1 = view0 + vec4(WORLD_SCALE_X * diameter, 0.f, 0.f, 0.f);

    vec3 clip0 = Homogenize(dvd_ProjectionMatrix * view0); // to clip space + projection
    vec3 clip1 = Homogenize(dvd_ProjectionMatrix * view1); // to clip space + projection
    //clip0.xy = clip0.xy * 0.5f + 0.5f;       // to NDC (DX11 sample skipped this)
    //clip1.xy = clip1.xy * 0.5f + 0.5f;       // to NDC (DX11 sample skipped this)
    clip0.xy *= dvd_ScreenDimensions;          // to pixels
    clip1.xy *= dvd_ScreenDimensions;          // to pixels

    return clamp(distance(clip0, clip1) / dvd_tessTriangleWidth, 1.f, float(MAX_TESS_LEVEL));
}


// The adjacency calculations ensure that neighbours have tessellations that agree.
// However, only power of two sizes *seem* to get correctly tessellated with no cracks.
// Clamp to the nearest larger power of two.  Any power of two works; larger means that we don't lose detail.
// Output is [4, MAX_TESS_LEVEL]. Our smaller neighbour's min tessellation is pow(2,1) = 2.  As we are twice its size, we can't go below 4.
#define SmallerNeighbourAdjacencyClamp(TESS) max(4, pow(2, ceil(log2(TESS))))

// Clamp to the nearest larger power of two.  Any power of two works; larger means that we don't lose detail.
// Our larger neighbour's max tessellation is MAX_TESS_LEVEL; as we are half its size, our tessellation must max out
// at MAX_TESS_LEVEL / 2, otherwise we could be over-tessellated relative to the neighbour.  Output is [2,MAX_TESS_LEVEL].
#define LargerNeighbourAdjacencyClamp(TESS) clamp(pow(2, ceil(log2(TESS))), 2.f, MAX_TESS_LEVEL * 0.5f)

float SmallerNeighbourAdjacencyFix(in int idx0, in int idx1, in float diameter, in mat4 worldViewMat) {
    vec3 p0 = gl_in[idx0].gl_Position.xyz;
    vec3 p1 = gl_in[idx1].gl_Position.xyz;
    p0.y = p1.y = GetHeight(WorldXZtoHeightUV(p0.xz));

    return SmallerNeighbourAdjacencyClamp(SphereToScreenSpaceTessellation(p0, p1, diameter, worldViewMat));
}

float LargerNeighbourAdjacencyFix(in int idx0, in int idx1, in int patchIdx, in float diameter, in mat4 worldViewMat) {
    vec3 p0 = gl_in[idx0].gl_Position.xyz;
    vec3 p1 = gl_in[idx1].gl_Position.xyz;

    // We move one of the corner vertices in 2D (x,z) to match where the corner vertex is 
    // on our larger neighbour.  We move p0 or p1 depending on the even/odd patch index.
    //
    // Larger neighbour
    // +-------------------+
    // +---------+
    // p0   Us   p1 ---->  +		Move p1
    // |    0    |    1    |		patchIdx % 2 
    //
    //           +---------+
    // +  <----  p0   Us   p1		Move p0
    // |    0    |    1    |		patchIdx % 2 
    //
    if (patchIdx % 2 != 0) {
        p0 += (p0 - p1);
    } else { 
        p1 += (p1 - p0);
    }

    // Having moved the vertex in (x,z), its height is no longer correct.
    p0.y = p1.y = GetHeight(WorldXZtoHeightUV(p0.xz));
    // Half the tessellation because the edge is twice as long.
    return LargerNeighbourAdjacencyClamp(0.5f * SphereToScreenSpaceTessellation(p0, p1, 2 * diameter, worldViewMat));
}

bool SphereInFrustum(in vec3 pos, in float r, in mat4 worldMat) {
    const float minCmp = -0.15f;

    const vec4 posW = worldMat * vec4(pos, 1.f);
    for (int i = 0; i < 6; i++) {
        if (dot(posW, dvd_frustumPlanes[i]) + r < minCmp) {
            return false;
        }
    }
    return true;
}


void main(void)
{
    PassData(0);

    const vec3 centre = 0.25f * (gl_in[0].gl_Position.xyz +
                                 gl_in[1].gl_Position.xyz +
                                 gl_in[2].gl_Position.xyz +
                                 gl_in[3].gl_Position.xyz);

    const mat4 worldMat = dvd_WorldMatrix(dvd_Transforms[TRANSFORM_IDX]) * dvd_terrainWorld;

    const vec4 pos0 = worldMat * gl_in[0].gl_Position;
    const vec4 pos1 = worldMat * gl_in[2].gl_Position;
    const float radius = length(pos0.xyz - pos1.xyz) * 0.5f;

    if (!SphereInFrustum(centre, radius, worldMat))
    {
          gl_TessLevelInner[0] = gl_TessLevelInner[1] = -1;
          gl_TessLevelOuter[0] = gl_TessLevelOuter[1] = -1;
          gl_TessLevelOuter[2] = gl_TessLevelOuter[3] = -1;
    }
    else
    {
#if MAX_TESS_LEVEL > 1
        const mat4 worldViewMat = dvd_ViewMatrix * worldMat;
        const float sideLen = max(abs(gl_in[1].gl_Position.x - gl_in[0].gl_Position.x), abs(gl_in[1].gl_Position.x - gl_in[2].gl_Position.x));
        // Outer tessellation level
        gl_TessLevelOuter[0] = SphereToScreenSpaceTessellation(gl_in[0].gl_Position.xyz, gl_in[1].gl_Position.xyz, sideLen, worldViewMat);
        gl_TessLevelOuter[1] = SphereToScreenSpaceTessellation(gl_in[3].gl_Position.xyz, gl_in[0].gl_Position.xyz, sideLen, worldViewMat);
        gl_TessLevelOuter[2] = SphereToScreenSpaceTessellation(gl_in[2].gl_Position.xyz, gl_in[3].gl_Position.xyz, sideLen, worldViewMat);
        gl_TessLevelOuter[3] = SphereToScreenSpaceTessellation(gl_in[1].gl_Position.xyz, gl_in[2].gl_Position.xyz, sideLen, worldViewMat);

//#if !defined(LOW_QUALITY)
        // Sadly can't disable this for reflection/refraction cases as the cracks get really obvious at certain angles
        // Edges that need adjacency adjustment are identified by the per-instance ip[0].adjacency 
        // scalars, in *conjunction* with a patch ID that puts them on the edge of a tile.
        const int PatchID = gl_PrimitiveID;
        ivec2 patchXY;
        patchXY.y = PatchID / PATCHES_PER_TILE_EDGE;
        patchXY.x = PatchID - patchXY.y * PATCHES_PER_TILE_EDGE;

        // Identify patch edges that are adjacent to a patch of a different size.  The size difference
        // is encoded in _in[n].adjacency, either 0.5, 1.0 or 2.0.
        // neighbourMinusX refers to our adjacent neighbour in the direction of -ve x.  The value
        // is the neighbour's size relative to ours.  Similarly for plus and Y, etc.  You really
        // need a diagram to make sense of the adjacency conditions in the if statements. :-(
        if (patchXY.x == 0) {
            if (vtx_adjancency[0].x < 0.55f) {
                // Deal with neighbours that are smaller.
                gl_TessLevelOuter[0] = SmallerNeighbourAdjacencyFix(0, 1, sideLen, worldViewMat);
            } else if (vtx_adjancency[0].x > 1.1f) {
                // Deal with neighbours that are larger than us.
                gl_TessLevelOuter[0] = LargerNeighbourAdjacencyFix(0, 1, patchXY.y, sideLen, worldViewMat);
            }
        } else if (patchXY.x == PATCHES_PER_TILE_EDGE - 1) {
            if (vtx_adjancency[0].z < 0.55f) {
                gl_TessLevelOuter[2] = SmallerNeighbourAdjacencyFix(2, 3, sideLen, worldViewMat);
            } else if (vtx_adjancency[0].z > 1.1f) {
                gl_TessLevelOuter[2] = LargerNeighbourAdjacencyFix(3, 2, patchXY.y, sideLen, worldViewMat);
            }
        }

        if (patchXY.y == 0) {
            if (vtx_adjancency[0].y < 0.55f) {
                gl_TessLevelOuter[1] = SmallerNeighbourAdjacencyFix(3, 0, sideLen, worldViewMat);
            } else if (vtx_adjancency[0].y > 1.1f) {
                gl_TessLevelOuter[1] = LargerNeighbourAdjacencyFix(0, 3, patchXY.x, sideLen, worldViewMat);	// NB: irregular index pattern - it's correct.
            }
        } else if (patchXY.y == PATCHES_PER_TILE_EDGE - 1) {
            if (vtx_adjancency[0].w < 0.55f) {
                gl_TessLevelOuter[3] = SmallerNeighbourAdjacencyFix(1, 2, sideLen, worldViewMat);
            } else if (vtx_adjancency[0].w > 1.1f) {
                gl_TessLevelOuter[3] = LargerNeighbourAdjacencyFix(1, 2, patchXY.x, sideLen, worldViewMat);	// NB: irregular index pattern - it's correct.
            }
        }
//#endif //LOW_QUALITY

#else //MAX_TESS_LEVEL > 1
        // Outer tessellation level
        gl_TessLevelOuter[0] = gl_TessLevelOuter[1] = gl_TessLevelOuter[2] = gl_TessLevelOuter[3] = 1.0f;
#endif //MAX_TESS_LEVEL > 1

        // Inner tessellation level
        gl_TessLevelInner[0] = 0.5f * (gl_TessLevelOuter[0] + gl_TessLevelOuter[3]);
        gl_TessLevelInner[1] = 0.5f * (gl_TessLevelOuter[2] + gl_TessLevelOuter[1]);

        // Pass the patch verts along
        gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
        // The VS displaces y for LOD calculations.  We drop it here so as not to displace twice in the DS.
        gl_out[gl_InvocationID].gl_Position.y = 0.0f;

        tcs_tileSize[gl_InvocationID] = vtx_tileSize[gl_InvocationID];
        tcs_ringID[gl_InvocationID] = vtx_ringID[gl_InvocationID];
#if defined(TOGGLE_DEBUG) || defined(TOGGLE_TESS_LEVEL)
        // Output tessellation level (used for wireframe coloring)
        // These are one colour for each tessellation level and linear graduations between.
        const vec3 DEBUG_COLOURS[6] =
        {
            vec3(0,0,1), //  2 - blue   
            vec3(0,1,1), //  4 - cyan   
            vec3(0,1,0), //  8 - green  
            vec3(1,1,0), // 16 - yellow 
            vec3(1,0,1), // 32 - purple
            vec3(1,0,0), // 64 - red
        };

        tcs_debugColour[gl_InvocationID][0] = DEBUG_COLOURS[clamp(int(log2(gl_TessLevelOuter[0])), 0, 5)];
        tcs_debugColour[gl_InvocationID][1] = DEBUG_COLOURS[clamp(int(log2(gl_TessLevelOuter[1])), 0, 5)];
        tcs_debugColour[gl_InvocationID][2] = DEBUG_COLOURS[clamp(int(log2(gl_TessLevelOuter[2])), 0, 5)];
        tcs_debugColour[gl_InvocationID][3] = DEBUG_COLOURS[clamp(int(log2(gl_TessLevelOuter[3])), 0, 5)];
        tcs_debugColour[gl_InvocationID][4] = DEBUG_COLOURS[clamp(int(log2(gl_TessLevelInner[0])), 0, 5)];
#endif //TOGGLE_DEBUG
    }
}

--TessellationE

#define NO_CLIP_CULL_IN
#if defined(TOGGLE_DEBUG) || defined(TOGGLE_TESS_LEVEL)
#define NO_CLIP_CULL_OUT
#endif //TOGGLE_DEBUG || TOGGLE_TESS_LEVEL

#define NEED_SCENE_DATA

layout(quads, fractional_even_spacing, cw) in;

#include "terrainUtils.cmn"
#include "sceneData.cmn"
#include "waterData.cmn"

layout(location = ATTRIB_FREE_START + 1) in float tcs_tileSize[];
layout(location = ATTRIB_FREE_START + 2) in flat uint tcs_ringID[];

layout(location = ATTRIB_FREE_START + 0) out vec2 tes_waterData;

#if defined(TOGGLE_DEBUG) || defined(TOGGLE_TESS_LEVEL)
#if defined(TOGGLE_DEBUG)
layout(location = ATTRIB_FREE_START + 1) out float tes_tileSize;
layout(location = ATTRIB_FREE_START + 2) out flat uint tes_ringID;
layout(location = ATTRIB_FREE_START + 3) out float tes_PatternValue;
#endif //TOGGLE_DEBUG

layout(location = ATTRIB_FREE_START + 4) in vec3[5] tcs_debugColour[];
layout(location = ATTRIB_FREE_START + 4) out vec3  tes_debugColour;
#endif //TOGGLE_DEBUG || TOGGLE_TESS_LEVEL

#define Bilerp(v0, v1, v2, v3, i) lerp(lerp(v0, v3, i.x), lerp(v1, v2, i.x), i.y)

#if defined(TOGGLE_DEBUG) || defined(TOGGLE_TESS_LEVEL)
#define BilerpColour(c0, c1, c2, c3, UV) lerp(lerp(c0, c1, UV.y), lerp(c2, c3, UV.y), UV.x)

vec3 LerpDebugColours(in vec3 cIn[5], vec2 uv) {
    if (uv.x < 0.5f && uv.y < 0.5f) {
        return BilerpColour(0.5f * (cIn[0] + cIn[1]), cIn[0], cIn[1], cIn[4], 2 * uv);
    } else if (uv.x < 0.5f && uv.y >= 0.5f) {
        return BilerpColour(cIn[0], 0.5f * (cIn[0] + cIn[3]), cIn[4], cIn[3], 2 * (uv - vec2(0.f, 0.5f)));
    } else if (uv.x >= 0.5f && uv.y < 0.5f) {
        return BilerpColour(cIn[1], cIn[4], 0.5f * (cIn[2] + cIn[1]), cIn[2], 2 * (uv - vec2(0.5f, 0.f)));
    } else {// x >= 0.5f && y >= 0.5f
        return BilerpColour(cIn[4], cIn[3], cIn[2], 0.5f * (cIn[2] + cIn[3]), 2 * (uv - vec2(0.5f, 0.5f)));
    }
}
#endif //TOGGLE_DEBUG || TOGGLE_TESS_LEVEL

mat3 getTBNW(in vec3 normalW) {
    // We need to compute tangent and bitengent vectors with 
    // as cotangent_frame's results do not apply for what we need them to do
    const vec3 N = normalize(normalW);
    const vec3 T1 = cross(N, WORLD_Z_AXIS);
    const vec3 T2 = cross(N, WORLD_Y_AXIS);
    const vec3 T = normalize(length(T1) > length(T2) ? T1 : T2);
    const vec3 B = normalize(-cross(N, T));
    // Orthogonal matrix(each axis is a perpendicular unit vector)
    // The transpose of an orthogonal matrix equals its inverse
    return  mat3(T, B, N);
}

void main()
{
    // Calculate the vertex position using the four original points and interpolate depending on the tessellation coordinates.
    const vec3 pos = Bilerp(gl_in[0].gl_Position.xyz,
                            gl_in[1].gl_Position.xyz,
                            gl_in[2].gl_Position.xyz,
                            gl_in[3].gl_Position.xyz,
                            gl_TessCoord.xy);
    _out._texCoord = WorldXZtoHeightUV(pos.xz);
    _out._vertexW = dvd_WorldMatrix(dvd_Transforms[TRANSFORM_IDX]) * dvd_terrainWorld * vec4(pos.x, GetHeight(_out._texCoord), pos.z, 1.0f);
    _out._vertexWV = dvd_ViewMatrix * _out._vertexW;
#if !defined(NO_CLIP_CULL_OUT)
    setClipPlanes(); //Only need world vertex position for clipping
#endif //!NO_CLIP_CULL_OUT
    _out._normalW = dvd_NormalMatrixW(dvd_Transforms[TRANSFORM_IDX]) * getNormal(_out._texCoord);
    _out._tbnWV = mat3(dvd_ViewMatrix) * getTBNW(_out._normalW);
    _out._indirectionIDs = _in[0]._indirectionIDs;
#if !defined(PRE_PASS) && !defined(SHADOW_PASS)
    _out._viewDirectionWV = mat3(dvd_ViewMatrix) * normalize(dvd_CameraPosition - _out._vertexW.xyz);
#endif //PRE_PASS && SHADOW_PASS

    tes_waterData = GetWaterDetails(_out._vertexW.xyz, TERRAIN_HEIGHT_OFFSET);

#if defined(TOGGLE_DEBUG) || defined(TOGGLE_TESS_LEVEL)
    tes_debugColour = LerpDebugColours(tcs_debugColour[0], gl_TessCoord.xy);
#endif //TOGGLE_DEBUG || TOGGLE_TESS_LEVEL

#if defined(TOGGLE_DEBUG)
    const int PatchID = gl_PrimitiveID;
    ivec2 patchXY;
    patchXY.y = PatchID / PATCHES_PER_TILE_EDGE;
    patchXY.x = PatchID - patchXY.y * PATCHES_PER_TILE_EDGE;

    tes_tileSize = tcs_tileSize[0];
    tes_ringID = tcs_ringID[0];
    tes_PatternValue = 0.5f * ((patchXY.x + patchXY.y) % 2);

    gl_Position = _out._vertexW;
#else //TOGGLE_DEBUG
    _out._LoDLevel = tcs_ringID[0];
    gl_Position = dvd_ProjectionMatrix * _out._vertexWV;
#endif //TOGGLE_DEBUG
}

--Geometry

#define NO_CLIP_CULL_IN
#define NEED_TEXTURE_DATA
#include "terrainUtils.cmn"

layout(triangles) in;

layout(location = ATTRIB_FREE_START + 0) in vec2 tes_waterData[];
layout(location = ATTRIB_FREE_START + 0) out vec2 gs_waterData;

layout(location = ATTRIB_FREE_START + 1) in float tes_tileSize[];
layout(location = ATTRIB_FREE_START + 2) in flat uint tes_ringID[];
layout(location = ATTRIB_FREE_START + 3) in float tes_PatternValue[];
layout(location = ATTRIB_FREE_START + 4) in vec3 tes_debugColour[];

layout(location = ATTRIB_FREE_START + 1) out vec3 gs_wireColor;
layout(location = ATTRIB_FREE_START + 2) noperspective out vec4 gs_edgeDist;  //w - patternValue

#if defined(TOGGLE_NORMALS)
layout(line_strip, max_vertices = 18) out;
#else //TOGGLE_NORMALS
layout(triangle_strip, max_vertices = 4) out;
#endif //TOGGLE_NORMALS

#define GetWVPPositon(i) (dvd_ViewProjectionMatrix * gl_in[i].gl_Position)

void PerVertex(in int i, in vec3 edge_dist) {
    PassData(i);
    gl_Position = GetWVPPositon(i);

    _out._LoDLevel = tes_ringID[i];
    
    gs_edgeDist = vec4(i == 0 ? edge_dist.x : 0.0,
                       i == 1 ? edge_dist.y : 0.0,
                       i >= 2 ? edge_dist.z : 0.0,
                       tes_PatternValue[i]);

    gs_waterData = tes_waterData[i];
}

void main(void)
{
    // Calculate edge distances for wireframe
    vec3 edge_dist = vec3(0.0);
    {
        vec4 pos0 = GetWVPPositon(0);
        vec4 pos1 = GetWVPPositon(1);
        vec4 pos2 = GetWVPPositon(2);

        vec2 p0 = vec2(dvd_ViewPort.zw * (pos0.xy / pos0.w));
        vec2 p1 = vec2(dvd_ViewPort.zw * (pos1.xy / pos1.w));
        vec2 p2 = vec2(dvd_ViewPort.zw * (pos2.xy / pos2.w));

        float a = length(p1 - p2);
        float b = length(p2 - p0);
        float c = length(p1 - p0);
        float alpha = acos((b * b + c * c - a * a) / (2.0 * b * c));
        float beta = acos((a * a + c * c - b * b) / (2.0 * a * c));
        edge_dist.x = abs(c * sin(beta));
        edge_dist.y = abs(c * sin(alpha));
        edge_dist.z = abs(b * sin(alpha));
    }

    const int count = gl_in.length();

#if defined(TOGGLE_NORMALS)
    const float sizeFactor = 0.75f;
    for (int i = 0; i < count; ++i) {
        // In world space
        const vec3 N = getNormal(_in[i]._texCoord);
        const vec3 B = cross(vec3(0.0f, 0.0f, 1.0f), N);
        const vec3 T = cross(N, B);

        vec3 P = gl_in[i].gl_Position.xyz;
        PerVertex(i, edge_dist);

        { // normals
            gs_wireColor = vec3(0.0f, 0.0f, 1.0f);
            gl_Position = dvd_ViewProjectionMatrix * vec4(P, 1.0);
            EmitVertex();

            PerVertex(1, edge_dist);
            gs_wireColor = vec3(0.0f, 0.0f, 1.0f);
            gl_Position = dvd_ViewProjectionMatrix * vec4(P + N * sizeFactor, 1.0);
            EmitVertex();

            EndPrimitive();
        }
        { // binormals
            gs_wireColor = vec3(0.0f, 1.0f, 0.0f);
            gl_Position = dvd_ViewProjectionMatrix * vec4(P, 1.0);
            EmitVertex();

            gs_wireColor = vec3(0.0f, 1.0f, 0.0f);
            gl_Position = dvd_ViewProjectionMatrix * vec4(P + B * sizeFactor, 1.0);
            EmitVertex();

            EndPrimitive();
        }
        { // tangents
            gs_wireColor = vec3(1.0f, 0.0f, 0.0f);
            gl_Position = dvd_ViewProjectionMatrix * vec4(P, 1.0);
            EmitVertex();

            gs_wireColor = vec3(1.0f, 0.0f, 0.0f);
            gl_Position = dvd_ViewProjectionMatrix * vec4(P + T * sizeFactor, 1.0);
            EmitVertex();

            EndPrimitive();
        }
    }
#else //TOGGLE_NORMALS

    // Output verts
    for (int i = 0; i < count; ++i) {
        PerVertex(i, edge_dist);
        gs_wireColor = tes_debugColour[i];
        EmitVertex();
    }

    // This closes the triangle
    PerVertex(0, edge_dist);
    gs_wireColor = tes_debugColour[0];
    EmitVertex();

    EndPrimitive();

#endif //TOGGLE_NORMALS
}

--Fragment

layout(early_fragment_tests) in;

layout(location = ATTRIB_FREE_START + 0) in vec2 tes_waterData;

#if defined(TOGGLE_DEBUG)

layout(location = ATTRIB_FREE_START + 1) in vec3 gs_wireColor;
layout(location = ATTRIB_FREE_START + 2) noperspective in vec4 gs_edgeDist;  //w - patternValue

#else //TOGGLE_DEBUG

#if defined(TOGGLE_TESS_LEVEL)
layout(location = ATTRIB_FREE_START + 3) in vec3 tes_debugColour;
#endif //TOGGLE_TESS_LEVEL

#endif //TOGGLE_DEBUG

#define NO_OCCLUSION_TEX
#define NO_METALNESS_TEX
#define NO_ROUGHNESS_TEX

#include "terrainUtils.cmn" 
#include "output.frag"
#include "BRDF.frag"
#include "terrainSplatting.frag"

#if defined (TOGGLE_LODS)
#include "turboColormap.frag"
#endif //TOGGLE_LODS

void main(void) {

    vec3 normalWV = WORLD_Y_AXIS;
    const vec4 albedo = BuildTerrainData(normalWV);

    vec4 colourOut = vec4(0.f, 0.f, 0.f, 1.f);

#if defined (TOGGLE_LODS)
    switch (VAR._LoDLevel) {
        case 0  : colourOut.r  = 1.f; break;
        case 1  : colourOut.g  = 1.f break;
        case 2  : colourOut.b  = 1.f; break;
        case 3  : colourOut.gb = vec2(1.f, 1.f); break;
        case 4  : colourOut.rb = vec2(1.f, 1.f); break;
        default : colourOut.rgb = turboColormap(float(VAR._LoDLevel)); break;
    }
#else  //TOGGLE_LODS
#if defined(TOGGLE_TESS_LEVEL)
    colourOut.rgb = tes_debugColour;
#else //TOGGLE_TESS_LEVEL
#if defined(TOGGLE_BLEND_MAP)
    for (int i = 0; i < MAX_TEXTURE_LAYERS; ++i) {
        colourOut.rgb += GetBlend(vec3(VAR._texCoord, i)).rgb;
    }
#else //TOGGLE_BLEND_MAP
    const NodeMaterialData materialData = dvd_Materials[MATERIAL_IDX];
    colourOut = getPixelColour(vec4(albedo.rgb, 1.f), materialData, normalWV);
#endif //TOGGLE_BLEND_MAP
#endif //TOGGLE_TESS_LEVEL

#if defined (TOGGLE_DEBUG)

#if defined(TOGGLE_NORMALS)
    colourOut.rgb = gs_wireColor;
#else //TOGGLE_NORMALS
    colourOut.rgb *= (0.5f * gs_edgeDist.w);

    #define LineWidth 0.75f
    #define D min(min(gs_edgeDist.x, gs_edgeDist.y), gs_edgeDist.z)
    colourOut = mix(vec4(gs_wireColor, 1.f), colourOut, smoothstep(LineWidth - 1, LineWidth + 1, D));
#endif //TOGGLE_NORMALS

#endif //TOGGLE_DEBUG

#endif //TOGGLE_LODS
    writeScreenColour(colourOut);
}

--Fragment.PrePass

layout(location = ATTRIB_FREE_START + 0) in vec2 tes_waterData;

#include "terrainUtils.cmn" 
#include "prePass.frag"
#include "terrainSplatting.frag"

void main(void) {

    const vec4 normalWVAndRoughness = GetTerrainNormalWVAndRoughness();
    writeGBuffer(normalWVAndRoughness.xyz, normalWVAndRoughness.w);
}

--Fragment.Shadow.VSM

#include "vsm.frag"
layout(location = 0) out vec2 _colourOut;

void main() {
    _colourOut = computeMoments();
}
