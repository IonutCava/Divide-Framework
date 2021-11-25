-- Fragment

layout(early_fragment_tests) in;

#if !defined(OIT_PASS) && defined(HAS_TRANSPARENCY)
#define USE_ALPHA_DISCARD
#endif

#include "output.frag"
#include "BRDF.frag"

void main (void) {
    NodeMaterialData data = dvd_Materials[MATERIAL_IDX];

    const vec4 albedo = getAlbedo(data, vec3(VAR._texCoord, 0));
  
#if defined(USE_ALPHA_DISCARD)
    if (albedo.a < INV_Z_TEST_SIGMA) {
        discard;
    }
#endif //USE_ALPHA_DISCARD

    float normalVariation = 0.f;
    const vec3 normalWV = getNormalWV(data, vec3(VAR._texCoord, 0), normalVariation);
    vec3 MetalnessRoughnessProbeID = vec3(0.f, 1.f, 0.f);
    const vec4 rgba = getPixelColour(albedo, data, normalWV, normalVariation, VAR._texCoord, MetalnessRoughnessProbeID);
    writeScreenColour(rgba, normalWV, MetalnessRoughnessProbeID);
}
