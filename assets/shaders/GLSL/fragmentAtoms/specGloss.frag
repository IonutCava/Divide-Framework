#ifndef _SPEC_GLOSS_BRDF_FRAG_
#define _SPEC_GLOSS_BRDF_FRAG_

//Return the PBR BRDF value
//   L  = normalised lightDir
//   V  = view direction
//   N  = surface normal of the pixel
//   lightColour = the colour of the light we're computing the BRDF factors for
//   lightAttenuation = attenuation factor to multiply the light's colour by (includes shadow, distance fade, etc)
//   ndl       = dot(normal,lightVec) [M_EPSILON,1.0f]
//   material = material value for the target pixel (base colour, OMR, spec value, etc)
vec3 GetBRDF(in vec3 L,
             in vec3 V,
             in vec3 N,
             in vec3 lightColour,
             in float lightAttenuation,
             in float ndl,
             in float ndv,
             in PBRMaterial material)
{
    if (ndl > M_EPSILON) {
        const vec3 diffColour = material._diffuseColour;
        const float occlusion = material._occlusion;
        const vec4 specColour = material._specular;

        const vec3 H = normalize(V + L);

        const float ndh = clamp((dot(N, H)), M_EPSILON, 1.f);

        const vec3 specular = specColour.rgb * pow(ndh, specColour.a);
        const vec3 brdf = (diffColour + specular) * occlusion * ndl;

        return brdf * lightColour * lightAttenuation;
    }

    return vec3(0.f);
}

#endif //_SPEC_GLOSS_BRDF_FRAG_