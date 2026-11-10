// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#include "Headers/Vegetation.h"

#include "Core/Headers/Kernel.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Managers/Headers/ProjectManager.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Scenes/Headers/SceneState.h"
#include "Scenes/Headers/Scene.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/Headers/LightPool.h"
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

#include <bit>

namespace Divide
{

    namespace
    {
        constexpr U16 GRASS_CACHE_VERSION = 4u;
        constexpr U16 TREE_CACHE_VERSION = 6u;

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
        // Compiles wind out of the grass and tree vertex shaders when the editor disables it.
        constexpr const char* g_windDisabledDefine = "VEGETATION_WIND_DISABLED";

        [[nodiscard]] U64 TreePlacementHash( const VegetationDescriptor& descriptor ) noexcept
        {
            U64 hash = _ID( "mixed-tree-placements-v5" );
            for ( size_t i = 0u; i < descriptor.treeMeshes.size(); ++i )
            {
                hash = _ID( descriptor.treeMeshes[i].c_str(), hash );
                const F32 values[] =
                {
                    descriptor.treeScales[i],
                    descriptor.treeDensities[i],
                    descriptor.treeRotations[i].x,
                    descriptor.treeRotations[i].y,
                    descriptor.treeRotations[i].z
                };
                for ( const F32 value : values )
                {
                    hash = (hash ^ std::bit_cast<U32>( value )) * prime_64_const;
                }
            }
            return hash;
        }

        template<typename Callback>
        void ForEachTreeRenderingComponent( SceneGraphNode& node, Callback&& callback )
        {
            if ( RenderingComponent* component = node.get<RenderingComponent>() )
            {
                callback( *component );
            }

            const SceneGraphNode::ChildContainer& children = node.getChildren();
            SharedLock<SharedMutex> r_lock( children._lock );
            for ( U32 i = 0u; i < children._count; ++i )
            {
                ForEachTreeRenderingComponent( *children._data[i], callback );
            }
        }
    }

    Vegetation::Vegetation( const ResourceDescriptor<Vegetation>& descriptor )
        : SceneNode( descriptor,
                     GetSceneNodeType<Vegetation>(),
                     to_base( ComponentType::TRANSFORM ) | to_base( ComponentType::BOUNDS ) | to_base( ComponentType::RENDERING ) )
        , _descriptor( descriptor._propertyDescriptor )
    {
        DIVIDE_ASSERT( _descriptor.treeMeshes.size() <= MAX_VEGETATION_TREE_MESHES );
        _treeMeshNames.insert( cend( _treeMeshNames ), 
                                       cbegin( descriptor._propertyDescriptor.treeMeshes ), 
                                       cend( descriptor._propertyDescriptor.treeMeshes ) );

        DIVIDE_ASSERT( !_descriptor.grassMap->imageLayers().empty() && !_descriptor.treeMap->imageLayers().empty() );

        setBounds( _descriptor.parentTerrain->getBounds() );

        renderState().addToDrawExclusionMask( RenderStage::SHADOW );
        // Because we span an entire terrain chunk, LoD calculation will always be off unless we use the closest possible point to the camera
        renderState().useBoundsCenterForLoD( false );
        renderState().lod0OnCollision( true );
        _grassExtents.w = _grassExtents.xyz.length();

        CachedResource::setState( ResourceState::RES_LOADING );
    }

    Vegetation::~Vegetation()
    {
    }

    bool Vegetation::unload()
    {
        Console::printfn( LOCALE_STR( "UNLOAD_VEGETATION_BEGIN" ), resourceName().c_str() );

        WAIT_FOR_CONDITION_TIMEOUT( getState() != ResourceState::RES_LOADING, Time::Milliseconds( 3000 ) );

        assert( getState() != ResourceState::RES_LOADING );
        for ( auto& [source, material] : _treeMaterials )
        {
            DIVIDE_UNUSED( source );
            DestroyResource( material );
        }
        _treeMaterials.clear();

        for ( Handle<Mesh>& mesh : _treeMeshes )
        {
            DestroyResource( mesh );
        }
        _treeMeshes.clear();

        DestroyResource( _vegetationMaterial );
        DestroyResource( _cullShaderGrass );
        DestroyResource( _cullShaderTrees );
        DestroyResource( _occlusionShaderGrass );
        DestroyResource( _occlusionShaderTrees );

        _treeData.reset();
        _treeDrawData.reset();
        _treeVisibleCounts.reset();
        _grassData.reset();
        _grassDrawData.reset();
        _grassVisibleCounts.reset();
        _buffer.reset();

        Console::printfn( LOCALE_STR( "UNLOAD_VEGETATION_END" ) );

        return SceneNode::unload();
    }

    bool Vegetation::load( PlatformContext& context )
    {
        // Make sure this is ONLY CALLED FROM THE MAIN LOADING THREAD. All instances should call this in a serialized fashion
        DIVIDE_ASSERT( _buffer == nullptr );

        registerEditorComponent( context );
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

        EditorComponentField windAnimationField = {};
        windAnimationField._name = "Wind animation";
        windAnimationField._tooltip = "Disabling wind compiles it out of the grass and tree shaders and keeps their shadows static.";
        windAnimationField._data = &_windAnimation;
        windAnimationField._type = EditorComponentFieldType::SWITCH_TYPE;
        windAnimationField._basicType = PushConstantType::BOOL;
        windAnimationField._readOnly = false;
        _editorComponent->registerField( MOV( windAnimationField ) );

        _lodPartitions.fill( 0u );

        constexpr F32 offsetBottom0 = 0.20f;
        constexpr F32 offsetBottom1 = 0.10f;

        const mat4<F32> transform[] =
        {
            mat4<F32>
            {
                float3( -offsetBottom0, 0.f, -offsetBottom0 ),
                VECTOR3_UNIT,
                quatf( Angle::to_RADIANS(vec3<Angle::DEGREES_F>(25.f, 0.f, 0.f)))
            },

            mat4<F32>
            {
                float3( -offsetBottom1, 0.f, offsetBottom1 ),
                float3( 0.85f ),
                quatf(Angle::to_RADIANS(vec3<Angle::DEGREES_F>(-12.5f,  0.f, 0.f))) * //Pitch
                quatf(Angle::to_RADIANS(vec3<Angle::DEGREES_F>(   0.f, 35.f, 0.f)))  //Yaw
            },

            mat4<F32>
            {
                float3( offsetBottom0, 0.f, -offsetBottom1 ),
                float3( 1.1f ),
                quatf(Angle::to_RADIANS(vec3<Angle::DEGREES_F>(30.f,   0.f, 0.f))) * //Pitch
                quatf(Angle::to_RADIANS(vec3<Angle::DEGREES_F>( 0.f, -75.f, 0.f)))  //Yaw
            },

            mat4<F32>
            {
                float3( offsetBottom1 * 2, 0.f, offsetBottom1 ),
                float3( 0.9f ),
                quatf(Angle::to_RADIANS(vec3<Angle::DEGREES_F>(-25.f,    0.f, 0.f))) * //Pitch
                quatf(Angle::to_RADIANS(vec3<Angle::DEGREES_F>(  0.f, -125.f, 0.f)))  //Yaw
            },

            mat4<F32>
            {
                float3( -offsetBottom1 * 2, 0.f, -offsetBottom1 * 2 ),
                float3( 1.2f ),
                quatf(Angle::to_RADIANS(vec3<Angle::DEGREES_F>(5.f,    0.f, 0.f))) * //Pitch
                quatf(Angle::to_RADIANS(vec3<Angle::DEGREES_F>(0.f, -225.f, 0.f)))  //Yaw
            },

            mat4<F32>
            {
                float3( offsetBottom0, 0.f, offsetBottom1 * 2 ),
                float3( 0.75f ),
                quatf(Angle::to_RADIANS(vec3<Angle::DEGREES_F>(-15.f,   0.f, 0.f))) * //Pitch
                quatf(Angle::to_RADIANS(vec3<Angle::DEGREES_F>(  0.f, 305.f, 0.f)))  //Yaw
            }
        };

        vector<float3> vertices{};
        constexpr U8 billboardsPlaneCount = to_U8( sizeof( transform ) / sizeof( transform[0] ) );
        vertices.reserve( billboardsPlaneCount * 4 );

        for ( U8 i = 0u; i < billboardsPlaneCount; ++i )
        {
            vertices.push_back( transform[i] * float4( -1.f, 0.f, 0.f, 1.f ) ); //BL
            vertices.push_back( transform[i] * float4( -1.f, 1.f, 0.f, 1.f ) ); //TL
            vertices.push_back( transform[i] * float4( 1.f, 1.f, 0.f, 1.f ) ); //TR
            vertices.push_back( transform[i] * float4( 1.f, 0.f, 0.f, 1.f ) ); //BR
        }
        for ( const float3& vertex : vertices )
        {
            _grassExtents.w = std::max( _grassExtents.w, vertex.length() );
        }

        const U16 indices[] = { 0, 1, 2,
                                0, 2, 3 };

        const float2 texCoords[] =
        {
            float2( 0.f, 0.f ),
            float2( 0.f, 1.f ),
            float2( 1.f, 1.f ),
            float2( 1.f, 0.f )
        };

        _buffer = context.gfx().newVB(VertexBuffer::Descriptor{ ._name = "Vegetation" });
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

        float2 intersections[2]{};
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
                        for ( const float2 record : intersections )
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

        _maxGrassInstances = ((_maxGrassInstances + WORK_GROUP_SIZE - 1u) / WORK_GROUP_SIZE) * WORK_GROUP_SIZE;
        _maxTreeInstances  = ((_maxTreeInstances  + WORK_GROUP_SIZE - 1u) / WORK_GROUP_SIZE) * WORK_GROUP_SIZE;

        const auto& chunks = _descriptor.parentTerrain->terrainChunks();

        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._elementSize = sizeof( VegetationData );
        bufferDescriptor._usageType = BufferUsageType::UNBOUND_BUFFER;
        bufferDescriptor._updateFrequency = BufferUpdateFrequency::OCCASIONAL;

        if ( _maxTreeInstances > 0 )
        {
            bufferDescriptor._elementCount = to_U32( _maxTreeInstances * chunks.size() );
            bufferDescriptor._name = "Tree_data";
            _treeData = context.gfx().newShaderBuffer( bufferDescriptor );
            bufferDescriptor._name = "Tree_draw_data";
            _treeDrawData = context.gfx().newShaderBuffer( bufferDescriptor );

            ShaderBufferDescriptor countDescriptor = {};
            countDescriptor._elementSize = sizeof( U32 );
            countDescriptor._elementCount = to_U32( chunks.size() * MAX_VEGETATION_TREE_MESHES );
            countDescriptor._usageType = BufferUsageType::UNBOUND_BUFFER;
            countDescriptor._updateFrequency = BufferUpdateFrequency::OCCASIONAL;
            countDescriptor._name = "Tree_visible_counts";
            vector<U32> zeroCounts( countDescriptor._elementCount, 0u );
            countDescriptor._initialData = { zeroCounts.data(), zeroCounts.size() * sizeof( U32 ) };
            _treeVisibleCounts = context.gfx().newShaderBuffer( countDescriptor );
        }
        if ( _maxGrassInstances > 0 )
        {
            bufferDescriptor._elementCount = to_U32( _maxGrassInstances * chunks.size() );
            bufferDescriptor._name = "Grass_data";
            _grassData = context.gfx().newShaderBuffer( bufferDescriptor );
            bufferDescriptor._name = "Grass_draw_data";
            _grassDrawData = context.gfx().newShaderBuffer( bufferDescriptor );

            ShaderBufferDescriptor countDescriptor = {};
            countDescriptor._elementSize = sizeof( U32 );
            countDescriptor._elementCount = to_U32( chunks.size() );
            countDescriptor._usageType = BufferUsageType::UNBOUND_BUFFER;
            countDescriptor._updateFrequency = BufferUpdateFrequency::OCCASIONAL;
            countDescriptor._name = "Grass_visible_counts";
            vector<U32> zeroCounts( countDescriptor._elementCount, 0u );
            countDescriptor._initialData = { zeroCounts.data(), zeroCounts.size() * sizeof( U32 ) };
            _grassVisibleCounts = context.gfx().newShaderBuffer( countDescriptor );
        }

        std::atomic_uint loadTasks = 0u;

        if ( context.config().debug.renderFilter.treeInstances && _maxTreeInstances > 0u )
        {
            for ( size_t meshIndex = 0u; meshIndex < _treeMeshNames.size(); ++meshIndex )
            {
                if ( _descriptor.treeDensities[meshIndex] <= 0.f )
                {
                    continue;
                }
                const auto& meshName = _treeMeshNames[meshIndex];
                if ( eastl::any_of( cbegin( _treeMeshes ), cend( _treeMeshes ),
                                    [&meshName]( const Handle<Mesh> mesh )
                                    {
                                        return Get( mesh )->assetName() == meshName;
                                    } ) )
                {
                    continue;
                }

                ResourceDescriptor<Mesh> model( "Tree" );
                model.assetLocation( Paths::g_modelsLocation );
                model.flag( true );
                model.waitForReady( true );
                model.assetName( meshName );
                Handle<Mesh> mesh = CreateResource( model );
                if ( mesh == INVALID_HANDLE<Mesh> || Get( mesh ) == nullptr ||
                     Get( mesh )->getState() != ResourceState::RES_LOADED ||
                     Get( mesh )->geometryBuffer() == nullptr )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_VEGETATION_TREE_MESH_LOAD" ), meshName.c_str() );
                    return false;
                }

                _treeMeshes.push_back( mesh );
            }
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

        ResourceDescriptor<ShaderProgram> occlusionGrass( "occludeVegetation_Grass" );
        occlusionGrass.waitForReady( false );
        occlusionGrass._propertyDescriptor._modules.push_back( compModule );
        occlusionGrass._propertyDescriptor._modules.back()._defines.emplace_back( "OCCLUSION_PASS" );
        _occlusionShaderGrass = CreateResource( occlusionGrass, loadTasks );

        compModule._defines.emplace_back( "CULL_TREES" );
        ResourceDescriptor<ShaderProgram> instanceCullShaderTrees( "instanceCullVegetation_Trees" );
        instanceCullShaderTrees.waitForReady( false );
        ShaderProgramDescriptor& shaderCompDescriptorTrees = instanceCullShaderTrees._propertyDescriptor;
        shaderCompDescriptorTrees._modules.push_back( compModule );
        _cullShaderTrees = CreateResource( instanceCullShaderTrees, loadTasks );

        ResourceDescriptor<ShaderProgram> occlusionTrees( "occludeVegetation_Trees" );
        occlusionTrees.waitForReady( false );
        occlusionTrees._propertyDescriptor._modules.push_back( compModule );
        occlusionTrees._propertyDescriptor._modules.back()._defines.emplace_back( "OCCLUSION_PASS" );
        _occlusionShaderTrees = CreateResource( occlusionTrees, loadTasks );

        WAIT_FOR_CONDITION( loadTasks.load() == 0u );

        PipelineDescriptor pipeDesc;
        pipeDesc._primitiveTopology = PrimitiveTopology::COMPUTE;
        pipeDesc._shaderProgramHandle = _cullShaderGrass;
        _cullPipelineGrass = context.gfx().newPipeline( pipeDesc );
        pipeDesc._shaderProgramHandle = _cullShaderTrees;
        _cullPipelineTrees = context.gfx().newPipeline( pipeDesc );
        pipeDesc._shaderProgramHandle = _occlusionShaderGrass;
        _occlusionPipelineGrass = context.gfx().newPipeline( pipeDesc );
        pipeDesc._shaderProgramHandle = _occlusionShaderTrees;
        _occlusionPipelineTrees = context.gfx().newPipeline( pipeDesc );

        Get( _vegetationMaterial )->computeShaderCBK( [grassInstances = _maxGrassInstances]( [[maybe_unused]] Material* material, const RenderStagePass stagePass )
        {
            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._modules.emplace_back( ShaderType::VERTEX, "grass.glsl" );
            shaderDescriptor._globalDefines.emplace_back( "ENABLE_TBN" );
            shaderDescriptor._globalDefines.emplace_back( Util::StringFormat( "MAX_GRASS_INSTANCES {}", grassInstances ) );

            ShaderModuleDescriptor fragModule{ ShaderType::FRAGMENT, "grass.glsl" };
            if ( IsDepthPass( stagePass ) )
            {
                if ( stagePass._stage == RenderStage::SHADOW )
                {
                    fragModule._variant = "Shadow.VSM";

                    shaderDescriptor._modules.push_back( fragModule );
                    shaderDescriptor._name = "grassShadow";
                }
                else
                {
                    fragModule._variant = "PrePass";

                    shaderDescriptor._modules.push_back( fragModule );
                    shaderDescriptor._name = "grassPrePass";
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

    void Vegetation::postLoad( SceneGraphNode* sgn )
    {
        SceneNode::postLoad( sgn );
        if ( _grassDrawData )
        {
            RenderingComponent* rComp = sgn->get<RenderingComponent>();
            rComp->setInstanceShaderBuffer( _grassDrawData.get(), sgn->dataFlag() * _maxGrassInstances );
            rComp->setInstanceCountSource( _grassVisibleCounts.get(), sgn->dataFlag() );
        }
    }

    void Vegetation::finalizeInstance( SceneGraphNode* sgn, const SceneState& sceneState )
    {
        DIVIDE_ASSERT( !sceneState.waterBodies().empty(), "Vegetation requires the global water body" );
        _globalWaterHeight = sceneState.waterBodies().front()._positionW.y;
        applyWindAnimation();
        VegetationInstance* instance = findInstance( sgn->dataFlag() );
        if ( instance == nullptr || instance->_initialized )
        {
            return;
        }

        instance->_drawState._grassInstanceCount = to_U32( instance->_grassCandidates.size() );
        instance->_drawState._treeInstanceCount = to_U32( instance->_treeCandidates.size() );

        if ( !instance->_treeCandidates.empty() )
        {
            auto current = instance->_treeCandidates.begin();
            for ( size_t meshIndex = 0u; meshIndex < _treeMeshNames.size(); ++meshIndex )
            {
                VegetationTreeRange& range = instance->_treeRanges[meshIndex];
                range._offset = to_U32( current - instance->_treeCandidates.begin() );
                const F32 species = to_F32( meshIndex );
                const auto next = std::partition( current, instance->_treeCandidates.end(), [species]( const VegetationData& entry )
                {
                    return entry._data.x == species;
                } );
                range._count = to_U32( next - current );
                current = next;
            }
            DIVIDE_ASSERT( current == instance->_treeCandidates.end(), "Tree candidate has an invalid species" );
        }

        prepareDraw( sgn, *instance );
        F32 instancePadding = 0.f;
        if ( instance->_drawState._grassInstanceCount > 0u )
        {
            const float4& scales = _descriptor.grassScales;
            const F32 maxScale = 1.25f * std::max( { std::abs( scales.x ), std::abs( scales.y ),
                                                     std::abs( scales.z ), std::abs( scales.w ) } );
            instancePadding = _grassExtents.w * maxScale + 0.25f * std::max( 1.f, std::abs( maxScale - 1.f ) );
        }
        for ( size_t meshIndex = 0u; meshIndex < instance->_treeRanges.size(); ++meshIndex )
        {
            const VegetationTreeRange& range = instance->_treeRanges[meshIndex];
            if ( range._count == 0u )
            {
                continue;
            }
            DIVIDE_ASSERT( range._parentNode != nullptr && range._cullRadius > 0.f );
            instancePadding = std::max( instancePadding, range._cullRadius * std::abs( _descriptor.treeScales[meshIndex] ) * 1.25f );
            for ( size_t i = range._offset; i < to_size( range._offset ) + range._count; ++i )
            {
                instance->_treeCandidates[i]._data.y = range._cullRadius;
            }
        }

        if ( instancePadding > _instanceBoundsPadding )
        {
            _instanceBoundsPadding = instancePadding;
            const BoundingBox& terrainBounds = _descriptor.parentTerrain->getBounds();
            const float3 padding{ _instanceBoundsPadding };
            setBounds( BoundingBox{ terrainBounds._min - padding, terrainBounds._max + padding } );
            sgn->sceneGraph()->invalidateStaticGeometryBounds( *sgn );
        }

        if ( !instance->_grassCandidates.empty() )
        {
            DIVIDE_ASSERT( _grassData != nullptr );
            const BufferLock lock = _grassData->writeData(
                ElementRange<>{ sgn->dataFlag() * _maxGrassInstances, instance->_drawState._grassInstanceCount },
                instance->_grassCandidates.data() );
            DIVIDE_UNUSED( lock );
            for ( VegetationData& entry : instance->_grassCandidates )
            {
                entry._data.z = -1.f;
            }
            const BufferLock drawLock = _grassDrawData->writeData(
                ElementRange<>{ sgn->dataFlag() * _maxGrassInstances, instance->_drawState._grassInstanceCount },
                instance->_grassCandidates.data() );
            DIVIDE_UNUSED( drawLock );
        }
        if ( !instance->_treeCandidates.empty() )
        {
            DIVIDE_ASSERT( _treeData != nullptr );
            const BufferLock lock = _treeData->writeData(
                ElementRange<>{ sgn->dataFlag() * _maxTreeInstances, instance->_drawState._treeInstanceCount },
                instance->_treeCandidates.data() );
            DIVIDE_UNUSED( lock );

            // Until the first cull compacts them, draw slots are explicitly culled.
            for ( VegetationData& entry : instance->_treeCandidates )
            {
                entry._data.z = -1.f;
            }
            DIVIDE_ASSERT( _treeDrawData != nullptr );
            const BufferLock drawLock = _treeDrawData->writeData(
                ElementRange<>{ sgn->dataFlag() * _maxTreeInstances, instance->_drawState._treeInstanceCount },
                instance->_treeCandidates.data() );
            DIVIDE_UNUSED( drawLock );
        }
        vector<VegetationData>{}.swap( instance->_grassCandidates );
        vector<VegetationData>{}.swap( instance->_treeCandidates );
        instance->_initialized = true;
    }

    void Vegetation::applyWindAnimation()
    {
        // Runs on the main thread between the scene update and rendering, so no render task reads the defines.
        const bool windAnimation = _windAnimation;
        if ( windAnimation == _appliedWindAnimation.load( std::memory_order_relaxed ) )
        {
            return;
        }

        const auto applyDefine = [windDisabled = !windAnimation]( const Handle<Material> handle )
        {
            Material* material = Get( handle );
            if ( material == nullptr )
            {
                return;
            }

            material->setShaderDefine( ShaderType::COUNT, g_windDisabledDefine, windDisabled );
            material->lockInstancesForRead();
            for ( const Handle<Material> instance : material->getInstancesLocked() )
            {
                Material* instanceMaterial = Get( instance );
                if ( instanceMaterial != nullptr )
                {
                    instanceMaterial->setShaderDefine( ShaderType::COUNT, g_windDisabledDefine, windDisabled );
                }
            }
            material->unlockInstancesForRead();
        };

        LockGuard<Mutex> w_lock( _treeMaterialLock );
        applyDefine( _vegetationMaterial );
        for ( const auto& entry : _treeMaterials )
        {
            applyDefine( entry.second );
        }
        _appliedWindAnimation.store( windAnimation, std::memory_order_relaxed );
    }

    Handle<Material> Vegetation::treeMaterialFor( const Handle<Material> source )
    {
        LockGuard<Mutex> w_lock( _treeMaterialLock );
        for ( const auto& [original, variant] : _treeMaterials )
        {
            if ( original == source )
            {
                return variant;
            }
        }

        Handle<Material> variant = INVALID_HANDLE<Material>;
        const string suffix = Util::StringFormat( "_vegetation_{}_{}", getGUID(), _maxTreeInstances );
        if ( !Material::Clone( source, variant, suffix.c_str() ) || variant == INVALID_HANDLE<Material> )
        {
            Console::errorfn( "Failed to create a vegetation tree material for {}", Get( source )->resourceName().c_str() );
            return INVALID_HANDLE<Material>;
        }

        ResourcePtr<Material> material = Get( variant );
        Material::ShaderData shaderData = material->baseShaderData();
        shaderData._depthShaderVertSource = "tree";
        shaderData._depthShaderVertVariant = "";
        shaderData._shadowShaderVertVariant = "";
        shaderData._colourShaderVertSource = "tree";
        shaderData._colourShaderVertVariant = "";
        material->baseShaderData( shaderData );
        material->addShaderDefine( Util::StringFormat( "MAX_TREE_INSTANCES {}", _maxTreeInstances ) );
        if ( !_appliedWindAnimation.load( std::memory_order_relaxed ) )
        {
            material->addShaderDefine( g_windDisabledDefine );
        }
        material->properties().isInstanced( true );
        material->properties().isStatic( false );
        const U32 updateResult = material->update( 0u );
        DIVIDE_UNUSED( updateResult );
        _treeMaterials.emplace_back( source, variant );
        return MOV(variant);
    }

    void Vegetation::prepareDraw( SceneGraphNode* sgn, VegetationInstance& instance )
    {
        RenderingComponent* owner = sgn->get<RenderingComponent>();
        if ( instance._drawState.hasInstances() )
        {
            owner->primitiveRestartRequired( true );
            owner->instantiateMaterial( _vegetationMaterial );
            owner->occlusionCull( false ); ///< We handle our own culling
            owner->dataFlag( to_F32( sgn->dataFlag() ) );
            owner->setPreparationOwner( owner );
            sgn->get<BoundsComponent>()->collisionsEnabled( false );///< Grass collision detection should be handled in shaders (for now)
        }

        const U32 ID = sgn->dataFlag();

        for ( size_t meshID = 0u; meshID < _treeMeshNames.size(); ++meshID )
        {
            VegetationTreeRange& range = instance._treeRanges[meshID];
            if ( range._count == 0u )
            {
                continue;
            }
            Handle<Mesh> crtMesh = INVALID_HANDLE<Mesh>;
            const auto& meshName = _treeMeshNames[meshID];
            for ( const Handle<Mesh> mesh : _treeMeshes )
            {
                if ( Get( mesh )->assetName() == meshName )
                {
                    crtMesh = mesh;
                    break;
                }
            }
            if ( crtMesh == INVALID_HANDLE<Mesh> )
            {
                Console::errorfn( LOCALE_STR( "ERROR_VEGETATION_TREE_MESH_LOAD" ), meshName.c_str() );
                continue;
            }

            constexpr U32 normalMask = to_base( ComponentType::TRANSFORM ) |
                                       to_base( ComponentType::BOUNDS ) |
                                       to_base( ComponentType::NETWORKING ) |
                                       to_base( ComponentType::RENDERING );

            SceneGraphNodeDescriptor nodeDescriptor = {};
            nodeDescriptor._componentMask = normalMask;
            nodeDescriptor._usageContext = NodeUsageContext::NODE_STATIC;
            nodeDescriptor._serialize = false;
            nodeDescriptor._instanceCount = range._count;
            nodeDescriptor._nodeHandle = FromSharedHandle( crtMesh );
            Util::StringFormatTo( nodeDescriptor._name, "Trees_chunk_{}_species_{}", ID, meshID );
            range._parentNode = sgn->addChildNode( nodeDescriptor );
            range._parentNode->setFlag( SceneGraphNode::Flags::VISIBILITY_LOCKED );

            TransformComponent* tComp = range._parentNode->get<TransformComponent>();
            const float4& offset = instance._chunk->getOffsetAndSize();
            tComp->setPositionX( offset.x + offset.z * 0.5f );
            tComp->setPositionZ( offset.y + offset.w * 0.5f );
            tComp->setScale( _descriptor.treeScales[meshID] );

            DIVIDE_ASSERT( _treeDrawData != nullptr && _treeVisibleCounts != nullptr );
            F32 cullRadius = 0.f;
            const U32 treeBase = to_U32( to_size( ID ) * _maxTreeInstances + range._offset );
            ForEachTreeRenderingComponent( *range._parentNode, [this, ID, treeBase, countElement = to_U32( to_size( ID ) * MAX_VEGETATION_TREE_MESHES + meshID ), owner, &cullRadius]( RenderingComponent& rComp )
            {
                rComp.parentSGN()->setFlag( SceneGraphNode::Flags::VEGETATION_TREE, false );
                rComp.dataFlag( to_F32( ID ) );
                rComp.setInstanceShaderBuffer( _treeDrawData.get(), treeBase );
                rComp.setInstanceCountSource( _treeVisibleCounts.get(), countElement );
                rComp.setPreparationOwner( owner );
                rComp.occlusionCull( false );
                const SceneNode& node = rComp.parentSGN()->getNode();
                if ( node.type() == SceneNodeType::TYPE_SUBMESH )
                {
                    const Handle<Material> source = node.getMaterialTemplate();
                    if ( source == INVALID_HANDLE<Material> )
                    {
                        Console::errorfn( "Tree submesh {} has no material", node.resourceName().c_str() );
                        return;
                    }
                    const Handle<Material> variant = treeMaterialFor( source );
                    if ( variant != INVALID_HANDLE<Material> )
                    {
                        rComp.instantiateMaterial( variant );
                    }
                    cullRadius = std::max( cullRadius, VegetationTreeCullRadius( node.getBounds() ) );
                }
            } );
            DIVIDE_ASSERT( cullRadius > 0.f, "Tree mesh has no valid culling bounds" );
            range._cullRadius = cullRadius;
        }
    }

    void Vegetation::prepareRender( SceneGraphNode* sgn,
                                    RenderingComponent& rComp,
                                    RenderPackage& pkg,
                                    GFX::MemoryBarrierCommand& postDrawMemCmd,
                                    RenderStagePass renderStagePass,
                                    const CameraSnapshot& cameraSnapshot,
                                    bool refreshData )
    {
        Handle<GFX::CommandBuffer> preparationHandle = INVALID_HANDLE<GFX::CommandBuffer>;
        if ( refreshData )
        {
            pkg.independentCompute( false );
            preparationHandle = GetPreparationCommandBuffer( pkg );
            DIVIDE_ASSERT( preparationHandle != INVALID_HANDLE<GFX::CommandBuffer> );
            GFX::Get( preparationHandle )->clear();
            GFX::Get( GetOcclusionCommandBuffer( pkg ) )->clear();
        }

        VegetationInstance* instance = findInstance( sgn->dataFlag() );
        if ( !DIVIDE_ASSERT( instance != nullptr, "Vegetation chunk has no registered instance" ) )
        {
            GFX::Get( GetCommandBuffer( pkg ) )->clear();
            return;
        }

        if ( IsZPrePass( renderStagePass ) )
        {
            STUBBED("Vegetation::prepareRender: Vegetation motion vectors are intentionally zeroed until previous instance and deformation state is implemented.");
        }

        {
            LockGuard<Mutex> w_lock( _geometryLock );
            _buffer->commitData( postDrawMemCmd );
            rComp.setIndexBufferElementOffset( _buffer->firstIndexOffsetCount() );
        }

        pkg.pushConstantsCmd()._uniformData->set( _ID( "dvd_terrainChunkOffset" ), PushConstantType::UINT, sgn->dataFlag() );

        Handle<GFX::CommandBuffer> cmdBuffer = GetCommandBuffer( pkg );
        DIVIDE_ASSERT(cmdBuffer != INVALID_HANDLE<GFX::CommandBuffer>);

        GFX::CommandBuffer& bufferInOut = *GFX::Get(cmdBuffer);
        bufferInOut.clear();

        const auto bindVegetationData = [&]( GFX::CommandBuffer& buffer )
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( buffer );
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
            if ( _treeDrawData )
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 14u, ShaderStageVisibility::COMPUTE );
                Set( binding._data, _treeDrawData.get(), { 0u, _treeDrawData->getPrimitiveCount() } );
            }
            if ( _treeVisibleCounts )
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 15u, ShaderStageVisibility::COMPUTE );
                Set( binding._data, _treeVisibleCounts.get(), { 0u, _treeVisibleCounts->getPrimitiveCount() } );
            }
        };

        if ( _grassData || _treeData )
        {
            bindVegetationData( bufferInOut );
        }

        if ( refreshData )
        {
            GFX::CommandBuffer& preparation = *GFX::Get( preparationHandle );
            if ( instance->_drawState.hasInstances() )
            {
                GFX::SendPushConstantsCommand cullConstantsCmd = pkg.additionalPushConstantsCmd();
                UniformData* uniforms = cullConstantsCmd._uniformData;
                const float3& rangeCameraEye = renderStagePass._stage == RenderStage::NODE_PREVIEW
                                                   ? cameraSnapshot._eye
                                                   : sgn->sceneGraph()->parentScene().playerCamera()->snapshot()._eye;
                uniforms->set( _ID( "dvd_cameraPosition" ), PushConstantType::VEC3, rangeCameraEye );
                uniforms->set( _ID( "dvd_frustumPlanes" ), PushConstantType::VEC4, cameraSnapshot._frustumPlanes );
                uniforms->set( _ID( "dvd_grassVisibilityDistance" ), PushConstantType::FLOAT, _grassDistance );
                uniforms->set( _ID( "dvd_treeVisibilityDistance" ), PushConstantType::FLOAT, _treeDistance );
                uniforms->set( _ID( "dvd_grassExtents" ), PushConstantType::VEC4, _grassExtents );
                uniforms->set( _ID( "dvd_globalWaterHeight" ), PushConstantType::FLOAT, _globalWaterHeight );
                uniforms->set( _ID( "dvd_terrainChunkOffset" ), PushConstantType::UINT, sgn->dataFlag() );
                uniforms->set( _ID( "dvd_grassInstanceCount" ), PushConstantType::UINT, instance->_drawState._grassInstanceCount );
                uniforms->set( _ID( "dvd_treeInstanceCount" ), PushConstantType::UINT, instance->_drawState._treeInstanceCount );
                uniforms->set( _ID( "dvd_treeWindAnimated" ), PushConstantType::UINT,
                               (_appliedWindAnimation.load( std::memory_order_relaxed ) && TreeWindAnimatedInPass( renderStagePass )) ? 1u : 0u );
                {
                    static_assert(MAX_VEGETATION_TREE_MESHES == 4u, "Tree range uniforms are packed in a uvec4");
                    const auto& ranges = instance->_treeRanges;
                    const uint4 treeRangeOffsets{ ranges[0]._offset, ranges[1]._offset, ranges[2]._offset, ranges[3]._offset };
                    const uint4 treeRangeCounts{ ranges[0]._count, ranges[1]._count, ranges[2]._count, ranges[3]._count };
                    uniforms->set( _ID( "dvd_treeRangeOffsets" ), PushConstantType::UVEC4, treeRangeOffsets );
                    uniforms->set( _ID( "dvd_treeRangeCounts" ), PushConstantType::UVEC4, treeRangeCounts );
                }
                uniforms->set( _ID( "dvd_occlusionViewProjection" ), PushConstantType::MAT4,
                               cameraSnapshot._projectionMatrix * cameraSnapshot._viewMatrix );

                const bool cullGrass = renderStagePass._stage != RenderStage::SHADOW &&
                                       instance->_drawState._grassInstanceCount > 0u;
                const auto addInstanceLocks = [&]( GFX::MemoryBarrierCommand& command, const BufferSyncUsage usage )
                {
                    if ( cullGrass )
                    {
                        DIVIDE_ASSERT( _grassData != nullptr );
                        command._bufferLocks.emplace_back( BufferLock
                        {
                            ._range = VegetationBufferRange( sgn->dataFlag(), _maxGrassInstances, instance->_drawState._grassInstanceCount ),
                            ._type = usage,
                            ._buffer = _grassDrawData->getBufferImpl()
                        } );
                        command._bufferLocks.emplace_back( BufferLock
                        {
                            ._range = { to_size( sgn->dataFlag() ) * sizeof( U32 ), sizeof( U32 ) },
                            ._type = usage,
                            ._buffer = _grassVisibleCounts->getBufferImpl()
                        } );
                    }
                    if ( instance->_drawState._treeInstanceCount > 0u )
                    {
                        DIVIDE_ASSERT( _treeData != nullptr );
                        command._bufferLocks.emplace_back( BufferLock
                        {
                            ._range = VegetationBufferRange( sgn->dataFlag(), _maxTreeInstances, instance->_drawState._treeInstanceCount ),
                            ._type = usage,
                            ._buffer = _treeData->getBufferImpl()
                        } );
                        DIVIDE_ASSERT( _treeDrawData != nullptr && _treeVisibleCounts != nullptr );
                        command._bufferLocks.emplace_back( BufferLock
                        {
                            ._range = VegetationBufferRange( sgn->dataFlag(), _maxTreeInstances, instance->_drawState._treeInstanceCount ),
                            ._type = usage,
                            ._buffer = _treeDrawData->getBufferImpl()
                        } );
                        command._bufferLocks.emplace_back( BufferLock
                        {
                            ._range = { to_size( sgn->dataFlag() ) * MAX_VEGETATION_TREE_MESHES * sizeof( U32 ), MAX_VEGETATION_TREE_MESHES * sizeof( U32 ) },
                            ._type = usage,
                            ._buffer = _treeVisibleCounts->getBufferImpl()
                        } );
                    }
                };

                const auto recordCull = [&]( GFX::CommandBuffer& commands, const bool occlusion )
                {
                    auto beforeCull = GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( commands );
                    addInstanceLocks( *beforeCull, BufferSyncUsage::GPU_READ_TO_GPU_WRITE );
                    addInstanceLocks( *beforeCull, BufferSyncUsage::GPU_WRITE_TO_GPU_WRITE );
                    GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( commands )->_scopeName = occlusion ? "Occlude Vegetation" : "Cull Vegetation";
                    bindVegetationData( commands );
                    GFX::DispatchShaderTaskCommand computeCmd = {};
                    if ( cullGrass )
                    {
                        auto resources = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( commands );
                        resources->_usage = DescriptorSetUsage::PER_PASS;
                        Set( AddBinding( resources->_set, 14u, ShaderStageVisibility::COMPUTE )._data,
                             _grassDrawData.get(), { 0u, _grassDrawData->getPrimitiveCount() } );
                        Set( AddBinding( resources->_set, 15u, ShaderStageVisibility::COMPUTE )._data,
                             _grassVisibleCounts.get(), { 0u, _grassVisibleCounts->getPrimitiveCount() } );
                        computeCmd._workGroupSize.set( 1, 1, 1 );
                        GFX::EnqueueCommand<GFX::BindPipelineCommand>( commands )->_pipeline = occlusion ? _occlusionPipelineGrass : _cullPipelineGrass;
                        GFX::EnqueueCommand( commands, cullConstantsCmd );
                        GFX::EnqueueCommand( commands, computeCmd );
                    }
                    if ( instance->_drawState._treeInstanceCount > 0u )
                    {
                        bindVegetationData( commands );
                        // One workgroup per species: each compacts its own range into the draw buffer.
                        computeCmd._workGroupSize.set( to_U32( MAX_VEGETATION_TREE_MESHES ), 1, 1 );
                        GFX::EnqueueCommand<GFX::BindPipelineCommand>( commands )->_pipeline = occlusion ? _occlusionPipelineTrees : _cullPipelineTrees;
                        GFX::EnqueueCommand( commands, cullConstantsCmd );
                        GFX::EnqueueCommand( commands, computeCmd );
                    }

                    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( commands );
                    auto afterCull = GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( commands );
                    addInstanceLocks( *afterCull, BufferSyncUsage::GPU_WRITE_TO_GPU_READ );
                };
                if ( cullGrass || instance->_drawState._treeInstanceCount > 0u )
                {
                    recordCull( preparation, false );
                    if ( renderStagePass._stage != RenderStage::SHADOW )
                    {
                        recordCull( *GFX::Get( GetOcclusionCommandBuffer( pkg ) ), true );
                    }
                    pkg.independentCompute( true );
                }
            }
        }

        SceneNode::prepareRender( sgn, rComp, pkg, postDrawMemCmd, renderStagePass, cameraSnapshot, refreshData );
    }

    void Vegetation::sceneUpdate( const U64 deltaTimeUS,
                                  SceneGraphNode* sgn,
                                  SceneState& sceneState )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        VegetationInstance* instance = findInstance( sgn->dataFlag() );
        if ( instance != nullptr && instance->_drawState.hasInstances() )
        {
            assert( getState() == ResourceState::RES_LOADED );
            LockGuard<Mutex> w_lock( _sceneUpdateLock );
            const F32 windX = sceneState.windDirX();
            const F32 windZ = sceneState.windDirZ();
            const F32 windPadding = _instanceBoundsPadding *
                (1.f + 0.075f * (3.f * std::abs(windX) + 1.5f * std::abs(windZ) + 0.128f)) + 0.75f;
            if (windPadding > _windBoundsPadding)
            {
                _windBoundsPadding = windPadding;
                const BoundingBox& terrainBounds = _descriptor.parentTerrain->getBounds();
                const float3 padding{windPadding};
                setBounds(BoundingBox{terrainBounds._min - padding, terrainBounds._max + padding});
                sgn->sceneGraph()->invalidateStaticGeometryBounds(*sgn);
            }
            if (instance->_drawState._treeInstanceCount > 0u &&
                _appliedWindAnimation.load(std::memory_order_relaxed) &&
                _windShadowState.update(GFXDevice::FrameCount(), deltaTimeUS, float3{windX, windZ, sceneState.windSpeed()}))
            {
                BoundingSphere bounds;
                bounds.fromBoundingBox(getBounds());
                LightPool& lights = *sceneState.parentScene().lightPool();
                lights.invalidateShadowLayers(bounds, 1u, LightType::DIRECTIONAL);
                const F32 nearTreeRange = 0.33f *
                    std::min(sceneState.renderState().treeVisibility(), sceneState.renderState().generalVisibility());
                if (nearTreeRange > 0.f)
                {
                    const BoundingSphere nearBounds{sceneState.parentScene().playerCamera()->snapshot()._eye, nearTreeRange};
                    lights.invalidateShadowLayers(bounds, 1u, LightType::POINT, nearBounds);
                    lights.invalidateShadowLayers(bounds, 1u, LightType::SPOT, nearBounds);
                }
            }

            const SceneRenderState& renderState = sceneState.renderState();

            const F32 sceneRenderRange = renderState.generalVisibility();
            const F32 sceneGrassDistance = std::min( renderState.grassVisibility(), sceneRenderRange );
            const F32 sceneTreeDistance = std::min( renderState.treeVisibility(), sceneRenderRange );
            _grassDistance = sceneGrassDistance;
            _treeDistance = sceneTreeDistance;

            RenderingComponent* rComp = sgn->get<RenderingComponent>();
            const F32 chunkDistance = instance->_drawState.requiredRenderRange( sceneGrassDistance, sceneTreeDistance );
            if ( !COMPARE( chunkDistance, rComp->renderRange().max ) )
            {
                rComp->setMaxRenderRange( chunkDistance );
            }
            for ( const VegetationTreeRange& range : instance->_treeRanges )
            {
                if ( range._parentNode == nullptr )
                {
                    continue;
                }
                RenderingComponent* treeRoot = range._parentNode->get<RenderingComponent>();
                if ( treeRoot == nullptr || !COMPARE( sceneTreeDistance, treeRoot->renderRange().max ) )
                {
                    ForEachTreeRenderingComponent( *range._parentNode, [sceneTreeDistance]( RenderingComponent& treeComponent )
                    {
                        treeComponent.setMaxRenderRange( sceneTreeDistance );
                    } );
                }
            }
        }

        SceneNode::sceneUpdate( deltaTimeUS, sgn, sceneState );
    }

    void Vegetation::buildDrawCommands( SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut )
    {
        VegetationInstance* instance = findInstance( sgn->dataFlag() );
        if ( instance == nullptr || !instance->_drawState.hasInstances() )
        {
            return;
        }

        GenericDrawCommand& cmd = cmdsOut.emplace_back();
        toggleOption( cmd, CmdRenderOptions::RENDER_INDIRECT );
        cmd._sourceBuffers = _buffer->handles().data();
        cmd._sourceBuffersCount = _buffer->handles().size();
        cmd._cmd.instanceCount = instance->_drawState._grassInstanceCount;
        cmd._cmd.indexCount = to_U32( _buffer->getPartitionIndexCount(_lodPartitions[0]) );
        cmd._cmd.firstIndex = to_U32( _buffer->getPartitionOffset(_lodPartitions[0]) );

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

        FORCE_INLINE bool ScaleAndCheckBounds( const float2 chunkPos, const float2 chunkSize, float2& point ) noexcept
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

    VegetationInstance* Vegetation::findInstance( const U32 chunkID )
    {
        {
            SharedLock<SharedMutex> r_lock( _instanceLock );
            for ( const auto& [id, instance] : _instances )
            {
                if ( id == chunkID )
                {
                    return instance;
                }
            }
        }

        Console::errorfn( LOCALE_STR( "ERROR_VEGETATION_MISSING_CHUNK" ), chunkID );
        return nullptr;
    }

    void Vegetation::registerInstance( const U32 chunkID, VegetationInstance* instance )
    {
        LockGuard<SharedMutex> w_lock(_instanceLock);
        for ( auto&[id, vegInstance] : _instances)
        {
            if ( id == chunkID)
            {
                vegInstance = instance;
                return;
            }
        }

        _instances.emplace_back(chunkID, instance);
    }

    void Vegetation::unregisterInstance( const U32 chunkID )
    {
        LockGuard<SharedMutex> w_lock( _instanceLock );
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
        _drawState = {};

        if ( context().config().debug.renderFilter.grassInstances )
        {
            _grassCandidates = computeTransforms( false );
        }

        bool hasEnabledTrees = false;
        for ( size_t i = 0u; i < parentPtr->_treeMeshNames.size(); ++i )
        {
            hasEnabledTrees |= parentPtr->_descriptor.treeDensities[i] > 0.f;
        }
        if ( context().config().debug.renderFilter.treeInstances && hasEnabledTrees )
        {
            _treeCandidates = computeTransforms( true );
        }
    }

    vector<VegetationData> VegetationInstance::computeTransforms( const bool treeData )
    {
        const U32 ID = _chunk->id();
        const F32 waterHeight = GetVariableF( _chunk->parent().descriptor(), "globalWaterHeight" );
        const U32 waterHeightKey = std::bit_cast<U32>( waterHeight == 0.f ? 0.f : waterHeight );

        ResourcePtr<Vegetation> parent = Get(_parent);
        const VegetationDescriptor& descriptor = parent->_descriptor;
        const string cacheFileName = treeData
            ? Util::StringFormat( "{}_{}_trees_{}_{:08x}_mix5_{:016x}.cache", _chunk->parent().resourceName().c_str(), parent->resourceName().c_str(), ID, waterHeightKey, TreePlacementHash( descriptor ) )
            : Util::StringFormat( "{}_{}_grass_{}_{:08x}_{:016x}.cache", _chunk->parent().resourceName().c_str(), parent->resourceName().c_str(), ID, waterHeightKey, GetHash( descriptor ) );
        Console::printfn( Locale::Get( treeData ? _ID( "CREATE_TREE_START" ) : _ID( "CREATE_GRASS_BEGIN" ) ), ID );

        const U16 cacheVersion = treeData ? TREE_CACHE_VERSION : GRASS_CACHE_VERSION;
        const size_t maxInstances = treeData ? parent->_maxTreeInstances : parent->_maxGrassInstances;
        const bool cacheEnabled = context().config().debug.cache.enabled && context().config().debug.cache.vegetation;
        vector<VegetationData> container;

        ByteBuffer chunkCache;
        bool cacheValid = false;
        constexpr size_t headerSize = sizeof( U16 ) + sizeof( F32 ) + sizeof( size_t );
        if ( cacheEnabled && fileExists( Paths::g_terrainCacheLocation, cacheFileName ) )
        {
            const ResourcePath cachePath = Paths::g_terrainCacheLocation / cacheFileName;
            std::error_code fileError;
            const auto fileSize = std::filesystem::file_size( cachePath.fileSystemPath(), fileError );
            const size_t maxCacheSize = headerSize + maxInstances * sizeof( VegetationData ) + 1u;
            if ( !fileError && fileSize >= headerSize && fileSize <= maxCacheSize &&
                 chunkCache.loadFromFile( Paths::g_terrainCacheLocation, cacheFileName, 0u ) &&
                 chunkCache.storageSize() >= headerSize && chunkCache.storageSize() <= maxCacheSize )
            {
                U16 version = 0u;
                F32 cachedWaterHeight = 0.f;
                size_t containerSize = 0u;
                chunkCache >> version;
                chunkCache >> cachedWaterHeight;
                chunkCache >> containerSize;
                if ( version == cacheVersion && cachedWaterHeight == waterHeight && containerSize <= maxInstances )
                {
                    const size_t dataSize = headerSize + containerSize * sizeof( VegetationData );
                    const size_t storedSize = chunkCache.storageSize();
                    const bool hasTrailer = storedSize == dataSize + 1u &&
                                            to_U8( chunkCache[dataSize] ) == ByteBuffer::BUFFER_FORMAT_VERSION;
                    const bool implicitTrailer = storedSize == dataSize &&
                                                 to_U8( chunkCache[dataSize - 1u] ) == ByteBuffer::BUFFER_FORMAT_VERSION;
                    cacheValid = hasTrailer || implicitTrailer;
                    if ( cacheValid )
                    {
                        container.resize( containerSize );
                        if ( !container.empty() )
                        {
                            chunkCache.read( reinterpret_cast<Byte*>( container.data() ), sizeof( VegetationData ) * container.size() );
                            cacheValid = eastl::all_of( cbegin( container ), cend( container ), [waterHeight, treeData, &descriptor]( const VegetationData& entry )
                            {
                                const bool validTransform = std::isfinite( entry._positionAndScale.x ) &&
                                                            std::isfinite( entry._positionAndScale.y ) &&
                                                            std::isfinite( entry._positionAndScale.z ) &&
                                                            std::isfinite( entry._positionAndScale.w ) &&
                                                            std::isfinite( entry._orientationQuat.x ) &&
                                                            std::isfinite( entry._orientationQuat.y ) &&
                                                            std::isfinite( entry._orientationQuat.z ) &&
                                                            std::isfinite( entry._orientationQuat.w ) &&
                                                            entry._positionAndScale.y >= waterHeight && entry._positionAndScale.w > 0.f;
                                if ( !validTransform || !treeData )
                                {
                                    return validTransform;
                                }
                                const F32 species = entry._data.x;
                                return std::isfinite( species ) && species >= 0.f &&
                                       species < to_F32( descriptor.treeMeshes.size() ) &&
                                       std::floor( species ) == species &&
                                       descriptor.treeDensities[to_size( species )] > 0.f;
                            } );
                        }
                    }
                }
            }
            if ( !cacheValid )
            {
                Console::warnfn( "Regenerating incompatible or invalid vegetation cache: {}", cacheFileName.c_str() );
            }
        }
        if ( !cacheValid )
        {
            container.clear();
            container.reserve( maxInstances );

            std::discrete_distribution<> distribution[] =
            {
                {5, 2, 2, 1},
                {1, 5, 2, 2},
                {2, 1, 5, 2},
                {2, 2, 1, 5}
            };

            std::default_random_engine generator( Random<U32>() );
            std::uniform_real_distribution<F32> rotation( 0.f, 360.f );
            std::array<U32, MAX_VEGETATION_TREE_MESHES> enabledMeshes{};
            std::array<D64, MAX_VEGETATION_TREE_MESHES> treeWeights{};
            size_t enabledCount = 0u;
            std::discrete_distribution<size_t> treeDistribution;
            if ( treeData )
            {
                for ( size_t i = 0u; i < parent->_treeMeshNames.size(); ++i )
                {
                    treeWeights[i] = descriptor.treeDensities[i];
                    if ( treeWeights[i] > 0.0 )
                    {
                        enabledMeshes[enabledCount++] = to_U32( i );
                    }
                }
                DIVIDE_ASSERT( enabledCount > 0u );
                std::shuffle( enabledMeshes.begin(), enabledMeshes.begin() + enabledCount, generator );
                treeDistribution = std::discrete_distribution<size_t>( treeWeights.begin(), treeWeights.begin() + parent->_treeMeshNames.size() );
            }

            const float2 chunkSize = _chunk->getOffsetAndSize().zw;
            const float2 chunkPos = _chunk->getOffsetAndSize().xy;
            const auto& map = treeData ? descriptor.treeMap : descriptor.grassMap;
            const U16 mapWidth = map->dimensions( 0u, 0u ).width;
            const U16 mapHeight = map->dimensions( 0u, 0u ).height;
            const auto& positions = treeData ? parent->_treePositions : parent->_grassPositions;
            const auto& scales = treeData ? descriptor.treeScales : descriptor.grassScales;
            const F32 slopeLimit = treeData ? g_slopeLimitTrees : g_slopeLimitGrass;

            const Terrain& terrain = _chunk->parent();
            const BoundingBox& terrainBounds = terrain.getBounds();

            for ( float2 pos : positions )
            {
                if ( !ScaleAndCheckBounds( chunkPos, chunkSize, pos ) )
                {
                    continue;
                }

                const F32 x_fac = std::clamp( (pos.x - terrainBounds._min.x) / (terrainBounds._max.x - terrainBounds._min.x), 0.f, 1.f );
                const F32 y_fac = std::clamp( (pos.y - terrainBounds._min.z) / (terrainBounds._max.z - terrainBounds._min.z), 0.f, 1.f );

                const Terrain::Vert vert = terrain.getVert( x_fac, y_fac, true );
                if ( vert._position.y < waterHeight )
                {
                    continue;
                }

                // terrain slope should be taken into account
                const F32 dot = Dot( vert._normal, WORLD_Y_AXIS );
                //const F32 lengthSq1 = vert._normal.lengthSquared(); = 1.0f (normalised)
                //const F32 lengthSq2 = WORLD_Y_AXIS.lengthSquared(); = 1.0f (normalised)
                //const F32 length = Sqrt(lengthSq1 * lengthSq2); = 1.0f
                constexpr F32 length = 1.0f;
                const Angle::DEGREES_F angle = Angle::to_DEGREES( Angle::RADIANS_F(std::acos( std::clamp( dot / length, -1.f, 1.f ) ) ));
                if ( angle > slopeLimit )
                {
                    continue;
                }
                const F32 slopeScaleFactor = 1.f - MAP( angle.value, 0.f, slopeLimit, 0.f, 0.9f);

                assert( vert._position != VECTOR3_ZERO );

                UColour4 colour = map->getColour( to_I32( x_fac * (mapWidth - 1u) ), to_I32( y_fac * (mapHeight - 1u) ) );
                if ( !treeData )
                {
                    UColour4 secondary{ 0u, 0u, 0u, 0u };
                    if ( descriptor.grassMapSecondary )
                    {
                        const auto dimensions = descriptor.grassMapSecondary->dimensions( 0u, 0u );
                        secondary = descriptor.grassMapSecondary->getColour(
                            to_I32( x_fac * (dimensions.width - 1u) ), to_I32( y_fac * (dimensions.height - 1u) ) );
                    }
                    colour = GrassPlacementColour( colour, descriptor.grassMapChannelMask,
                                                    secondary, descriptor.grassMapSecondaryChannelMask );
                }
                const U8 index = BestIndex( colour );
                const F32 colourVal = colour[index];
                if ( colourVal <= EPSILON_F32 )
                {
                    continue;
                }

                const U32 meshID = treeData
                    ? (container.size() < enabledCount ? enabledMeshes[container.size()] : to_U32( treeDistribution( generator ) ))
                    : 0u;
                const U8 arrayLayer = treeData ? 0u : to_U8( distribution[index]( generator ) );

                const F32 xmlScale = scales[treeData ? meshID : index];
                // Don't go under 75% of the scale specified in the data files
                const F32 minXmlScale = xmlScale * 7.5f / 10.0f;
                // Don't go over 125% of the scale specified in the data files
                const F32 maxXmlScale = xmlScale * 1.25f;

                const F32 scale = CLAMPED( (colourVal + 1.f) / 256.0f * xmlScale, minXmlScale, maxXmlScale ) *
                                  (treeData ? 1.f : slopeScaleFactor);

                assert( scale > EPSILON_F32 );

                //vert._position.y = (((0.0f*heightExtent) + vert._position.y) - ((0.0f*scale) + vert._position.y)) + vert._position.y;
                VegetationData entry = {};
                entry._positionAndScale.set( vert._position, scale );
                quatf modelRotation;
                if ( treeData )
                {
                    modelRotation.fromEuler( Angle::to_RADIANS( vec3<Angle::DEGREES_F>( descriptor.treeRotations[meshID] ) ) );
                }
                else
                {
                    modelRotation = RotationFromVToU( WORLD_Y_AXIS, vert._normal, WORLD_Z_NEG_AXIS );
                }

                entry._orientationQuat = (quatf( vert._normal, Angle::to_RADIANS(Angle::DEGREES_F(rotation( generator )))) * modelRotation)._elements;
                entry._data = {
                    treeData ? to_F32( meshID ) : to_F32( arrayLayer ),
                    treeData ? 0.f : to_F32( ID ),
                    1.0f,
                    1.0f
                };

                container.push_back( entry );
            }

            if ( cacheEnabled )
            {
                chunkCache.clear();
                chunkCache << cacheVersion;
                chunkCache << waterHeight;
                chunkCache << container.size();
                if ( !container.empty() )
                {
                    chunkCache.append( container.data(), container.size() );
                }
                DIVIDE_EXPECTED_CALL( chunkCache.dumpToFile( Paths::g_terrainCacheLocation, cacheFileName ) );
            }
        }

        Console::printfn( LOCALE_STR( "CREATE_GRASS_END" ) );

        return container;
    }

}
