-- Fragment

#if defined(USE_ALBEDO_ALPHA) || defined(USE_OPACITY_MAP)
#   define HAS_TRANSPARENCY
#endif

layout(early_fragment_tests) in;

#include "BRDF.frag"
#include "output.frag"

void main (void) {
    NodeMaterialData data = dvd_Materials[MATERIAL_IDX];

    const vec4 albedo = getAlbedo(data, VAR._texCoord);
  
#if !defined(OIT_PASS) && defined(USE_ALPHA_DISCARD)
    if (albedo.a < INV_Z_TEST_SIGMA) {
        discard;
    }
#endif
    float normalVariation = 0.f;
    const vec3 normalWV = getNormalWV(VAR._texCoord, normalVariation);
    vec3 MetalnessRoughnessProbeID = vec3(0.f, 1.f, 0.f);
    vec3 SpecularColourOut = vec3(0.f);
    const vec4 rgba = getPixelColour(albedo, data, normalWV, normalVariation, VAR._texCoord, SpecularColourOut, MetalnessRoughnessProbeID);
    writeScreenColour(rgba, normalWV, SpecularColourOut, MetalnessRoughnessProbeID);
}
