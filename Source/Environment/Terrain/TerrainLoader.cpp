

#include "Headers/Terrain.h"
#include "Headers/TerrainLoader.h"
#include "Headers/TerrainDescriptor.h"

#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/RenderStateBlock.h"

#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/SceneManager.h"

namespace Divide {

namespace {
    constexpr U16 BYTE_BUFFER_VERSION = 1u;

    ResourcePath ClimatesLocation(U8 textureQuality) {
       CLAMP<U8>(textureQuality, 0u, 3u);

       return Paths::g_assetsLocation + Paths::g_heightmapLocation +
             (textureQuality == 3u ? Paths::g_climatesHighResLocation
                                   : textureQuality == 2u ? Paths::g_climatesMedResLocation
                                                          : Paths::g_climatesLowResLocation);
    }

    std::pair<U8, bool> FindOrInsert(const U8 textureQuality, vector<string>& container, const string& texture, string materialName) {
        if (!fileExists((ClimatesLocation(textureQuality) + "/" + materialName + "/" + texture).c_str())) {
            materialName = "std_default";
        }
        const string item = materialName + "/" + texture;
        const auto* const it = eastl::find(eastl::cbegin(container),
                                    eastl::cend(container),
                                    item);
        if (it != eastl::cend(container)) {
            return std::make_pair(to_U8(eastl::distance(eastl::cbegin(container), it)), false);
        }

        container.push_back(item);
        return std::make_pair(to_U8(container.size() - 1), true);
    }
}

bool TerrainLoader::loadTerrain(const Terrain_ptr& terrain,
                                const std::shared_ptr<TerrainDescriptor>& terrainDescriptor,
                                PlatformContext& context,
                                bool threadedLoading) {

    const string& name = terrainDescriptor->getVariable("terrainName");

    ResourcePath terrainLocation = Paths::g_assetsLocation + Paths::g_heightmapLocation;
    terrainLocation += terrainDescriptor->getVariable("descriptor");

    Attorney::TerrainLoader::descriptor(*terrain, terrainDescriptor);
    const U8 textureQuality = context.config().terrain.textureQuality;

    // Noise texture
    SamplerDescriptor noiseMediumSampler = {};
    noiseMediumSampler._wrapU = TextureWrap::REPEAT;
    noiseMediumSampler._wrapV = TextureWrap::REPEAT;
    noiseMediumSampler._wrapW = TextureWrap::REPEAT;
    noiseMediumSampler._anisotropyLevel = 16u;
    
    ImageTools::ImportOptions importOptions{};
    importOptions._useDDSCache = true;

    TextureDescriptor noiseMediumDescriptor(TextureType::TEXTURE_2D_ARRAY, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA );
    importOptions._alphaChannelTransparency = false;
    importOptions._isNormalMap = false;
    noiseMediumDescriptor.textureOptions(importOptions);

    ResourceDescriptor textureNoiseMedium("Terrain Noise Map_" + name);
    textureNoiseMedium.assetLocation(Paths::g_assetsLocation + Paths::g_imagesLocation);
    textureNoiseMedium.assetName(ResourcePath{ "medium_noise.png" });
    textureNoiseMedium.propertyDescriptor(noiseMediumDescriptor);

    // Blend maps
    ResourceDescriptor textureBlendMap("Terrain Blend Map_" + name);
    textureBlendMap.assetLocation(terrainLocation);

    // Albedo maps and roughness
    ResourceDescriptor textureAlbedoMaps("Terrain Albedo Maps_" + name);
    textureAlbedoMaps.assetLocation(ClimatesLocation(textureQuality));

    // Normals
    ResourceDescriptor textureNormalMaps("Terrain Normal Maps_" + name);
    textureNormalMaps.assetLocation(ClimatesLocation(textureQuality));

    // AO and displacement
    ResourceDescriptor textureExtraMaps("Terrain Extra Maps_" + name);
    textureExtraMaps.assetLocation(ClimatesLocation(textureQuality));

    //temp data
    string layerOffsetStr;
    string currentMaterial;

    U8 layerCount = terrainDescriptor->textureLayers();

    const vector<std::pair<string, TerrainTextureChannel>> channels = {
        {"red", TerrainTextureChannel::TEXTURE_RED_CHANNEL},
        {"green", TerrainTextureChannel::TEXTURE_GREEN_CHANNEL},
        {"blue", TerrainTextureChannel::TEXTURE_BLUE_CHANNEL},
        {"alpha", TerrainTextureChannel::TEXTURE_ALPHA_CHANNEL}
    };

    vector<string> textures[to_base(TerrainTextureType::COUNT)] = {};
    vector<string> splatTextures = {};

    const char* textureNames[to_base(TerrainTextureType::COUNT)] = {
        "Albedo_roughness", "Normal", "Displacement"
    };

    for (U8 i = 0u; i < layerCount; ++i) {
        layerOffsetStr = Util::to_string(i);
        splatTextures.push_back(terrainDescriptor->getVariable("blendMap" + layerOffsetStr));
    }

    for (U8 i = 0u; i < layerCount; ++i) {
        layerOffsetStr = Util::to_string(i);
        U8 j = 0u;
        for (const auto& [channelName, channel] : channels) {
            currentMaterial = terrainDescriptor->getVariable(channelName + layerOffsetStr + "_mat");
            if (currentMaterial.empty()) {
                continue;
            }

            for (U8 k = 0u; k < to_base(TerrainTextureType::COUNT); ++k) {
                FindOrInsert(textureQuality, textures[k], Util::StringFormat("%s.%s", textureNames[k], k == to_base(TerrainTextureType::ALBEDO_ROUGHNESS) ? "png" : "jpg"), currentMaterial);
            }

            ++j;
        }
    }

    ResourcePath blendMapArray = {};
    ResourcePath albedoMapArray = {};
    ResourcePath normalMapArray = {};
    ResourcePath extraMapArray = {};

    U16 extraMapCount = 0;
    for (const string& tex : textures[to_base(TerrainTextureType::ALBEDO_ROUGHNESS)]) {
        albedoMapArray.append(tex + ",");
    }
    for (const string& tex : textures[to_base(TerrainTextureType::NORMAL)]) {
        normalMapArray.append(tex + ",");
    }

    for (U8 i = to_U8(TerrainTextureType::DISPLACEMENT_AO); i < to_U8(TerrainTextureType::COUNT); ++i) {
        for (const string& tex : textures[i]) {
            extraMapArray.append(tex + ",");
            ++extraMapCount;
        }
    }

    for (const string& tex : splatTextures) {
        blendMapArray.append(tex + ",");
    }

    blendMapArray.pop_back();
    albedoMapArray.pop_back();
    normalMapArray.pop_back();
    extraMapArray.pop_back();

    SamplerDescriptor heightMapSampler = {};
    heightMapSampler._wrapU = TextureWrap::CLAMP_TO_BORDER;
    heightMapSampler._wrapV = TextureWrap::CLAMP_TO_BORDER;
    heightMapSampler._wrapW = TextureWrap::CLAMP_TO_BORDER;
    heightMapSampler._mipSampling = TextureMipSampling::NONE;
    heightMapSampler._minFilter = TextureFilter::LINEAR;
    heightMapSampler._magFilter = TextureFilter::LINEAR;
    heightMapSampler._anisotropyLevel = 0u;

    SamplerDescriptor blendMapSampler = {};
    blendMapSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
    blendMapSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
    blendMapSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
    blendMapSampler._anisotropyLevel = 16u;

    SamplerDescriptor albedoSampler = {};
    albedoSampler._wrapU = TextureWrap::REPEAT;
    albedoSampler._wrapV = TextureWrap::REPEAT;
    albedoSampler._wrapW = TextureWrap::REPEAT;
    albedoSampler._anisotropyLevel = 16u;

    TextureDescriptor albedoDescriptor(TextureType::TEXTURE_2D_ARRAY, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA );
    albedoDescriptor.layerCount(to_U16(textures[to_base(TerrainTextureType::ALBEDO_ROUGHNESS)].size()));
    importOptions._alphaChannelTransparency = false; //roughness
    importOptions._isNormalMap = false;
    albedoDescriptor.textureOptions(importOptions);

    TextureDescriptor blendMapDescriptor(TextureType::TEXTURE_2D_ARRAY, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA );
    blendMapDescriptor.layerCount(to_U16(splatTextures.size()));
    importOptions._alphaChannelTransparency = false; //splat lookup
    importOptions._isNormalMap = false;
    blendMapDescriptor.textureOptions(importOptions);

    TextureDescriptor normalDescriptor(TextureType::TEXTURE_2D_ARRAY, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA );
    normalDescriptor.layerCount(to_U16(textures[to_base(TerrainTextureType::NORMAL)].size()));
    importOptions._alphaChannelTransparency = false; //not really needed
    importOptions._isNormalMap = true;
    importOptions._useDDSCache = false;
    normalDescriptor.textureOptions(importOptions);
    importOptions._useDDSCache = true;

    TextureDescriptor extraDescriptor(TextureType::TEXTURE_2D_ARRAY, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA );
    extraDescriptor.layerCount(extraMapCount);
    importOptions._alphaChannelTransparency = false; //who knows what we pack here?
    importOptions._isNormalMap = false;
    extraDescriptor.textureOptions(importOptions);

    textureBlendMap.assetName(blendMapArray);
    textureBlendMap.propertyDescriptor(blendMapDescriptor);

    textureAlbedoMaps.assetName(albedoMapArray);
    textureAlbedoMaps.propertyDescriptor(albedoDescriptor);

    textureNormalMaps.assetName(normalMapArray);
    textureNormalMaps.propertyDescriptor(normalDescriptor);

    textureExtraMaps.assetName(extraMapArray);
    textureExtraMaps.propertyDescriptor(extraDescriptor);

    ResourceDescriptor terrainMaterialDescriptor("terrainMaterial_" + name);
    Material_ptr terrainMaterial = CreateResource<Material>(terrain->parentResourceCache(), terrainMaterialDescriptor);
    terrainMaterial->ignoreXMLData(true);

    terrainMaterial->updatePriorirty(Material::UpdatePriority::Medium);

    const vec2<U16> terrainDimensions = terrainDescriptor->dimensions();
    const vec2<F32> altitudeRange = terrainDescriptor->altitudeRange();

    Console::d_printfn(LOCALE_STR("TERRAIN_INFO"), terrainDimensions.width, terrainDimensions.height);

    const F32 underwaterTileScale = terrainDescriptor->getVariablef("underwaterTileScale");
    terrainMaterial->properties().shadingMode(ShadingMode::PBR_MR);

    const Terrain::ParallaxMode pMode = static_cast<Terrain::ParallaxMode>(CLAMPED(to_I32(to_U8(context.config().terrain.parallaxMode)), 0, 2));
    if (pMode == Terrain::ParallaxMode::NORMAL) {
        terrainMaterial->properties().bumpMethod(BumpMethod::PARALLAX);
    } else if (pMode == Terrain::ParallaxMode::OCCLUSION) {
        terrainMaterial->properties().bumpMethod(BumpMethod::PARALLAX_OCCLUSION);
    } else {
        terrainMaterial->properties().bumpMethod(BumpMethod::NONE);
    }

    terrainMaterial->properties().baseColour(FColour4(DefaultColours::WHITE.rgb * 0.5f, 1.0f));
    terrainMaterial->properties().metallic(0.0f);
    terrainMaterial->properties().roughness(0.8f);
    terrainMaterial->properties().parallaxFactor(0.3f);
    terrainMaterial->properties().toggleTransparency(false);

    const TerrainDescriptor::LayerData& layerTileData = terrainDescriptor->layerDataEntries();
    string tileFactorStr = Util::StringFormat("const vec2 CURRENT_TILE_FACTORS[%d] = {\n", layerCount * 4);
    for (U8 i = 0u; i < layerCount; ++i) {
        const TerrainDescriptor::LayerDataEntry& entry = layerTileData[i];
        for (U8 j = 0u; j < 4u; ++j) {
            const vec2<F32> factors = entry[j];
            tileFactorStr.append(Util::StringFormat("%*c", 8, ' '));
            tileFactorStr.append(Util::StringFormat("vec2(%3.2f, %3.2f),\n", factors.s, factors.t));
        }
    }
    tileFactorStr.pop_back();
    tileFactorStr.pop_back();
    tileFactorStr.append("\n};");

    ResourcePath helperTextures { terrainDescriptor->getVariable("waterCaustics") + "," +
                                  terrainDescriptor->getVariable("underwaterAlbedoTexture") + "," +
                                  terrainDescriptor->getVariable("underwaterDetailTexture") + "," +
                                  terrainDescriptor->getVariable("tileNoiseTexture") };

    TextureDescriptor helperTexDescriptor(TextureType::TEXTURE_2D_ARRAY, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA );
    helperTexDescriptor.textureOptions()._alphaChannelTransparency = false;

    ResourceDescriptor textureWaterCaustics("Terrain Helper Textures_" + name);
    textureWaterCaustics.assetLocation(Paths::g_assetsLocation + Paths::g_imagesLocation);
    textureWaterCaustics.assetName(helperTextures);
    textureWaterCaustics.propertyDescriptor(helperTexDescriptor);

    ResourceDescriptor heightMapTexture("Terrain Heightmap_" + name);
    heightMapTexture.assetLocation(terrainLocation);
    heightMapTexture.assetName(ResourcePath{ terrainDescriptor->getVariable("heightfieldTex") });

    ImageTools::ImportOptions options{};
    options._useDDSCache = false;

    TextureDescriptor heightMapDescriptor(TextureType::TEXTURE_2D_ARRAY, GFXDataFormat::FLOAT_16, GFXImageFormat::RED, GFXImagePacking::UNNORMALIZED );
    heightMapDescriptor.textureOptions(options);
    heightMapDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

    heightMapTexture.propertyDescriptor(heightMapDescriptor);
    terrainMaterial->properties().isStatic(true);
    terrainMaterial->properties().isInstanced(true);
    terrainMaterial->properties().texturesInFragmentStageOnly(false);
    terrainMaterial->setTexture(TextureSlot::UNIT0, CreateResource<Texture>(terrain->parentResourceCache(), textureAlbedoMaps), albedoSampler, TextureOperation::NONE, true);
    terrainMaterial->setTexture(TextureSlot::UNIT1, CreateResource<Texture>(terrain->parentResourceCache(), textureNoiseMedium), noiseMediumSampler, TextureOperation::NONE, true);
    terrainMaterial->setTexture(TextureSlot::OPACITY, CreateResource<Texture>(terrain->parentResourceCache(), textureBlendMap), blendMapSampler, TextureOperation::NONE, true);
    terrainMaterial->setTexture(TextureSlot::NORMALMAP, CreateResource<Texture>(terrain->parentResourceCache(), textureNormalMaps), albedoSampler, TextureOperation::NONE);
    terrainMaterial->setTexture(TextureSlot::HEIGHTMAP, CreateResource<Texture>(terrain->parentResourceCache(), heightMapTexture), heightMapSampler, TextureOperation::NONE, true);
    terrainMaterial->setTexture(TextureSlot::SPECULAR, CreateResource<Texture>(terrain->parentResourceCache(), textureWaterCaustics), albedoSampler, TextureOperation::NONE, true);
    terrainMaterial->setTexture(TextureSlot::METALNESS, CreateResource<Texture>(terrain->parentResourceCache(), textureExtraMaps), albedoSampler, TextureOperation::NONE);
    terrainMaterial->setTexture(TextureSlot::EMISSIVE, Texture::DefaultTexture2D(), albedoSampler, TextureOperation::NONE);

    const Configuration::Terrain terrainConfig = context.config().terrain;
    const vec2<F32> WorldScale = terrain->tessParams().WorldScale();
    Texture_ptr albedoTile = terrainMaterial->getTexture(TextureSlot::UNIT0).lock();
    WAIT_FOR_CONDITION(albedoTile->getState() == ResourceState::RES_LOADED);
    const U16 tileMapSize = albedoTile->width();

    terrainMaterial->addShaderDefine(ShaderType::COUNT, "ENABLE_TBN");
    terrainMaterial->addShaderDefine(ShaderType::COUNT, "TEXTURE_TILE_SIZE " + Util::to_string(tileMapSize));
    terrainMaterial->addShaderDefine(ShaderType::COUNT, "TERRAIN_HEIGHT_OFFSET " + Util::to_string(altitudeRange.x));
    terrainMaterial->addShaderDefine(ShaderType::COUNT, "WORLD_SCALE_X " + Util::to_string(WorldScale.width));
    terrainMaterial->addShaderDefine(ShaderType::COUNT, "WORLD_SCALE_Y " + Util::to_string(altitudeRange.y - altitudeRange.x));
    terrainMaterial->addShaderDefine(ShaderType::COUNT, "WORLD_SCALE_Z " + Util::to_string(WorldScale.height));
    terrainMaterial->addShaderDefine(ShaderType::COUNT, "INV_CONTROL_VTX_PER_TILE_EDGE " + Util::to_string(1.f / TessellationParams::VTX_PER_TILE_EDGE));
    terrainMaterial->addShaderDefine(ShaderType::COUNT, Util::StringFormat("CONTROL_VTX_PER_TILE_EDGE %d", TessellationParams::VTX_PER_TILE_EDGE));
    terrainMaterial->addShaderDefine(ShaderType::COUNT, Util::StringFormat("PATCHES_PER_TILE_EDGE %d", TessellationParams::PATCHES_PER_TILE_EDGE));
    terrainMaterial->addShaderDefine(ShaderType::COUNT, Util::StringFormat("MAX_TEXTURE_LAYERS %d", layerCount));
    if (terrainConfig.detailLevel > 1) {
        terrainMaterial->addShaderDefine(ShaderType::COUNT, "REDUCE_TEXTURE_TILE_ARTIFACT");
        if (terrainConfig.detailLevel > 2) {
            terrainMaterial->addShaderDefine(ShaderType::COUNT, "REDUCE_TEXTURE_TILE_ARTIFACT_ALL_LODS");
        }
    }
    terrainMaterial->addShaderDefine(ShaderType::FRAGMENT, Util::StringFormat("UNDERWATER_TILE_SCALE %d", to_I32(underwaterTileScale)));
    terrainMaterial->addShaderDefine(ShaderType::FRAGMENT, tileFactorStr, false);

    if (!terrainMaterial->properties().receivesShadows()) {
        terrainMaterial->addShaderDefine(ShaderType::FRAGMENT, "DISABLE_SHADOW_MAPPING");
    }

    terrainMaterial->computeShaderCBK([name, terrainConfig]([[maybe_unused]] Material* material, const RenderStagePass stagePass) {
        
        const Terrain::WireframeMode wMode = terrainConfig.wireframe 
                                                    ? Terrain::WireframeMode::EDGES 
                                                    : terrainConfig.showNormals
                                                            ? Terrain::WireframeMode::NORMALS
                                                            : terrainConfig.showLoDs 
                                                                ? Terrain::WireframeMode::LODS
                                                                : terrainConfig.showTessLevels 
                                                                    ? Terrain::WireframeMode::TESS_LEVELS
                                                                    : terrainConfig.showBlendMap 
                                                                        ? Terrain::WireframeMode::BLEND_MAP
                                                                        : Terrain::WireframeMode::NONE;

        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "terrainTess.glsl";

        ShaderModuleDescriptor tescModule = {};
        tescModule._moduleType = ShaderType::TESSELLATION_CTRL;
        tescModule._sourceFile = "terrainTess.glsl";

        ShaderModuleDescriptor teseModule = {};
        teseModule._moduleType = ShaderType::TESSELLATION_EVAL;
        teseModule._sourceFile = "terrainTess.glsl";

        ShaderModuleDescriptor geomModule = {};
        geomModule._moduleType = ShaderType::GEOMETRY;
        geomModule._sourceFile = "terrainTess.glsl";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "terrainTess.glsl";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(tescModule);
        shaderDescriptor._modules.push_back(teseModule);
        shaderDescriptor._modules.push_back(fragModule);

        const bool hasGeometryPass = wMode == Terrain::WireframeMode::EDGES || wMode == Terrain::WireframeMode::NORMALS;
        if (hasGeometryPass) {
            shaderDescriptor._modules.push_back(geomModule);
        }

        string propName;
        for (ShaderModuleDescriptor& shaderModule : shaderDescriptor._modules) {
            string shaderPropName;

            if (wMode != Terrain::WireframeMode::NONE) {
                if (hasGeometryPass) {
                    shaderPropName += ".DebugView";
                    shaderModule._defines.emplace_back("TOGGLE_DEBUG");
                }

                if (wMode == Terrain::WireframeMode::EDGES) {
                    shaderPropName += ".WireframeView";
                    shaderModule._defines.emplace_back("TOGGLE_WIREFRAME");
                } else if (wMode == Terrain::WireframeMode::NORMALS) {
                    shaderPropName += ".PreviewNormals";
                    shaderModule._defines.emplace_back("TOGGLE_NORMALS");
                } else if (wMode == Terrain::WireframeMode::LODS) {
                    shaderPropName += ".PreviewLoDs";
                    shaderModule._defines.emplace_back("TOGGLE_LODS");
                } else if (wMode == Terrain::WireframeMode::TESS_LEVELS) {
                    shaderPropName += ".PreviewTessLevels";
                    shaderModule._defines.emplace_back("TOGGLE_TESS_LEVEL");
                } else if (wMode == Terrain::WireframeMode::BLEND_MAP) {
                    shaderPropName += ".PreviewBlendMap";
                    shaderModule._defines.emplace_back("TOGGLE_BLEND_MAP");
                }
            }

            if (shaderPropName.length() > propName.length()) {
                propName = shaderPropName;
            }
        }

        if (stagePass._stage == RenderStage::SHADOW) {
            for (ShaderModuleDescriptor& shaderModule : shaderDescriptor._modules) {
                if (shaderModule._moduleType == ShaderType::FRAGMENT) {
                    shaderModule._variant = "Shadow.VSM";
                }
                shaderModule._defines.emplace_back("MAX_TESS_LEVEL 32");
            }
            shaderDescriptor._name = ("Terrain_ShadowVSM-" + name + propName).c_str();
        } else if (stagePass._stage == RenderStage::DISPLAY) {
            //ToDo: Implement this! -Ionut
            constexpr bool hasParallax = false;
            if (hasParallax) {
                for (ShaderModuleDescriptor& shaderModule : shaderDescriptor._modules) {
                    if (shaderModule._moduleType == ShaderType::FRAGMENT) {
                        shaderModule._defines.emplace_back("HAS_PARALLAX");
                        break;
                    }
                }
            }
            if (stagePass._passType == RenderPassType::PRE_PASS) {
                for (ShaderModuleDescriptor& shaderModule : shaderDescriptor._modules) {
                    if (shaderModule._moduleType == ShaderType::FRAGMENT) {
                        shaderModule._variant = "PrePass";
                    }
                    shaderModule._defines.emplace_back("PRE_PASS");
                }
                shaderDescriptor._name = ("Terrain_PrePass-" + name + propName + (hasParallax ? ".Parallax" : "")).c_str();
            } else {
                shaderDescriptor._name = ("Terrain_Colour-" + name + propName + (hasParallax ? ".Parallax" : "")).c_str();
            }
        } else { // Not RenderStage::DISPLAY
            if (IsDepthPass(stagePass)) {
                shaderDescriptor._modules.pop_back(); //No frag shader

                for (ShaderModuleDescriptor& shaderModule : shaderDescriptor._modules) {
                    shaderModule._defines.emplace_back("LOW_QUALITY");
                    shaderModule._defines.emplace_back("MAX_TESS_LEVEL 16");
                }

                shaderDescriptor._name = ("Terrain_PrePass_LowQuality-" + name + propName).c_str();
            } else {
                for (ShaderModuleDescriptor& shaderModule : shaderDescriptor._modules) {
                    if (shaderModule._moduleType == ShaderType::FRAGMENT) {
                        shaderModule._variant = "LQPass";
                    }

                    shaderModule._defines.emplace_back("LOW_QUALITY");
                    shaderModule._defines.emplace_back("MAX_TESS_LEVEL 16");
                }
                if (stagePass._stage == RenderStage::REFLECTION) {
                    shaderDescriptor._name = ("Terrain_Colour_LowQuality_Reflect-" + name + propName).c_str();
                } else {
                    shaderDescriptor._name = ("Terrain_Colour_LowQuality-" + name + propName).c_str();
                }
            }
        }

        return shaderDescriptor;
    });
    
    terrainMaterial->computeRenderStateCBK([]([[maybe_unused]] Material* material, const RenderStagePass stagePass, RenderStateBlock& blockInOut ) {
        const bool isReflectionPass = stagePass._stage == RenderStage::REFLECTION;

        blockInOut._tessControlPoints = 4;
        blockInOut._cullMode = isReflectionPass ? CullMode::FRONT : CullMode::BACK;
        blockInOut._zFunc = IsDepthPass(stagePass) ? ComparisonFunction::LEQUAL : ComparisonFunction::EQUAL;
    });

    AttributeMap vertexFormat{};
    auto& vertBindings = vertexFormat._vertexBindings.emplace_back();
    vertBindings._bufferBindIndex = 0u;
    vertBindings._perVertexInputRate = false;
    vertBindings._strideInBytes = 2 * (4u * sizeof(F32));

    {
        AttributeDescriptor& desc = vertexFormat._attributes[to_base(AttribLocation::POSITION)];
        desc._vertexBindingIndex = vertBindings._bufferBindIndex;
        desc._componentsPerElement = 4u;
        desc._dataType = GFXDataFormat::FLOAT_32;
        desc._normalized = false;
        desc._strideInBytes = 0u * sizeof(F32);
    }
    {
        AttributeDescriptor& desc = vertexFormat._attributes[to_base(AttribLocation::COLOR)];
        desc._vertexBindingIndex = vertBindings._bufferBindIndex;
        desc._componentsPerElement = 4u;
        desc._dataType = GFXDataFormat::FLOAT_32;
        desc._normalized = false;
        desc._strideInBytes = 4u * sizeof(F32);
    }
    terrainMaterial->setPipelineLayout(PrimitiveTopology::PATCH, vertexFormat);

    terrain->setMaterialTpl(terrainMaterial);

    Start(*CreateTask([terrain, terrainDescriptor, &context](const Task&) {
        if (!loadThreadedResources(terrain, context, terrainDescriptor)) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }), 
    context.taskPool(TaskPoolType::HIGH_PRIORITY),
    threadedLoading ? TaskPriority::DONT_CARE : TaskPriority::REALTIME);

    return true;
}

bool TerrainLoader::loadThreadedResources(const Terrain_ptr& terrain,
                                          PlatformContext& context,
                                          const std::shared_ptr<TerrainDescriptor>& terrainDescriptor) {

    ResourcePath terrainMapLocation{ Paths::g_assetsLocation + Paths::g_heightmapLocation + terrainDescriptor->getVariable("descriptor") };
    ResourcePath terrainRawFile{ terrainDescriptor->getVariable("heightfield") };

    const vec2<U16> terrainDimensions = terrainDescriptor->dimensions();
    
    const F32 minAltitude = terrainDescriptor->altitudeRange().x;
    const F32 maxAltitude = terrainDescriptor->altitudeRange().y;
    BoundingBox& terrainBB = Attorney::TerrainLoader::boundingBox(*terrain);
    terrainBB.set(vec3<F32>(-terrainDimensions.x * 0.5f, minAltitude, -terrainDimensions.y * 0.5f),
                  vec3<F32>( terrainDimensions.x * 0.5f, maxAltitude,  terrainDimensions.y * 0.5f));

    const vec3<F32>& bMin = terrainBB.getMin();
    const vec3<F32>& bMax = terrainBB.getMax();

    ByteBuffer terrainCache;
    if (terrainCache.loadFromFile((Paths::g_cacheLocation + Paths::g_terrainCacheLocation).c_str(), (terrainRawFile + ".cache").c_str())) {
        auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
        terrainCache >> tempVer;
        if (tempVer == BYTE_BUFFER_VERSION) {
            terrainCache >> terrain->_physicsVerts;
        } else {
            terrainCache.clear();
        }
    }

    if (terrain->_physicsVerts.empty()) {

        vector<Byte> data(to_size(terrainDimensions.width) * terrainDimensions.height * (sizeof(U16) / sizeof(char)), Byte{0});
        if (readFile((terrainMapLocation + "/").c_str(), terrainRawFile.c_str(), data, FileType::BINARY) != FileError::NONE) {
            NOP();
        }

        constexpr F32 ushortMax = 1.f + U16_MAX;

        const I32 terrainWidth = to_I32(terrainDimensions.x);
        const I32 terrainHeight = to_I32(terrainDimensions.y);

        terrain->_physicsVerts.resize(to_size(terrainWidth) * terrainHeight);

        // scale and translate all heights by half to convert from 0-255 (0-65335) to -127 - 128 (-32767 - 32768)
        const F32 altitudeRange = maxAltitude - minAltitude;
        
        const F32 bXRange = bMax.x - bMin.x;
        const F32 bZRange = bMax.z - bMin.z;

        const bool flipHeight = !ImageTools::UseUpperLeftOrigin();

        #pragma omp parallel for
        for (I32 height = 0; height < terrainHeight; ++height) {
            for (I32 width = 0; width < terrainWidth; ++width) {
                const I32 idxTER = TER_COORD(width, height, terrainWidth);
                vec3<F32>& vertexData = terrain->_physicsVerts[idxTER]._position;


                F32 yOffset = 0.0f;
                const U16* heightData = reinterpret_cast<U16*>(data.data());

                const I32 coordX = width < terrainWidth - 1 ? width : width - 1;
                I32 coordY = (height < terrainHeight - 1 ? height : height - 1);
                if (flipHeight) {
                    coordY = terrainHeight - 1 - coordY;
                }
                const I32 idxIMG = TER_COORD(coordX, coordY, terrainWidth);
                yOffset = altitudeRange * (heightData[idxIMG] / ushortMax) + minAltitude;


                //#pragma omp critical
                //Surely the id is unique and memory has also been allocated beforehand
                vertexData.set(bMin.x + to_F32(width) * bXRange / (terrainWidth - 1),       //X
                               yOffset,                                                     //Y
                               bMin.z + to_F32(height) * bZRange / (terrainHeight - 1));    //Z
            }
        }

        constexpr I32 offset = 2;
        #pragma omp parallel for
        for (I32 j = offset; j < terrainHeight - offset; ++j) {
            for (I32 i = offset; i < terrainWidth - offset; ++i) {
                vec3<F32> vU, vV, vUV;

                vU.set(terrain->_physicsVerts[TER_COORD(i + offset, j + 0, terrainWidth)]._position -
                       terrain->_physicsVerts[TER_COORD(i - offset, j + 0, terrainWidth)]._position);
                vV.set(terrain->_physicsVerts[TER_COORD(i + 0, j + offset, terrainWidth)]._position -
                       terrain->_physicsVerts[TER_COORD(i + 0, j - offset, terrainWidth)]._position);

                vUV.cross(vV, vU);
                vUV.normalize();
                vU = -vU;
                vU.normalize();

                //Again, not needed, I think
                //#pragma omp critical
                {
                    const I32 idx = TER_COORD(i, j, terrainWidth);
                    VertexBuffer::Vertex& vert = terrain->_physicsVerts[idx];
                    vert._normal = Util::PACK_VEC3(vUV);
                    vert._tangent = Util::PACK_VEC3(vU);
                }
            }
        }
        
        for (I32 j = 0; j < offset; ++j) {
            for (I32 i = 0; i < terrainWidth; ++i) {
                I32 idx0 = TER_COORD(i, j, terrainWidth);
                I32 idx1 = TER_COORD(i, offset, terrainWidth);

                terrain->_physicsVerts[idx0]._normal = terrain->_physicsVerts[idx1]._normal;
                terrain->_physicsVerts[idx0]._tangent = terrain->_physicsVerts[idx1]._tangent;

                idx0 = TER_COORD(i, terrainHeight - 1 - j, terrainWidth);
                idx1 = TER_COORD(i, terrainHeight - 1 - offset, terrainWidth);

                terrain->_physicsVerts[idx0]._normal = terrain->_physicsVerts[idx1]._normal;
                terrain->_physicsVerts[idx0]._tangent = terrain->_physicsVerts[idx1]._tangent;
            }
        }

        for (I32 i = 0; i < offset; ++i) {
            for (I32 j = 0; j < terrainHeight; ++j) {
                I32 idx0 = TER_COORD(i, j, terrainWidth);
                I32 idx1 = TER_COORD(offset, j, terrainWidth);

                terrain->_physicsVerts[idx0]._normal = terrain->_physicsVerts[idx1]._normal;
                terrain->_physicsVerts[idx0]._tangent = terrain->_physicsVerts[idx1]._tangent;

                idx0 = TER_COORD(terrainWidth - 1 - i, j, terrainWidth);
                idx1 = TER_COORD(terrainWidth - 1 - offset, j, terrainWidth);

                terrain->_physicsVerts[idx0]._normal = terrain->_physicsVerts[idx1]._normal;
                terrain->_physicsVerts[idx0]._tangent = terrain->_physicsVerts[idx1]._tangent;
            }
        }

        terrainCache << BYTE_BUFFER_VERSION;
        terrainCache << terrain->_physicsVerts;
        if (!terrainCache.dumpToFile((Paths::g_cacheLocation + Paths::g_terrainCacheLocation).c_str(), (terrainRawFile + ".cache").c_str())) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }

    // Then compute quadtree and all additional terrain-related structures
    Attorney::TerrainLoader::postBuild(*terrain);

    // Do this first in case we have any threaded loads
    const VegetationDetails& vegDetails = initializeVegetationDetails(terrain, context, terrainDescriptor);
    Vegetation::createAndUploadGPUData(context.gfx(), terrain, vegDetails);

    Console::printfn(LOCALE_STR("TERRAIN_LOAD_END"), terrain->resourceName().c_str());
    return terrain->load();
}

VegetationDetails& TerrainLoader::initializeVegetationDetails(const Terrain_ptr& terrain,
                                                              PlatformContext& context,
                                                              const std::shared_ptr<TerrainDescriptor>& terrainDescriptor) {
    VegetationDetails& vegDetails = Attorney::TerrainLoader::vegetationDetails(*terrain);

    const U32 chunkSize = terrain->getQuadtree().targetChunkDimension();
    assert(chunkSize > 0u);

    const U32 terrainWidth = terrainDescriptor->dimensions().width;
    const U32 terrainHeight = terrainDescriptor->dimensions().height;
    const U32 maxChunkCount = to_U32(std::ceil(terrainWidth * terrainHeight / (chunkSize * chunkSize * 1.0f)));

    Vegetation::precomputeStaticData(context.gfx(), chunkSize, maxChunkCount);

    for (I32 i = 1; i < 5; ++i) {
        string currentMesh = terrainDescriptor->getVariable(Util::StringFormat("treeMesh%d", i));
        if (!currentMesh.empty()) {
            vegDetails.treeMeshes.push_back(ResourcePath{ currentMesh });
        }

        vegDetails.treeRotations[i - 1].set(
            terrainDescriptor->getVariablef(Util::StringFormat("treeRotationX%d", i)),
            terrainDescriptor->getVariablef(Util::StringFormat("treeRotationY%d", i)),
            terrainDescriptor->getVariablef(Util::StringFormat("treeRotationZ%d", i))
        );
    }

    vegDetails.grassScales.set(
        terrainDescriptor->getVariablef("grassScale1"),
        terrainDescriptor->getVariablef("grassScale2"),
        terrainDescriptor->getVariablef("grassScale3"),
        terrainDescriptor->getVariablef("grassScale4"));

    vegDetails.treeScales.set(
        terrainDescriptor->getVariablef("treeScale1"),
        terrainDescriptor->getVariablef("treeScale2"),
        terrainDescriptor->getVariablef("treeScale3"),
        terrainDescriptor->getVariablef("treeScale4"));
 
    string currentImage = terrainDescriptor->getVariable("grassBillboard1");
    if (!currentImage.empty()) {
        vegDetails.billboardTextureArray += currentImage;
        vegDetails.billboardCount++;
    }

    currentImage = terrainDescriptor->getVariable("grassBillboard2");
    if (!currentImage.empty()) {
        vegDetails.billboardTextureArray += "," + currentImage;
        vegDetails.billboardCount++;
    }

    currentImage = terrainDescriptor->getVariable("grassBillboard3");
    if (!currentImage.empty()) {
        vegDetails.billboardTextureArray += "," + currentImage;
        vegDetails.billboardCount++;
    }

    currentImage = terrainDescriptor->getVariable("grassBillboard4");
    if (!currentImage.empty()) {
        vegDetails.billboardTextureArray += "," + currentImage;
        vegDetails.billboardCount++;
    }

    vegDetails.name = terrain->resourceName() + "_vegetation";
    vegDetails.parentTerrain = terrain;

    const ResourcePath terrainLocation{ Paths::g_assetsLocation + Paths::g_heightmapLocation + terrainDescriptor->getVariable("descriptor") };

    vegDetails.grassMap.reset(new ImageTools::ImageData);
    vegDetails.treeMap.reset(new ImageTools::ImageData);

    const ResourcePath grassMap{ terrainDescriptor->getVariable("grassMap")};
    const ResourcePath treeMap{ terrainDescriptor->getVariable("treeMap") };
    ImageTools::ImportOptions options{};
    options._alphaChannelTransparency = false;
    options._isNormalMap = false;
    if (!vegDetails.grassMap->loadFromFile(context, false, 0, 0, terrainLocation, grassMap, options))
    {
        DIVIDE_UNEXPECTED_CALL();
    }
    if (!vegDetails.treeMap->loadFromFile( context, false, 0, 0, terrainLocation, treeMap, options))
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    return vegDetails;
}

bool TerrainLoader::Save([[maybe_unused]] const char* fileName) {
    return true;
}

bool TerrainLoader::Load([[maybe_unused]] const char* fileName) {
    return true;
}

}
