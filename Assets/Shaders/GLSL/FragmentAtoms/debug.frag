#ifndef _DEBUG_FRAG_
#define _DEBUG_FRAG_

#include "lightingCalc.frag"
#include "turboColormap.frag"

bool getDebugColour(in PBRMaterial material, in NodeMaterialData materialData, in vec2 uv, in vec3 normalWV, out vec3 debugColour) {

    const vec3 viewVec = computeViewDirectionWV();

    debugColour = vec3(0.f);

    bool returnFlag = true;
    switch (dvd_MaterialDebugFlag) {
        case DEBUG_NONE:
        case DEBUG_SSR:
        case DEBUG_VELOCITY:
        case DEBUG_SSR_BLEND:
        case DEBUG_DEPTH: {
            returnFlag = false;
        } break;
        case DEBUG_ALBEDO:
        {
            debugColour = material._diffuseColour;
        } break;
        case DEBUG_LIGHTING:
        {
            PBRMaterial materialCopy = material;
            materialCopy._diffuseColour = vec3(1.f);
            materialCopy._emissive = vec3(0.f);
            debugColour = dvd_AmbientColour.rgb * materialCopy._diffuseColour * materialCopy._occlusion;
            debugColour = getLightContribution(materialCopy, normalWV, viewVec, dvd_ReceivesShadows(materialData), debugColour);
        } break;
        case DEBUG_SPECULAR:
        {
            debugColour = material._specular.rgb;
        } break;
        case DEBUG_KS:
        {
            const vec3 H = normalize(normalWV + viewVec);
            const vec3 kS = computeFresnelSchlickRoughness(H, viewVec, material._F0, material._roughness);
            debugColour = kS;
        } break;
        case DEBUG_IBL:
        {
            const float NdotV = max(dot(normalWV, viewVec), 0.f);
            vec3 iblRadiance = vec3(0.f);
    #if !defined(NO_ENV_MAPPING) && !defined(NO_IBL)
            PBRMaterial materialCopy = material;
            materialCopy._diffuseColour = vec3(1.f);
            iblRadiance = ApplyIBL(materialCopy, viewVec, normalWV, NdotV, VAR._vertexW.xyz, dvd_ProbeIndex(materialData));
    #endif
            debugColour = iblRadiance;
        } break;
        case DEBUG_SSAO: {
            #if !defined(NO_SSAO)
                debugColour = vec3(texture(texSSAO, dvd_screenPositionNormalised).r);
            #else //!NO_SSAO
                debugColour = vec3(1.f);
            #endif //!NO_SSAO
        } break;
        case DEBUG_UV: {
            debugColour = vec3(fract(uv), 0.f);
        } break;
        case DEBUG_EMISSIVE: {
            debugColour = material._emissive;
        } break;
        case DEBUG_ROUGHNESS: {
            debugColour = vec3(material._roughness);
        } break;
        case DEBUG_METALNESS: {
            debugColour = vec3(material._metallic);
        } break;
        case DEBUG_NORMALS:
        {
            const vec3 normalW = normalize(mat3(dvd_InverseViewMatrix) * normalWV);
            debugColour = abs(normalW);
        } break;
        case DEBUG_TANGENTS: {
            debugColour = normalize(mat3(dvd_InverseViewMatrix) * getTBNWV()[0]);
        } break;
        case DEBUG_BITANGENTS: {
            debugColour = normalize(mat3(dvd_InverseViewMatrix) * getTBNWV()[1]);
        } break;
        case DEBUG_SHADOW_MAPS:
        {
            debugColour = dvd_ReceivesShadows(materialData) ? vec3(getShadowMultiplier(normalWV)) : vec3(1.f);
        } break;
        case DEBUG_CSM_SPLITS:
        {
            #if defined(DISABLE_SHADOW_MAPPING)
                debugColour =  vec3(0.f);
            #else //DISABLE_SHADOW_MAPPING
                vec3 colour = vec3(0.f);

                const uint dirLightCount = dvd_LightData.x;
                for (int lightIdx = 0; lightIdx < dirLightCount; ++lightIdx)
                {
                    const Light light = dvd_LightSource[lightIdx];
                    const int shadowIndex = dvd_LightSource[lightIdx]._options.y;
                    if (shadowIndex > -1)
                    {
                        switch (getCSMSlice(dvd_CSMShadowTransforms[shadowIndex].dvd_shadowLightPosition))
                        {
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

                debugColour = colour;
            #endif //DISABLE_SHADOW_MAPPING
        } break;
        case DEBUG_LIGHT_HEATMAP:
        {
            LightGrid cluster = lightGrid[GetClusterIndex( gl_FragCoord )];
            uint lights = cluster._countPoint + cluster._countSpot;

            // show possible clipping
            if (lights == 0) {
                --lights;
            }
            else if (lights == MAX_LIGHTS_PER_CLUSTER) {
                ++lights;
            }

            debugColour = turboColormap(float(lights) / MAX_LIGHTS_PER_CLUSTER);
        } break;
        case DEBUG_DEPTH_CLUSTERS:
        {
            const vec3 colors[8] = vec3[](
                vec3( 0, 0, 0 ), vec3( 0, 0, 1 ), vec3( 0, 1, 0 ), vec3( 0, 1, 1 ),
                vec3( 1, 0, 0 ), vec3( 1, 0, 1 ), vec3( 1, 1, 0 ), vec3( 1, 1, 1 )
            );
            debugColour = colors[uint(mod( GetClusterZIndex( gl_FragCoord.z ), 8 ))];
        } break;
        case DEBUG_DEPTH_CLUSTER_AABBS :
        {
            const uint clusterIdx = GetClusterIndex(gl_FragCoord);
            debugColour = turboColormap( float( clusterIdx ) / (CLUSTERS_X * CLUSTERS_Y * CLUSTERS_Z) );
        }break;
        case DEBUG_REFRACTIONS:
        case DEBUG_REFLECTIONS: {
            debugColour = ApplySSR(material._roughness, vec3(0.f));
        } break;
        case DEBUG_MATERIAL_IDS: {
            debugColour = turboColormap(float(MATERIAL_IDX + 1) / dvd_Materials.length());
        } break;
        case DEBUG_SHADING_MODE: {
            debugColour = turboColormap(float(dvd_ShadingMode(materialData) + 1) / SHADING_COUNT); break;
        } break;
        case DEBUG_WORLD_AO:
        {
            debugColour = vec3( getWorldAO());
        } break;
    }

    return returnFlag;
}

#endif //_DEBUG_FRAG_