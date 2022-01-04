#ifndef _DEBUG_FRAG_
#define _DEBUG_FRAG_

vec3 ApplyIBL(in PBRMaterial material, in vec3 viewDirectionWV, in vec3 normalWV, in vec3 positionW, in uint probeID);
void getLightContribution(in PBRMaterial material, in vec3 N, in vec3 V, in bool receivesShadows, inout vec3 radianceOut);
vec3 computeFresnelSchlickRoughness(in vec3 H, in vec3 V, in vec3 F0, in float roughness);
float getShadowMultiplier(in vec3 normalWV);
vec3 ApplySSR(in vec3 radianceIn);

vec3 getDebugColour(in PBRMaterial material, in vec2 uv, in vec3 normalWV, in uint probeID, in bool receivesShadows) {
    const vec3 viewVec = normalize(VAR._viewDirectionWV);
    vec3 radianceOut = vec3(0.f);

    switch (dvd_materialDebugFlag) {
        case DEBUG_ALBEDO:
        {
            return material._diffuseColour;
        }
        case DEBUG_LIGHTING:
        {
            PBRMaterial materialCopy = material;
            materialCopy._diffuseColour = vec3(1.f);
            radianceOut = dvd_AmbientColour.rgb;
            getLightContribution(materialCopy, normalWV, viewVec, receivesShadows, radianceOut);
            radianceOut += dvd_AmbientColour.rgb * materialCopy._diffuseColour * materialCopy._occlusion;
            return radianceOut;
        }
        case DEBUG_SPECULAR:
        {
            return material._specular.rgb;
        }
        case DEBUG_KS:
        {
            const vec3 H = normalize(normalWV + viewVec);
            const vec3 kS = computeFresnelSchlickRoughness(H, viewVec, material._F0, material._roughness);
            return kS;
        }
        case DEBUG_IBL:
        {
            vec3 iblRadiance = vec3(0.f);
    #if !defined(NO_ENV_MAPPING) && !defined(NO_IBL)
            PBRMaterial materialCopy = material;
            materialCopy._diffuseColour = vec3(1.f);
            iblRadiance = ApplyIBL(materialCopy, viewVec, normalWV, VAR._vertexW.xyz, probeID);
    #endif
            return iblRadiance;
        }
        case DEBUG_SSAO: {
            #if !defined(NO_SSAO)
                return vec3(texture(texSSAO, dvd_screenPositionNormalised).r);
            #else //!NO_SSAO
                return vec3(1.f);
            #endif //!NO_SSAO
        }
        case DEBUG_UV:             return vec3(fract(uv), 0.f);
        case DEBUG_EMISSIVE:       return material._emissive;
        case DEBUG_ROUGHNESS:      return vec3(material._roughness);
        case DEBUG_METALNESS:      return vec3(material._metallic);
        case DEBUG_NORMALS:
        {
            const vec3 normalW = normalize(mat3(dvd_InverseViewMatrix) * normalWV);
            return abs(normalW);
        }
        case DEBUG_TANGENTS:       return normalize(mat3(dvd_InverseViewMatrix) * getTBNWV()[0]);
        case DEBUG_BITANGENTS:     return normalize(mat3(dvd_InverseViewMatrix) * getTBNWV()[1]);
        case DEBUG_SHADOW_MAPS:
        {
            return receivesShadows ? vec3(getShadowMultiplier(normalWV)) : vec3(1.f);
        }
        case DEBUG_CSM_SPLITS:
        {
            #if defined(DISABLE_SHADOW_MAPPING)
                return vec3(0.f);
            #else //DISABLE_SHADOW_MAPPING
                vec3 colour = vec3(0.f);

                const uint dirLightCount = dvd_LightData.x;
                for (uint lightIdx = 0; lightIdx < dirLightCount; ++lightIdx) {
                    const Light light = dvd_LightSource[lightIdx];
                    const int shadowIndex = dvd_LightSource[lightIdx]._options.y;
                    if (shadowIndex > -1) {
                        switch (getCSMSlice(dvd_CSMShadowTransforms[shadowIndex].dvd_shadowLightPosition)) {
                            case  0: colour.r += 0.15f; break;
                            case  1: colour.g += 0.25f; break;
                            case  2: colour.b += 0.40f; break;
                            case  3: colour += 1 * vec3(0.15f, 0.25f, 0.40f); break;
                            case  4: colour += 2 * vec3(0.15f, 0.25f, 0.40f); break;
                            case  5: colour += 3 * vec3(0.15f, 0.25f, 0.40f); break;
                        };
                        break;
                    }
                }

                return colour;
            #endif //DISABLE_SHADOW_MAPPING
        }
        case DEBUG_LIGHT_HEATMAP:
        {
            uint lights = lightGrid[GetClusterIndex(gl_FragCoord)]._countTotal;

            // show possible clipping
            if (lights == 0) {
                --lights;
            }
            else if (lights == MAX_LIGHTS_PER_CLUSTER) {
                ++lights;
            }

            return turboColormap(float(lights) / MAX_LIGHTS_PER_CLUSTER);
        }
        case DEBUG_DEPTH_CLUSTERS:
        {
            switch (GetClusterZIndex(gl_FragCoord.z) % 8) {
                case 0:  return vec3(1.0f, 0.0f, 0.0f);
                case 1:  return vec3(0.0f, 1.0f, 0.0f);
                case 2:  return vec3(0.0f, 0.0f, 1.0f);
                case 3:  return vec3(1.0f, 1.0f, 0.0f);
                case 4:  return vec3(1.0f, 0.0f, 1.0f);
                case 5:  return vec3(1.0f, 1.0f, 1.0f);
                case 6:  return vec3(1.0f, 0.5f, 0.5f);
                case 7:  return vec3(0.0f, 0.0f, 0.0f);
            }

            return vec3(0.5f, 0.25f, 0.75f);
        }
        case DEBUG_REFRACTIONS:
        case DEBUG_REFLECTIONS:    return ApplySSR(vec3(0.f));
        case DEBUG_MATERIAL_IDS:   return turboColormap(float(MATERIAL_IDX + 1) / dvd_Materials.length());
        case DEBUG_SHADING_MODE:   return turboColormap(float(material._shadingMode + 1) / SHADING_COUNT);
    }

    return vec3(0.f);
}

#endif //_DEBUG_FRAG_