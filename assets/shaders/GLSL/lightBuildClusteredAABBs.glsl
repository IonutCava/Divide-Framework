--Compute

// Ref:https://github.com/Angelo1211/HybridRenderingEngine/blob/master/assets/shaders/ComputeShaders/clusterCullLightShader.comp
// From blog post: http://www.aortiz.me/2018/12/21/CG.html#tiled-shading--forward

#define COMPUTE_LIGHT_CLUSTERS
#include "lightInput.cmn"

//Function prototypes
vec3 screen2View(in vec4 screen);
vec3 lineIntersectionToZPlane(vec3 A, vec3 B, float zDistance);

layout(local_size_x = CLUSTERS_X_THREADS, local_size_y = CLUSTERS_Y_THREADS, local_size_z = CLUSTERS_Z_THREADS) in;
void main() {
    //Shared between all clusters
    const float zNear = dvd_ZPlanes.x;
    const float zFar = dvd_ZPlanes.y;

    //Eye position is zero in view space
    const vec3 eyePos = vec3(0.f);

    //Per Tile variables
    const uint clusterIndex = gl_GlobalInvocationID.z * gl_WorkGroupSize.x * gl_WorkGroupSize.y +
                              gl_GlobalInvocationID.y * gl_WorkGroupSize.x +
                              gl_GlobalInvocationID.x;

    //Calculating the min and max point in screen space
    const vec4 minPoint_sS = vec4( gl_GlobalInvocationID.xy               * dvd_ClusterSizes, -1.f, 1.f); // Bottom left
    const vec4 maxPoint_sS = vec4((gl_GlobalInvocationID.xy + vec2(1, 1)) * dvd_ClusterSizes, -1.f, 1.f); // Top Right

    //Pass min and max to view space
    const vec3 maxPoint_vS = screen2View(maxPoint_sS);
    const vec3 minPoint_vS = screen2View(minPoint_sS);

    //Near and far values of the cluster in view space
    const float tileNear = 2.f * (-zNear * pow(zFar / zNear,  gl_GlobalInvocationID.z      / float(CLUSTERS_Z))) - 1.f;
    const float tileFar  = 2.f * (-zNear * pow(zFar / zNear, (gl_GlobalInvocationID.z + 1) / float(CLUSTERS_Z))) - 1.f;

    //Finding the 4 intersection points made from the maxPoint to the cluster near/far plane
    const vec3 minPointNear = lineIntersectionToZPlane(eyePos, minPoint_vS, tileNear);
    const vec3 minPointFar  = lineIntersectionToZPlane(eyePos, minPoint_vS, tileFar);
    const vec3 maxPointNear = lineIntersectionToZPlane(eyePos, maxPoint_vS, tileNear);
    const vec3 maxPointFar  = lineIntersectionToZPlane(eyePos, maxPoint_vS, tileFar);

    vec3 minPointAABB = min(min(minPointNear, minPointFar), min(maxPointNear, maxPointFar));
    vec3 maxPointAABB = max(max(minPointNear, minPointFar), max(maxPointNear, maxPointFar));

    lightClusterAABBs[clusterIndex].minPoint = vec4(minPointAABB, 0.f);
    lightClusterAABBs[clusterIndex].maxPoint = vec4(maxPointAABB, 0.f);
}

//Creates a line from the eye to the screenpoint, then finds its intersection
//With a z oriented plane located at the given distance to the origin
vec3 lineIntersectionToZPlane(vec3 A, vec3 B, float zDistance) {
    //Because this is a Z based normal this is fixed
    const vec3 normal = vec3(0.f, 0.f, 1.f);

    const vec3 ab = B - A;

    //Computing the intersection length for the line and the plane
    const float t = (zDistance - dot(normal, A)) / dot(normal, ab);

    //Computing the actual xyz position of the point along the line
    return A + t * ab;
}

vec3 screen2View(in vec4 coord) {
    vec3 ndc = vec3
    (
        2.f * (coord.x - dvd_ViewPort.x) / dvd_ViewPort.z - 1.f,
        2.f * (coord.y - dvd_ViewPort.y) / dvd_ViewPort.w - 1.f,
        2.f * coord.z - 1.f // -> [-1, 1]
    );
    return Homogenize(dvd_InverseProjectionMatrix * vec4(ndc, 1.f));
}