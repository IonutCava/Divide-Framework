-- Fragment

layout(early_fragment_tests) in;

#include "output.frag"
#include "BRDF.frag"

void main (void) {
    NodeMaterialData data = dvd_Materials[MATERIAL_IDX];

    const vec3 texCoord = vec3( VAR._texCoord, 0 );

    const vec4 albedo = getAlbedo(data, texCoord);
  
#if defined(USE_ALPHA_DISCARD)
    if (albedo.a < ALPHA_DISCARD_THRESHOLD)
    {
        discard;
    }
#endif //USE_ALPHA_DISCARD

    const vec3 normalWV = getNormalWV( data, texCoord );
    writeScreenColour( getPixelColour( albedo, data, normalWV ), normalWV );
}
