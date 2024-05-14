

#include "Headers/Vegetation.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Managers/Headers/ProjectManager.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/RenderPackage.h"
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
    }

    Vegetation::Vegetation( PlatformContext& context, const ResourceDescriptor<Vegetation>& descriptor )
        : SceneNode( descriptor,
                     GetSceneNodeType<Vegetation>(),
                     to_base( ComponentType::TRANSFORM ) | to_base( ComponentType::BOUNDS ) | to_base( ComponentType::RENDERING ) )
        , _context( context.gfx() )
        , _descriptor( descriptor._propertyDescriptor )
    {
        _treeMeshNames.insert( cend( _treeMeshNames ), 
                                       cbegin( descriptor._propertyDescriptor.treeMeshes ), 
                                       cend( descriptor._propertyDescriptor.treeMeshes ) );

        DIVIDE_ASSERT( !_descriptor.grassMap->imageLayers().empty() && !_descriptor.treeMap->imageLayers().empty() );

        setBounds( _descriptor.parentTerrain->getBounds() );

        renderState().addToDrawExclusionMask( RenderStage::REFLECTION );
        renderState().addToDrawExclusionMask( RenderStage::REFRACTION );
        renderState().addToDrawExclusionMask( RenderStage::SHADOW, RenderPassType::COUNT, static_cast<RenderStagePass::VariantType>(ShadowType::CUBEMAP) );
        renderState().addToDrawExclusionMask( RenderStage::SHADOW, RenderPassType::COUNT, static_cast<RenderStagePass::VariantType>(ShadowType::SINGLE) );
        for ( U8 i = 1u; i < Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT; ++i )
        {
            renderState().addToDrawExclusionMask(
                RenderStage::SHADOW,
                RenderPassType::COUNT,
                static_cast<RenderStagePass::VariantType>(ShadowType::CSM),
                g_AllIndicesID,
                static_cast<RenderStagePass::PassIndex>(i) );
        }
        // Because we span an entire terrain chunk, LoD calculation will always be off unless we use the closest possible point to the camera
        renderState().useBoundsCenterForLoD( false );
        renderState().lod0OnCollision( true );
        renderState().drawState( false );

        CachedResource::setState( ResourceState::RES_LOADING );

        registerEditorComponent( _context.context() );
        DIVIDE_ASSERT( _editorComponent != nullptr );
        EditorComponentField visDistanceGrassField = {};
        visDistanceGrassField._name = "Grass Draw distance";
        visDistanceGrassField._data = &_grassDistance;
        visDistanceGrassField._type = EditorComponentFieldType::PUSH_TYPE;
        visDistanceGrassField._readOnly = true;
        visDistanceGrassField._basicType = PushConstantType::FLOAT;
        _editorComponent->registerField( MOV( visDistanceGrassField ) );

        EditorComponentField visDistanceTreesField = {};
        visDistanceTreesField._name = "Tree Draw Instance";
        visDistanceTreesField._data = &_treeDistance;
        visDistanceTreesField._type = EditorComponentFieldType::PUSH_TYPE;
        visDistanceTreesField._readOnly = true;
        visDistanceTreesField._basicType = PushConstantType::FLOAT;
        _editorComponent->registerField( MOV( visDistanceTreesField ) );
    }

    Vegetation::~Vegetation()
    {
    }

    bool Vegetation::unload()
    {
        Console::printfn( LOCALE_STR( "UNLOAD_VEGETATION_BEGIN" ), resourceName().c_str() );

        WAIT_FOR_CONDITION_TIMEOUT( getState() != ResourceState::RES_LOADING, Time::Milliseconds( 3000.0 ) );

        assert( getState() != ResourceState::RES_LOADING );
        {
            LockGuard<SharedMutex> w_lock( _treeMeshLock );
            for ( Handle<Mesh>& mesh : _treeMeshes )
            {
                DestroyResource( mesh );
            }
            _treeMeshes.clear();
        }

        DestroyResource( _treeMaterial );
        DestroyResource( _vegetationMaterial );
        DestroyResource( _cullShaderGrass );
        DestroyResource( _cullShaderTrees );

        _treeData.reset();
        _grassData.reset();
        _buffer.reset();

        Console::printfn( LOCALE_STR( "UNLOAD_VEGETATION_END" ) );

        return SceneNode::unload();
    }

    bool Vegetation::load( PlatformContext& context )
    {
        // Make sure this is ONLY CALLED FROM THE MAIN LOADING THREAD. All instances should call this in a serialized fashion
        DIVIDE_ASSERT( _buffer == nullptr );

        _lodPartitions.fill( 0u );

        constexpr F32 offsetBottom0 = 0.20f;
        constexpr F32 offsetBottom1 = 0.10f;

        const mat4<F32> transform[] =
        {
            mat4<F32>
            {
                vec3<F32>( -offsetBottom0, 0.f, -offsetBottom0 ),
                VECTOR3_UNIT,
                GetMatrix( Quaternion<F32>( Angle::DEGREES<F32>( 25.f ), Angle::DEGREES<F32>( 0.f ), Angle::DEGREES<F32>( 0.f ) ) )
            },

            mat4<F32>
            {
                vec3<F32>( -offsetBottom1, 0.f, offsetBottom1 ),
                vec3<F32>( 0.85f ),
                GetMatrix( Quaternion<F32>( Angle::DEGREES<F32>( -12.5f ), Angle::DEGREES<F32>( 0.f ),  Angle::DEGREES<F32>( 0.f ) )* //Pitch
                            Quaternion<F32>( Angle::DEGREES<F32>( 0.f ),    Angle::DEGREES<F32>( 35.f ), Angle::DEGREES<F32>( 0.f ) ) )  //Yaw
            },

            mat4<F32>
            {
                vec3<F32>( offsetBottom0, 0.f, -offsetBottom1 ),
                vec3<F32>( 1.1f ),
                GetMatrix( Quaternion<F32>( Angle::DEGREES<F32>( 30.f ), Angle::DEGREES<F32>( 0.f ),   Angle::DEGREES<F32>( 0.f ) )* //Pitch
                            Quaternion<F32>( Angle::DEGREES<F32>( 0.f ),  Angle::DEGREES<F32>( -75.f ), Angle::DEGREES<F32>( 0.f ) ) )  //Yaw
            },

            mat4<F32>
            {
                vec3<F32>( offsetBottom1 * 2, 0.f, offsetBottom1 ),
                vec3<F32>( 0.9f ),
                GetMatrix( Quaternion<F32>( Angle::DEGREES<F32>( -25.f ), Angle::DEGREES<F32>( 0.f ),    Angle::DEGREES<F32>( 0.f ) )* //Pitch
                            Quaternion<F32>( Angle::DEGREES<F32>( 0.f ),   Angle::DEGREES<F32>( -125.f ), Angle::DEGREES<F32>( 0.f ) ) )  //Yaw
            },

            mat4<F32>
            {
                vec3<F32>( -offsetBottom1 * 2, 0.f, -offsetBottom1 * 2 ),
                vec3<F32>( 1.2f ),
                GetMatrix( Quaternion<F32>( Angle::DEGREES<F32>( 5.f ), Angle::DEGREES<F32>( 0.f ),    Angle::DEGREES<F32>( 0.f ) )* //Pitch
                            Quaternion<F32>( Angle::DEGREES<F32>( 0.f ), Angle::DEGREES<F32>( -225.f ), Angle::DEGREES<F32>( 0.f ) ) )  //Yaw
            },

            mat4<F32>
            {
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
        }

        const U16 indices[] = { 0, 1, 2,
                                0, 2, 3 };

        const vec2<F32> texCoords[] =
        {
            vec2<F32>( 0.f, 0.f ),
            vec2<F32>( 0.f, 1.f ),
            vec2<F32>( 1.f, 1.f ),
            vec2<F32>( 1.f, 0.f )
        };

        VertexBuffer::Descriptor descriptor{};
        descriptor._name = "Vegetation";
        descriptor._allowDynamicUpdates = false;
        descriptor._keepCPUData = false;
        descriptor._largeIndices = false;

        _buffer = context.gfx().newVB( descriptor );
        _buffer->setVertexCount( vertices.size() );
        for ( U8 i = 0u; i < to_U8( vertices.size() ); ++i )
        {
            _buffer->modifyPositionValue( i, vertices[i] );
            _buffer->modifyNormalValue( i, WORLD_Y_AXIS );
            _buffer->modifyTangentValue( i, WORLD_X_AXIS );
            _buffer->modifyTexCoordValue( i, texCoords[i % 4].s, texCoords[i % 4].t );
        }

        const auto addPlanes = [&]( const U8 count )
        {
            for ( U8 i = 0u; i < count; ++i )
            {
                if ( i > 0 )
                {
                    _buffer->addRestartIndex();
                }
                for ( const U16 idx : indices )
                {
                    _buffer->addIndex( idx + i * 4 );
                }
            }
        };

        for ( U8 i = 0; i < _lodPartitions.size(); ++i )
        {
            addPlanes( billboardsPlaneCount / (i + 1) );
            _lodPartitions[i] = _buffer->partitionBuffer();
        }

        //ref: http://mollyrocket.com/casey/stream_0016.html
        _grassPositions.reserve( to_size( SQUARED(_descriptor.chunkSize )));
        _treePositions.reserve( to_size( SQUARED( _descriptor.chunkSize )));

        const F32 posOffset = to_F32( _descriptor.chunkSize * 2 );

        vec2<F32> intersections[2]{};
        Util::Circle circleA{}, circleB{};
        circleA.center[0] = circleB.center[0] = -posOffset;
        circleA.center[1] = -posOffset;
        circleB.center[1] = posOffset;

        constexpr F32 dR[2] = 
        {
            g_distanceRingsBaseGrass * g_PointRadiusBaseGrass,
            g_distanceRingsBaseTrees * g_PointRadiusBaseTrees
        };

        for ( U8 i = 0; i < 2; ++i )
        {
            auto& set = (i == 0 ? _grassPositions : _treePositions);

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
                        for ( const vec2<F32> record : intersections )
                        {
                            if ( IS_IN_RANGE_EXCLUSIVE( record.x, -to_F32( _descriptor.chunkSize ), to_F32( _descriptor.chunkSize ) ) &&
                                 IS_IN_RANGE_EXCLUSIVE( record.y, -to_F32( _descriptor.chunkSize ), to_F32( _descriptor.chunkSize ) ) )
                            {
                                set.insert( record );
                            }
                        }
                    }
                }
            }
        }

        _maxGrassInstances = to_U32(_grassPositions.size());
        _maxTreeInstances  = to_U32(_treePositions.size());

        if ( _maxTreeInstances == 0u && _maxGrassInstances == 0u )
        {
            return SceneNode::load( context );
        }

        _maxGrassInstances += _maxGrassInstances % WORK_GROUP_SIZE;
        _maxTreeInstances  += _maxTreeInstances  % WORK_GROUP_SIZE;

        const auto& chunks = _descriptor.parentTerrain->terrainChunks();

        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._bufferParams._elementSize = sizeof( VegetationData );
        bufferDescriptor._bufferParams._flags._usageType = BufferUsageType::UNBOUND_BUFFER;
        bufferDescriptor._bufferParams._flags._updateFrequency = BufferUpdateFrequency::ONCE;
        bufferDescriptor._bufferParams._flags._updateUsage = BufferUpdateUsage::GPU_TO_GPU;

        if ( _maxTreeInstances > 0 )
        {
            bufferDescriptor._bufferParams._elementCount = to_U32( _maxTreeInstances * chunks.size() );
            bufferDescriptor._name = "Tree_data";
            _treeData = context.gfx().newSB( bufferDescriptor );
        }
        if ( _maxGrassInstances > 0 )
        {
            bufferDescriptor._bufferParams._elementCount = to_U32( _maxGrassInstances * chunks.size() );
            bufferDescriptor._name = "Grass_data";
            _grassData = context.gfx().newSB( bufferDescriptor );
        }

        std::atomic_uint loadTasks = 0u;

        ResourceDescriptor<Material> matDesc( "Tree_material" );
        _treeMaterial = CreateResource( matDesc );
        {
            Material::ShaderData treeShaderData = {};
            treeShaderData._depthShaderVertSource = "tree";
            treeShaderData._depthShaderVertVariant = "";
            treeShaderData._colourShaderVertSource = "tree";
            treeShaderData._colourShaderVertVariant = "";

            ResourcePtr<Material> matPtr = Get( _treeMaterial );
            matPtr->baseShaderData( treeShaderData );
            matPtr->properties().shadingMode( ShadingMode::BLINN_PHONG );
            matPtr->properties().isInstanced( true );
            matPtr->addShaderDefine( ShaderType::COUNT, Util::StringFormat( "MAX_TREE_INSTANCES {}", _maxTreeInstances ).c_str() );
        }


        ResourceDescriptor<Material> vegetationMaterial( "grassMaterial" );
        _vegetationMaterial = CreateResource( vegetationMaterial );
        {
            ResourcePtr<Material> matPtr = Get( _vegetationMaterial );
            matPtr->properties().shadingMode( ShadingMode::BLINN_PHONG );
            matPtr->properties().baseColour( DefaultColours::WHITE );
            matPtr->properties().roughness( 0.9f );
            matPtr->properties().metallic( 0.02f );
            matPtr->properties().doubleSided( true );
            matPtr->properties().isStatic( false );
            matPtr->properties().isInstanced( true );
            matPtr->setPipelineLayout( PrimitiveTopology::TRIANGLE_STRIP, _buffer->generateAttributeMap() );

            SamplerDescriptor grassSampler = {};
            grassSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
            grassSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
            grassSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
            grassSampler._anisotropyLevel = 8u;

            ResourceDescriptor<Texture> vegetationBillboards( "Vegetation Billboards" );
            vegetationBillboards.assetLocation( ResourcePath { GetVariable( _descriptor.parentTerrain->descriptor(), "vegetationTextureLocation" ) } );
            vegetationBillboards.assetName(_descriptor.billboardTextureArray );
            vegetationBillboards.waitForReady( false );
            TextureDescriptor& grassTexDescriptor = vegetationBillboards._propertyDescriptor;
            grassTexDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
            grassTexDescriptor._packing = GFXImagePacking::NORMALIZED_SRGB;
            Handle<Texture> grassBillboardArray = CreateResource( vegetationBillboards, loadTasks );
            matPtr->setTexture( TextureSlot::UNIT0, grassBillboardArray, grassSampler, TextureOperation::REPLACE, true );
        }

        ShaderModuleDescriptor compModule = {};
        compModule._moduleType = ShaderType::COMPUTE;
        compModule._sourceFile = "instanceCullVegetation.glsl";
        compModule._defines.emplace_back( Util::StringFormat( "WORK_GROUP_SIZE {}", WORK_GROUP_SIZE ) );
        compModule._defines.emplace_back( Util::StringFormat( "MAX_TREE_INSTANCES {}", _maxTreeInstances ) );
        compModule._defines.emplace_back( Util::StringFormat( "MAX_GRASS_INSTANCES {}", _maxGrassInstances ) );
        

        ResourceDescriptor<ShaderProgram> instanceCullShaderGrass( "instanceCullVegetation_Grass" );
        instanceCullShaderGrass.waitForReady( false );
        ShaderProgramDescriptor& shaderCompDescriptorGrass = instanceCullShaderGrass._propertyDescriptor;
        shaderCompDescriptorGrass._modules.push_back( compModule );

        _cullShaderGrass = CreateResource( instanceCullShaderGrass, loadTasks );

        compModule._defines.emplace_back( "CULL_TREES" );
        ResourceDescriptor<ShaderProgram> instanceCullShaderTrees( "instanceCullVegetation_Trees" );
        instanceCullShaderTrees.waitForReady( false );
        ShaderProgramDescriptor& shaderCompDescriptorTrees = instanceCullShaderTrees._propertyDescriptor;
        shaderCompDescriptorTrees._modules.push_back( compModule );
        _cullShaderTrees = CreateResource( instanceCullShaderTrees, loadTasks );

        WAIT_FOR_CONDITION( loadTasks.load() == 0u );

        Get( _vegetationMaterial )->computeShaderCBK( [grassInstances = _maxGrassInstances]( [[maybe_unused]] Material* material, const RenderStagePass stagePass )
        {
            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._modules.emplace_back( ShaderType::VERTEX, "grass.glsl" );
            shaderDescriptor._globalDefines.emplace_back( "ENABLE_TBN" );
            shaderDescriptor._globalDefines.emplace_back( Util::StringFormat( "MAX_GRASS_INSTANCES {}", grassInstances ) );

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

        WAIT_FOR_CONDITION( Get(_cullShaderGrass)->getState() == ResourceState::RES_LOADED &&
                            Get(_cullShaderTrees)->getState() == ResourceState::RES_LOADED );

        return SceneNode::load(context);
    }

    void Vegetation::prepareDraw( SceneGraphNode* sgn )
    {
        VegetationInstance* instance = nullptr;
        {
            SharedLock<SharedMutex> w_lock( _instanceLock );
            for ( const auto& [id, vegInstance] : _instances )
            {
                if ( id == sgn->dataFlag() )
                {
                    instance = vegInstance;
                    break;
                }
            }
        }
        if ( instance == nullptr )
        {
            return;
        }

        if ( instance->_instanceCountGrass > 0u || instance->_instanceCountTrees > 0u )
        {
            sgn->get<RenderingComponent>()->primitiveRestartRequired( true );
            sgn->get<RenderingComponent>()->instantiateMaterial( _vegetationMaterial );
            sgn->get<RenderingComponent>()->occlusionCull( false ); ///< We handle our own culling
            sgn->get<BoundsComponent>()->collisionsEnabled( false );///< Grass collision detection should be handled in shaders (for now)

            PipelineDescriptor pipeDesc;
            pipeDesc._primitiveTopology = PrimitiveTopology::COMPUTE;
            pipeDesc._shaderProgramHandle = _cullShaderGrass;
            _cullPipelineGrass = _context.newPipeline( pipeDesc );
            pipeDesc._shaderProgramHandle = _cullShaderTrees;
            _cullPipelineTrees = _context.newPipeline( pipeDesc );

            renderState().drawState( true );
        }

        const U32 ID = sgn->dataFlag();
        const U32 meshID = to_U32( ID % _treeMeshNames.size() );

        if ( instance->_instanceCountTrees > 0 && !_treeMeshNames.empty() )
        {
            LockGuard<SharedMutex> w_lock( _treeMeshLock );
            if ( _treeMeshes.empty() )
            {
                for ( const auto& meshName : _treeMeshNames )
                {
                    if ( !eastl::any_of( eastl::cbegin( _treeMeshes ),
                                         eastl::cend( _treeMeshes ),
                                         [&meshName]( const Handle<Mesh> ptr ) noexcept
                                         {
                                             return Get(ptr)->assetName() == meshName;
                                         }))
                    {
                        ResourceDescriptor<Mesh> model( "Tree" );
                        model.assetLocation( Paths::g_modelsLocation );
                        model.flag( true );
                        model.waitForReady( true );
                        model.assetName( meshName );
                        Handle<Mesh> meshPtr = CreateResource( model );
                        Get(meshPtr)->setMaterialTpl( _treeMaterial );
                        // CSM last split should probably avoid rendering trees since it would cover most of the scene :/
                        Get(meshPtr)->renderState().addToDrawExclusionMask( RenderStage::SHADOW,
                                                                            RenderPassType::MAIN_PASS,
                                                                            static_cast<RenderStagePass::VariantType>(ShadowType::CSM),
                                                                            g_AllIndicesID,
                                                                            RenderStagePass::PassIndex::PASS_2 );
                        _treeMeshes.push_back( meshPtr );
                    }
                }
            }

            Handle<Mesh> crtMesh = INVALID_HANDLE<Mesh>;
            {
                SharedLock<SharedMutex> r_lock( _treeMeshLock );
                crtMesh = _treeMeshes.front();
                const auto& meshName = _treeMeshNames[meshID];
                for ( const Handle<Mesh> mesh : _treeMeshes )
                {
                    if ( Get(mesh)->resourceName() == meshName )
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
            nodeDescriptor._instanceCount = instance->_instanceCountTrees;
            nodeDescriptor._nodeHandle = FromHandle(crtMesh);
            nodeDescriptor._name = Util::StringFormat( "Trees_chunk_{}", ID ).c_str();
            _treeParentNode = sgn->addChildNode( nodeDescriptor );

            TransformComponent* tComp = _treeParentNode->get<TransformComponent>();
            const vec4<F32>& offset = instance->_chunk->getOffsetAndSize();
            tComp->setPositionX( offset.x + offset.z * 0.5f );
            tComp->setPositionZ( offset.y + offset.w * 0.5f );
            tComp->setScale( _descriptor.treeScales[meshID] );

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

    void Vegetation::prepareRender( SceneGraphNode* sgn,
                                    RenderingComponent& rComp,
                                    RenderPackage& pkg,
                                    GFX::MemoryBarrierCommand& postDrawMemCmd,
                                    RenderStagePass renderStagePass,
                                    const CameraSnapshot& cameraSnapshot,
                                    bool refreshData )
    {
        VegetationInstance* instance = nullptr;
        {
            SharedLock<SharedMutex> w_lock( _instanceLock );
            for ( const auto& [id, vegInstance] : _instances )
            {
                if ( id == sgn->dataFlag() )
                {
                    instance = vegInstance;
                    break;
                }
            }
        }
        if ( instance == nullptr )
        {
            return;
        }

        pkg.pushConstantsCmd()._constants.set( _ID( "dvd_terrainChunkOffset" ), PushConstantType::UINT, sgn->dataFlag() );

        Handle<GFX::CommandBuffer> cmdBuffer = GetCommandBuffer( pkg );
        DIVIDE_ASSERT(cmdBuffer != INVALID_HANDLE<GFX::CommandBuffer>);

        GFX::CommandBuffer& bufferInOut = *GFX::Get(cmdBuffer);
        bufferInOut.clear();

        if ( _grassData || _treeData )
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_PASS;
            if ( _treeData )
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 5u, ShaderStageVisibility::ALL );
                Set( binding._data, _treeData.get(), { 0u, _treeData->getPrimitiveCount() } );
            }
            if ( _grassData )
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 6u, ShaderStageVisibility::ALL );
                Set( binding._data, _grassData.get(), { 0u, _grassData->getPrimitiveCount() } );
            }
        }

        // Culling lags one full frame
        if ( renderState().drawState( renderStagePass ) &&
             refreshData &&
             (instance->_instanceCountGrass > 0 || instance->_instanceCountTrees > 0) )
        {
            const RenderTargetID hiZSourceTarget = renderStagePass._stage == RenderStage::REFLECTION
                                                                           ? RenderTargetNames::HI_Z_REFLECT
                                                                           : RenderTargetNames::HI_Z;

            const RenderTarget* hizTarget = _context.renderTargetPool().getRenderTarget( hiZSourceTarget );
            const RTAttachment* hizAttachment = hizTarget->getAttachment( RTAttachmentType::COLOUR );

            if ( hizAttachment != nullptr )
            {
                ResourcePtr<Texture> hizTexture = Get(hizAttachment->texture());

                mat4<F32> viewProjectionMatrix;
                mat4<F32>::Multiply( cameraSnapshot._projectionMatrix, cameraSnapshot._viewMatrix, viewProjectionMatrix );

                PushConstantsStruct fastConstants{};
                fastConstants.data[0] = viewProjectionMatrix;
                fastConstants.data[1] = cameraSnapshot._viewMatrix;

                GFX::SendPushConstantsCommand cullConstantsCmd{};
                PushConstants& constants = cullConstantsCmd._constants;
                constants.set( _ID( "dvd_viewSize" ), PushConstantType::VEC2, vec2<F32>( hizTexture->width(), hizTexture->height() ) );
                constants.set( _ID( "dvd_cameraPosition" ), PushConstantType::VEC3, cameraSnapshot._eye );
                constants.set( _ID( "dvd_frustumPlanes" ), PushConstantType::VEC4, cameraSnapshot._frustumPlanes );
                constants.set( _ID( "dvd_grassVisibilityDistance" ), PushConstantType::FLOAT, _grassDistance );
                constants.set( _ID( "dvd_treeVisibilityDistance" ), PushConstantType::FLOAT, _treeDistance );
                constants.set( _ID( "dvd_treeExtents" ), PushConstantType::VEC4, _treeExtents );
                constants.set( _ID( "dvd_grassExtents" ), PushConstantType::VEC4, _grassExtents );
                constants.set( _ID( "dvd_terrainChunkOffset" ), PushConstantType::UINT, sgn->dataFlag() );
                constants.set( fastConstants );

                GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut)->_scopeName = "Occlusion Cull Vegetation";

                {
                    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
                    cmd->_usage = DescriptorSetUsage::PER_DRAW;
                    DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::COMPUTE );
                    Set( binding._data, hizTexture->getView(), hizAttachment->_descriptor._sampler );
                }

                GFX::DispatchComputeCommand computeCmd = {};

                auto memCmd = GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut ); // GPU to GPU command needed BEFORE draw (so ignore postDrawMemCmd)
                if ( instance->_instanceCountGrass > 0 )
                {
                    computeCmd._computeGroupSize.set( (instance->_instanceCountGrass + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1 );

                    //Cull grass
                    GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = _cullPipelineGrass;
                    GFX::EnqueueCommand( bufferInOut, cullConstantsCmd );
                    GFX::EnqueueCommand( bufferInOut, computeCmd );
                    memCmd->_bufferLocks.emplace_back( BufferLock
                    {
                        ._range = { 0u, U32_MAX },
                        ._type = BufferSyncUsage::GPU_WRITE_TO_GPU_READ,
                        ._buffer = _grassData->getBufferImpl()
                    });
                }
                if ( instance->_instanceCountTrees > 0 )
                {
                    computeCmd._computeGroupSize.set( (instance->_instanceCountTrees + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1 );
                    // Cull trees
                    GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = _cullPipelineTrees;
                    GFX::EnqueueCommand( bufferInOut, cullConstantsCmd );
                    GFX::EnqueueCommand( bufferInOut, computeCmd );

                    memCmd->_bufferLocks.emplace_back( BufferLock
                    {
                        ._range = { 0u, U32_MAX },
                        ._type = BufferSyncUsage::GPU_WRITE_TO_GPU_READ,
                        ._buffer = _treeData->getBufferImpl()
                    });
                }

                GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
            }
        }

        SceneNode::prepareRender( sgn, rComp, pkg, postDrawMemCmd, renderStagePass, cameraSnapshot, refreshData );
    }

    void Vegetation::sceneUpdate( const U64 deltaTimeUS,
                                  SceneGraphNode* sgn,
                                  SceneState& sceneState )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        if ( !renderState().drawState() )
        {
            prepareDraw( sgn );
            sgn->get<RenderingComponent>()->dataFlag( to_F32( sgn->dataFlag() ) );
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

    void Vegetation::buildDrawCommands( SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut )
    {
        const U16 partitionID = _lodPartitions[0];

        VegetationInstance* instance = nullptr;
        {
            SharedLock<SharedMutex> w_lock( _instanceLock );
            for ( const auto&[id, vegInstance] : _instances )
            {
                if (id == sgn->dataFlag())
                {
                    instance = vegInstance;
                    break;
                }
            }
        }
        if (instance == nullptr)
        {
            return;
        }

        GenericDrawCommand& cmd = cmdsOut.emplace_back();
        toggleOption( cmd, CmdRenderOptions::RENDER_INDIRECT );

        cmd._sourceBuffer = _buffer->handle();
        cmd._cmd.instanceCount = instance->_instanceCountGrass;
        cmd._cmd.indexCount = to_U32( _buffer->getPartitionIndexCount( partitionID ) );
        cmd._cmd.firstIndex = to_U32( _buffer->getPartitionOffset( partitionID ) );

        RenderingComponent* rComp = sgn->get<RenderingComponent>();
        U16 prevID = 0;
        for ( U8 i = 0; i < to_U8( _lodPartitions.size() ); ++i )
        {
            U16 id = _lodPartitions[i];
            if ( id == U16_MAX )
            {
                assert( i > 0 );
                id = prevID;
            }
            rComp->setLoDIndexOffset( i, _buffer->getPartitionOffset( id ), _buffer->getPartitionIndexCount( id ) );
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

        FORCE_INLINE bool ScaleAndCheckBounds( const vec2<F32> chunkPos, const vec2<F32> chunkSize, vec2<F32>& point ) noexcept
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

    void Vegetation::registerInstance( const U32 chunkID, VegetationInstance* instance )
    {
        UniqueLock<SharedMutex> w_lock(_instanceLock);
        for ( auto&[id, instance] : _instances)
        {
            if ( id == chunkID)
            {
                instance = instance;
                return;
            }
        }

        _instances.emplace_back(chunkID, instance);
    }

    void Vegetation::unregisterInstance( const U32 chunkID )
    {
        UniqueLock<SharedMutex> w_lock( _instanceLock );
        dvd_erase_if(_instances, [chunkID]( const auto& entry ){ return entry.first == chunkID; });
    }

    VegetationInstance::VegetationInstance( PlatformContext& context, const Handle<Vegetation> parent, TerrainChunk* chunk )
        : PlatformContextComponent(context)
        , _parent( parent )
        , _chunk( chunk )
    {
        Get(_parent)->registerInstance( _chunk->id(), this);
    }

    VegetationInstance::~VegetationInstance()
    {
        Get( _parent )->unregisterInstance( _chunk->id() );
    }

    void VegetationInstance::computeTransforms()
    {
        ResourcePtr<Vegetation> parentPtr = Get(_parent);

        if ( context().config().debug.renderFilter.grassInstances )
        {
            vector<VegetationData> data = computeTransforms( false );
            _instanceCountGrass = to_U32(data.size());

            const BufferLock lock = parentPtr->_grassData->writeData(BufferRange{._startOffset = _chunk->id() * parentPtr->_maxGrassInstances, ._length = _instanceCountGrass}, data.data());
            DIVIDE_UNUSED(lock);
        }

        if ( context().config().debug.renderFilter.treeInstances )
        {
            vector<VegetationData> data = computeTransforms( true );
            _instanceCountTrees = to_U32(data.size());
            const BufferLock lock = parentPtr->_treeData->writeData( BufferRange{ ._startOffset = _chunk->id() * parentPtr->_maxTreeInstances, ._length = _instanceCountTrees }, data.data() );
            DIVIDE_UNUSED( lock );
        }
    }

    vector<VegetationData> VegetationInstance::computeTransforms( const bool treeData )
    {
        const U32 ID = _chunk->id();

        const string cacheFileName = Util::StringFormat( "{}_{}_{}_{}.cache", _chunk->parent().resourceName().c_str(), Get(_parent)->resourceName().c_str(), treeData ? "trees" : "grass", ID );
        Console::printfn( Locale::Get( treeData ? _ID( "CREATE_TREE_START" ) : _ID( "CREATE_GRASS_BEGIN" ) ), ID );

        vector<VegetationData> container;

        ByteBuffer chunkCache;
        if ( context().config().debug.cache.enabled && 
             context().config().debug.cache.vegetation &&
             chunkCache.loadFromFile( Paths::g_terrainCacheLocation, cacheFileName ) )
        {
            auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
            chunkCache >> tempVer;
            if ( tempVer == BYTE_BUFFER_VERSION )
            {
                size_t containerSize = 0u;
                chunkCache.read<size_t>(containerSize);
                container.resize( containerSize );
                chunkCache.read( reinterpret_cast<Byte*>(container.data()), sizeof( VegetationData ) * container.size() );
            }
            else
            {
                chunkCache.clear();
            }
        }
        else
        {
            ResourcePtr<Vegetation> parent = Get(_parent);
            const VegetationDescriptor& descriptor = parent->_descriptor;

            const size_t maxInstances = treeData ? parent->_maxTreeInstances : parent->_maxGrassInstances;

            container.reserve( maxInstances);

            static std::discrete_distribution<> distribution[] = 
            {
                {5, 2, 2, 1},
                {1, 5, 2, 2},
                {2, 1, 5, 2},
                {2, 2, 1, 5}
            };

            std::default_random_engine generator( to_U32( std::chrono::system_clock::now().time_since_epoch().count() ) );

            const U32 meshID = to_U32( ID % parent->_treeMeshNames.size() );

            const vec2<F32> chunkSize = _chunk->getOffsetAndSize().zw;
            const vec2<F32> chunkPos = _chunk->getOffsetAndSize().xy;
            //const F32 waterLevel = 0.0f;// ToDo: make this dynamic! (cull underwater points later on?)
            const auto& map = treeData ? descriptor.treeMap : descriptor.grassMap;
            const U16 mapWidth = map->dimensions( 0u, 0u ).width;
            const U16 mapHeight = map->dimensions( 0u, 0u ).height;
            const auto& positions = treeData ? parent->_treePositions : parent->_grassPositions;
            const auto& scales = treeData ? descriptor.treeScales : descriptor.grassScales;
            const F32 slopeLimit = treeData ? g_slopeLimitTrees : g_slopeLimitGrass;

            const Terrain& terrain = _chunk->parent();

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

                const F32 scale = CLAMPED( (colourVal + 1.f) / 256.0f * xmlScale, minXmlScale, maxXmlScale ) * slopeScaleFactor;

                assert( scale > EPSILON_F32 );

                //vert._position.y = (((0.0f*heightExtent) + vert._position.y) - ((0.0f*scale) + vert._position.y)) + vert._position.y;
                VegetationData entry = {};
                entry._positionAndScale.set( vert._position, scale );
                Quaternion<F32> modelRotation;
                if ( treeData )
                {
                    modelRotation.fromEuler( descriptor.treeRotations[meshID] );
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
            if ( !chunkCache.dumpToFile(Paths::g_terrainCacheLocation, cacheFileName ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }

        Console::printfn( LOCALE_STR( "CREATE_GRASS_END" ) );

        return container;
    }

}
