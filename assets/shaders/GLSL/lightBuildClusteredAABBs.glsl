--Compute

// Ref:https://github.com/Angelo1211/HybridRenderingEngine/blob/master/assets/shaders/ComputeShaders/clusterCullLightShader.comp
// From blog post: http://www.aortiz.me/2018/12/21/CG.html#tiled-shading--forward

#include "lightInput.cmn"

//Function prototypes
vec3 screen2View( vec4 screen );
vec3 lineIntersectionToZPlane( vec3 A, vec3 B, float zDistance );

layout( local_size_x = 1, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
    #define zNear _zPlanes.x
    #define zFar _zPlanes.y

    //Eye position is zero in view space
    const vec3 eyePos = vec3( 0.0 );

    //Per Tile variables
    const uint tileIndex = gl_WorkGroupID.x +
                           gl_WorkGroupID.y * gl_NumWorkGroups.x +
                           gl_WorkGroupID.z * (gl_NumWorkGroups.x * gl_NumWorkGroups.y);

    const vec4 minScreen = vec4( (gl_WorkGroupID.xy + vec2( 0, 0 )) * dvd_ClusterSizes, 0.f, 1.f);
    const vec4 maxScreen = vec4( (gl_WorkGroupID.xy + vec2( 1, 1 )) * dvd_ClusterSizes, 0.f, 1.f);

    //Calculating the min and max point in screen space
    const vec4 maxPoint_sS = vec4( vec2( gl_WorkGroupID.x + 1, gl_WorkGroupID.y + 1 ) * dvd_ClusterSizes, -1.f, 1.f ); // Top Right
    const vec4 minPoint_sS = vec4( vec2( gl_WorkGroupID.x + 0, gl_WorkGroupID.y + 0 ) * dvd_ClusterSizes, -1.f, 1.f ); // Bottom left

    //Pass min and max to view space
    const vec3 maxPoint_vS = screen2View( maxPoint_sS );
    const vec3 minPoint_vS = screen2View( minPoint_sS );

    //Near and far values of the cluster in view space
    const float tileNear = -zNear * pow( zFar / zNear, (gl_WorkGroupID.z + 0) / float( gl_NumWorkGroups.z ) );
    const float tileFar  = -zNear * pow( zFar / zNear, (gl_WorkGroupID.z + 1) / float( gl_NumWorkGroups.z ) );

    //Finding the 4 intersection points made from the maxPoint to the cluster near/far plane
    const vec3 minPointNear = lineIntersectionToZPlane( eyePos, minPoint_vS, tileNear );
    const vec3 minPointFar  = lineIntersectionToZPlane( eyePos, minPoint_vS, tileFar );
    const vec3 maxPointNear = lineIntersectionToZPlane( eyePos, maxPoint_vS, tileNear );
    const vec3 maxPointFar  = lineIntersectionToZPlane( eyePos, maxPoint_vS, tileFar );

    const vec3 minPointAABB = min( min( minPointNear, minPointFar ), min( maxPointNear, maxPointFar ) );
    const vec3 maxPointAABB = max( max( minPointNear, minPointFar ), max( maxPointNear, maxPointFar ) );

    lightClusterAABBs[tileIndex].minPoint = vec4( minPointAABB, 0.f );
    lightClusterAABBs[tileIndex].maxPoint = vec4( maxPointAABB, 0.f );
}

//Creates a line from the eye to the screenpoint, then finds its intersection
//With a z oriented plane located at the given distance to the origin
vec3 lineIntersectionToZPlane( in vec3 A, in vec3 B, in float zDistance )
{
    //Because this is a Z based normal this is fixed
    const vec3 normal = WORLD_Z_AXIS;

    const vec3 ab = B - A;

    //Computing the intersection length for the line and the plane
    const float t = (zDistance - dot( normal, A )) / dot( normal, ab );

    //Computing the actual xyz position of the point along the line
    return A + t * ab;
}

vec3 screen2View( in vec4 screen )
{
    //Convert to NDC
    const vec2 texCoord = (screen.xy - viewport.xy) / viewport.zw;

    //Convert to clipSpace
    const vec4 clip = vec4(2.f * texCoord.xy - 1.f, screen.zw );

    //View space transform + Perspective projection
    return Homogenize( inverseProjectionMatrix * clip );
}
