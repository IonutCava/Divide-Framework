
#include "Headers/Terrain.h"

#include "Headers/TerrainChunk.h"
#include "Headers/TerrainDescriptor.h"
#include "Headers/TileRing.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/File/Headers/FileManagement.h"

#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Managers/Headers/ProjectManager.h"

#include "Environment/Vegetation/Headers/Vegetation.h"

#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"

#include "Geometry/Material/Headers/Material.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"

namespace Divide {

namespace
{
    constexpr U16 BYTE_BUFFER_VERSION = 1u;

    ResourcePath ClimatesLocation( U8 textureQuality )
    {
        CLAMP<U8>( textureQuality, 0u, 3u );

        return (textureQuality == 3u ? Paths::g_climatesHighResLocation
                 : textureQuality == 2u ? Paths::g_climatesMedResLocation
                 : Paths::g_climatesLowResLocation);
    }

    std::pair<U8, bool> FindOrInsert( const U8 textureQuality, vector<ResourcePath>& container, const ResourcePath& texture, string materialName )
    {

        if ( !fileExists( (ClimatesLocation( textureQuality ) / materialName / texture) ) )
        {
            materialName = "std_default";
        }

        const ResourcePath item = ResourcePath{ materialName } / texture;

        const auto* const it = eastl::find( eastl::cbegin( container ),
                                            eastl::cend( container ),
                                            item );

        if ( it != eastl::cend( container ) )
        {
            return std::make_pair( to_U8( eastl::distance( eastl::cbegin( container ), it ) ), false );
        }

        container.push_back( item );
        return std::make_pair( to_U8( container.size() - 1 ), true );
    }

    vector<U16> CreateTileQuadListIB()
    {
        vector<U16> indices(TessellationParams::QUAD_LIST_INDEX_COUNT, 0u);

        U16 index = 0u;

        // The IB describes one tile of NxN patches.
        // Four vertices per quad, with VTX_PER_TILE_EDGE-1 quads per tile edge.
        for (U8 y = 0u; y < TessellationParams::VTX_PER_TILE_EDGE - 1; ++y)
        {
            const U16 rowStart = y * TessellationParams::VTX_PER_TILE_EDGE;

            for (U8 x = 0u; x < TessellationParams::VTX_PER_TILE_EDGE - 1; ++x) {
                indices[index++] = rowStart + x;
                indices[index++] = rowStart + x + TessellationParams::VTX_PER_TILE_EDGE;
                indices[index++] = rowStart + x + TessellationParams::VTX_PER_TILE_EDGE + 1;
                indices[index++] = rowStart + x + 1;
            }
        }
        assert(index == TessellationParams::QUAD_LIST_INDEX_COUNT);

        return indices;
    }
}

void TessellationParams::fromDescriptor(const TerrainDescriptor& descriptor) noexcept
{
    WorldScale(descriptor._dimensions * 0.5f / to_F32(PATCHES_PER_TILE_EDGE));
}

Terrain::Terrain( const ResourceDescriptor<Terrain>& descriptor )
    : Object3D(descriptor, GetSceneNodeType<Terrain>() )
    , _terrainQuadtree()
    , _descriptor( descriptor._propertyDescriptor )
{
    _tessParams.fromDescriptor( _descriptor );
    _renderState.addToDrawExclusionMask(RenderStage::SHADOW, RenderPassType::COUNT, static_cast<RenderStagePass::VariantType>(ShadowType::CUBEMAP));
    _renderState.addToDrawExclusionMask(RenderStage::SHADOW, RenderPassType::COUNT, static_cast<RenderStagePass::VariantType>(ShadowType::SINGLE));
}

bool Terrain::load( PlatformContext& context )
{
     const string& name = GetVariable( _descriptor, "terrainName");

    Console::printfn( LOCALE_STR( "TERRAIN_LOAD_START" ), name );

    ResourcePath terrainLocation = Paths::g_heightmapLocation;
    terrainLocation /= ResourcePath {GetVariable( _descriptor, "descriptor") };

    const U8 textureQuality = context.config().terrain.textureQuality;

    // Noise texture
    SamplerDescriptor noiseMediumSampler = {};
    noiseMediumSampler._wrapU = TextureWrap::REPEAT;
    noiseMediumSampler._wrapV = TextureWrap::REPEAT;
    noiseMediumSampler._wrapW = TextureWrap::REPEAT;
    noiseMediumSampler._anisotropyLevel = 16u;

    ResourceDescriptor<Texture> textureNoiseMedium("Terrain Noise Map_" + name);
    textureNoiseMedium.assetLocation(Paths::g_imagesLocation);
    textureNoiseMedium.assetName("medium_noise.png");

    TextureDescriptor& noiseMediumDescriptor = textureNoiseMedium._propertyDescriptor;
    noiseMediumDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
    noiseMediumDescriptor._textureOptions._useDDSCache = true;
    noiseMediumDescriptor._textureOptions._alphaChannelTransparency = false;
    noiseMediumDescriptor._textureOptions._isNormalMap = false;

    // Blend maps
    ResourceDescriptor<Texture> textureBlendMap("Terrain Blend Map_" + name);
    textureBlendMap.assetLocation(terrainLocation);

    // Albedo maps and roughness
    ResourceDescriptor<Texture> textureAlbedoMaps("Terrain Albedo Maps_" + name);
    textureAlbedoMaps.assetLocation(ClimatesLocation(textureQuality));

    // Normals
    ResourceDescriptor<Texture> textureNormalMaps("Terrain Normal Maps_" + name);
    textureNormalMaps.assetLocation(ClimatesLocation(textureQuality));

    // AO and displacement
    ResourceDescriptor<Texture> textureExtraMaps("Terrain Extra Maps_" + name);
    textureExtraMaps.assetLocation(ClimatesLocation(textureQuality));

    //temp data
    string layerOffsetStr;
    string currentMaterial;

    U8 layerCount = _descriptor._textureLayers;

    const vector<std::pair<string, TerrainTextureChannel>> channels = {
        {"red", TerrainTextureChannel::TEXTURE_RED_CHANNEL},
        {"green", TerrainTextureChannel::TEXTURE_GREEN_CHANNEL},
        {"blue", TerrainTextureChannel::TEXTURE_BLUE_CHANNEL},
        {"alpha", TerrainTextureChannel::TEXTURE_ALPHA_CHANNEL}
    };

    vector<ResourcePath> textures[to_base( TextureUsageType::COUNT)] = {};
    vector<string> splatTextures = {};

    const char* textureNames[to_base( TextureUsageType::COUNT)] =
    {
        "Albedo_roughness", "Normal", "Displacement"
    };

    for (U8 i = 0u; i < layerCount; ++i) {
        layerOffsetStr = Util::to_string(i);
        splatTextures.push_back(GetVariable( _descriptor, "blendMap" + layerOffsetStr));
    }

    for (U8 i = 0u; i < layerCount; ++i) {
        layerOffsetStr = Util::to_string(i);
        for (const auto& [channelName, channel] : channels) {
            currentMaterial = GetVariable( _descriptor, channelName + layerOffsetStr + "_mat");
            if (currentMaterial.empty()) {
                continue;
            }

            for (U8 k = 0u; k < to_base( TextureUsageType::COUNT); ++k)
            {
                const string textureName = Util::StringFormat( "{}.{}", textureNames[k], k == to_base( TextureUsageType::ALBEDO_ROUGHNESS ) ? "png" : "jpg" );
                FindOrInsert(textureQuality, textures[k], ResourcePath{ textureName }, currentMaterial);
            }
        }
    }

    string blendMapArray  = "";
    string albedoMapArray = "";
    string normalMapArray = "";
    string extraMapArray  = "";

    U16 extraMapCount = 0;
    for (const auto& tex : textures[to_base( TextureUsageType::ALBEDO_ROUGHNESS)])
    {
        if ( !albedoMapArray.empty() )
        {
            albedoMapArray.append( "," );
        }

        albedoMapArray.append(tex.string());
    }

    for (const auto& tex : textures[to_base( TextureUsageType::NORMAL)])
    {
        if ( !normalMapArray.empty() )
        {
            normalMapArray.append( "," );
        }

        normalMapArray.append(tex.string());
    }

    for (U8 i = to_U8( TextureUsageType::DISPLACEMENT_AO); i < to_U8( TextureUsageType::COUNT); ++i)
    {
        for (const auto& tex : textures[i])
        {
            if ( !extraMapArray.empty() )
            {
                extraMapArray.append( "," );
            }

            extraMapArray.append(tex.string());
            ++extraMapCount;
        }
    }

    for (const string& tex : splatTextures)
    {
        if (!blendMapArray.empty())
        {
            blendMapArray.append( "," );
        }
        blendMapArray.append(tex);
    }

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

    textureAlbedoMaps.assetName( albedoMapArray );
    textureBlendMap.assetName( blendMapArray );
    textureNormalMaps.assetName( normalMapArray );
    textureExtraMaps.assetName( extraMapArray );

    TextureDescriptor& albedoDescriptor = textureAlbedoMaps._propertyDescriptor;
    albedoDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
    albedoDescriptor._layerCount = to_U16(textures[to_base( TextureUsageType::ALBEDO_ROUGHNESS)].size());
    albedoDescriptor._textureOptions._alphaChannelTransparency = false; //roughness
    albedoDescriptor._textureOptions._isNormalMap = false;
    albedoDescriptor._textureOptions._useDDSCache = true;

    TextureDescriptor& blendMapDescriptor = textureBlendMap._propertyDescriptor;
    blendMapDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
    blendMapDescriptor._layerCount = to_U16(splatTextures.size());
    blendMapDescriptor._textureOptions._alphaChannelTransparency = false; //splat lookup
    blendMapDescriptor._textureOptions._isNormalMap = false;
    blendMapDescriptor._textureOptions._useDDSCache = true;

    TextureDescriptor& normalDescriptor = textureNormalMaps._propertyDescriptor;
    normalDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
    normalDescriptor._layerCount = to_U16(textures[to_base( TextureUsageType::NORMAL)].size());
    normalDescriptor._textureOptions._alphaChannelTransparency = false; //not really needed
    normalDescriptor._textureOptions._isNormalMap = true;
    normalDescriptor._textureOptions._useDDSCache = false;

    TextureDescriptor& extraDescriptor = textureExtraMaps._propertyDescriptor;
    extraDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
    extraDescriptor._layerCount = extraMapCount;
    extraDescriptor._textureOptions._alphaChannelTransparency = false; //who knows what we pack here?
    extraDescriptor._textureOptions._isNormalMap = false;
    extraDescriptor._textureOptions._useDDSCache = true;

    ResourceDescriptor<Material> terrainMaterialDescriptor("terrainMaterial_" + name);
    Handle<Material> terrainMaterialHandle = CreateResource(terrainMaterialDescriptor);
    Material* terrainMaterial = Get(terrainMaterialHandle);

    terrainMaterial->ignoreXMLData(true);

    terrainMaterial->updatePriorirty(Material::UpdatePriority::Medium);

    const vec2<U16> terrainDimensions = _descriptor._dimensions;
    const float2 altitudeRange = _descriptor._altitudeRange;

    Console::d_printfn(LOCALE_STR("TERRAIN_INFO"), terrainDimensions.width, terrainDimensions.height);

    const F32 underwaterTileScale = GetVariableF( _descriptor, "underwaterTileScale");
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

    const TerrainDescriptor::LayerData& layerTileData = _descriptor._layerDataEntries;
    string tileFactorStr = Util::StringFormat("const vec2 CURRENT_TILE_FACTORS[{}] = {\n", layerCount * 4);
    for (U8 i = 0u; i < layerCount; ++i) {
        const TerrainDescriptor::LayerDataEntry& entry = layerTileData[i];
        for (U8 j = 0u; j < 4u; ++j) {
            const float2 factors = entry[j];
            tileFactorStr.append("        ");
            tileFactorStr.append(Util::StringFormat("vec2({:3.2f}, {:3.2f}),\n", factors.s, factors.t));
        }
    }
    tileFactorStr.pop_back();
    tileFactorStr.pop_back();
    tileFactorStr.append("\n};");

    string helperTextures { GetVariable( _descriptor, "waterCaustics" ) + "," +
                            GetVariable( _descriptor, "underwaterAlbedoTexture" ) + "," +
                            GetVariable( _descriptor, "underwaterDetailTexture" ) + "," +
                            GetVariable( _descriptor, "tileNoiseTexture") };

    ResourceDescriptor<Texture> textureWaterCaustics("Terrain Helper Textures_" + name);
    textureWaterCaustics.assetLocation(Paths::g_imagesLocation);
    textureWaterCaustics.assetName(helperTextures);
    TextureDescriptor& helperTexDescriptor = textureWaterCaustics._propertyDescriptor;
    helperTexDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
    helperTexDescriptor._textureOptions._alphaChannelTransparency = false;

    ResourceDescriptor<Texture> heightMapTexture("Terrain Heightmap_" + name);
    heightMapTexture.assetLocation(terrainLocation);
    heightMapTexture.assetName(GetVariable(_descriptor, "heightfieldTex"));
    TextureDescriptor& heightMapDescriptor = heightMapTexture._propertyDescriptor;
    heightMapDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
    heightMapDescriptor._dataType = GFXDataFormat::FLOAT_16;
    heightMapDescriptor._baseFormat = GFXImageFormat::RED;
    heightMapDescriptor._packing = GFXImagePacking::UNNORMALIZED;
    heightMapDescriptor._mipMappingState = MipMappingState::OFF;
    heightMapDescriptor._textureOptions._alphaChannelTransparency = false;
    heightMapDescriptor._textureOptions._useDDSCache = false;

    terrainMaterial->properties().isStatic(true);
    terrainMaterial->properties().isInstanced(true);
    terrainMaterial->properties().texturesInFragmentStageOnly(false);
    terrainMaterial->setTexture(TextureSlot::UNIT0, textureAlbedoMaps, albedoSampler, TextureOperation::NONE, true);
    terrainMaterial->setTexture(TextureSlot::UNIT1, textureNoiseMedium, noiseMediumSampler, TextureOperation::NONE, true);
    terrainMaterial->setTexture(TextureSlot::OPACITY, textureBlendMap, blendMapSampler, TextureOperation::NONE, true);
    terrainMaterial->setTexture(TextureSlot::NORMALMAP, textureNormalMaps, albedoSampler, TextureOperation::NONE);
    terrainMaterial->setTexture(TextureSlot::HEIGHTMAP,heightMapTexture, heightMapSampler, TextureOperation::NONE, true);
    terrainMaterial->setTexture(TextureSlot::SPECULAR, textureWaterCaustics, albedoSampler, TextureOperation::NONE, true);
    terrainMaterial->setTexture(TextureSlot::METALNESS, textureExtraMaps, albedoSampler, TextureOperation::NONE);
    terrainMaterial->setTexture(TextureSlot::EMISSIVE, Texture::DefaultTexture2D(), albedoSampler, TextureOperation::NONE);

    const Configuration::Terrain terrainConfig = context.config().terrain;
    const float2 WorldScale = tessParams().WorldScale();
    Handle<Texture> albedoTileHandle = terrainMaterial->getTexture(TextureSlot::UNIT0);
    ResourcePtr<Texture> albedoTile = Get(albedoTileHandle);

    WAIT_FOR_CONDITION(albedoTile->getState() == ResourceState::RES_LOADED);
    const U16 tileMapSize = albedoTile->width();

    terrainMaterial->addShaderDefine("ENABLE_TBN");
    terrainMaterial->addShaderDefine("TEXTURE_TILE_SIZE " + Util::to_string(tileMapSize));
    terrainMaterial->addShaderDefine("TERRAIN_HEIGHT_OFFSET " + Util::to_string(altitudeRange.x));
    terrainMaterial->addShaderDefine("WORLD_SCALE_X " + Util::to_string(WorldScale.width));
    terrainMaterial->addShaderDefine("WORLD_SCALE_Y " + Util::to_string(altitudeRange.y - altitudeRange.x));
    terrainMaterial->addShaderDefine("WORLD_SCALE_Z " + Util::to_string(WorldScale.height));
    terrainMaterial->addShaderDefine("INV_CONTROL_VTX_PER_TILE_EDGE " + Util::to_string(1.f / TessellationParams::VTX_PER_TILE_EDGE));
    terrainMaterial->addShaderDefine(Util::StringFormat("CONTROL_VTX_PER_TILE_EDGE {}", TessellationParams::VTX_PER_TILE_EDGE));
    terrainMaterial->addShaderDefine(Util::StringFormat("PATCHES_PER_TILE_EDGE {}", TessellationParams::PATCHES_PER_TILE_EDGE));
    terrainMaterial->addShaderDefine(Util::StringFormat("MAX_TEXTURE_LAYERS {}", layerCount));
    if (terrainConfig.detailLevel > 1)
    {
        terrainMaterial->addShaderDefine("REDUCE_TEXTURE_TILE_ARTIFACT");
        if (terrainConfig.detailLevel > 2)
        {
            terrainMaterial->addShaderDefine("REDUCE_TEXTURE_TILE_ARTIFACT_ALL_LODS");
        }
    }
    terrainMaterial->addShaderDefine(ShaderType::FRAGMENT, Util::StringFormat("UNDERWATER_TILE_SCALE {}", to_I32(underwaterTileScale)));
    terrainMaterial->addShaderDefine(ShaderType::FRAGMENT, tileFactorStr, false);

    if (!terrainMaterial->properties().receivesShadows())
    {
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

    setMaterialTpl(terrainMaterialHandle);

    if (!loadResources(context))
    {
        Console::errorfn( LOCALE_STR( "ERROR_TERRAIN_LOAD" ), name );
        return false;
    }

    return Object3D::load( context );
}

bool Terrain::unload()
{
    DestroyResource(_vegetation);
    return Object3D::unload();
}

bool Terrain::loadResources( PlatformContext& context )
{
    ResourcePath terrainMapLocation{ Paths::g_heightmapLocation / GetVariable( _descriptor, "descriptor" ) };
    ResourcePath terrainRawFile{ GetVariable( _descriptor, "heightfield" ) };

    const vec2<U16> terrainDimensions = _descriptor._dimensions;

    const F32 minAltitude = _descriptor._altitudeRange.x;
    const F32 maxAltitude = _descriptor._altitudeRange.y;
    BoundingBox& terrainBB = _boundingBox;

    terrainBB.set( float3( -terrainDimensions.x * 0.5f, minAltitude, -terrainDimensions.y * 0.5f ),
                   float3( terrainDimensions.x * 0.5f, maxAltitude, terrainDimensions.y * 0.5f ) );

    const float3& bMin = terrainBB._min;
    const float3& bMax = terrainBB._max;

    ByteBuffer terrainCache;
    if ( terrainCache.loadFromFile( Paths::g_terrainCacheLocation, (terrainRawFile.string() + ".cache") ) )
    {
        auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
        terrainCache >> tempVer;
        if ( tempVer == BYTE_BUFFER_VERSION )
        {
            terrainCache >> _physicsVerts;
        }
        else
        {
            terrainCache.clear();
        }
    }

    if ( _physicsVerts.empty() )
    {
        size_t dataSize = to_size( terrainDimensions.width ) * terrainDimensions.height * (sizeof( U16 ) / sizeof( char ));
        vector<Byte> data( dataSize, Byte_ZERO );
        if ( readFile( terrainMapLocation, terrainRawFile.string(), FileType::BINARY, data.data(), dataSize ) != FileError::NONE )
        {
            NOP();
        }

        constexpr F32 ushortMax = 1.f + U16_MAX;

        const U32 terrainWidth  = terrainDimensions.x;
        const U32 terrainHeight = terrainDimensions.y;

        _physicsVerts.resize( to_size( terrainWidth ) * terrainHeight );

        // scale and translate all heights by half to convert from 0-255 (0-65335) to -127 - 128 (-32767 - 32768)
        const F32 altitudeRange = maxAltitude - minAltitude;

        const F32 bXRange = bMax.x - bMin.x;
        const F32 bZRange = bMax.z - bMin.z;

        const bool flipHeight = !ImageTools::UseUpperLeftOrigin();

        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = terrainHeight;
        descriptor._partitionSize = std::min(terrainHeight, 64u);
        Parallel_For(context.taskPool(TaskPoolType::HIGH_PRIORITY), descriptor, [&](const Task*, const U32 start, const U32 end)
        {
            for ( U32 height = start; height < end; ++height )
            {
                for ( U32 width = 0; width < terrainWidth; ++width )
                {
                    const U32 idxTER = TER_COORD( width, height, terrainWidth );
                    float3& vertexData = _physicsVerts[idxTER]._position;


                    F32 yOffset = 0.0f;
                    const U16* heightData = reinterpret_cast<U16*>(data.data());

                    const U32 coordX = width < terrainWidth - 1 ? width : width - 1;
                    U32 coordY = (height < terrainHeight - 1 ? height : height - 1);
                    if ( flipHeight )
                    {
                        coordY = terrainHeight - 1 - coordY;
                    }
                    const U32 idxIMG = TER_COORD( coordX, coordY, terrainWidth );
                    yOffset = altitudeRange * (heightData[idxIMG] / ushortMax) + minAltitude;


                    //Surely the id is unique and memory has also been allocated beforehand
                    vertexData.set( bMin.x + to_F32( width ) * bXRange / (terrainWidth - 1),       //X
                                    yOffset,                                                     //Y
                                    bMin.z + to_F32( height ) * bZRange / (terrainHeight - 1) );    //Z
                }
            }
        });

        constexpr U32 offset = 2;
        Parallel_For(context.taskPool(TaskPoolType::HIGH_PRIORITY), descriptor, [&](const Task*, const U32 start, const U32 end)
        {
            for ( U32 j = start; j > offset && j < end && j < terrainHeight - offset; ++j )
            {
                for ( U32 i = offset; i < terrainWidth - offset; ++i )
                {
                    float3 vU, vV, vUV;

                    vU.set( _physicsVerts[TER_COORD( i + offset, j + 0, terrainWidth )]._position -
                            _physicsVerts[TER_COORD( i - offset, j + 0, terrainWidth )]._position );
                    vV.set( _physicsVerts[TER_COORD( i + 0, j + offset, terrainWidth )]._position -
                            _physicsVerts[TER_COORD( i + 0, j - offset, terrainWidth )]._position );

                    vUV.cross( vV, vU );
                    vUV.normalize();
                    vU = -vU;
                    vU.normalize();

                    {
                        const I32 idx = TER_COORD( i, j, terrainWidth );
                        VertexBuffer::Vertex& vert = _physicsVerts[idx];
                        vert._normal = Util::PACK_VEC3( vUV );
                        vert._tangent = Util::PACK_VEC3( vU );
                    }
                }
            }
        });

        for ( U32 j = 0u; j < offset; ++j )
        {
            for ( U32 i = 0u; i < terrainWidth; ++i )
            {
                U32 idx0 = TER_COORD( i, j, terrainWidth );
                U32 idx1 = TER_COORD( i, offset, terrainWidth );

                _physicsVerts[idx0]._normal  = _physicsVerts[idx1]._normal;
                _physicsVerts[idx0]._tangent = _physicsVerts[idx1]._tangent;

                idx0 = TER_COORD( i, terrainHeight - 1 - j, terrainWidth );
                idx1 = TER_COORD( i, terrainHeight - 1 - offset, terrainWidth );

                _physicsVerts[idx0]._normal  = _physicsVerts[idx1]._normal;
                _physicsVerts[idx0]._tangent = _physicsVerts[idx1]._tangent;
            }
        }

        for ( U32 i = 0u; i < offset; ++i )
        {
            for ( U32 j = 0u; j < terrainHeight; ++j )
            {
                U32 idx0 = TER_COORD( i, j, terrainWidth );
                U32 idx1 = TER_COORD( offset, j, terrainWidth );

                _physicsVerts[idx0]._normal  = _physicsVerts[idx1]._normal;
                _physicsVerts[idx0]._tangent = _physicsVerts[idx1]._tangent;

                idx0 = TER_COORD( terrainWidth - 1 - i, j, terrainWidth );
                idx1 = TER_COORD( terrainWidth - 1 - offset, j, terrainWidth );

                _physicsVerts[idx0]._normal  = _physicsVerts[idx1]._normal;
                _physicsVerts[idx0]._tangent = _physicsVerts[idx1]._tangent;
            }
        }

        terrainCache << BYTE_BUFFER_VERSION;
        terrainCache << _physicsVerts;
        DIVIDE_EXPECTED_CALL( terrainCache.dumpToFile( Paths::g_terrainCacheLocation, terrainRawFile.string() + ".cache" ) );
    }

    // Then compute quadtree and all additional terrain-related structures
    postBuild(context);

    createVegetation(context );

    TaskPool& pool = context.taskPool( TaskPoolType::HIGH_PRIORITY );
    Task* buildTask = CreateTask(TASK_NOP);
    for ( TerrainChunk* chunk : terrainChunks() )
    {
        Start( *CreateTask( buildTask, [&](const Task&)
        {
            Attorney::TerrainChunkVegetation::initVegetation( *chunk, context, _vegetation );
        }), pool);
    }
    Start(*buildTask, pool);
    Wait( *buildTask, pool );

    Console::printfn( LOCALE_STR( "TERRAIN_LOAD_END" ), resourceName() );
    return load( context );
}

void Terrain::createVegetation( PlatformContext& context )
{
    VegetationDescriptor vegDetails = {};
    vegDetails.parentTerrain = this;
    vegDetails.chunkSize = getQuadtree().targetChunkDimension();
    assert( vegDetails.chunkSize > 0u );

    for ( I32 i = 1; i < 5; ++i )
    {
        string currentMesh = GetVariable( _descriptor, Util::StringFormat( "treeMesh{}", i ) );
        if ( !currentMesh.empty() )
        {
            vegDetails.treeMeshes.push_back( currentMesh );
        }

        vegDetails.treeRotations[i - 1].set(
            GetVariableF( _descriptor, Util::StringFormat( "treeRotationX{}", i ) ),
            GetVariableF( _descriptor, Util::StringFormat( "treeRotationY{}", i ) ),
            GetVariableF( _descriptor, Util::StringFormat( "treeRotationZ{}", i ) )
        );
    }

    vegDetails.grassScales.set(
        GetVariableF( _descriptor, "grassScale1" ),
        GetVariableF( _descriptor, "grassScale2" ),
        GetVariableF( _descriptor, "grassScale3" ),
        GetVariableF( _descriptor, "grassScale4" ) );

    vegDetails.treeScales.set(
        GetVariableF( _descriptor, "treeScale1" ),
        GetVariableF( _descriptor, "treeScale2" ),
        GetVariableF( _descriptor, "treeScale3" ),
        GetVariableF( _descriptor, "treeScale4" ) );

    string currentImage = GetVariable( _descriptor, "grassBillboard1" );
    if ( !currentImage.empty() )
    {
        vegDetails.billboardTextureArray += currentImage;
    }

    currentImage = GetVariable( _descriptor, "grassBillboard2" );
    if ( !currentImage.empty() )
    {
        vegDetails.billboardTextureArray += "," + currentImage;
    }

    currentImage = GetVariable( _descriptor, "grassBillboard3" );
    if ( !currentImage.empty() )
    {
        vegDetails.billboardTextureArray += "," + currentImage;
    }

    currentImage = GetVariable( _descriptor, "grassBillboard4" );
    if ( !currentImage.empty() )
    {
        vegDetails.billboardTextureArray += "," + currentImage;
    }

    vegDetails.name = resourceName() + "_vegetation";

    const ResourcePath terrainLocation{ Paths::g_heightmapLocation / GetVariable( _descriptor, "descriptor" ) };

    vegDetails.grassMap.reset( new ImageTools::ImageData );
    vegDetails.treeMap.reset( new ImageTools::ImageData );

    const auto grassMap = GetVariable( _descriptor, "grassMap" );
    const auto treeMap = GetVariable( _descriptor, "treeMap" );

    ImageTools::ImportOptions options{};
    options._alphaChannelTransparency = false;
    options._isNormalMap = false;
    DIVIDE_EXPECTED_CALL( vegDetails.grassMap->loadFromFile( context, false, 0, 0, terrainLocation, grassMap, options ) );
    DIVIDE_EXPECTED_CALL( vegDetails.treeMap->loadFromFile( context, false, 0, 0, terrainLocation, treeMap, options ) );

    ResourceDescriptor<Vegetation> descriptor{ vegDetails.name, vegDetails };
    descriptor.assetName( vegDetails.name );
    _vegetation = CreateResource(descriptor);
}

void Terrain::postLoad(SceneGraphNode* sgn)
{
    if (!_initialSetupDone)
    {
        PlatformContext& pContext = sgn->context();
        registerEditorComponent( pContext );
        DIVIDE_ASSERT(_editorComponent != nullptr);

        _editorComponent->onChangedCbk([this](const std::string_view field) {onEditorChange(field); });

        EditorComponentField tessTriangleWidthField = {};
        tessTriangleWidthField._name = "Tessellated Triangle Width";
        tessTriangleWidthField._dataGetter = [&](void* dataOut, [[maybe_unused]] void* user_data) noexcept
        {
            *static_cast<U32*>(dataOut) = tessParams().tessellatedTriangleWidth();
        };
        tessTriangleWidthField._dataSetter = [&](const void* data, [[maybe_unused]] void* user_data) noexcept 
        {
            _tessParams.tessellatedTriangleWidth(*static_cast<const U32*>(data));
        };
        tessTriangleWidthField._type = EditorComponentFieldType::SLIDER_TYPE;
        tessTriangleWidthField._readOnly = false;
        tessTriangleWidthField._serialise = true;
        tessTriangleWidthField._basicType = PushConstantType::UINT;
        tessTriangleWidthField._range = { 1.0f, 50.0f };
        tessTriangleWidthField._step = 1.0f;

        _editorComponent->registerField(MOV(tessTriangleWidthField));

        EditorComponentField toggleBoundsField = {};
        toggleBoundsField._name = "Toggle Quadtree Bounds";
        toggleBoundsField._range = { toggleBoundsField._name.length() * 10.0f, 20.0f };//dimensions
        toggleBoundsField._type = EditorComponentFieldType::BUTTON;
        toggleBoundsField._readOnly = false; //disabled/enabled
        _editorComponent->registerField(MOV(toggleBoundsField));

        auto& sMgr = pContext.kernel().projectManager();

        EditorComponentField grassVisibilityDistanceField = {};
        grassVisibilityDistanceField._name = "Grass visibility distance";
        grassVisibilityDistanceField._range = { 0.01f, 10000.0f };
        grassVisibilityDistanceField._serialise = false;
        grassVisibilityDistanceField._dataGetter = [&sMgr](void* dataOut, [[maybe_unused]] void* user_data) noexcept
        {
            const SceneRenderState& rState = sMgr->activeProject()->getActiveScene()->state()->renderState();
            *static_cast<F32*>(dataOut) = rState.grassVisibility();
        };
        grassVisibilityDistanceField._dataSetter = [&sMgr](const void* data, [[maybe_unused]] void* user_data) noexcept
        {
            SceneRenderState& rState = sMgr->activeProject()->getActiveScene()->state()->renderState();
            rState.grassVisibility(*static_cast<const F32*>(data)); 
        };
        grassVisibilityDistanceField._type = EditorComponentFieldType::PUSH_TYPE;
        grassVisibilityDistanceField._basicType = PushConstantType::FLOAT;
        grassVisibilityDistanceField._readOnly = false;
        _editorComponent->registerField(MOV(grassVisibilityDistanceField));

        EditorComponentField treeVisibilityDistanceField = {};
        treeVisibilityDistanceField._name = "Tree visibility distance";
        treeVisibilityDistanceField._range = { 0.01f, 10000.0f };
        treeVisibilityDistanceField._serialise = false;
        treeVisibilityDistanceField._dataGetter = [&sMgr](void* dataOut, [[maybe_unused]] void* user_data) noexcept
        {
            const SceneRenderState& rState = sMgr->activeProject()->getActiveScene()->state()->renderState();
            *static_cast<F32*>(dataOut) = rState.treeVisibility();
        };
        treeVisibilityDistanceField._dataSetter = [&sMgr](const void* data, [[maybe_unused]] void* user_data) noexcept
        {
            SceneRenderState& rState = sMgr->activeProject()->getActiveScene()->state()->renderState();
            rState.treeVisibility(*static_cast<const F32*>(data));
        };
        treeVisibilityDistanceField._type = EditorComponentFieldType::PUSH_TYPE;
        treeVisibilityDistanceField._basicType = PushConstantType::FLOAT;
        treeVisibilityDistanceField._readOnly = false;
        _editorComponent->registerField(MOV(treeVisibilityDistanceField));
        _initialSetupDone = true;
    }

    ResourceDescriptor<TransformNode> vegTransformDescriptor{ "Vegetation" };
    Handle<TransformNode> vegTransformHandle = CreateResource( vegTransformDescriptor );

    SceneGraphNodeDescriptor vegetationParentNode;
    vegetationParentNode._serialize = false;
    vegetationParentNode._name = "Vegetation";
    vegetationParentNode._usageContext = NodeUsageContext::NODE_STATIC;
    vegetationParentNode._componentMask = to_base( ComponentType::TRANSFORM ) | to_base( ComponentType::BOUNDS );
    vegetationParentNode._nodeHandle = FromHandle( vegTransformHandle );

    SceneGraphNode* vegParent = sgn->addChildNode( vegetationParentNode );
    assert( vegParent != nullptr );

    SceneGraphNodeDescriptor vegetationNodeDescriptor;
    vegetationNodeDescriptor._serialize = false;
    vegetationNodeDescriptor._usageContext = NodeUsageContext::NODE_STATIC;
    vegetationNodeDescriptor._componentMask = to_base( ComponentType::TRANSFORM ) |
                                              to_base( ComponentType::BOUNDS ) |
                                              to_base( ComponentType::RENDERING );

    for ( const TerrainChunk* chunk : _terrainChunks )
    {
        vegetationNodeDescriptor._nodeHandle = FromHandle(_vegetation);
        Util::StringFormatTo( vegetationNodeDescriptor._name, "Vegetation_chunk_{}", chunk->id() );
        vegetationNodeDescriptor._dataFlag = chunk->id();
        vegParent->addChildNode( vegetationNodeDescriptor );
    }

    sgn->get<RenderingComponent>()->lockLoD(0u);

    SceneNode::postLoad(sgn);
}

bool Terrain::postLoad()
{
    return Object3D::postLoad();
}

void Terrain::onEditorChange(const std::string_view field)
{
    if (field == "Toggle Quadtree Bounds")
    {
        toggleBoundingBoxes();
    }
}

void Terrain::postBuild( PlatformContext& context )
{
    const U16 terrainWidth = _descriptor._dimensions.width;
    const U16 terrainHeight = _descriptor._dimensions.height;
    {
        vector<uint3> triangles;
        // Generate index buffer
        triangles.reserve((terrainWidth - 1) * (terrainHeight - 1) * 2);

        //Ref : https://www.3dgep.com/multi-textured-terrain-in-opengl/
        for (U32 height = 0; height < to_U32(terrainHeight - 1); ++height) {
            for (U32 width = 0; width < to_U32(terrainWidth - 1); ++width) {
                const U32 vertexIndex = TER_COORD(width, height, to_U32(terrainWidth));
                // Top triangle (T0)
                triangles.emplace_back(vertexIndex, vertexIndex + terrainWidth + 1u, vertexIndex + 1u);
                // Bottom triangle (T1)
                triangles.emplace_back(vertexIndex, vertexIndex + terrainWidth, vertexIndex + terrainWidth + 1u);
            }
        }

        addTriangles(0, triangles);
    }

    // Approximate bounding box
    const F32 halfWidth = terrainWidth * 0.5f;
    _boundingBox.setMin(-halfWidth, _descriptor._altitudeRange.min, -halfWidth);
    _boundingBox.setMax(halfWidth, _descriptor._altitudeRange.max, halfWidth);

    _terrainQuadtree.build(_boundingBox, _descriptor._dimensions, this);

    // The terrain's final bounding box is the QuadTree's root bounding box
    _boundingBox.set(_terrainQuadtree.computeBoundingBox());

    {
        // widths[0] doesn't define a ring hence -1
        const U8 ringCount = _descriptor._ringCount - 1;
        _tileRings.reserve(ringCount);
        F32 tileWidth = _descriptor._startWidth;
        for (U8 i = 0; i < ringCount ; ++i) {
            const U8 count0 = _descriptor._ringTileCount[i + 0];
            const U8 count1 = _descriptor._ringTileCount[i + 1];
            _tileRings.emplace_back( TileRing{ count0 / 2, count1, tileWidth });
            tileWidth *= 2.0f;
        }

        // This is a whole fraction of the max tessellation, i.e., 64/N.  The intent is that 
        // the height field scrolls through the terrain mesh in multiples of the polygon spacing.
        // So polygon vertices mostly stay fixed relative to the displacement map and this reduces
        // scintillation.  Without snapping, it scintillates badly.  Additionally, we make the
        // snap size equal to one patch width, purely to stop the patches dancing around like crazy.
        // The non-debug rendering works fine either way, but crazy flickering of the debug patches 
        // makes understanding much harder.
        _tessParams.SnapGridSize(tessParams().WorldScale() * _tileRings[ringCount - 1].tileSize() / TessellationParams::PATCHES_PER_TILE_EDGE);
        vector<U16> indices = CreateTileQuadListIB();

        { // Create a single buffer to hold the data for all of our tile rings
            GenericVertexData::IndexBuffer idxBuff{};
            idxBuff.smallIndices = true;
            idxBuff.count = indices.size();
            idxBuff.data = indices.data();
            idxBuff.dynamic = false;

            _terrainBuffer = context.gfx().newGVD(1, _descriptor._name.c_str());
            {
                const BufferLock lock = _terrainBuffer->setIndexBuffer(idxBuff);
                DIVIDE_UNUSED(lock);
            }
            vector<TileRing::InstanceData> vbData;
            vbData.reserve(TessellationParams::QUAD_LIST_INDEX_COUNT * ringCount);

            GenericVertexData::SetBufferParams params = {};
            params._bindConfig = { 0u, 0u };
            params._bufferParams._elementSize = sizeof(TileRing::InstanceData);
            params._bufferParams._updateFrequency = BufferUpdateFrequency::ONCE;

            for (size_t i = 0u; i < ringCount; ++i)
            {
                vector<TileRing::InstanceData> ringData = _tileRings[i].createInstanceDataVB(to_I32(i));
                vbData.insert(cend(vbData), cbegin(ringData), cend(ringData));
                params._bufferParams._elementCount += to_U32(ringData.size());
            }

            params._initialData = { (Byte*)vbData.data(), vbData.size() * sizeof(TileRing::InstanceData) };
            {
                const BufferLock lock = _terrainBuffer->setBuffer(params);
                DIVIDE_UNUSED(lock);
            }
        }
    }
}

void Terrain::toggleBoundingBoxes()
{
    _terrainQuadtree.toggleBoundingBoxes();
    rebuildDrawCommands(true);
}

void Terrain::prepareRender(SceneGraphNode* sgn,
                            RenderingComponent& rComp,
                            RenderPackage& pkg,
                            GFX::MemoryBarrierCommand& postDrawMemCmd,
                            const RenderStagePass renderStagePass,
                            const CameraSnapshot& cameraSnapshot,
                            const bool refreshData)
{
    if (renderStagePass._stage == RenderStage::DISPLAY && renderStagePass._passType == RenderPassType::MAIN_PASS)
    {
        _terrainQuadtree.drawBBox(sgn->context().gfx());
    }

    rComp.setIndexBufferElementOffset(_terrainBuffer->firstIndexOffsetCount());

    const F32 triangleWidth = to_F32(tessParams().tessellatedTriangleWidth());
    if (renderStagePass._stage == RenderStage::REFLECTION ||
        renderStagePass._stage == RenderStage::REFRACTION)                 
    {
        // Lower the level of detail in reflections and refractions
        //triangleWidth *= 1.5f;
    } else if (renderStagePass._stage == RenderStage::SHADOW) {
        //triangleWidth *= 2.0f;
    }

    const float2 SNAP_GRID_SIZE = tessParams().SnapGridSize();
    float3 cullingEye = cameraSnapshot._eye;
    const float2 eyeXZ = cullingEye.xz();

    float2 snappedXZ = eyeXZ;
    for (U8 i = 0; i < 2; ++i) {
        if (SNAP_GRID_SIZE[i] > 0.f) {
            snappedXZ[i] = FLOOR(snappedXZ[i] / SNAP_GRID_SIZE[i]) * SNAP_GRID_SIZE[i];
        }
    }

    float2 uvEye = snappedXZ;
    uvEye /= tessParams().WorldScale();
    uvEye *= -1;
    uvEye /= (TessellationParams::PATCHES_PER_TILE_EDGE * 2);

    const float2 dXZ = eyeXZ - snappedXZ;
    snappedXZ = eyeXZ - dXZ;
        
    cullingEye.x += snappedXZ[0];
    cullingEye.z += snappedXZ[1];

    const mat4<F32> terrainWorldMat(float3(snappedXZ[0], 0.f, snappedXZ[1]),
                                    float3(tessParams().WorldScale()[0], 1.f, tessParams().WorldScale()[1]),
                                    mat3<F32>());

    STUBBED("ToDo: Convert terrain uniforms from UBO to push constants! -Ionut");
    UniformData* uniforms = pkg.pushConstantsCmd()._uniformData;
    uniforms->set(_ID("dvd_terrainWorld"), PushConstantType::MAT4, terrainWorldMat);
    uniforms->set(_ID("dvd_uvEyeOffset"), PushConstantType::VEC2, uvEye);
    uniforms->set(_ID("dvd_tessTriangleWidth"),  PushConstantType::FLOAT, triangleWidth);
    uniforms->set(_ID("dvd_frustumPlanes"), PushConstantType::VEC4, cameraSnapshot._frustumPlanes );

    Object3D::prepareRender(sgn, rComp, pkg, postDrawMemCmd, renderStagePass, cameraSnapshot, refreshData);
}

void Terrain::buildDrawCommands(SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut)
{
    GenericDrawCommand& cmd = cmdsOut.emplace_back();
    toggleOption( cmd, CmdRenderOptions::RENDER_INDIRECT );

    cmd._sourceBuffer = _terrainBuffer->handle();
    cmd._cmd.indexCount = to_U32(TessellationParams::QUAD_LIST_INDEX_COUNT);
    for (const auto& tileRing : _tileRings)
    {
        cmd._cmd.instanceCount += tileRing.tileCount();
    }

    Object3D::buildDrawCommands(sgn, cmdsOut);
}

const vector<VertexBuffer::Vertex>& Terrain::getVerts() const noexcept
{
    return _physicsVerts;
}

Terrain::Vert Terrain::getVertFromGlobal(F32 x, F32 z, const bool smooth) const
{
    x -= _boundingBox.getCenter().x;
    z -= _boundingBox.getCenter().z;
    const vec2<U16>& dim = _descriptor._dimensions;
    const F32 xClamp = (0.5f * dim.width + x) / dim.width;
    const F32 zClamp = (0.5f * dim.height - z) / dim.height;
    return getVert(xClamp, 1 - zClamp, smooth);
}

Terrain::Vert Terrain::getVert(const F32 x_clampf, const F32 z_clampf, const bool smooth) const
{
    return smooth ? getSmoothVert(x_clampf, z_clampf)
                  : getVert(x_clampf, z_clampf);
}

Terrain::Vert Terrain::getSmoothVert(const F32 x_clampf, const F32 z_clampf) const
{
    assert(!(x_clampf < 0.0f || z_clampf < 0.0f || 
             x_clampf > 1.0f || z_clampf > 1.0f));

    const vec2<U16>& dim   = _descriptor._dimensions;
    const float3& bbMin = _boundingBox._min;
    const float3& bbMax = _boundingBox._max;

    const float2 posF(x_clampf * dim.width,    z_clampf * dim.height);
          int2 posI(to_I32(posF.width),      to_I32(posF.height));
    const float2 posD(posF.width - posI.width, posF.height - posI.height);

    if (posI.width >= to_I32(dim.width) - 1) {
        posI.width = dim.width - 2;
    }

    if (posI.height >= to_I32(dim.height) - 1) {
        posI.height = dim.height - 2;
    }

    assert(posI.width  >= 0 && posI.width  < to_I32(dim.width)  - 1 &&
           posI.height >= 0 && posI.height < to_I32(dim.height) - 1);

    const VertexBuffer::Vertex& tempVert1 = _physicsVerts[TER_COORD(posI.width,     posI.height,     to_I32(dim.width))];
    const VertexBuffer::Vertex& tempVert2 = _physicsVerts[TER_COORD(posI.width + 1, posI.height,     to_I32(dim.width))];
    const VertexBuffer::Vertex& tempVert3 = _physicsVerts[TER_COORD(posI.width,     posI.height + 1, to_I32(dim.width))];
    const VertexBuffer::Vertex& tempVert4 = _physicsVerts[TER_COORD(posI.width + 1, posI.height + 1, to_I32(dim.width))];

    const float3 normals[4]{
        Util::UNPACK_VEC3(tempVert1._normal),
        Util::UNPACK_VEC3(tempVert2._normal),
        Util::UNPACK_VEC3(tempVert3._normal),
        Util::UNPACK_VEC3(tempVert4._normal)
    };

    const float3 tangents[4]{
        Util::UNPACK_VEC3(tempVert1._tangent),
        Util::UNPACK_VEC3(tempVert2._tangent),
        Util::UNPACK_VEC3(tempVert3._tangent),
        Util::UNPACK_VEC3(tempVert4._tangent)
    };

    Vert ret = {};
    ret._position.set(
        // X
        bbMin.x + x_clampf * (bbMax.x - bbMin.x),

        //Y
        tempVert1._position.y * (1.0f - posD.width) * (1.0f - posD.height) +
        tempVert2._position.y *         posD.width  * (1.0f - posD.height) +
        tempVert3._position.y * (1.0f - posD.width) *         posD.height +
        tempVert4._position.y *         posD.width  *         posD.height,

        //Z
        bbMin.z + z_clampf * (bbMax.z - bbMin.z));

    ret._normal.set(normals[0] * (1.0f - posD.width) * (1.0f - posD.height) +
                    normals[1] *         posD.width  * (1.0f - posD.height) +
                    normals[2] * (1.0f - posD.width) *         posD.height +
                    normals[3] *         posD.width  *         posD.height);

    ret._tangent.set(tangents[0] * (1.0f - posD.width) * (1.0f - posD.height) +
                     tangents[1] *         posD.width  * (1.0f - posD.height) +
                     tangents[2] * (1.0f - posD.width) *         posD.height +
                     tangents[3] *         posD.width  *         posD.height);

    ret._normal.normalize();
    ret._tangent.normalize();
    return ret;

}

Terrain::Vert Terrain::getVert(const F32 x_clampf, const F32 z_clampf) const
{
    assert(!(x_clampf < 0.0f || z_clampf < 0.0f ||
             x_clampf > 1.0f || z_clampf > 1.0f));

    const vec2<U16>& dim = _descriptor._dimensions;
    
    int2 posI(to_I32(x_clampf * dim.width), 
                   to_I32(z_clampf * dim.height));

    if (posI.width >= to_I32(dim.width) - 1) {
        posI.width = dim.width - 2;
    }

    if (posI.height >= to_I32(dim.height) - 1) {
        posI.height = dim.height - 2;
    }

    assert(posI.width  >= 0 && posI.width  < to_I32(dim.width)  - 1 &&
           posI.height >= 0 && posI.height < to_I32(dim.height) - 1);

    const VertexBuffer::Vertex& tempVert1 = _physicsVerts[TER_COORD(posI.width, posI.height, to_I32(dim.width))];

    Vert ret = {};
    ret._position.set(tempVert1._position);
    ret._normal.set(Util::UNPACK_VEC3(tempVert1._normal));
    ret._tangent.set(Util::UNPACK_VEC3(tempVert1._tangent));

    return ret;
}

vec2<U16> Terrain::getDimensions() const noexcept
{
    return _descriptor._dimensions;
}

float2 Terrain::getAltitudeRange() const noexcept
{
    return _descriptor._altitudeRange;
}

void Terrain::saveToXML(boost::property_tree::ptree& pt) const
{
    SaveToXML(_descriptor, pt);
    Object3D::saveToXML(pt);
}

void Terrain::loadFromXML(const boost::property_tree::ptree& pt)
{
    Object3D::loadFromXML(pt);
}

} //namespace Divide
