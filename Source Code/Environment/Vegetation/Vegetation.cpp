#include "stdafx.h"

#include "Headers/Vegetation.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Managers/Headers/SceneManager.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Geometry/Shapes/Headers/Mesh.h"
#include "Geometry/Shapes/Headers/SubMesh.h"
#include "Geometry/Material/Headers/Material.h"
#include "Environment/Terrain/Headers/Terrain.h"
#include "Environment/Terrain/Headers/TerrainChunk.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"

#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"

namespace Divide {

namespace {
    constexpr U32 WORK_GROUP_SIZE = 64;
    constexpr I16 g_maxRadiusSteps = 512;
    constexpr F32 g_ArBase = 1.0f; // Starting radius of circle A
    constexpr F32 g_BrBase = 1.0f; // Starting radius of circle B
    constexpr F32 g_PointRadiusBaseGrass = 0.935f;
    constexpr F32 g_PointRadiusBaseTrees = 5.f;
    // Distance between concentric rings
    constexpr F32 g_distanceRingsBaseGrass = 2.35f;
    constexpr F32 g_distanceRingsBaseTrees = 2.5f;
    constexpr F32 g_slopeLimitGrass = 30.0f;
    constexpr F32 g_slopeLimitTrees = 10.0f;

    SharedMutex g_treeMeshLock;
}

bool Vegetation::s_buffersBound = false;
Material_ptr Vegetation::s_treeMaterial = nullptr;
Material_ptr Vegetation::s_vegetationMaterial = nullptr;

eastl::unordered_set<vec2<F32>> Vegetation::s_treePositions;
eastl::unordered_set<vec2<F32>> Vegetation::s_grassPositions;
ShaderBuffer* Vegetation::s_treeData = nullptr;
ShaderBuffer* Vegetation::s_grassData = nullptr;
VertexBuffer* Vegetation::s_buffer = nullptr;
ShaderProgram_ptr Vegetation::s_cullShaderGrass = nullptr;
ShaderProgram_ptr Vegetation::s_cullShaderTrees = nullptr;
vectorEASTL<Mesh_ptr> Vegetation::s_treeMeshes;
std::atomic_uint Vegetation::s_bufferUsage = 0;
U32 Vegetation::s_maxChunks = 0u;
std::array<U16, 3> Vegetation::s_lodPartitions;

//Per-chunk
U32 Vegetation::s_maxGrassInstances = 0u;
U32 Vegetation::s_maxTreeInstances = 0u;

Vegetation::Vegetation(GFXDevice& context, 
                       TerrainChunk& parentChunk,
                       const VegetationDetails& details)
    : SceneNode(context.parent().resourceCache(), 
                parentChunk.parent().descriptorHash() + parentChunk.ID(),
                details.name,
                ResourcePath{ details.name + "_" + Util::to_string(parentChunk.ID()) },
                {},
                SceneNodeType::TYPE_VEGETATION,
                to_base(ComponentType::TRANSFORM) | to_base(ComponentType::BOUNDS) | to_base(ComponentType::RENDERING)),

      _context(context),
      _terrainChunk(parentChunk),
      _terrain(details.parentTerrain),
      _grassScales(details.grassScales),
      _treeScales(details.treeScales),
      _treeRotations(details.treeRotations),
      _grassMap(details.grassMap),
      _treeMap(details.treeMap)
{
    _treeMeshNames.insert(cend(_treeMeshNames), cbegin(details.treeMeshes), cend(details.treeMeshes));

    assert(!_grassMap->imageLayers().empty() && !_treeMap->imageLayers().empty());

    setBounds(parentChunk.bounds());

    renderState().addToDrawExclusionMask(RenderStage::DISPLAY, RenderPassType::MAIN_PASS);
    renderState().addToDrawExclusionMask(RenderStage::REFLECTION);
    renderState().addToDrawExclusionMask(RenderStage::REFRACTION);
    renderState().addToDrawExclusionMask(RenderStage::SHADOW, RenderPassType::COUNT, to_U8(LightType::POINT));
    renderState().addToDrawExclusionMask(RenderStage::SHADOW, RenderPassType::COUNT, to_U8(LightType::SPOT));
    for (U16 i = 1; i < Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT; ++i) {
        renderState().addToDrawExclusionMask(RenderStage::SHADOW, RenderPassType::COUNT, to_U8(LightType::DIRECTIONAL), g_AllIndicesID, i);
    }
    // Because we span an entire terrain chunk, LoD calculation will always be off unless we use the closest possible point to the camera
    renderState().useBoundsCenterForLoD(false);
    renderState().lod0OnCollision(true);
    renderState().drawState(false);

    CachedResource::setState(ResourceState::RES_LOADING);
    _buildTask = CreateTask(
        [this](const Task& /*parentTask*/) {
            s_bufferUsage.fetch_add(1);
            computeVegetationTransforms(false);
            computeVegetationTransforms(true);
            _instanceCountGrass = to_U32(_tempGrassData.size());
            _instanceCountTrees = to_U32(_tempTreeData.size());
        });

    Start(*_buildTask, _context.context().taskPool(TaskPoolType::HIGH_PRIORITY), TaskPriority::DONT_CARE);

    EditorComponentField instanceCountGrassField = {};
    instanceCountGrassField._name = "Num Grass Instances";
    instanceCountGrassField._data = &_instanceCountGrass;
    instanceCountGrassField._type = EditorComponentFieldType::PUSH_TYPE;
    instanceCountGrassField._readOnly = true;
    instanceCountGrassField._basicType = GFX::PushConstantType::UINT;
    _editorComponent.registerField(MOV(instanceCountGrassField));

    EditorComponentField visDistanceGrassField = {};
    visDistanceGrassField._name = "Grass Draw distance";
    visDistanceGrassField._data = &_grassDistance;
    visDistanceGrassField._type = EditorComponentFieldType::PUSH_TYPE;
    visDistanceGrassField._readOnly = true;
    visDistanceGrassField._basicType = GFX::PushConstantType::FLOAT;
    _editorComponent.registerField(MOV(visDistanceGrassField));

    EditorComponentField instanceCountTreesField = {};
    instanceCountTreesField._name = "Num Tree Instances";
    instanceCountTreesField._data = &_instanceCountTrees;
    instanceCountTreesField._type = EditorComponentFieldType::PUSH_TYPE;
    instanceCountTreesField._readOnly = true;
    instanceCountTreesField._basicType = GFX::PushConstantType::UINT;
    _editorComponent.registerField(MOV(instanceCountTreesField));

    EditorComponentField visDistanceTreesField = {};
    visDistanceTreesField._name = "Tree Draw Instance";
    visDistanceTreesField._data = &_treeDistance;
    visDistanceTreesField._type = EditorComponentFieldType::PUSH_TYPE;
    visDistanceTreesField._readOnly = true;
    visDistanceTreesField._basicType = GFX::PushConstantType::FLOAT;
    _editorComponent.registerField(MOV(visDistanceTreesField));

    EditorComponentField terrainIDField = {};
    terrainIDField._name = "Terrain Chunk ID";
    terrainIDField._dataGetter = [this](void *dataOut) {*static_cast<U32*>(dataOut) = _terrainChunk.ID(); };
    terrainIDField._type = EditorComponentFieldType::PUSH_TYPE;
    terrainIDField._readOnly = true;
    terrainIDField._basicType = GFX::PushConstantType::UINT;
    _editorComponent.registerField(MOV(terrainIDField));

}

Vegetation::~Vegetation()
{
    Console::printfn(Locale::Get(_ID("UNLOAD_VEGETATION_BEGIN")), resourceName().c_str());
    U32 timer = 0;
    while (getState() == ResourceState::RES_LOADING) {
        // wait for the loading thread to finish first;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        timer += 10;
        if (timer > 3000) {
            break;
        }
    }
    assert(getState() != ResourceState::RES_LOADING);
    if (s_bufferUsage.fetch_sub(1) == 1) {
        destroyStaticData();
    }

    Console::printfn(Locale::Get(_ID("UNLOAD_VEGETATION_END")));
}

void Vegetation::destroyStaticData() {
    {
        UniqueLock<SharedMutex> w_lock(g_treeMeshLock);
        s_treeMeshes.clear();
    }
    s_treeMaterial.reset();
    s_vegetationMaterial.reset();
    s_cullShaderGrass.reset();
    s_cullShaderTrees.reset();
}

void Vegetation::precomputeStaticData(GFXDevice& gfxDevice, const U32 chunkSize, const U32 maxChunkCount) {
    // Make sure this is ONLY CALLED FROM THE MAIN LOADING THREAD. All instances should call this in a serialized fashion
    if (s_buffer == nullptr) {
        s_lodPartitions.fill(0u);

        constexpr F32 offsetBottom0 = 0.20f;
        constexpr F32 offsetBottom1 = 0.10f;

        const mat4<F32> transform[] = {
            mat4<F32>{
                vec3<F32>(-offsetBottom0, 0.f, -offsetBottom0),
                VECTOR3_UNIT,
                GetMatrix(Quaternion<F32>(Angle::DEGREES<F32>(25.f), Angle::DEGREES<F32>(0.f), Angle::DEGREES<F32>(0.f)))
            },

            mat4<F32>{
                vec3<F32>(-offsetBottom1, 0.f, offsetBottom1),
                vec3<F32>(0.85f),
                GetMatrix(Quaternion<F32>(Angle::DEGREES<F32>(-12.5f), Angle::DEGREES<F32>(0.f),  Angle::DEGREES<F32>(0.f)) * //Pitch
                          Quaternion<F32>(Angle::DEGREES<F32>(0.f),    Angle::DEGREES<F32>(35.f), Angle::DEGREES<F32>(0.f)))  //Yaw
            },

            mat4<F32>{
                vec3<F32>(offsetBottom0, 0.f, -offsetBottom1),
                vec3<F32>(1.1f),
                GetMatrix(Quaternion<F32>(Angle::DEGREES<F32>(30.f), Angle::DEGREES<F32>(0.f),   Angle::DEGREES<F32>(0.f)) * //Pitch
                          Quaternion<F32>(Angle::DEGREES<F32>(0.f),  Angle::DEGREES<F32>(-75.f), Angle::DEGREES<F32>(0.f)))  //Yaw
            },

            mat4<F32>{
                vec3<F32>(offsetBottom1 * 2, 0.f, offsetBottom1),
                vec3<F32>(0.9f),
                GetMatrix(Quaternion<F32>(Angle::DEGREES<F32>(-25.f), Angle::DEGREES<F32>(0.f),    Angle::DEGREES<F32>(0.f)) * //Pitch
                          Quaternion<F32>(Angle::DEGREES<F32>(0.f),   Angle::DEGREES<F32>(-125.f), Angle::DEGREES<F32>(0.f)))  //Yaw
            },

            mat4<F32>{
                vec3<F32>(-offsetBottom1 * 2, 0.f, -offsetBottom1 * 2),
                vec3<F32>(1.2f),
                GetMatrix(Quaternion<F32>(Angle::DEGREES<F32>(5.f), Angle::DEGREES<F32>(0.f),    Angle::DEGREES<F32>(0.f)) * //Pitch
                          Quaternion<F32>(Angle::DEGREES<F32>(0.f), Angle::DEGREES<F32>(-225.f), Angle::DEGREES<F32>(0.f)))  //Yaw
            },

            mat4<F32>{
                vec3<F32>(offsetBottom0, 0.f, offsetBottom1 * 2),
                vec3<F32>(0.75f),
                GetMatrix(Quaternion<F32>(Angle::DEGREES<F32>(-15.f), Angle::DEGREES<F32>(0.f),   Angle::DEGREES<F32>(0.f)) * //Pitch
                          Quaternion<F32>(Angle::DEGREES<F32>(0.f),   Angle::DEGREES<F32>(305.f), Angle::DEGREES<F32>(0.f)))  //Yaw
            }
        };

        vectorEASTL<vec3<F32>> vertices{};

        constexpr U8 billboardsPlaneCount = to_U8(sizeof(transform) / sizeof(transform[0]));
        vertices.reserve(billboardsPlaneCount * 4);

        for (U8 i = 0u; i < billboardsPlaneCount; ++i) {
            vertices.push_back(transform[i] * vec4<F32>(-1.f, 0.f, 0.f, 1.f)); //BL
            vertices.push_back(transform[i] * vec4<F32>(-1.f, 1.f, 0.f, 1.f)); //TL
            vertices.push_back(transform[i] * vec4<F32>( 1.f, 1.f, 0.f, 1.f)); //TR
            vertices.push_back(transform[i] * vec4<F32>( 1.f, 0.f, 0.f, 1.f)); //BR
        };

        const U16 indices[] = { 0, 1, 2, 
                                0, 2, 3 };

        const vec2<F32> texCoords[] = {
            vec2<F32>(0.f, 0.f),
            vec2<F32>(0.f, 1.f),
            vec2<F32>(1.f, 1.f),
            vec2<F32>(1.f, 0.f)
        };

        s_buffer = gfxDevice.newVB();
        s_buffer->useLargeIndices(false);
        s_buffer->setVertexCount(vertices.size());

        for (U8 i = 0u; i < to_U8(vertices.size()); ++i) {
            s_buffer->modifyPositionValue(i, vertices[i]);
            s_buffer->modifyNormalValue(i, WORLD_Y_AXIS);
            s_buffer->modifyTangentValue(i, WORLD_X_AXIS);
            s_buffer->modifyTexCoordValue(i, texCoords[i % 4].s, texCoords[i % 4].t);
        }

        const auto addPlanes = [&indices](const U8 count) {
            for (U8 i = 0u; i < count; ++i) {
                if (i > 0) {
                    s_buffer->addRestartIndex();
                }
                for (U16 idx : indices) {
                    s_buffer->addIndex(idx + i * 4);
                }
            }
        };

        for (U8 i = 0; i < s_lodPartitions.size(); ++i) {
            addPlanes(billboardsPlaneCount / (i + 1));
            s_lodPartitions[i] = s_buffer->partitionBuffer();
        }

        s_buffer->create(true);
        s_buffer->keepData(false);
    }

    //ref: http://mollyrocket.com/casey/stream_0016.html
    s_grassPositions.reserve(to_size(chunkSize) * chunkSize);
    s_treePositions.reserve(to_size(chunkSize) * chunkSize);

    const F32 posOffset = to_F32(chunkSize * 2);

    vec2<F32> intersections[2]{};
    Util::Circle circleA{}, circleB{};
    circleA.center[0] = circleB.center[0] = -posOffset;
    circleA.center[1] = -posOffset;
    circleB.center[1] = posOffset;

    F32 dR[2] = { g_distanceRingsBaseGrass * g_PointRadiusBaseGrass,
                  g_distanceRingsBaseTrees * g_PointRadiusBaseTrees };

    for (U8 i = 0; i < 2; ++i) {
        for (I16 RadiusStepA = 0; RadiusStepA < g_maxRadiusSteps; ++RadiusStepA) {
            const F32 Ar = g_ArBase + dR[i] * to_F32(RadiusStepA);
            for (I16 RadiusStepB = 0; RadiusStepB < g_maxRadiusSteps; ++RadiusStepB) {
                const F32 Br = g_BrBase + dR[i] * to_F32(RadiusStepB);
                circleA.radius = Ar + (RadiusStepB % 3 ? 0.0f : 0.3f * dR[i]);
                circleB.radius = Br + (RadiusStepA % 3 ? 0.0f : 0.3f * dR[i]);
                // Intersect circle Ac,UseAr and Bc,UseBr
                if (IntersectCircles(circleA, circleB, intersections)) {
                    // Add the resulting points if they are within the pattern bounds
                    for (const vec2<F32>& record : intersections) {
                        if (IS_IN_RANGE_EXCLUSIVE(record.x, -to_F32(chunkSize), to_F32(chunkSize)) &&
                            IS_IN_RANGE_EXCLUSIVE(record.y, -to_F32(chunkSize), to_F32(chunkSize)))                     {
                            if (i == 0) {
                                s_grassPositions.insert(record);
                            } else {
                                s_treePositions.insert(record);
                            }
                        }
                    }
                }
            }
        }
    }

    s_maxChunks = maxChunkCount;
}

void Vegetation::createVegetationMaterial(GFXDevice& gfxDevice, const Terrain_ptr& terrain, const VegetationDetails& vegDetails) {
    if (vegDetails.billboardCount == 0) {
        return;
    }

    assert(s_maxGrassInstances != 0u && "Vegetation error: call \"precomputeStaticData\" first!");

    std::atomic_uint loadTasks = 0u;
    Material::ShaderData treeShaderData = {};
    treeShaderData._depthShaderVertSource = "tree";
    treeShaderData._depthShaderVertVariant = "";
    treeShaderData._colourShaderVertSource = "tree";
    treeShaderData._colourShaderVertVariant = "";

    ResourceDescriptor matDesc("Tree_material");
    s_treeMaterial = CreateResource<Material>(gfxDevice.parent().resourceCache(), matDesc);
    s_treeMaterial->shadingMode(ShadingMode::BLINN_PHONG);
    s_treeMaterial->baseShaderData(treeShaderData);
    s_treeMaterial->addShaderDefine(ShaderType::VERTEX, "HAS_CULLING_OUT", true);
    s_treeMaterial->addShaderDefine(ShaderType::COUNT, "OVERRIDE_DATA_IDX", true);
    s_treeMaterial->addShaderDefine(ShaderType::COUNT, Util::StringFormat("MAX_TREE_INSTANCES %d", s_maxTreeInstances).c_str(), true);

    SamplerDescriptor grassSampler = {};
    grassSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    grassSampler.anisotropyLevel(8);

    TextureDescriptor grassTexDescriptor(TextureType::TEXTURE_2D_ARRAY);
    grassTexDescriptor.layerCount(vegDetails.billboardCount);
    grassTexDescriptor.srgb(true);
    grassTexDescriptor.autoMipMaps(true);

    ResourceDescriptor textureDetailMaps("Vegetation Billboards");
    textureDetailMaps.assetLocation(Paths::g_assetsLocation + terrain->descriptor()->getVariable("vegetationTextureLocation"));
    textureDetailMaps.assetName(ResourcePath{ vegDetails.billboardTextureArray });
    textureDetailMaps.propertyDescriptor(grassTexDescriptor);
    textureDetailMaps.waitForReady(false);
    Texture_ptr grassBillboardArray = CreateResource<Texture>(terrain->parentResourceCache(), textureDetailMaps, loadTasks);

    ResourceDescriptor vegetationMaterial("grassMaterial");
    Material_ptr vegMaterial = CreateResource<Material>(terrain->parentResourceCache(), vegetationMaterial);
    vegMaterial->shadingMode(ShadingMode::COOK_TORRANCE);
    vegMaterial->baseColour(DefaultColours::WHITE);
    vegMaterial->roughness(0.9f);
    vegMaterial->metallic(0.02f);
    vegMaterial->doubleSided(true);
    vegMaterial->isStatic(false);

    Material::ApplyDefaultStateBlocks(*vegMaterial);

    ShaderModuleDescriptor vertModule = {};
    vertModule._batchSameFile = false;
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "grass.glsl";

    vertModule._defines.emplace_back(Util::StringFormat("MAX_GRASS_INSTANCES %d", s_maxGrassInstances).c_str(), true);
    vertModule._defines.emplace_back("OVERRIDE_DATA_IDX", true);
    vertModule._defines.emplace_back("NODE_DYNAMIC", true);
    vertModule._defines.emplace_back("HAS_CLIPPING_OUT", true);
    vertModule._defines.emplace_back("HAS_CULLING_OUT", true);

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "grass.glsl";
    fragModule._defines.emplace_back("OVERRIDE_DATA_IDX", true);
    fragModule._defines.emplace_back("NODE_DYNAMIC", true);
    fragModule._defines.emplace_back("SKIP_TEX0", true);
    fragModule._defines.emplace_back(Util::StringFormat("MAX_GRASS_INSTANCES %d", s_maxGrassInstances).c_str(), true);
    fragModule._defines.emplace_back("USE_DOUBLE_SIDED", true);
    
    ProcessShadowMappingDefines(gfxDevice.context().config(), fragModule._defines);
    fragModule._variant = "Colour";

    ShaderProgramDescriptor shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    ResourceDescriptor grassColourShader("GrassColour");
    grassColourShader.propertyDescriptor(shaderDescriptor);
    grassColourShader.waitForReady(true);
    ShaderProgram_ptr grassColour = CreateResource<ShaderProgram>(terrain->parentResourceCache(), grassColourShader, loadTasks);

    ShaderProgramDescriptor shaderOitDescriptor = shaderDescriptor;
    shaderOitDescriptor._modules.back()._defines.emplace_back("OIT_PASS", true);
    shaderOitDescriptor._modules.back()._defines.emplace_back("MAIN_DISPLAY_PASS", true);
    shaderOitDescriptor._modules.back()._variant = "Colour.OIT";

    ResourceDescriptor grassColourOITShader("grassColourOIT");
    grassColourOITShader.propertyDescriptor(shaderOitDescriptor);
    grassColourOITShader.waitForReady(false);
    ShaderProgram_ptr grassColourOIT = CreateResource<ShaderProgram>(terrain->parentResourceCache(), grassColourOITShader, loadTasks);

    ShaderProgramDescriptor shaderOitDescriptorLQ = shaderDescriptor;
    shaderOitDescriptorLQ._modules.back()._defines.emplace_back("OIT_PASS", true);
    shaderOitDescriptorLQ._modules.back()._variant = "Colour.OIT";

    ResourceDescriptor grassColourOITLQShader("grassColourOITLQ");
    grassColourOITLQShader.propertyDescriptor(shaderOitDescriptor);
    grassColourOITLQShader.waitForReady(false);
    ShaderProgram_ptr grassColourOITLQ = CreateResource<ShaderProgram>(terrain->parentResourceCache(), grassColourOITLQShader, loadTasks);

    fragModule._variant = "PrePass";

    shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);
    shaderDescriptor._modules[0]._defines.emplace_back("PRE_PASS", true);
    shaderDescriptor._modules[1]._defines.emplace_back("PRE_PASS", true);

    ResourceDescriptor grassPrePassLQShader("grassPrePassLQ");
    grassPrePassLQShader.propertyDescriptor(shaderDescriptor);
    grassPrePassLQShader.waitForReady(false);
    ShaderProgram_ptr grassPrePassLQ = CreateResource<ShaderProgram>(terrain->parentResourceCache(), grassPrePassLQShader, loadTasks);

    ResourceDescriptor grassPrePassShader("grassPrePass");
    grassPrePassShader.propertyDescriptor(shaderDescriptor);
    grassPrePassShader.waitForReady(false);
    ShaderProgram_ptr grassPrePass = CreateResource<ShaderProgram>(terrain->parentResourceCache(), grassPrePassShader, loadTasks);

    fragModule._variant = "Shadow.VSM";
    shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    ResourceDescriptor grassShadowShader("grassShadow");
    grassShadowShader.propertyDescriptor(shaderDescriptor);
    grassShadowShader.waitForReady(false);
    ShaderProgram_ptr grassShadowVSM = CreateResource<ShaderProgram>(terrain->parentResourceCache(), grassShadowShader, loadTasks);

    shaderDescriptor._modules.back()._variant += ".ORTHO";
    shaderDescriptor._modules.back()._defines.emplace_back("ORTHO_PROJECTION", true);

    ResourceDescriptor grassShadowShaderOrtho("grassShadowOrtho");
    grassShadowShaderOrtho.propertyDescriptor(shaderDescriptor);
    grassShadowShaderOrtho.waitForReady(false);
    ShaderProgram_ptr grassShadowVSMOrtho = CreateResource<ShaderProgram>(terrain->parentResourceCache(), grassShadowShaderOrtho, loadTasks);

    ShaderModuleDescriptor compModule = {};
    compModule._moduleType = ShaderType::COMPUTE;
    compModule._sourceFile = "instanceCullVegetation.glsl";
    compModule._defines.emplace_back(Util::StringFormat("WORK_GROUP_SIZE %d", WORK_GROUP_SIZE), true);
    compModule._defines.emplace_back(Util::StringFormat("MAX_TREE_INSTANCES %d", s_maxTreeInstances).c_str(), true);
    compModule._defines.emplace_back(Util::StringFormat("MAX_GRASS_INSTANCES %d", s_maxGrassInstances).c_str(), true);
    ShaderProgramDescriptor shaderCompDescriptor = {};
    shaderCompDescriptor._modules.push_back(compModule);

    ResourceDescriptor instanceCullShaderGrass("instanceCullVegetation_Grass");
    instanceCullShaderGrass.waitForReady(false);
    instanceCullShaderGrass.propertyDescriptor(shaderCompDescriptor);
    s_cullShaderGrass = CreateResource<ShaderProgram>(terrain->parentResourceCache(), instanceCullShaderGrass, loadTasks);

    compModule._defines.emplace_back("CULL_TREES", true);
    shaderCompDescriptor = {};
    shaderCompDescriptor._modules.push_back(compModule);

    ResourceDescriptor instanceCullShaderTrees("instanceCullVegetation_Trees");
    instanceCullShaderTrees.waitForReady(false);
    instanceCullShaderTrees.propertyDescriptor(shaderCompDescriptor);
    s_cullShaderTrees = CreateResource<ShaderProgram>(terrain->parentResourceCache(), instanceCullShaderTrees, loadTasks);

    WAIT_FOR_CONDITION(loadTasks.load() == 0u);

    vegMaterial->setShaderProgram(grassColour,           RenderStage::COUNT,   RenderPassType::COUNT);
    vegMaterial->setShaderProgram(grassPrePassLQ,        RenderStage::COUNT,   RenderPassType::PRE_PASS);
    vegMaterial->setShaderProgram(grassPrePass,          RenderStage::DISPLAY, RenderPassType::PRE_PASS);
    vegMaterial->setShaderProgram(grassColourOITLQ,      RenderStage::COUNT,   RenderPassType::OIT_PASS);
    vegMaterial->setShaderProgram(grassColourOIT,        RenderStage::DISPLAY, RenderPassType::OIT_PASS);
    vegMaterial->setShaderProgram(grassShadowVSM,        RenderStage::SHADOW,  RenderPassType::COUNT);
    vegMaterial->setShaderProgram(grassShadowVSMOrtho,   RenderStage::SHADOW,  RenderPassType::COUNT, to_base(LightType::DIRECTIONAL));

    vegMaterial->setTexture(TextureUsage::UNIT0, grassBillboardArray, grassSampler.getHash());
    s_vegetationMaterial = vegMaterial;
}

void Vegetation::createAndUploadGPUData(GFXDevice& gfxDevice, const Terrain_ptr& terrain, const VegetationDetails& vegDetails) {
    assert(s_grassData == nullptr);
    for (TerrainChunk* chunk : terrain->terrainChunks()) {
        chunk->initializeVegetation(vegDetails);
    }
    terrain->getVegetationStats(s_maxGrassInstances, s_maxTreeInstances);

    s_maxGrassInstances += s_maxGrassInstances % WORK_GROUP_SIZE;
    s_maxTreeInstances += s_maxTreeInstances % WORK_GROUP_SIZE;

    if (s_maxTreeInstances > 0 || s_maxGrassInstances > 0) {
        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._usage = ShaderBuffer::Usage::UNBOUND_BUFFER;
        bufferDescriptor._bufferParams._elementSize = sizeof(VegetationData);
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
        bufferDescriptor._flags = to_U32(ShaderBuffer::Flags::NO_SYNC);

        if (s_maxTreeInstances > 0) {
            bufferDescriptor._bufferParams._elementCount = to_U32(s_maxTreeInstances * s_maxChunks);
            bufferDescriptor._name = "Tree_data";
            s_treeData = gfxDevice.newSB(bufferDescriptor);
        }
        if (s_maxGrassInstances > 0) {
            bufferDescriptor._bufferParams._elementCount = to_U32(s_maxGrassInstances * s_maxChunks);
            bufferDescriptor._name = "Grass_data";
            s_grassData = gfxDevice.newSB(bufferDescriptor);
        }

        createVegetationMaterial(gfxDevice, terrain, vegDetails);
    }
}

void Vegetation::uploadVegetationData(SceneGraphNode* sgn) {
    OPTICK_EVENT();

    assert(s_buffer != nullptr);
    Wait(*_buildTask, _context.context().taskPool(TaskPoolType::HIGH_PRIORITY));

    bool hasVegetation = false;
    if (_instanceCountGrass > 0u) {
        hasVegetation = true;
        if (_terrainChunk.ID() < s_maxChunks) {
            s_grassData->writeData(_terrainChunk.ID() * s_maxGrassInstances, _instanceCountGrass, (bufferPtr)_tempGrassData.data());
            renderState().drawState(true);
        } else {
            Console::errorfn("Vegetation::uploadGrassData: insufficient buffer space for grass data");
        }
        _tempGrassData.clear();
    }

    if (_instanceCountTrees > 0u) {
        hasVegetation = true;

        if (_terrainChunk.ID() < s_maxChunks) {
            s_treeData->writeData(_terrainChunk.ID() * s_maxTreeInstances, _instanceCountTrees, (bufferPtr)_tempTreeData.data());
            renderState().drawState(true);
        } else {
            Console::errorfn("Vegetation::uploadGrassData: insufficient buffer space for tree data");
        }
        _tempTreeData.clear();
    }

    if (hasVegetation) {
        sgn->get<RenderingComponent>()->instantiateMaterial(s_vegetationMaterial);
        sgn->get<RenderingComponent>()->occlusionCull(false); //< We handle our own culling

        WAIT_FOR_CONDITION(s_cullShaderGrass->getState() == ResourceState::RES_LOADED &&
                           s_cullShaderTrees->getState() == ResourceState::RES_LOADED);

        PipelineDescriptor pipeDesc;
        pipeDesc._shaderProgramHandle = s_cullShaderGrass->getGUID();
        _cullPipelineGrass = _context.newPipeline(pipeDesc);
        pipeDesc._shaderProgramHandle = s_cullShaderTrees->getGUID();
        _cullPipelineTrees = _context.newPipeline(pipeDesc);
    }

    const U32 ID = _terrainChunk.ID();
    const U32 meshID = to_U32(ID % _treeMeshNames.size());

    if (_instanceCountTrees > 0 && !_treeMeshNames.empty()) {
        UniqueLock<SharedMutex> w_lock(g_treeMeshLock);
        if (s_treeMeshes.empty()) {
            for (const ResourcePath& meshName : _treeMeshNames) {
                if (!eastl::any_of(eastl::cbegin(s_treeMeshes),
                                   eastl::cend(s_treeMeshes),
                                   [&meshName](const Mesh_ptr& ptr) {
                                       return Util::CompareIgnoreCase(ptr->assetName(), meshName);
                                   }))
                {
                    ResourceDescriptor model("Tree");
                    model.assetLocation(Paths::g_assetsLocation + "models");
                    model.flag(true);
                    model.threaded(false); ///< we need the extents asap!
                    model.assetName(meshName);
                    Mesh_ptr meshPtr = CreateResource<Mesh>(_context.parent().resourceCache(), model);
                    meshPtr->setMaterialTpl(s_treeMaterial);
                    // CSM last split should probably avoid rendering trees since it would cover most of the scene :/
                    meshPtr->renderState().addToDrawExclusionMask(RenderStage::SHADOW, RenderPassType::MAIN_PASS, to_U8(LightType::DIRECTIONAL), 2u);
                    for (const SubMesh_ptr& subMesh : meshPtr->subMeshList()) {
                        subMesh->renderState().addToDrawExclusionMask(RenderStage::SHADOW, RenderPassType::MAIN_PASS, to_U8(LightType::DIRECTIONAL), 2u);
                    }
                    s_treeMeshes.push_back(meshPtr);
                }
            }
        }

        Mesh_ptr crtMesh = nullptr;
        {
            SharedLock<SharedMutex> r_lock(g_treeMeshLock);
            crtMesh = s_treeMeshes.front();
            const ResourcePath& meshName = _treeMeshNames[meshID];
            for (const Mesh_ptr& mesh : s_treeMeshes) {
                if (mesh->assetName() == meshName) {
                    crtMesh = mesh;
                    break;
                }
            }
        }
        constexpr U32 normalMask = to_base(ComponentType::TRANSFORM) |
                                   to_base(ComponentType::BOUNDS) |
                                   to_base(ComponentType::NETWORKING) |
                                   to_base(ComponentType::RENDERING);

        SceneGraphNodeDescriptor nodeDescriptor = {};
        nodeDescriptor._componentMask = normalMask;
        nodeDescriptor._usageContext = NodeUsageContext::NODE_STATIC;
        nodeDescriptor._serialize = false;
        nodeDescriptor._instanceCount = _instanceCountTrees;
        nodeDescriptor._node = crtMesh;
        nodeDescriptor._name = Util::StringFormat("Trees_chunk_%d", ID);
        _treeParentNode = sgn->addChildNode(nodeDescriptor);
        _treeParentNode->setFlag(SceneGraphNode::Flags::VISIBILITY_LOCKED);

        TransformComponent* tComp = _treeParentNode->get<TransformComponent>();
        const vec4<F32>& offset = _terrainChunk.getOffsetAndSize();
        tComp->setPositionX(offset.x + offset.z * 0.5f);
        tComp->setPositionZ(offset.y + offset.w * 0.5f);
        tComp->setScale(_treeScales[meshID]);

        _treeParentNode->forEachChild([ID](SceneGraphNode* child, I32 /*childIdx*/) {
            RenderingComponent* rComp = child->get<RenderingComponent>();
            rComp->dataFlag(to_F32(ID));
            rComp->occlusionCull(false);
            return true;
        });

        BoundingBox aabb = _treeParentNode->get<BoundsComponent>()->updateAndGetBoundingBox();
        BoundingSphere bs;
        bs.fromBoundingBox(aabb);

        const vec3<F32>& extents = aabb.getExtent();
        _treeExtents.set(extents, bs.getRadius());
        _grassExtents.w = _grassExtents.xyz.length();
        _cullPushConstants.set(_ID("treeExtents"), GFX::PushConstantType::VEC4, _treeExtents);
    }

    _grassExtents.w = _grassExtents.xyz.length();
    _cullPushConstants.set(_ID("dvd_terrainChunkOffset"), GFX::PushConstantType::UINT, ID);
    _cullPushConstants.set(_ID("grassExtents"), GFX::PushConstantType::VEC4, _grassExtents);

    setState(ResourceState::RES_LOADED);
}

void Vegetation::getStats(U32& maxGrassInstances, U32& maxTreeInstances) const {
    Wait(*_buildTask, _context.context().taskPool(TaskPoolType::HIGH_PRIORITY));

    maxGrassInstances = _instanceCountGrass;
    maxTreeInstances = _instanceCountTrees;
}

void Vegetation::occlusionCull(const RenderStagePass& stagePass,
                               const Texture_ptr& depthBuffer,
                               const Camera& camera,
                               GFX::SendPushConstantsCommand& HIZPushConstantsCMDInOut,
                               GFX::CommandBuffer& bufferInOut) const {
    if (!s_buffersBound || !renderState().drawState(stagePass)) {
        return;
    }
    // Culling lags one full frame
    if (_instanceCountGrass > 0 || _instanceCountTrees > 0) {
        // This will always lag one frame

        GFX::SendPushConstantsCommand cullConstants = { _cullPushConstants };
        GFX::DispatchComputeCommand computeCmd = {};
        GFX::BindPipelineCommand bindPipelineCmd = {};

        if (_instanceCountGrass > 0) {
            computeCmd._computeGroupSize.set((_instanceCountGrass + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

            //Cull grass
            bindPipelineCmd._pipeline = _cullPipelineGrass;
            EnqueueCommand(bufferInOut, bindPipelineCmd);
            EnqueueCommand(bufferInOut, HIZPushConstantsCMDInOut);
            EnqueueCommand(bufferInOut, cullConstants);
            EnqueueCommand(bufferInOut, computeCmd);
        }

        if (_instanceCountTrees > 0) {
            computeCmd._computeGroupSize.set((_instanceCountTrees + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

            // Cull trees
            bindPipelineCmd._pipeline = _cullPipelineTrees;
            EnqueueCommand(bufferInOut, bindPipelineCmd);
            EnqueueCommand(bufferInOut, HIZPushConstantsCMDInOut);
            EnqueueCommand(bufferInOut, cullConstants);
            EnqueueCommand(bufferInOut, computeCmd);
        }

        GFX::MemoryBarrierCommand memCmd = {};
        memCmd._barrierMask = to_base(MemoryBarrierType::SHADER_STORAGE);
        EnqueueCommand(bufferInOut, memCmd);
    }


    SceneNode::occlusionCull(stagePass, depthBuffer, camera, HIZPushConstantsCMDInOut, bufferInOut);
}

void Vegetation::sceneUpdate(const U64 deltaTimeUS,
                             SceneGraphNode* sgn,
                             SceneState& sceneState) {
    OPTICK_EVENT();

    if (!renderState().drawState()) {
        uploadVegetationData(sgn);
        sgn->get<RenderingComponent>()->dataFlag(to_F32(_terrainChunk.ID()));
    }

    if (!s_buffersBound) {
        if (s_treeData != nullptr) {
            s_treeData->bind(ShaderBufferLocation::TREE_DATA);
        }
        if (s_grassData != nullptr) {
            s_grassData->bind(ShaderBufferLocation::GRASS_DATA);
        }
        s_treePositions.clear();
        s_grassPositions.clear();
        s_buffersBound = true;
    }

    if (renderState().drawState()) {
        assert(getState() == ResourceState::RES_LOADED);
        // Query shadow state every "_stateRefreshInterval" microseconds
        if (_stateRefreshIntervalBufferUS >= _stateRefreshIntervalUS) {
            _windX = sceneState.windDirX();
            _windZ = sceneState.windDirZ();
            _windS = sceneState.windSpeed();
            _stateRefreshIntervalBufferUS -= _stateRefreshIntervalUS;
        }
        _stateRefreshIntervalBufferUS += deltaTimeUS;

        const SceneRenderState& renderState = sceneState.renderState();

        const F32 sceneRenderRange = renderState.generalVisibility();
        const F32 sceneGrassDistance = std::min(renderState.grassVisibility(), sceneRenderRange);
        const F32 sceneTreeDistance = std::min(renderState.treeVisibility(), sceneRenderRange);
        if (!COMPARE(sceneGrassDistance, _grassDistance)) {
            _grassDistance = sceneGrassDistance;
            _cullPushConstants.set(_ID("dvd_grassVisibilityDistance"), GFX::PushConstantType::FLOAT, _grassDistance);
            sgn->get<RenderingComponent>()->setMaxRenderRange(_grassDistance);
        }
        if (!COMPARE(sceneTreeDistance, _treeDistance)) {
            _treeDistance = sceneTreeDistance;
            _cullPushConstants.set(_ID("dvd_treeVisibilityDistance"), GFX::PushConstantType::FLOAT, _treeDistance);
            if (_treeParentNode != nullptr) {
                _treeParentNode->forEachChild([sceneTreeDistance](SceneGraphNode* child, I32 /*childIdx*/) {
                    RenderingComponent* rComp = child->get<RenderingComponent>();
                    rComp->setMaxRenderRange(sceneTreeDistance);
                    return true;
                });
            }
        }
    }

    SceneNode::sceneUpdate(deltaTimeUS, sgn, sceneState);
}


void Vegetation::buildDrawCommands(SceneGraphNode* sgn,
                                   const RenderStagePass& renderStagePass,
                                   const Camera& crtCamera,
                                   RenderPackage& pkgInOut) {

    const U16 partitionID = s_lodPartitions[0];

    GenericDrawCommand cmd = {};
    cmd._primitiveType = PrimitiveType::TRIANGLE_STRIP;
    cmd._sourceBuffer = s_buffer->handle();
    cmd._cmd.primCount = _instanceCountGrass;
    cmd._cmd.indexCount = to_U32(s_buffer->getPartitionIndexCount(partitionID));
    cmd._cmd.firstIndex = to_U32(s_buffer->getPartitionOffset(partitionID));
    pkgInOut.add(GFX::DrawCommand{ cmd });

    U16 prevID = 0;
    for (U8 i = 0; i < to_U8(s_lodPartitions.size()); ++i) {
        U16 id = s_lodPartitions[i];
        if (id == std::numeric_limits<U16>::max()) {
            assert(i > 0);
            id = prevID;
        }
        pkgInOut.setLoDIndexOffset(i, s_buffer->getPartitionOffset(id), s_buffer->getPartitionIndexCount(id));
        prevID = id;
    }

    SceneNode::buildDrawCommands(sgn, renderStagePass, crtCamera, pkgInOut);
}

namespace {
    FORCE_INLINE U8 BestIndex(const UColour4& in) {
        U8 maxValue = 0;
        U8 bestIndex = 0;
        for (U8 i = 0; i < 4; ++i) {
            if (in[i] > maxValue) {
                maxValue = in[i];
                bestIndex = i;
            }
        }

        return bestIndex;
    }

    FORCE_INLINE bool ScaleAndCheckBounds(const vec2<F32>& chunkPos, const vec2<F32>& chunkSize, vec2<F32>& point) {
        if (point.x > -chunkSize.x && point.x < chunkSize.x && 
            point.y > -chunkSize.y && point.y < chunkSize.y) 
        {
            // [-chunkSize * 0.5f, chunkSize * 0.5f] to [0, chunkSize]
            point = (point + chunkSize) * 0.5f;
            point += chunkPos;
            return true;
        }

        return false;
    }
};

void Vegetation::computeVegetationTransforms(bool treeData) {
    // Grass disabled
    if (!treeData && !_context.context().config().debug.enableGrassInstances) {
        return;
    }
    // Trees disabled
    if (treeData && !_context.context().config().debug.enableTreeInstances) {
        return;
    }

    const Terrain& terrain = _terrainChunk.parent();
    const U32 ID = _terrainChunk.ID();

    const stringImpl cacheFileName = Util::StringFormat("%s_%s_%s_%d.cache", terrain.resourceName().c_str(), resourceName().c_str(), treeData ? "trees" : "grass", ID);
    Console::printfn(Locale::Get(treeData ? _ID("CREATE_TREE_START") : _ID("CREATE_GRASS_BEGIN")), ID);

    vectorEASTL<VegetationData>& container = treeData ? _tempTreeData : _tempGrassData;

    ByteBuffer chunkCache;
    if (_context.context().config().debug.useVegetationCache && chunkCache.loadFromFile((Paths::g_cacheLocation + Paths::g_terrainCacheLocation).c_str(), cacheFileName.c_str())) {
        container.resize(chunkCache.read<size_t>());
        chunkCache.read(reinterpret_cast<Byte*>(container.data()), sizeof(VegetationData) * container.size());
    } else {

        std::discrete_distribution<> distribution[] = {
            {5, 2, 2, 1},
            {1, 5, 2, 2},
            {2, 1, 5, 2},
            {2, 2, 1, 5}
        };

        std::default_random_engine generator(to_U32(std::chrono::system_clock::now().time_since_epoch().count()));

        const U32 meshID = to_U32(ID % _treeMeshNames.size());

        const vec2<F32>& chunkSize = _terrainChunk.getOffsetAndSize().zw;
        const vec2<F32>& chunkPos = _terrainChunk.getOffsetAndSize().xy;
        //const F32 waterLevel = 0.0f;// ToDo: make this dynamic! (cull underwater points later on?)
        auto& map = treeData ? _treeMap : _grassMap;
        const U16 mapWidth = map->dimensions(0u, 0u).width;
        const U16 mapHeight = map->dimensions(0u, 0u).height;
        const auto& positions = treeData ? s_treePositions : s_grassPositions;
        const auto& scales = treeData ? _treeScales : _grassScales;
        const F32 slopeLimit = treeData ? g_slopeLimitTrees : g_slopeLimitGrass;

        for (vec2<F32> pos : positions) {
            if (!ScaleAndCheckBounds(chunkPos, chunkSize, pos)) {
                continue;
            }

            const vec2<F32> mapCoord(pos.x + mapWidth * 0.5f, pos.y + mapHeight * 0.5f);

            const F32 x_fac = mapCoord.x / mapWidth;
            const F32 y_fac = mapCoord.y / mapHeight;

            const Terrain::Vert vert = terrain.getVert(x_fac, y_fac, true);

            // terrain slope should be taken into account
            const F32 dot = Dot(vert._normal, WORLD_Y_AXIS);
            //const F32 lengthSq1 = vert._normal.lengthSquared(); = 1.0f (normalised)
            //const F32 lengthSq2 = WORLD_Y_AXIS.lengthSquared(); = 1.0f (normalised)
            //const F32 length = Divide::Sqrt(lengthSq1 * lengthSq2); = 1.0f
            constexpr F32 length = 1.0f;
            const Angle::DEGREES<F32> angle = Angle::to_DEGREES(std::acos(dot / length));
            if (angle > slopeLimit) {
                continue;
            }
            const F32 slopeScaleFactor = 1.f - MAP(angle, 0.f, slopeLimit, 0.f, 0.9f);

            assert(vert._position != VECTOR3_ZERO);

            const UColour4 colour = map->getColour(to_I32(mapCoord.x), to_I32(mapCoord.y));
            const U8 index = BestIndex(colour);
            const F32 colourVal = colour[index];
            if (colourVal <= std::numeric_limits<F32>::epsilon()) {
                continue;
            }
            
            const U8 arrayLayer = to_U8(distribution[index](generator));

            const F32 xmlScale = scales[treeData ? meshID : index];
            // Don't go under 75% of the scale specified in the data files
            const F32 minXmlScale = xmlScale * 7.5f / 10.0f;
            // Don't go over 75% of the scale specified in the data files
            const F32 maxXmlScale = xmlScale * 1.25f;

            const F32 scale = CLAMPED((colourVal + 1.f) / 256.0f * xmlScale, minXmlScale, maxXmlScale) * 
                              slopeScaleFactor;

            assert(scale > std::numeric_limits<F32>::epsilon());

            //vert._position.y = (((0.0f*heightExtent) + vert._position.y) - ((0.0f*scale) + vert._position.y)) + vert._position.y;
            VegetationData entry = {};
            entry._positionAndScale.set(vert._position, scale);
            Quaternion<F32> modelRotation;
            if (treeData) {
                modelRotation.fromEuler(_treeRotations[meshID]);
            } else {
                modelRotation = RotationFromVToU(WORLD_Y_AXIS, vert._normal, WORLD_Z_NEG_AXIS);
            }

            entry._orientationQuat = (Quaternion<F32>(vert._normal, Random(360.0f)) * modelRotation).asVec4();
            entry._data = {
                to_F32(arrayLayer),
                to_F32(ID),
                1.0f,
                1.0f
            };

            container.push_back(entry);
        }

        container.shrink_to_fit();
        chunkCache << container.size();
        chunkCache.append(container.data(), container.size());
        if (!chunkCache.dumpToFile((Paths::g_cacheLocation + Paths::g_terrainCacheLocation).c_str(), cacheFileName.c_str())) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }
    Console::printfn(Locale::Get(_ID("CREATE_GRASS_END")));
}

}
