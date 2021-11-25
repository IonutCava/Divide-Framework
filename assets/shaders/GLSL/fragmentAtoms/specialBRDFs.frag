#ifndef _SPECIAL_BRDF_FRAG_
#define _SPECIAL_BRDF_FRAG_


//Return the PBR BRDF value
//   L  = normalised lightDir
//   V  = view direction
//   N  = surface normal of the pixel
//   lightColour = the colour of the light we're computing the BRDF factors for
//   lightAttenuation = attenuation factor to multiply the light's colour by (includes shadow, distance fade, etc)
//   ndl       = dot(normal,lightVec) [M_EPSILON,1.0f]
//   material = material value for the target pixel (base colour, OMR, spec value, etc)
vec3 GetBRDF_Special(in vec3 L,
                     in vec3 V,
                     in vec3 N,
                     in vec3 lightColour,
                     in float lightAttenuation,
                     in float ndl,
                     in float ndv,
                     in PBRMaterial material)
{
    if (material._shadingMode == SHADING_TOON) {
        const vec3 diffColour = material._diffuseColour;
        const float occlusion = material._occlusion;

        const float intensity = ndl;
        vec3 color2 = vec3(1.f);

        if (intensity > 0.95f) color2 = vec3(1.0f);
        else if (intensity > 0.75f) color2 = vec3(0.8f);
        else if (intensity > 0.50f) color2 = vec3(0.6f);
        else if (intensity > 0.25f) color2 = vec3(0.4f);
        else                        color2 = vec3(0.2f);

        const vec3 brdf = color2 * diffColour * occlusion;

        return brdf * lightColour * lightAttenuation;
    } else if (material._shadingMode == SHADING_FLAT) {
        return vec3(0.f);
    }

    return vec3(0.6f, 1.0f, 0.7f); //obvious lime-green
}


#endif //_SPECIAL_BRDF_FRAG_