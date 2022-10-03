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
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"
#include "Geometry/Shapes/Headers/Mesh.h"
#include "Geometry/Shapes/Headers/SubMesh.h"
#include "Geometry/Material/Headers/Material.h"
#include "Environment/Terrain/Headers/Terrain.h"
#include "Environment/Terrain/Headers/TerrainChunk.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"
#include "Platform/Video/Headers/CommandBufferPool.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/File/Headers/FileManagement.h"

#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"

namespace Divide
{

    namespace
    {
        constexpr U16 BYTE_BUFFER_VERSION = 1u;

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

    Material_ptr Vegetation::s_treeMaterial = nullptr;
    Material_ptr Vegetation::s_vegetationMaterial = nullptr;

    eastl::unordered_set<vec2<F32>> Vegetation::s_treePositions;
    eastl::unordered_set<vec2<F32>> Vegetation::s_grassPositions;
    ShaderBuffer_uptr Vegetation::s_treeData = nullptr;
    ShaderBuffer_uptr Vegetation::s_grassData = nullptr;
    VertexBuffer_ptr Vegetation::s_buffer = nullptr;
    ShaderProgram_ptr Vegetation::s_cullShaderGrass = nullptr;
    ShaderProgram_ptr Vegetation::s_cullShaderTrees = nullptr;
    vector<Mesh_ptr> Vegetation::s_treeMeshes;
    std::atomic_uint Vegetation::s_bufferUsage = 0;
    U32 Vegetation::s_maxChunks = 0u;
    std::array<U16, 3> Vegetation::s_lodPartitions;

    //Per-chunk
    U32 Vegetation::s_maxGrassInstances = 0u;
    U32 Vegetation::s_maxTreeInstances = 0u;

    Vegetation::Vegetation( GFXDevice& context,
                            TerrainChunk& parentChunk,
                            const VegetationDetails& details )
        : SceneNode( context.parent().resourceCache(),
                     parentChunk.parent().descriptorHash() + parentChunk.ID(),
                     details.name,
                     ResourcePath{ details.name + "_" + Util::to_string( parentChunk.ID() ) },
                     {},
                     SceneNodeType::TYPE_VEGETATION,
                     to_base( ComponentType::TRANSFORM ) | to_base( ComponentType::BOUNDS ) | to_base( ComponentType::RENDERING ) ),

        _context( context ),
        _terrainChunk( parentChunk ),
        _terrain( details.parentTerrain ),
        _grassScales( details.grassScales ),
        _treeScales( details.treeScales ),
        _treeRotations( details.treeRotations ),
        _grassMap( details.grassMap ),
        _treeMap( details.treeMap )
    {
        _treeMeshNames.insert( cend( _treeMeshNames ), cbegin( details.treeMeshes ), cend( details.treeMeshes ) );

        assert( !_grassMap->imageLayers().empty() && !_treeMap->imageLayers().empty() );

        setBounds( parentChunk.bounds() );

        renderState().addToDrawExclusionMask( RenderStage::REFLECTION );
        renderState().addToDrawExclusionMask( RenderStage::REFRACTION );
        renderState().addToDrawExclusionMask( RenderStage::SHADOW, RenderPassType::COUNT, static_cast<RenderStagePass::VariantType>(LightType::POINT) );
        renderState().addToDrawExclusionMask( RenderStage::SHADOW, RenderPassType::COUNT, static_cast<RenderStagePass::VariantType>(LightType::SPOT) );
        for ( U8 i = 1u; i < Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT; ++i )
        {
            renderState().addToDrawExclusionMask(
                RenderStage::SHADOW,
                RenderPassType::COUNT,
                static_cast<RenderStagePass::VariantType>(LightType::DIRECTIONAL),
                g_AllIndicesID,
                static_cast<RenderStagePass::PassIndex>(i) );
        }
        // Because we span an entire terrain chunk, LoD calculation will always be off unless we use the closest possible point to the camera
        renderState().useBoundsCenterForLoD( false );
        renderState().lod0OnCollision( true );
        renderState().drawState( false );

        CachedResource::setState( ResourceState::RES_LOADING );
        _buildTask = CreateTask(
            [this]( const Task& /*parentTask*/ )
            {
                s_bufferUsage.fetch_add( 1 );
                computeVegetationTransforms( false );
                computeVegetationTransforms( true );
                _instanceCountGrass = to_U32( _tempGrassData.size() );
                _instanceCountTrees = to_U32( _tempTreeData.size() );
            } );

        Start( *_buildTask, _context.context().taskPool( TaskPoolType::HIGH_PRIORITY ), TaskPriority::DONT_CARE );

        EditorComponentField instanceCountGrassField = {};
        instanceCountGrassField._name = "Num Grass Instances";
        instanceCountGrassField._data = &_instanceCountGrass;
        instanceCountGrassField._type = EditorComponentFieldType::PUSH_TYPE;
        instanceCountGrassField._readOnly = true;
        instanceCountGrassField._basicType = GFX::PushConstantType::UINT;
        _editorComponent.registerField( MOV( instanceCountGrassField ) );

        EditorComponentField visDistanceGrassField = {};
        visDistanceGrassField._name = "Grass Draw distance";
        visDistanceGrassField._data = &_grassDistance;
        visDistanceGrassField._type = EditorComponentFieldType::PUSH_TYPE;
        visDistanceGrassField._readOnly = true;
        visDistanceGrassField._basicType = GFX::PushConstantType::FLOAT;
        _editorComponent.registerField( MOV( visDistanceGrassField ) );

        EditorComponentField instanceCountTreesField = {};
        instanceCountTreesField._name = "Num Tree Instances";
        instanceCountTreesField._data = &_instanceCountTrees;
        instanceCountTreesField._type = EditorComponentFieldType::PUSH_TYPE;
        instanceCountTreesField._readOnly = true;
        instanceCountTreesField._basicType = GFX::PushConstantType::UINT;
        _editorComponent.registerField( MOV( instanceCountTreesField ) );

        EditorComponentField visDistanceTreesField = {};
        visDistanceTreesField._name = "Tree Draw Instance";
        visDistanceTreesField._data = &_treeDistance;
        visDistanceTreesField._type = EditorComponentFieldType::PUSH_TYPE;
        visDistanceTreesField._readOnly = true;
        visDistanceTreesField._basicType = GFX::PushConstantType::FLOAT;
        _editorComponent.registerField( MOV( visDistanceTreesField ) );

        EditorComponentField terrainIDField = {};
        terrainIDField._name = "Terrain Chunk ID";
        terrainIDField._dataGetter = [this]( void* dataOut ) noexcept
        {
            *static_cast<U32*>(dataOut) = _terrainChunk.ID();
        };
        terrainIDField._type = EditorComponentFieldType::PUSH_TYPE;
        terrainIDField._readOnly = true;
        terrainIDField._basicType = GFX::PushConstantType::UINT;
        _editorComponent.registerField( MOV( terrainIDField ) );

    }

    Vegetation::~Vegetation()
    {
        Console::printfn( Locale::Get( _ID( "UNLOAD_VEGETATION_BEGIN" ) ), resourceName().c_str() );
        U32 timer = 0;
        while ( getState() == ResourceState::RES_LOADING )
        {
            // wait for the loading thread to finish first;
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
            timer += 10;
            if ( timer > 3000 )
            {
                break;
            }
        }
        assert( getState() != ResourceState::RES_LOADING );
        if ( s_bufferUsage.fetch_sub( 1 ) == 1 )
        {
            destroyStaticData();
        }

        Console::printfn( Locale::Get( _ID( "UNLOAD_VEGETATION_END" ) ) );
    }

    void Vegetation::destroyStaticData()
    {
        {
            ScopedLock<SharedMutex> w_lock( g_treeMeshLock );
            s_treeMeshes.clear();
        }
        s_treeMaterial.reset();
        s_vegetationMaterial.reset();
        s_cullShaderGrass.reset();
        s_cullShaderTrees.reset();
        s_treeData.reset();
        s_grassData.reset();
        s_buffer.reset();
    }

    void Vegetation::precomputeStaticData( GFXDevice& gfxDevice, const U32 chunkSize, const U32 maxChunkCount )
    {
        // Make sure this is ONLY CALLED FROM THE MAIN LOADING THREAD. All instances should call this in a serialized fashion
        if ( s_buffer == nullptr )
        {
            s_lodPartitions.fill( 0u );

            constexpr F32 offsetBottom0 = 0.20f;
            constexpr F32 offsetBottom1 = 0.10f;

            const mat4<F32> transform[] = {
                mat4<F32>{
                    vec3<F32>( -offsetBottom0, 0.f, -offsetBottom0 ),
                    VECTOR3_UNIT,
                    GetMatrix( Quaternion<F32>( Angle::DEGREES<F32>( 25.f ), Angle::DEGREES<F32>( 0.f ), Angle::DEGREES<F32>( 0.f ) ) )
                },

                mat4<F32>{
                    vec3<F32>( -offsetBottom1, 0.f, offsetBottom1 ),
                    vec3<F32>( 0.85f ),
                    GetMatrix( Quaternion<F32>( Angle::DEGREES<F32>( -12.5f ), Angle::DEGREES<F32>( 0.f ),  Angle::DEGREES<F32>( 0.f ) )* //Pitch
                              Quaternion<F32>( Angle::DEGREES<F32>( 0.f ),    Angle::DEGREES<F32>( 35.f ), Angle::DEGREES<F32>( 0.f ) ) )  //Yaw
                },

                mat4<F32>{
                    vec3<F32>( offsetBottom0, 0.f, -offsetBottom1 ),
                    vec3<F32>( 1.1f ),
                    GetMatrix( Quaternion<F32>( Angle::DEGREES<F32>( 30.f ), Angle::DEGREES<F32>( 0.f ),   Angle::DEGREES<F32>( 0.f ) )* //Pitch
                              Quaternion<F32>( Angle::DEGREES<F32>( 0.f ),  Angle::DEGREES<F32>( -75.f ), Angle::DEGREES<F32>( 0.f ) ) )  //Yaw
                },

                mat4<F32>{
                    vec3<F32>( offsetBottom1 * 2, 0.f, offsetBottom1 ),
                    vec3<F32>( 0.9f ),
                    GetMatrix( Quaternion<F32>( Angle::DEGREES<F32>( -25.f ), Angle::DEGREES<F32>( 0.f ),    Angle::DEGREES<F32>( 0.f ) )* //Pitch
                              Quaternion<F32>( Angle::DEGREES<F32>( 0.f ),   Angle::DEGREES<F32>( -125.f ), Angle::DEGREES<F32>( 0.f ) ) )  //Yaw
                },

                mat4<F32>{
                    vec3<F32>( -offsetBottom1 * 2, 0.f, -offsetBottom1 * 2 ),
                    vec3<F32>( 1.2f ),
                    GetMatrix( Quaternion<F32>( Angle::DEGREES<F32>( 5.f ), Angle::DEGREES<F32>( 0.f ),    Angle::DEGREES<F32>( 0.f ) )* //Pitch
                              Quaternion<F32>( Angle::DEGREES<F32>( 0.f ), Angle::DEGREES<F32>( -225.f ), Angle::DEGREES<F32>( 0.f ) ) )  //Yaw
                },

                mat4<F32>{
                    vec3<F32>( offsetBottom0, 0.f, offsetBottom1 * 2 ),
                    vec3<F32>( 0.75f ),
                    GetMatrix( Quaternion<F32>( Angle::DEGREES<F32>( -15.f ), Angle::DEGREES<F32>( 0.f ),   Angle::DEGREES<F32>( 0.f ) )* //Pitch
                              Quaternion<F32>( Angle::DEGREES<F32>( 0.f ),   Angle::DEGREES<F32>( 305.f ), Angle::DEGREES<F32>( 0.f ) ) )  //Yaw
                }
            };

            vector<vec3<F32>> vertices{};

            constexpr U8 billboardsPlaneCount = to_U8( sizeof( transform ) / sizeof( transform[0] ) );
            vertices.reserve( billboardsPlaneCount * 4 );

            for ( U8 i = 0u; i < billboardsPlaneCount; ++i )
            {
                vertices.push_back( transform[i] * vec4<F32>( -1.f, 0.f, 0.f, 1.f ) ); //BL
                vertices.push_back( transform[i] * vec4<F32>( -1.f, 1.f, 0.f, 1.f ) ); //TL
                vertices.push_back( transform[i] * vec4<F32>( 1.f, 1.f, 0.f, 1.f ) ); //TR
                vertices.push_back( transform[i] * vec4<F32>( 1.f, 0.f, 0.f, 1.f ) ); //BR
            };

            const U16 indices[] = { 0, 1, 2,
                                    0, 2, 3 };

            const vec2<F32> texCoords[] = {
                vec2<F32>( 0.f, 0.f ),
                vec2<F32>( 0.f, 1.f ),
                vec2<F32>( 1.f, 1.f ),
                vec2<F32>( 1.f, 0.f )
            };

            s_buffer = gfxDevice.newVB( "Vegetation" );
            s_buffer->useLargeIndices( false );
            s_buffer->setVertexCount( vertices.size() );

            for ( U8 i = 0u; i < to_U8( vertices.size() ); ++i )
            {
                s_buffer->modifyPositionValue( i, vertices[i] );
                s_buffer->modifyNormalValue( i, WORLD_Y_AXIS );
                s_buffer->modifyTangentValue( i, WORLD_X_AXIS );
                s_buffer->modifyTexCoordValue( i, texCoords[i % 4].s, texCoords[i % 4].t );
            }

            const auto addPlanes = [&indices]( const U8 count )
            {
                for ( U8 i = 0u; i < count; ++i )
                {
                    if ( i > 0 )
                    {
                        s_buffer->addRestartIndex();
                    }
                    for ( const U16 idx : indices )
                    {
                        s_buffer->addIndex( idx + i * 4 );
                    }
                }
            };

            for ( U8 i = 0; i < s_lodPartitions.size(); ++i )
            {
                addPlanes( billboardsPlaneCount / (i + 1) );
                s_lodPartitions[i] = s_buffer->partitionBuffer();
            }

            s_buffer->create( true, false );
        }

        //ref: http://mollyrocket.com/casey/stream_0016.html
        s_grassPositions.reserve( to_size( chunkSize ) * chunkSize );
        s_treePositions.reserve( to_size( chunkSize ) * chunkSize );

        const F32 posOffset = to_F32( chunkSize * 2 );

        vec2<F32> intersections[2]{};
        Util::Circle circleA{}, circleB{};
        circleA.center[0] = circleB.center[0] = -posOffset;
        circleA.center[1] = -posOffset;
        circleB.center[1] = posOffset;

        const F32 dR[2] = { g_distanceRingsBaseGrass * g_PointRadiusBaseGrass,
                            g_distanceRingsBaseTrees * g_PointRadiusBaseTrees };

        for ( U8 i = 0; i < 2; ++i )
        {
            for ( I16 RadiusStepA = 0; RadiusStepA < g_maxRadiusSteps; ++RadiusStepA )
            {
                const F32 Ar = g_ArBase + dR[i] * to_F32( RadiusStepA );
                for ( I16 RadiusStepB = 0; RadiusStepB < g_maxRadiusSteps; ++RadiusStepB )
                {
                    const F32 Br = g_BrBase + dR[i] * to_F32( RadiusStepB );
                    circleA.radius = Ar + (RadiusStepB % 3 ? 0.0f : 0.3f * dR[i]);
                    circleB.radius = Br + (RadiusStepA % 3 ? 0.0f : 0.3f * dR[i]);
                    // Intersect circle Ac,UseAr and Bc,UseBr
                    if ( IntersectCircles( circleA, circleB, intersections ) )
                    {
                        // Add the resulting points if they are within the pattern bounds
                        for ( const vec2<F32>& record : intersections )
                        {
                            if ( IS_IN_RANGE_EXCLUSIVE( record.x, -to_F32( chunkSize ), to_F32( chunkSize ) ) &&
                                 IS_IN_RANGE_EXCLUSIVE( record.y, -to_F32( chunkSize ), to_F32( chunkSize ) ) )
                            {
                                if ( i == 0 )
                                {
                                    s_grassPositions.insert( record );
                                }
                                else
                                {
                                    s_treePositions.insert( record );
                                }
                            }
                        }
                    }
                }
            }
        }

        s_maxChunks = maxChunkCount;
    }

    void Vegetation::createVegetationMaterial( GFXDevice& gfxDevice, const Terrain_ptr& terrain, const VegetationDetails& vegDetails )
    {
        if ( vegDetails.billboardCount == 0 )
        {
            return;
        }

        assert( s_maxGrassInstances != 0u && "Vegetation error: call \"precomputeStaticData\" first!" );

        std::atomic_uint loadTasks = 0u;
        Material::ShaderData treeShaderData = {};
        treeShaderData._depthShaderVertSource = "tree";
        treeShaderData._depthShaderVertVariant = "";
        treeShaderData._colourShaderVertSource = "tree";
        treeShaderData._colourShaderVertVariant = "";

        ResourceDescriptor matDesc( "Tree_material" );
        s_treeMaterial = CreateResource<Material>( gfxDevice.parent().resourceCache(), matDesc );
        s_treeMaterial->baseShaderData( treeShaderData );
        s_treeMaterial->properties().shadingMode( ShadingMode::BLINN_PHONG );
        s_treeMaterial->properties().isInstanced( true );
        s_treeMaterial->addShaderDefine( ShaderType::COUNT, Util::StringFormat( "MAX_TREE_INSTANCES %d", s_maxTreeInstances ).c_str() );

        SamplerDescriptor grassSampler = {};
        grassSampler.wrapUVW( TextureWrap::CLAMP_TO_EDGE );
        grassSampler.anisotropyLevel( 8 );

        TextureDescriptor grassTexDescriptor( TextureType::TEXTURE_2D_ARRAY );
        grassTexDescriptor.srgb( true );

        ResourceDescriptor vegetationBillboards( "Vegetation Billboards" );
        vegetationBillboards.assetLocation( Paths::g_assetsLocation + terrain->descriptor()->getVariable( "vegetationTextureLocation" ) );
        vegetationBillboards.assetName( ResourcePath{ vegDetails.billboardTextureArray } );
        vegetationBillboards.propertyDescriptor( grassTexDescriptor );
        vegetationBillboards.waitForReady( false );
        Texture_ptr grassBillboardArray = CreateResource<Texture>( terrain->parentResourceCache(), vegetationBillboards, loadTasks );

        ResourceDescriptor vegetationMaterial( "grassMaterial" );
        Material_ptr vegMaterial = CreateResource<Material>( terrain->parentResourceCache(), vegetationMaterial );
        vegMaterial->properties().shadingMode( ShadingMode::BLINN_PHONG );
        vegMaterial->properties().baseColour( DefaultColours::WHITE );
        vegMaterial->properties().roughness( 0.9f );
        vegMaterial->properties().metallic( 0.02f );
        vegMaterial->properties().doubleSided( true );
        vegMaterial->properties().isStatic( false );
        vegMaterial->properties().isInstanced( true );
        vegMaterial->setPipelineLayout( PrimitiveTopology::TRIANGLE_STRIP, s_buffer->generateAttributeMap() );

        ShaderModuleDescriptor compModule = {};
        compModule._moduleType = ShaderType::COMPUTE;
        compModule._sourceFile = "instanceCullVegetation.glsl";
        compModule._defines.emplace_back( Util::StringFormat( "WORK_GROUP_SIZE %d", WORK_GROUP_SIZE ) );
        compModule._defines.emplace_back( Util::StringFormat( "MAX_TREE_INSTANCES %d", s_maxTreeInstances ) );
        compModule._defines.emplace_back( Util::StringFormat( "MAX_GRASS_INSTANCES %d", s_maxGrassInstances ) );
        ShaderProgramDescriptor shaderCompDescriptor = {};
        shaderCompDescriptor._modules.push_back( compModule );

        ResourceDescriptor instanceCullShaderGrass( "instanceCullVegetation_Grass" );
        instanceCullShaderGrass.waitForReady( false );
        instanceCullShaderGrass.propertyDescriptor( shaderCompDescriptor );
        s_cullShaderGrass = CreateResource<ShaderProgram>( terrain->parentResourceCache(), instanceCullShaderGrass, loadTasks );

        compModule._defines.emplace_back( "CULL_TREES" );
        shaderCompDescriptor = {};
        shaderCompDescriptor._modules.push_back( compModule );

        ResourceDescriptor instanceCullShaderTrees( "instanceCullVegetation_Trees" );
        instanceCullShaderTrees.waitForReady( false );
        instanceCullShaderTrees.propertyDescriptor( shaderCompDescriptor );
        s_cullShaderTrees = CreateResource<ShaderProgram>( terrain->parentResourceCache(), instanceCullShaderTrees, loadTasks );

        WAIT_FOR_CONDITION( loadTasks.load() == 0u );
        DIVIDE_ASSERT( grassBillboardArray->numLayers() == vegDetails.billboardCount );

        vegMaterial->computeShaderCBK( []( [[maybe_unused]] Material* material, const RenderStagePass stagePass )
                                       {
                                           ShaderProgramDescriptor shaderDescriptor = {};
                                           shaderDescriptor._modules.emplace_back( ShaderType::VERTEX, "grass.glsl" );
                                           shaderDescriptor._globalDefines.emplace_back( "ENABLE_TBN" );
                                           shaderDescriptor._globalDefines.emplace_back( Util::StringFormat( "MAX_GRASS_INSTANCES %d", s_maxGrassInstances ) );

                                           ShaderModuleDescriptor fragModule{ ShaderType::FRAGMENT, "grass.glsl" };
                                           if ( IsDepthPass( stagePass ) )
                                           {
                                               if ( stagePass._stage == RenderStage::DISPLAY )
                                               {
                                                   fragModule._variant = "PrePass";

                                                   shaderDescriptor._modules.push_back( fragModule );
                                                   shaderDescriptor._name = "grassPrePass";
                                               }
                                               else if ( stagePass._stage == RenderStage::SHADOW )
                                               {
                                                   fragModule._variant = "Shadow.VSM";

                                                   shaderDescriptor._modules.push_back( fragModule );
                                                   shaderDescriptor._name = "grassShadow";
                                               }
                                               else
                                               {
                                                   shaderDescriptor._name = "grassDepth";
                                               }
                                           }
                                           else
                                           {
                                               if ( stagePass._passType == RenderPassType::OIT_PASS )
                                               {
                                                   fragModule._variant = "Colour.OIT";
                                                   shaderDescriptor._name = "grassColourOIT";
                                               }
                                               else
                                               {
                                                   fragModule._variant = "Colour";
                                                   shaderDescriptor._name = "GrassColour";
                                               }
                                               shaderDescriptor._modules.push_back( fragModule );
                                           }

                                           return shaderDescriptor;
                                       } );

        vegMaterial->setTexture( TextureSlot::UNIT0, grassBillboardArray, grassSampler.getHash(), TextureOperation::REPLACE, true );
        s_vegetationMaterial = vegMaterial;
    }

    void Vegetation::createAndUploadGPUData( GFXDevice& gfxDevice, const Terrain_ptr& terrain, const VegetationDetails& vegDetails )
    {
        assert( s_grassData == nullptr );
        for ( TerrainChunk* chunk : terrain->terrainChunks() )
        {
            chunk->initializeVegetation( gfxDevice, vegDetails );
        }

        terrain->getVegetationStats( s_maxGrassInstances, s_maxTreeInstances );

        if ( s_maxTreeInstances > 0 || s_maxGrassInstances > 0 )
        {
            s_maxGrassInstances += s_maxGrassInstances % WORK_GROUP_SIZE;
            s_maxTreeInstances += s_maxTreeInstances % WORK_GROUP_SIZE;

            vector<Byte> grassData( s_maxGrassInstances * s_maxChunks * sizeof( VegetationData ) );
            vector<Byte> treeData( s_maxTreeInstances * s_maxChunks * sizeof( VegetationData ) );

            for ( TerrainChunk* chunk : terrain->terrainChunks() )
            {
                Vegetation& veg = *chunk->getVegetation().get();
                veg.uploadVegetationData( grassData, treeData );
            }

            ShaderBufferDescriptor bufferDescriptor = {};
            bufferDescriptor._usage = ShaderBuffer::Usage::UNBOUND_BUFFER;
            bufferDescriptor._bufferParams._elementSize = sizeof( VegetationData );
            bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::ONCE;
            bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::GPU_R_GPU_W;

            if ( s_maxTreeInstances > 0 )
            {
                bufferDescriptor._bufferParams._elementCount = to_U32( s_maxTreeInstances * s_maxChunks );
                bufferDescriptor._name = "Tree_data";
                bufferDescriptor._initialData = { treeData.data(), treeData.size() };
                s_treeData = gfxDevice.newSB( bufferDescriptor );
            }
            if ( s_maxGrassInstances > 0 )
            {
                bufferDescriptor._bufferParams._elementCount = to_U32( s_maxGrassInstances * s_maxChunks );
                bufferDescriptor._name = "Grass_data";
                bufferDescriptor._initialData = { grassData.data(), grassData.size() };
                s_grassData = gfxDevice.newSB( bufferDescriptor );
            }

            createVegetationMaterial( gfxDevice, terrain, vegDetails );
        }
        s_treePositions.clear();
        s_grassPositions.clear();
    }

    void Vegetation::uploadVegetationData( vector<Byte>& grassDataOut, vector<Byte> treeDataOut )
    {
        OPTICK_EVENT();

        assert( s_buffer != nullptr );
        Wait( *_buildTask, _context.context().taskPool( TaskPoolType::HIGH_PRIORITY ) );

        if ( _instanceCountGrass > 0u )
        {
            if ( _terrainChunk.ID() < s_maxChunks )
            {
                std::memcpy( &grassDataOut[_terrainChunk.ID() * s_maxGrassInstances * sizeof( VegetationData )], _tempGrassData.data(), _instanceCountGrass * sizeof( VegetationData ) );
            }
            else
            {
                Console::errorfn( "Vegetation::uploadGrassData: insufficient buffer space for grass data" );
            }
            _tempGrassData.clear();
        }

        if ( _instanceCountTrees > 0u )
        {
            if ( _terrainChunk.ID() < s_maxChunks )
            {
                std::memcpy( &treeDataOut[_terrainChunk.ID() * s_maxGrassInstances * sizeof( VegetationData )], _tempTreeData.data(), _instanceCountTrees * sizeof( VegetationData ) );
            }
            else
            {
                Console::errorfn( "Vegetation::uploadGrassData: insufficient buffer space for tree data" );
            }
            _tempTreeData.clear();
        }
    }

    void Vegetation::prepareDraw( SceneGraphNode* sgn )
    {
        if ( _instanceCountGrass > 0u || _instanceCountTrees > 0u )
        {
            sgn->get<RenderingComponent>()->primitiveRestartRequired( true );
            sgn->get<RenderingComponent>()->instantiateMaterial( s_vegetationMaterial );
            sgn->get<RenderingComponent>()->occlusionCull( false ); //< We handle our own culling

            WAIT_FOR_CONDITION( s_cullShaderGrass->getState() == ResourceState::RES_LOADED &&
                                s_cullShaderTrees->getState() == ResourceState::RES_LOADED );

            PipelineDescriptor pipeDesc;
            pipeDesc._primitiveTopology = PrimitiveTopology::COMPUTE;
            pipeDesc._shaderProgramHandle = s_cullShaderGrass->handle();
            _cullPipelineGrass = _context.newPipeline( pipeDesc );
            pipeDesc._shaderProgramHandle = s_cullShaderTrees->handle();
            _cullPipelineTrees = _context.newPipeline( pipeDesc );

            renderState().drawState( true );
        }

        const U32 ID = _terrainChunk.ID();
        const U32 meshID = to_U32( ID % _treeMeshNames.size() );

        if ( _instanceCountTrees > 0 && !_treeMeshNames.empty() )
        {
            ScopedLock<SharedMutex> w_lock( g_treeMeshLock );
            if ( s_treeMeshes.empty() )
            {
                for ( const ResourcePath& meshName : _treeMeshNames )
                {
                    if ( !eastl::any_of( eastl::cbegin( s_treeMeshes ),
                                         eastl::cend( s_treeMeshes ),
                                         [&meshName]( const Mesh_ptr& ptr ) noexcept
                                         {
                                             return Util::CompareIgnoreCase( ptr->assetName(), meshName );
                                         } ) )
                    {
                        ResourceDescriptor model( "Tree" );
                        model.assetLocation( Paths::g_assetsLocation + Paths::g_modelsLocation );
                        model.flag( true );
                        model.waitForReady( true );
                        model.assetName( meshName );
                        Mesh_ptr meshPtr = CreateResource<Mesh>( _context.parent().resourceCache(), model );
                        meshPtr->setMaterialTpl( s_treeMaterial );
                        // CSM last split should probably avoid rendering trees since it would cover most of the scene :/
                        meshPtr->renderState().addToDrawExclusionMask(
                            RenderStage::SHADOW,
                            RenderPassType::MAIN_PASS,
                            static_cast<RenderStagePass::VariantType>(LightType::DIRECTIONAL),
                            g_AllIndicesID,
                            RenderStagePass::PassIndex::PASS_2 );
                        s_treeMeshes.push_back( meshPtr );
                    }
                }
            }

            Mesh_ptr crtMesh = nullptr;
            {
                SharedLock<SharedMutex> r_lock( g_treeMeshLock );
                crtMesh = s_treeMeshes.front();
                const ResourcePath& meshName = _treeMeshNames[meshID];
                for ( const Mesh_ptr& mesh : s_treeMeshes )
                {
                    if ( mesh->assetName() == meshName )
                    {
                        crtMesh = mesh;
                        break;
                    }
                }
            }
            constexpr U32 normalMask = to_base( ComponentType::TRANSFORM ) |
                to_base( ComponentType::BOUNDS ) |
                to_base( ComponentType::NETWORKING ) |
                to_base( ComponentType::RENDERING );

            SceneGraphNodeDescriptor nodeDescriptor = {};
            nodeDescriptor._componentMask = normalMask;
            nodeDescriptor._usageContext = NodeUsageContext::NODE_STATIC;
            nodeDescriptor._serialize = false;
            nodeDescriptor._instanceCount = _instanceCountTrees;
            nodeDescriptor._node = crtMesh;
            nodeDescriptor._name = Util::StringFormat( "Trees_chunk_%d", ID );
            _treeParentNode = sgn->addChildNode( nodeDescriptor );

            TransformComponent* tComp = _treeParentNode->get<TransformComponent>();
            const vec4<F32>& offset = _terrainChunk.getOffsetAndSize();
            tComp->setPositionX( offset.x + offset.z * 0.5f );
            tComp->setPositionZ( offset.y + offset.w * 0.5f );
            tComp->setScale( _treeScales[meshID] );

            const SceneGraphNode::ChildContainer& children = _treeParentNode->getChildren();
            const U32 childCount = children._count;
            for ( U32 i = 0u; i < childCount; ++i )
            {
                RenderingComponent* rComp = children._data[i]->get<RenderingComponent>();
                rComp->dataFlag( to_F32( ID ) );
                rComp->occlusionCull( false );
            }

            const BoundingBox aabb = _treeParentNode->get<BoundsComponent>()->getBoundingBox();
            BoundingSphere bs;
            bs.fromBoundingBox( aabb );

            const vec3<F32>& extents = aabb.getExtent();
            _treeExtents.set( extents, bs.getRadius() );
            _grassExtents.w = _grassExtents.xyz.length();
        }

        _grassExtents.w = _grassExtents.xyz.length();

        setState( ResourceState::RES_LOADED );
    }

    void Vegetation::getStats( U32& maxGrassInstances, U32& maxTreeInstances ) const
    {
        Wait( *_buildTask, _context.context().taskPool( TaskPoolType::HIGH_PRIORITY ) );

        maxGrassInstances = _instanceCountGrass;
        maxTreeInstances = _instanceCountTrees;
    }

    void Vegetation::prepareRender( SceneGraphNode* sgn,
                                    RenderingComponent& rComp,
                                    RenderPackage& pkg,
                                    RenderStagePass renderStagePass,
                                    const CameraSnapshot& cameraSnapshot,
                                    bool refreshData )
    {
        pkg.pushConstantsCmd()._constants.set( _ID( "dvd_terrainChunkOffset" ), GFX::PushConstantType::UINT, _terrainChunk.ID() );
        
        GFX::CommandBuffer& bufferInOut = *GetCommandBuffer(pkg);
        bufferInOut.clear(false);

        if ( s_grassData || s_treeData )
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_PASS;
            if ( s_grassData )
            {
                auto& binding = cmd->_bindings.emplace_back( ShaderStageVisibility::ALL );
                binding._slot = 6;
                binding._data.As<ShaderBufferEntry>() = { *s_grassData, { 0u, s_grassData->getPrimitiveCount() } };
            }

            if ( s_treeData )
            {
                auto& binding = cmd->_bindings.emplace_back( ShaderStageVisibility::ALL );
                binding._slot = 5;
                binding._data.As<ShaderBufferEntry>() = { *s_treeData, { 0u, s_treeData->getPrimitiveCount() } };
            }
        }

        // Culling lags one full frame
        if ( renderState().drawState( renderStagePass ) &&
             refreshData &&
             (_instanceCountGrass > 0 || _instanceCountTrees > 0) )
        {
            const RenderTargetID hiZSourceTarget = renderStagePass._stage == RenderStage::REFLECTION
                ? RenderTargetNames::HI_Z_REFLECT
                : RenderTargetNames::HI_Z;

            const RenderTarget* hizTarget = _context.renderTargetPool().getRenderTarget( hiZSourceTarget );
            const RTAttachment* hizAttachment = hizTarget->getAttachment( RTAttachmentType::COLOUR );

            if ( hizAttachment != nullptr )
            {
                const Texture_ptr& hizTexture = hizAttachment->texture();

                mat4<F32> viewProjectionMatrix;
                mat4<F32>::Multiply( cameraSnapshot._viewMatrix, cameraSnapshot._projectionMatrix, viewProjectionMatrix );

                PushConstantsStruct fastConstants{};
                fastConstants.data0 = viewProjectionMatrix;
                fastConstants.data1 = cameraSnapshot._viewMatrix;

                GFX::SendPushConstantsCommand cullConstantsCmd{};
                PushConstants& constants = cullConstantsCmd._constants;
                constants.set( _ID( "nearPlane" ), GFX::PushConstantType::FLOAT, cameraSnapshot._zPlanes.min );
                constants.set( _ID( "viewSize" ), GFX::PushConstantType::VEC2, vec2<F32>( hizTexture->width(), hizTexture->height() ) );
                constants.set( _ID( "frustumPlanes" ), GFX::PushConstantType::VEC4, cameraSnapshot._frustumPlanes );
                constants.set( _ID( "cameraPosition" ), GFX::PushConstantType::VEC3, cameraSnapshot._eye );
                constants.set( _ID( "dvd_grassVisibilityDistance" ), GFX::PushConstantType::FLOAT, _grassDistance );
                constants.set( _ID( "dvd_treeVisibilityDistance" ), GFX::PushConstantType::FLOAT, _treeDistance );
                constants.set( _ID( "treeExtents" ), GFX::PushConstantType::VEC4, _treeExtents );
                constants.set( _ID( "grassExtents" ), GFX::PushConstantType::VEC4, _grassExtents );
                constants.set( _ID( "dvd_terrainChunkOffset" ), GFX::PushConstantType::UINT, _terrainChunk.ID() );
                constants.set( fastConstants );

                GFX::EnqueueCommand( bufferInOut, GFX::BeginDebugScopeCommand{ "Occlusion Cull Vegetation" } );

                {
                    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
                    cmd->_usage = DescriptorSetUsage::PER_DRAW;
                    auto& binding = cmd->_bindings.emplace_back( ShaderStageVisibility::COMPUTE );
                    binding._slot = 0;
                    binding._data.As<DescriptorCombinedImageSampler>() = { hizTexture->getView( ImageUsage::SHADER_SAMPLE ), hizAttachment->descriptor()._samplerHash };
                }

                GFX::DispatchComputeCommand computeCmd = {};
                if ( _instanceCountGrass > 0 )
                {
                    computeCmd._computeGroupSize.set( (_instanceCountGrass + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1 );

                    //Cull grass
                    GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = _cullPipelineGrass;
                    GFX::EnqueueCommand( bufferInOut, cullConstantsCmd );
                    GFX::EnqueueCommand( bufferInOut, computeCmd );
                }
                if ( _instanceCountTrees > 0 )
                {
                    computeCmd._computeGroupSize.set( (_instanceCountTrees + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1 );
                    // Cull trees
                    GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = _cullPipelineTrees;
                    GFX::EnqueueCommand( bufferInOut, cullConstantsCmd );
                    GFX::EnqueueCommand( bufferInOut, computeCmd );
                }

                GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_barrierMask = to_base( MemoryBarrierType::SHADER_STORAGE );

                GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
            }
        }

        SceneNode::prepareRender( sgn, rComp, pkg, renderStagePass, cameraSnapshot, refreshData );
    }

    void Vegetation::sceneUpdate( const U64 deltaTimeUS,
                                  SceneGraphNode* sgn,
                                  SceneState& sceneState )
    {
        OPTICK_EVENT();

        if ( !renderState().drawState() )
        {
            prepareDraw( sgn );
            sgn->get<RenderingComponent>()->dataFlag( to_F32( _terrainChunk.ID() ) );
        }
        else
        {
            assert( getState() == ResourceState::RES_LOADED );
            // Query shadow state every "_stateRefreshInterval" microseconds
            if ( _stateRefreshIntervalBufferUS >= _stateRefreshIntervalUS )
            {
                _windX = sceneState.windDirX();
                _windZ = sceneState.windDirZ();
                _windS = sceneState.windSpeed();
                _stateRefreshIntervalBufferUS -= _stateRefreshIntervalUS;
            }
            _stateRefreshIntervalBufferUS += deltaTimeUS;

            const SceneRenderState& renderState = sceneState.renderState();

            const F32 sceneRenderRange = renderState.generalVisibility();
            const F32 sceneGrassDistance = std::min( renderState.grassVisibility(), sceneRenderRange );
            const F32 sceneTreeDistance = std::min( renderState.treeVisibility(), sceneRenderRange );
            if ( !COMPARE( sceneGrassDistance, _grassDistance ) )
            {
                _grassDistance = sceneGrassDistance;
                sgn->get<RenderingComponent>()->setMaxRenderRange( _grassDistance );
            }
            if ( !COMPARE( sceneTreeDistance, _treeDistance ) )
            {
                _treeDistance = sceneTreeDistance;
                if ( _treeParentNode != nullptr )
                {
                    const SceneGraphNode::ChildContainer& children = _treeParentNode->getChildren();
                    const U32 childCount = children._count;
                    for ( U32 i = 0u; i < childCount; ++i )
                    {
                        RenderingComponent* rComp = children._data[i]->get<RenderingComponent>();
                        rComp->setMaxRenderRange( sceneTreeDistance );
                    }
                }
            }
        }

        SceneNode::sceneUpdate( deltaTimeUS, sgn, sceneState );
    }


    void Vegetation::buildDrawCommands( SceneGraphNode* sgn, vector_fast<GFX::DrawCommand>& cmdsOut )
    {

        const U16 partitionID = s_lodPartitions[0];

        GenericDrawCommand cmd = {};
        cmd._sourceBuffer = s_buffer->handle();
        cmd._cmd.primCount = _instanceCountGrass;
        cmd._cmd.indexCount = to_U32( s_buffer->getPartitionIndexCount( partitionID ) );
        cmd._cmd.firstIndex = to_U32( s_buffer->getPartitionOffset( partitionID ) );
        cmdsOut.emplace_back( GFX::DrawCommand{ cmd } );

        RenderingComponent* rComp = sgn->get<RenderingComponent>();
        U16 prevID = 0;
        for ( U8 i = 0; i < to_U8( s_lodPartitions.size() ); ++i )
        {
            U16 id = s_lodPartitions[i];
            if ( id == U16_MAX )
            {
                assert( i > 0 );
                id = prevID;
            }
            rComp->setLoDIndexOffset( i, s_buffer->getPartitionOffset( id ), s_buffer->getPartitionIndexCount( id ) );
            prevID = id;
        }

        SceneNode::buildDrawCommands( sgn, cmdsOut );
    }

    namespace
    {
        FORCE_INLINE U8 BestIndex( const UColour4& in ) noexcept
        {
            U8 maxValue = 0;
            U8 bestIndex = 0;
            for ( U8 i = 0; i < 4; ++i )
            {
                if ( in[i] > maxValue )
                {
                    maxValue = in[i];
                    bestIndex = i;
                }
            }

            return bestIndex;
        }

        FORCE_INLINE bool ScaleAndCheckBounds( const vec2<F32>& chunkPos, const vec2<F32>& chunkSize, vec2<F32>& point ) noexcept
        {
            if ( point.x > -chunkSize.x && point.x < chunkSize.x &&
                 point.y > -chunkSize.y && point.y < chunkSize.y )
            {
                // [-chunkSize * 0.5f, chunkSize * 0.5f] to [0, chunkSize]
                point = (point + chunkSize) * 0.5f;
                point += chunkPos;
                return true;
            }

            return false;
        }
    };

    void Vegetation::computeVegetationTransforms( bool treeData )
    {
        // Grass disabled
        if ( !treeData && !_context.context().config().debug.enableGrassInstances )
        {
            return;
        }
        // Trees disabled
        if ( treeData && !_context.context().config().debug.enableTreeInstances )
        {
            return;
        }

        const Terrain& terrain = _terrainChunk.parent();
        const U32 ID = _terrainChunk.ID();

        const string cacheFileName = Util::StringFormat( "%s_%s_%s_%d.cache", terrain.resourceName().c_str(), resourceName().c_str(), treeData ? "trees" : "grass", ID );
        Console::printfn( Locale::Get( treeData ? _ID( "CREATE_TREE_START" ) : _ID( "CREATE_GRASS_BEGIN" ) ), ID );

        vector<VegetationData>& container = treeData ? _tempTreeData : _tempGrassData;

        ByteBuffer chunkCache;
        if ( _context.context().config().debug.useVegetationCache && chunkCache.loadFromFile( (Paths::g_cacheLocation + Paths::g_terrainCacheLocation).c_str(), cacheFileName.c_str() ) )
        {
            auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
            chunkCache >> tempVer;
            if ( tempVer == BYTE_BUFFER_VERSION )
            {
                container.resize( chunkCache.read<size_t>() );
                chunkCache.read( reinterpret_cast<Byte*>(container.data()), sizeof( VegetationData ) * container.size() );
            }
            else
            {
                chunkCache.clear();
            }
        }
        else
        {

            std::discrete_distribution<> distribution[] = {
                {5, 2, 2, 1},
                {1, 5, 2, 2},
                {2, 1, 5, 2},
                {2, 2, 1, 5}
            };

            std::default_random_engine generator( to_U32( std::chrono::system_clock::now().time_since_epoch().count() ) );

            const U32 meshID = to_U32( ID % _treeMeshNames.size() );

            const vec2<F32>& chunkSize = _terrainChunk.getOffsetAndSize().zw;
            const vec2<F32>& chunkPos = _terrainChunk.getOffsetAndSize().xy;
            //const F32 waterLevel = 0.0f;// ToDo: make this dynamic! (cull underwater points later on?)
            const auto& map = treeData ? _treeMap : _grassMap;
            const U16 mapWidth = map->dimensions( 0u, 0u ).width;
            const U16 mapHeight = map->dimensions( 0u, 0u ).height;
            const auto& positions = treeData ? s_treePositions : s_grassPositions;
            const auto& scales = treeData ? _treeScales : _grassScales;
            const F32 slopeLimit = treeData ? g_slopeLimitTrees : g_slopeLimitGrass;

            for ( vec2<F32> pos : positions )
            {
                if ( !ScaleAndCheckBounds( chunkPos, chunkSize, pos ) )
                {
                    continue;
                }

                const vec2<F32> mapCoord( pos.x + mapWidth * 0.5f, pos.y + mapHeight * 0.5f );

                const F32 x_fac = mapCoord.x / mapWidth;
                const F32 y_fac = mapCoord.y / mapHeight;

                const Terrain::Vert vert = terrain.getVert( x_fac, y_fac, true );

                // terrain slope should be taken into account
                const F32 dot = Dot( vert._normal, WORLD_Y_AXIS );
                //const F32 lengthSq1 = vert._normal.lengthSquared(); = 1.0f (normalised)
                //const F32 lengthSq2 = WORLD_Y_AXIS.lengthSquared(); = 1.0f (normalised)
                //const F32 length = Divide::Sqrt(lengthSq1 * lengthSq2); = 1.0f
                constexpr F32 length = 1.0f;
                const Angle::DEGREES<F32> angle = Angle::to_DEGREES( std::acos( dot / length ) );
                if ( angle > slopeLimit )
                {
                    continue;
                }
                const F32 slopeScaleFactor = 1.f - MAP( angle, 0.f, slopeLimit, 0.f, 0.9f );

                assert( vert._position != VECTOR3_ZERO );

                const UColour4 colour = map->getColour( to_I32( mapCoord.x ), to_I32( mapCoord.y ) );
                const U8 index = BestIndex( colour );
                const F32 colourVal = colour[index];
                if ( colourVal <= EPSILON_F32 )
                {
                    continue;
                }

                const U8 arrayLayer = to_U8( distribution[index]( generator ) );

                const F32 xmlScale = scales[treeData ? meshID : index];
                // Don't go under 75% of the scale specified in the data files
                const F32 minXmlScale = xmlScale * 7.5f / 10.0f;
                // Don't go over 75% of the scale specified in the data files
                const F32 maxXmlScale = xmlScale * 1.25f;

                const F32 scale = CLAMPED( (colourVal + 1.f) / 256.0f * xmlScale, minXmlScale, maxXmlScale ) *
                    slopeScaleFactor;

                assert( scale > EPSILON_F32 );

                //vert._position.y = (((0.0f*heightExtent) + vert._position.y) - ((0.0f*scale) + vert._position.y)) + vert._position.y;
                VegetationData entry = {};
                entry._positionAndScale.set( vert._position, scale );
                Quaternion<F32> modelRotation;
                if ( treeData )
                {
                    modelRotation.fromEuler( _treeRotations[meshID] );
                }
                else
                {
                    modelRotation = RotationFromVToU( WORLD_Y_AXIS, vert._normal, WORLD_Z_NEG_AXIS );
                }

                entry._orientationQuat = (Quaternion<F32>( vert._normal, Random( 360.0f ) ) * modelRotation).asVec4();
                entry._data = {
                    to_F32( arrayLayer ),
                    to_F32( ID ),
                    1.0f,
                    1.0f
                };

                container.push_back( entry );
            }

            container.shrink_to_fit();
            chunkCache << BYTE_BUFFER_VERSION;
            chunkCache << container.size();
            chunkCache.append( container.data(), container.size() );
            if ( !chunkCache.dumpToFile( (Paths::g_cacheLocation + Paths::g_terrainCacheLocation).c_str(), cacheFileName.c_str() ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }
        Console::printfn( Locale::Get( _ID( "CREATE_GRASS_END" ) ) );
    }

}
