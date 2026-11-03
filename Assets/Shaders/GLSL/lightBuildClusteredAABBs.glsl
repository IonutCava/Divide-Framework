--Compute

// Ref: https://github.com/Angelo1211/HybridRenderingEngine/blob/master/assets/shaders/ComputeShaders/clusterShader.comp
// From blog post: http://www.aortiz.me/2018/12/21/CG.html#tiled-shading--forward

#include "lightInput.cmn"

// Point on the line through A and B at view-space depth Z. Valid for perspective and orthographic projections.
#define PointAtViewZ(A, B, Z) mix((A), (B), ((Z) - (A).z) / ((B).z - (A).z))

// One invocation per cluster. Inputs (push data): inverseProjectionMatrix, viewport (x, y, width, height) and _zPlanes (near, far).
layout( local_size_x = 1, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
    const uint tileIndex = gl_WorkGroupID.x +
                           gl_WorkGroupID.y * CLUSTERS_X +
                           gl_WorkGroupID.z * (CLUSTERS_X * CLUSTERS_Y);

    // Must match dvd_ClusterSizes and GetClusterIndex: tiles are ceil(viewport / grid) pixels, relative to the viewport origin.
    const vec2 viewportSize = vec2( viewport.zw );
    const vec2 clusterSize = ceil( viewportSize / vec2( CLUSTERS_X, CLUSTERS_Y ) );
    const vec2 minUV = (vec2( gl_WorkGroupID.xy ) * clusterSize) / viewportSize;
    const vec2 maxUV = (vec2( gl_WorkGroupID.xy + 1u ) * clusterSize) / viewportSize;

    // Tile corners on the near (depth 0) and far (depth 1) planes, in view space.
    const vec3 minNear = ViewSpacePos( minUV, 0.f, inverseProjectionMatrix );
    const vec3 minFar  = ViewSpacePos( minUV, 1.f, inverseProjectionMatrix );
    const vec3 maxNear = ViewSpacePos( maxUV, 0.f, inverseProjectionMatrix );
    const vec3 maxFar  = ViewSpacePos( maxUV, 1.f, inverseProjectionMatrix );

    // Exponential depth slices, matching dvd_LightScale/dvd_LightBias in GetClusterZIndex. View space looks down -Z.
    const float zNear = _zPlanes.x;
    const float zFar = _zPlanes.y;
    const float sliceNear = -zNear * pow( zFar / zNear, float( gl_WorkGroupID.z + 0u ) / CLUSTERS_Z );
    const float sliceFar  = -zNear * pow( zFar / zNear, float( gl_WorkGroupID.z + 1u ) / CLUSTERS_Z );

    // X depends only on U and Y only on V, and both are linear along each edge, so these four points bound the cell.
    const vec3 p0 = PointAtViewZ( minNear, minFar, sliceNear );
    const vec3 p1 = PointAtViewZ( minNear, minFar, sliceFar );
    const vec3 p2 = PointAtViewZ( maxNear, maxFar, sliceNear );
    const vec3 p3 = PointAtViewZ( maxNear, maxFar, sliceFar );

    lightClusterAABBs[tileIndex].minPoint = vec4( min( min( p0, p1 ), min( p2, p3 ) ), 0.f );
    lightClusterAABBs[tileIndex].maxPoint = vec4( max( max( p0, p1 ), max( p2, p3 ) ), 0.f );
}
