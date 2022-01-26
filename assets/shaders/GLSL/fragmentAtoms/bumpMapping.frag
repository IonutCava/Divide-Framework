#ifndef _BUMP_MAPPING_FRAG_
#define _BUMP_MAPPING_FRAG_

#if !defined(HAS_TBN)
#define HAS_TBN
#endif //!HAS_TBN

#define BUMP_NONE  0
#define BUMP_NORMAL 1
#define BUMP_PARALLAX 2
#define BUMP_PARALLAX_OCCLUSION 3

void aproximateTBN(in vec3 normalWV, out vec3 tangent, out vec3 bitangent) {
    vec3 c1 = cross(normalWV, vec3(0.0, 0.0, 1.0));
    vec3 c2 = cross(normalWV, vec3(0.0, 1.0, 0.0));
    if (length(c1) > length(c2)) {
        tangent = c1;
    } else {
        tangent = c2;
    }
    tangent = normalize(tangent);
    bitangent = normalize(cross(normalWV, tangent));
}
/*---------------- Normal blending -----------------*/
//ref: http://blog.selfshadow.com/sandbox/normals.html

vec3 normalLinearBlend(in vec3 n1, in vec3 n2) {
    vec3 r = (n1 + n2) * 2 - 2;
    return normalize(r);
}

vec3 normalOverlayBlend(vec3 n1, vec3 n2) {
    return normalize(2.f * overlayVec(n1, n2) - 1.f);
}

vec3 normalPartialDerivativesBlend(in vec3 n1, in vec3 n2) {
    return normalize(vec3(n1.xy * n2.z + n2.xy * n1.z, n1.z * n2.z));
}

vec3 normalWhiteoutBlend(vec3 n1, vec3 n2) {
    n1 = n1 * 2 - 1;
    n2 = n2 * 2 - 1;
    vec3 r = vec3(n1.xy + n2.xy, n1.z * n2.z);
    return normalize(r);
}

vec3 normalUDNBlend(in vec3 n1, in vec3 n2) {
    vec3 c = vec3(2, 1, 0);
    vec3 r;
    r = n2 * c.yyz + n1.xyz;
    r = r * c.xxx - c.xxy;
    return normalize(r);
}

vec3 normalRNMBlend(in vec3 n1, in vec3 n2) {
    vec3 t = n1.xyz * vec3(2, 2, 2) + vec3(-1, -1, 0);
    vec3 u = n2.xyz * vec3(-2, -2, 2) + vec3(1, 1, -1);
    vec3 r = t * dot(t, u) - u * t.z;
    return normalize(r);
}

vec3 normalUnityBlend(in vec3 n1, in vec3 n2) {
    mat3 nBasis = mat3(
        vec3(n1.z, n1.y, -n1.x), // +90 degree rotation around y axis
        vec3(n1.x, n1.z, -n1.y), // -90 degree rotation around x axis
        vec3(n1.x, n1.y,  n1.z));
    return normalize(n2.x * nBasis[0] + n2.y * nBasis[1] + n2.z * nBasis[2]);
}

#endif //_BUMP_MAPPING_FRAG_
