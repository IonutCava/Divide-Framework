--Vertex

#include "vbInputData.vert"

struct TerrainNodeData {
    vec4 _positionAndTileScale;
    vec4 _tScale;
};

layout(binding = BUFFER_TERRAIN_DATA, std430) coherent readonly buffer dvd_TerrainBlock
{
    TerrainNodeData dvd_TerrainData[];
};

vec2 calcTerrainTexCoord(in vec4 pos)
{
    vec2 TerrainOrigin = vec2(-(TERRAIN_WIDTH * 0.5), -(TERRAIN_LENGTH * 0.5));
    return vec2(abs(pos.x - TerrainOrigin.x) / TERRAIN_WIDTH, abs(pos.z - TerrainOrigin.y) / TERRAIN_LENGTH);
}

void main(void)
{
    computeData();

    vec4 posAndScale = dvd_TerrainData[VAR.dvd_drawID]._positionAndTileScale;

    vec4 patchPosition = vec4(dvd_Vertex.xyz * posAndScale.w, 1.0);
    
    VAR._vertexW = /*dvd_WorldMatrix(VAR.dvd_instanceID) * */vec4(patchPosition + vec4(posAndScale.xyz, 0.0));

    // Calcuate texture coordantes (u,v) relative to entire terrain
    VAR._texCoord = calcTerrainTexCoord(VAR._vertexW);

    // Send vertex position along
    gl_Position = patchPosition;
}

--TessellationC

#include "nodeBufferedInput.cmn"

layout(binding = TEXTURE_OPACITY)   uniform sampler2D TexTerrainHeight;

struct TerrainNodeData {
    vec4 _positionAndTileScale;
    vec4 _tScale;
};

layout(binding = BUFFER_TERRAIN_DATA, std430) coherent readonly buffer dvd_TerrainBlock
{
    TerrainNodeData dvd_TerrainData[];
};

//
// Outputs
//
layout(vertices = 4) out;

patch out float gl_TessLevelOuter[4];
patch out float gl_TessLevelInner[2];

out float tcs_tessLevel[];

mat4 mvMatrix;

/**
* Dynamic level of detail using camera distance algorithm.
*/
float dlodCameraDistance(vec4 p0, vec4 p1, vec2 t0, vec2 t1)
{
    float sampleHeight = texture(TexTerrainHeight, t0).r;
    p0.y = TERRAIN_MIN_HEIGHT + TERRAIN_HEIGHT_RANGE * sampleHeight;
    sampleHeight = texture(TexTerrainHeight, t1).r;
    p1.y = TERRAIN_MIN_HEIGHT + TERRAIN_HEIGHT_RANGE * sampleHeight;

    vec3 offset = dvd_TerrainData[VAR.dvd_drawID]._positionAndTileScale.xyz;
    vec4 view0 = mvMatrix * vec4(p0.xyz + offset, p0.w);
    vec4 view1 = mvMatrix * vec4(p1.xyz + offset, p1.w);

    float MinDepth = 5.0;
    float MaxDepth = 150.0;

    float d0 = clamp((abs(p0.z) - MinDepth) / (MaxDepth - MinDepth), 0.0, 1.0);
    float d1 = clamp((abs(p1.z) - MinDepth) / (MaxDepth - MinDepth), 0.0, 1.0);

    float t = mix(64, 2, (d0 + d1) * 0.5);

    if (t <= 2.0)
    {
        return 2.0;
    }
    if (t <= 4.0)
    {
        return 4.0;
    }
    if (t <= 8.0)
    {
        return 8.0;
    }
    if (t <= 16.0)
    {
        return 16.0;
    }
    if (t <= 32.0)
    {
        return 32.0;
    }

    return 64.0;
}

/**
* Dynamic level of detail using sphere algorithm.
* Source adapated from the DirectX 11 Terrain Tessellation example.
*/
float dlodSphere(vec4 p0, vec4 p1, vec2 t0, vec2 t1)
{
    float g_tessellatedTriWidth = 10.0;

    float sampleHeight = texture(TexTerrainHeight, t0).r;
    p0.y = TERRAIN_MIN_HEIGHT + TERRAIN_HEIGHT_RANGE * sampleHeight;
    sampleHeight = texture(TexTerrainHeight, t1).r;
    p1.y = TERRAIN_MIN_HEIGHT + TERRAIN_HEIGHT_RANGE * sampleHeight;


    vec3 offset = dvd_TerrainData[_in[0].dvd_drawID]._positionAndTileScale.xyz;

    vec4 center = 0.5 * (p0 + p1);
    vec4 view0 = mvMatrix * vec4(center.xyz + offset, center.w);
    vec4 view1 = view0;
    view1.x += distance(p0, p1);

    vec4 clip0 = dvd_ProjectionMatrix * view0;
    vec4 clip1 = dvd_ProjectionMatrix * view1;

    clip0 /= clip0.w;
    clip1 /= clip1.w;

    //clip0.xy *= dvd_ViewPort.zw;
    //clip1.xy *= dvd_ViewPort.zw;

    vec2 screen0 = ((clip0.xy + 1.0) / 2.0) * dvd_ViewPort.zw;
    vec2 screen1 = ((clip1.xy + 1.0) / 2.0) * dvd_ViewPort.zw;
    float d = distance(screen0, screen1);

    // g_tessellatedTriWidth is desired pixels per tri edge
    float t = clamp(d / g_tessellatedTriWidth, 0, 64);

    if (t <= 2.0)
    {
        return 2.0;
    }
    if (t <= 4.0)
    {
        return 4.0;
    }
    if (t <= 8.0)
    {
        return 8.0;
    }
    if (t <= 16.0)
    {
        return 16.0;
    }
    if (t <= 32.0)
    {
        return 32.0;
    }

    return 64.0;
}

void main(void)
{
    PassData(gl_InvocationID);

    mat4 mvMatrix = dvd_WorldViewMatrix(VAR.dvd_instanceID);

    // Outer tessellation level
    gl_TessLevelOuter[0] = dlodCameraDistance(gl_in[3].gl_Position, gl_in[0].gl_Position, _in[3]._texCoord, _in[0]._texCoord);
    gl_TessLevelOuter[1] = dlodCameraDistance(gl_in[0].gl_Position, gl_in[1].gl_Position, _in[0]._texCoord, _in[1]._texCoord);
    gl_TessLevelOuter[2] = dlodCameraDistance(gl_in[1].gl_Position, gl_in[2].gl_Position, _in[1]._texCoord, _in[2]._texCoord);
    gl_TessLevelOuter[3] = dlodCameraDistance(gl_in[2].gl_Position, gl_in[3].gl_Position, _in[2]._texCoord, _in[3]._texCoord);

    /*gl_TessLevelOuter[0] = dlodSphere(gl_in[3].gl_Position, gl_in[0].gl_Position, _in[3]._texCoord, _in[0]._texCoord);
    gl_TessLevelOuter[1] = dlodSphere(gl_in[0].gl_Position, gl_in[1].gl_Position, _in[0]._texCoord, _in[1]._texCoord);
    gl_TessLevelOuter[2] = dlodSphere(gl_in[1].gl_Position, gl_in[2].gl_Position, _in[1]._texCoord, _in[2]._texCoord);
    gl_TessLevelOuter[3] = dlodSphere(gl_in[2].gl_Position, gl_in[3].gl_Position, _in[2]._texCoord, _in[3]._texCoord);*/
    
    float tscale_negx = dvd_TerrainData[VAR.dvd_drawID]._tScale.x;
    float tscale_negz = dvd_TerrainData[VAR.dvd_drawID]._tScale.y;
    float tscale_posx = dvd_TerrainData[VAR.dvd_drawID]._tScale.z;
    float tscale_posz = dvd_TerrainData[VAR.dvd_drawID]._tScale.w;

    if (tscale_negx == 2.0) {
        gl_TessLevelOuter[0] = max(2.0, gl_TessLevelOuter[0] * 0.5);
    }

    if (tscale_negz == 2.0) {
        gl_TessLevelOuter[1] = max(2.0, gl_TessLevelOuter[1] * 0.5);
    }

    if (tscale_posx == 2.0) {
        gl_TessLevelOuter[2] = max(2.0, gl_TessLevelOuter[2] * 0.5);
    }

    if (tscale_posz == 2.0) {
        gl_TessLevelOuter[3] = max(2.0, gl_TessLevelOuter[3] * 0.5);
    }

    // Inner tessellation level
    gl_TessLevelInner[0] = 0.5 * (gl_TessLevelOuter[0] + gl_TessLevelOuter[3]);
    gl_TessLevelInner[1] = 0.5 * (gl_TessLevelOuter[2] + gl_TessLevelOuter[1]);

    // Pass the patch verts along
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

    // Output tessellation level (used for wireframe coloring)
    tcs_tessLevel[gl_InvocationID] = gl_TessLevelOuter[0];
}

--TessellationE

#include "nodeBufferedInput.cmn"

layout(binding = TEXTURE_OPACITY)   uniform sampler2D TexTerrainHeight;

struct TerrainNodeData {
    vec4 _positionAndTileScale;
    vec4 _tScale;
};

layout(binding = BUFFER_TERRAIN_DATA, std430) coherent readonly buffer dvd_TerrainBlock
{
    TerrainNodeData dvd_TerrainData[];
};

layout(quads, fractional_even_spacing) in;

patch in float gl_TessLevelOuter[4];
patch in float gl_TessLevelInner[2];

in float tcs_tessLevel[];

out float tes_tessLevel;

vec4 interpolate(in vec4 v0, in vec4 v1, in vec4 v2, in vec4 v3)
{
    vec4 a = mix(v0, v1, gl_TessCoord.x);
    vec4 b = mix(v3, v2, gl_TessCoord.x);
    return mix(a, b, gl_TessCoord.y);
}

vec2 interpolate2(in vec2 v0, in vec2 v1, in vec2 v2, in vec2 v3)
{
    vec2 a = mix(v0, v1, gl_TessCoord.x);
    vec2 b = mix(v3, v2, gl_TessCoord.x);
    return mix(a, b, gl_TessCoord.y);
}

vec4 getHeightOffsets(in vec2 tex_coord) {
    const ivec3 off = ivec3(-1, 0, 1);

    float s01 = textureOffset(TexTerrainHeight, tex_coord, off.xy).r;
    float s21 = textureOffset(TexTerrainHeight, tex_coord, off.zy).r;
    float s10 = textureOffset(TexTerrainHeight, tex_coord, off.yx).r;
    float s12 = textureOffset(TexTerrainHeight, tex_coord, off.yz).r;

    return vec4(s01, s21, s10, s12);
}

float getHeight(in vec4 heightOffsets) {
    
    float s01 = heightOffsets.r;
    float s21 = heightOffsets.g;
    float s10 = heightOffsets.b;
    float s12 = heightOffsets.a;

    return (s01 + s21 + s10 + s12) * 0.25;
}

vec3 getNormal(in float sampleHeight, in vec4 heightOffsets) {
    const vec2 size = vec2(2.0, 0.0);

    float s11 = sampleHeight;
    float s01 = heightOffsets.r;
    float s21 = heightOffsets.g;
    float s10 = heightOffsets.b;
    float s12 = heightOffsets.a;

    vec3 va = normalize(vec3(size.xy, s21 - s01));
    vec3 vb = normalize(vec3(size.yx, s12 - s10));

    return cross(va, vb);
}

vec3 getTangent(in vec3 normal) {
    vec3 temp_tangent = vec3(0.0, 0.0, 1.0);
    vec3 bitangent = cross(temp_tangent, normal);
    vec3 tangent = cross(normal, bitangent);
    return tangent;
}

void main()
{
    PassData(0);

    // Calculate the vertex position using the four original points and interpolate depending on the tessellation coordinates.	
    gl_Position = interpolate(gl_in[0].gl_Position, gl_in[1].gl_Position, gl_in[2].gl_Position, gl_in[3].gl_Position);

    // Terrain heightmap coords
    vec2 terrainTexCoord = interpolate2(VAR[0]._texCoord, VAR[1]._texCoord, VAR[2]._texCoord, VAR[3]._texCoord);


    vec4 heightOffsets = getHeightOffsets(terrainTexCoord);

    // Sample the heightmap and offset y position of vertex
    float sampleHeight = getHeight(heightOffsets);
    gl_Position.y = TERRAIN_MIN_HEIGHT + TERRAIN_HEIGHT_RANGE * sampleHeight;

    // Project the vertex to clip space and send it along
    vec3 offset = dvd_TerrainData[VAR[0].dvd_drawID]._positionAndTileScale.xyz;
    _out._vertexW = vec4(gl_Position.xyz + offset, gl_Position.w);

#if !defined(SHADOW_PASS)
    _out._vertexW = dvd_WorldMatrix(VAR[0].dvd_instanceID) * _out._vertexW;
    mat3 normalMatrix = dvd_NormalMatrixWV(VAR[0].dvd_instanceID);
    vec3 normal = getNormal(sampleHeight, heightOffsets);
    _out._normalWV = normalize(normalMatrix * normal);
    _out._tangentWV = normalize(normalMatrix * getTangent(normal));
    _out._bitangentWV = normalize(cross(_out._normalWV, _out._tangentWV));
#endif

    gl_Position = _out._vertexW;

    _out._texCoord = terrainTexCoord;
    _out._vertexWV = dvd_ViewMatrix * _out._vertexW;

    tes_tessLevel = tcs_tessLevel[0];
}

--Geometry

uniform float ToggleWireframe = 0.0;

layout(triangles) in;

in float tes_tessLevel[];

layout(triangle_strip, max_vertices = 4) out;

out vec4 _scrollingUV;
smooth out float _waterDistance;
smooth out float _waterDepth;

#if defined(_DEBUG)
out vec4 gs_wireColor;
noperspective out vec3 gs_edgeDist;
#endif

#if defined(SHADOW_PASS)
out vec4 geom_vertexWVP;
#endif

float waterDepth(int index) {
    return 1.0 - (dvd_clip_plane[0].w - VAR[index]._vertexW.y);
}

vec4 getScrollingUV(int index) {
    float time2 = float(dvd_time) * 0.0001;
    vec2 noiseUV = VAR[index]._texCoord * UNDERWATER_DIFFUSE_SCALE;
    vec4 scrollingUV;
    scrollingUV.st = noiseUV;
    scrollingUV.pq = noiseUV + time2;
    scrollingUV.s -= time2;
    return scrollingUV;
}

vec4 wireframeColor()
{
    if (tes_tessLevel[0] == 64.0) {
        return vec4(0.0, 0.0, 1.0, 1.0);
    } else if (tes_tessLevel[0] >= 32.0) {
        return vec4(0.0, 1.0, 1.0, 1.0);
    } else if (tes_tessLevel[0] >= 16.0) {
        return vec4(1.0, 1.0, 0.0, 1.0);
    } else if (tes_tessLevel[0] >= 8.0) {
        return vec4(1.0, 1.0, 1.0, 1.0);
    } else {
        return vec4(1.0, 0.0, 0.0, 1.0);
    }
}

vec4 getWVPPositon(int index) {
    return dvd_ViewProjectionMatrix * gl_in[index].gl_Position;
}

void main(void)
{
    vec4 wireColor = wireframeColor();

    // Calculate edge distances for wireframe
    float ha, hb, hc;
#if defined(_DEBUG)
    if (ToggleWireframe == 1.0)
    {
        vec4 pos0 = getWVPPositon(0);
        vec4 pos1 = getWVPPositon(1);
        vec4 pos2 = getWVPPositon(2);

        vec2 p0 = vec2(dvd_ViewPort.zw * (pos0.xy / pos0.w));
        vec2 p1 = vec2(dvd_ViewPort.zw * (pos1.xy / pos1.w));
        vec2 p2 = vec2(dvd_ViewPort.zw * (pos2.xy / pos2.w));

        float a = length(p1 - p2);
        float b = length(p2 - p0);
        float c = length(p1 - p0);
        float alpha = acos((b*b + c*c - a*a) / (2.0*b*c));
        float beta = acos((a*a + c*c - b*b) / (2.0*a*c));
        ha = abs(c * sin(beta));
        hb = abs(c * sin(alpha));
        hc = abs(b * sin(alpha));
    }
    else
#endif
    {
        ha = hb = hc = 0.0;
    }

    // Output verts
    for (int i = 0; i < gl_in.length(); ++i)
    {
        PassData(i);

    #if defined(SHADOW_PASS)
        geom_vertexWVP = gl_in[i].gl_Position;
    #endif

        gl_Position = getWVPPositon(i);

#if !defined(SHADOW_PASS)
        setClipPlanes(VAR[i]._vertexW);
        _waterDistance = gl_ClipDistance[0];
        _waterDepth = waterDepth(i);

        _scrollingUV = getScrollingUV(i);

#   if defined(_DEBUG)
        gs_wireColor = wireColor;

        if (i == 0) {
            gs_edgeDist = vec3(ha, 0, 0);
        } else if (i == 1) {
            gs_edgeDist = vec3(0, hb, 0);
        } else {
            gs_edgeDist = vec3(0, 0, hc);
        }
#   endif
#endif
        EmitVertex();
    }

    PassData(0);
#if defined(SHADOW_PASS)
    geom_vertexWVP = gl_in[0].gl_Position;
#endif

    // This closes the triangle
    gl_Position = getWVPPositon(0);

#if !defined(SHADOW_PASS)
    setClipPlanes(VAR[0]._vertexW);
    _waterDistance = gl_ClipDistance[0];
    _scrollingUV = getScrollingUV(0);
    _waterDepth = waterDepth(0);
#   if defined(_DEBUG)
    gs_edgeDist = vec3(ha, 0, 0);
    gs_wireColor = wireColor;
#   endif
#endif
    EmitVertex();

    EndPrimitive();
}

--Fragment.Depth

layout(early_fragment_tests) in;

void main()
{
}


--Fragment.Shadow

out vec2 _colourOut;
in vec4 geom_vertexWVP;

#include "nodeBufferedInput.cmn"

vec2 computeMoments(in float depth) {
    // Compute partial derivatives of depth.  
    float dx = dFdx(depth);
    float dy = dFdy(depth);
    // Compute second moment over the pixel extents.  
    return vec2(depth, depth*depth + 0.25*(dx*dx + dy*dy));
}

void main()
{
    // Adjusting moments (this is sort of bias per pixel) using partial derivative
    float depth = geom_vertexWVP.z / geom_vertexWVP.w;
    depth = depth * 0.5 + 0.5;
    //_colourOut = computeMoments(exp(DEPTH_EXP_WARP * depth));
    _colourOut = computeMoments(depth);
}

--Fragment

#define CUSTOM_MATERIAL_DATA

#include "BRDF.frag"
#include "terrainSplatting.frag"
#include "velocityCalc.frag"

uniform float ToggleWireframe = 0.0;

in vec4 _scrollingUV;
smooth in float _waterDistance;
smooth in float _waterDepth;

#if defined(_DEBUG)
in vec4 gs_wireColor;
noperspective in vec3 gs_edgeDist;
#endif

layout(location = 0) out vec4 _colourOut;
layout(location = 1) out vec2 _normalOut;
layout(location = 2) out vec2 _velocityOut;

float getOpacity() {
    return 1.0;
}

vec4 private_albedo = vec4(1.0);
void setAlbedo(in vec4 albedo) {
    private_albedo = albedo;
}

vec4 getAlbedo() {
    return private_albedo;
}

vec3 getEmissive() {
    return private_getEmissive();
}

vec3 getSpecular() {
    return private_getSpecular();
}

float getShininess() {
    return private_getShininess();
}

vec4 CausticsColour() {
    setAlbedo((texture(texWaterCaustics, _scrollingUV.st) +
               texture(texWaterCaustics, _scrollingUV.pq)) * 0.5);

    return getAlbedo(); //getPixelColour(VAR._texCoord);
}

vec4 UnderwaterColour() {

    vec2 coords = VAR._texCoord * UNDERWATER_DIFFUSE_SCALE;
    setAlbedo(texture(texUnderwaterAlbedo, coords));

    vec3 tbn = normalize(2.0 * texture(texUnderwaterDetail, coords).rgb - 1.0);
    setProcessedNormal(getTBNMatrix() * tbn);

    return getAlbedo(); //getPixelColour(VAR._texCoord);
}

vec4 UnderwaterMappingRoutine() {
    return mix(CausticsColour(), UnderwaterColour(), _waterDepth + 1.5);
}

vec4 TerrainMappingRoutine() {
    setAlbedo(getTerrainAlbedo());

    return getAlbedo();//getPixelColour(VAR._texCoord);
}

void main(void)
{
    bumpInit();

    setProcessedNormal(getTerrainNormal());

    vec4 colour = ToSRGB(applyFog(mix(TerrainMappingRoutine(), UnderwaterMappingRoutine(), min(_waterDistance, 0.0))));
    
#if defined(_DEBUG)
    float d = min(gs_edgeDist.x, gs_edgeDist.y);
    d = min(d, gs_edgeDist.z);

    float LineWidth = 0.75;
    float mixVal = smoothstep(LineWidth - 1, LineWidth + 1, d);

    if (ToggleWireframe == 1.0) {
        _colourOut = mix(gs_wireColor, colour, mixVal);
    } else {
        _colourOut = colour;
    }
#else
    _colourOut = colour;
#endif
    _normalOut = packNormal(getProcessedNormal());
    _velocityOut = velocityCalc(dvd_InvProjectionMatrix, getScreenPositionNormalised());
}