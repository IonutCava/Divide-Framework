-- Fragment

layout(early_fragment_tests) in;

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
    const vec4 rgba = getPixelColour(albedo, data, normalWV, normalVariation, VAR._texCoord);
    writeScreenColour(rgba);
}
