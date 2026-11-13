// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#include "Headers/RenderingComponent.h"
#include "Headers/AnimationComponent.h"
#include "Headers/BoundsComponent.h"
#include "Headers/EnvironmentProbeComponent.h"
#include "Headers/TransformComponent.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Editor/Headers/Editor.h"

#include "Managers/Headers/ProjectManager.h"
#include "Scenes/Headers/SceneEnvironmentProbePool.h"

#include "Graphs/Headers/SceneGraphNode.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

#include "Scenes/Headers/SceneState.h"
#include "Scenes/Headers/Scene.h"

#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Headers/Mesh.h"

#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/Headers/LightPool.h"
#include "Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h"
#include "Rendering/RenderPass/Headers/NodeBufferedData.h"
#include "Rendering/RenderPass/Headers/RenderPassExecutor.h"

#include "Utility/Headers/Localization.h"

namespace Divide
{

    DVD_INSTANTIATE_SGN_COMPONENT_NODE_OPS(RenderingComponent);

    namespace
    {
        constexpr I16 g_renderRangeLimit = I16_MAX;

        void SetInstanceBufferDefine( Material& material, const bool enabled )
        {
            const auto& bindings = material.shaderAttributes()._vertexBindings;
            if ( enabled && eastl::any_of( bindings.begin(), bindings.end(), []( const VertexBinding& binding )
            {
                return !binding._perVertexInputRate;
            } ) )
            {
                DIVIDE_UNEXPECTED_CALL_MSG( "Storage-buffer instancing cannot offset per-instance vertex attributes" );
            }
            material.setShaderDefine( ShaderType::COUNT, "STORAGE_BUFFER_INSTANCING", enabled );
        }
    }

    void SetNodeIndirectionIndex( RenderPackage& package, const bool uniformIndirection, const U32 indirectionIndex ) noexcept
    {
        if ( uniformIndirection )
        {
            if ( indirectionIndex == NodeIndirectionData::INVALID_IDX )
            {
                DIVIDE_UNEXPECTED_CALL_MSG( "Instanced draw has no node indirection index" );
            }
            package.pushConstantsCmd()._uniformData->set( _ID( "INDIRECT_DATA_IDX" ), PushConstantType::UINT, indirectionIndex );
        }
        else
        {
            package.pushConstantsCmd()._uniformData->remove( _ID( "INDIRECT_DATA_IDX" ) );
        }
    }

    RenderingComponent::RenderingComponent( SceneGraphNode* parentSGN, PlatformContext& context )
        : BaseComponentType<RenderingComponent, ComponentType::RENDERING>( parentSGN, context ),
        _gfxContext( context.gfx() ),
        _config( context.config() ),
        _reflectionProbeIndex( SceneEnvironmentProbePool::SkyProbeLayerIndex() )
    {
        _lodLevels.fill( 0u );
        _lodLockLevels.fill( { false, U8_ZERO } );
        _renderRange.min = 0.f;
        _renderRange.max = g_renderRangeLimit;

        instantiateMaterial( parentSGN->getNode().getMaterialTemplate() );

        toggleRenderOption( RenderOptions::RENDER_GEOMETRY, true );
        toggleRenderOption( RenderOptions::CAST_SHADOWS, true );
        toggleRenderOption( RenderOptions::RECEIVE_SHADOWS, true );
        toggleRenderOption( RenderOptions::IS_VISIBLE, true );

        _showAxis = renderOptionEnabled( RenderOptions::RENDER_AXIS );
        _receiveShadows = renderOptionEnabled( RenderOptions::RECEIVE_SHADOWS );
        _castsShadows = renderOptionEnabled( RenderOptions::CAST_SHADOWS );
        {
            EditorComponentField occlusionCullField = {};
            occlusionCullField._name = "HiZ Occlusion Cull";
            occlusionCullField._data = &_occlusionCull;
            occlusionCullField._type = EditorComponentFieldType::SWITCH_TYPE;
            occlusionCullField._basicType = PushConstantType::BOOL;
            occlusionCullField._readOnly = false;
            _editorComponent.registerField( MOV( occlusionCullField ) );
        }
        {
            EditorComponentField vaxisField = {};
            vaxisField._name = "Show Debug Axis";
            vaxisField._data = &_showAxis;
            vaxisField._type = EditorComponentFieldType::SWITCH_TYPE;
            vaxisField._basicType = PushConstantType::BOOL;
            vaxisField._readOnly = false;
            _editorComponent.registerField( MOV( vaxisField ) );
        }
        {
            EditorComponentField receivesShadowsField = {};
            receivesShadowsField._name = "Receives Shadows";
            receivesShadowsField._data = &_receiveShadows;
            receivesShadowsField._type = EditorComponentFieldType::SWITCH_TYPE;
            receivesShadowsField._basicType = PushConstantType::BOOL;
            receivesShadowsField._readOnly = false;
            _editorComponent.registerField( MOV( receivesShadowsField ) );
        }
        {
            EditorComponentField worldAOField = {};
            worldAOField._name = "World AO Participation";
            worldAOField._tooltip = "This renderable node only. Automatic excludes terrain, animation, vegetation and translucent materials.";
            worldAOField._type = EditorComponentFieldType::DROPDOWN_TYPE;
            worldAOField._basicType = PushConstantType::UINT;
            worldAOField._range = {0.f, 3.f};
            worldAOField._dataGetter = [this](void* dataOut, [[maybe_unused]] void* userData)
            {
                const std::optional<bool> state = drawStateOverride(ShadowMap::WorldAOStagePass());
                *static_cast<U32*>(dataOut) = state.has_value() ? (*state ? 1u : 2u) : 0u;
            };
            worldAOField._dataSetter = [this](const void* data, [[maybe_unused]] void* userData)
            {
                const U32 value = *static_cast<const U32*>(data);
                if (value == 0u)
                {
                    clearDrawStateOverride(ShadowMap::WorldAOStagePass());
                }
                else if (value < 3u)
                {
                    setDrawStateOverride(ShadowMap::WorldAOStagePass(), value == 1u);
                }
                else
                {
                    Console::errorfn("Invalid World AO participation value: {}", value);
                }
            };
            worldAOField._displayNameGetter = [](const U32 index, [[maybe_unused]] void* userData) noexcept
            {
                constexpr const char* names[] = {"Automatic", "Include", "Exclude"};
                return index < 3u ? names[index] : "Invalid";
            };
            _editorComponent.registerField(MOV(worldAOField));
        }
        {
            EditorComponentField castsShadowsField = {};
            castsShadowsField._name = "Casts Shadows";
            castsShadowsField._data = &_castsShadows;
            castsShadowsField._type = EditorComponentFieldType::SWITCH_TYPE;
            castsShadowsField._basicType = PushConstantType::BOOL;
            castsShadowsField._readOnly = false;
            _editorComponent.registerField( MOV( castsShadowsField ) );
        }
        _editorComponent.onChangedCbk( [this]( const std::string_view field )
                                       {
                                           if ( field == "Show Axis" )
                                           {
                                               toggleRenderOption( RenderOptions::RENDER_AXIS, _showAxis );
                                           }
                                           else if ( field == "Receives Shadows" )
                                           {
                                               toggleRenderOption( RenderOptions::RECEIVE_SHADOWS, _receiveShadows );
                                           }
                                           else if ( field == "Casts Shadows" )
                                           {
                                               toggleRenderOption( RenderOptions::CAST_SHADOWS, _castsShadows );
                                           }
                                       } );

        const SceneNode& node = _parentSGN->getNode();
        if ( Is3DObject (node.type()) )
        {
            // Do not cull the sky
            if ( static_cast<const Object3D&>(node).type() == SceneNodeType::TYPE_SKY )
            {
                occlusionCull( false );
            }
        }

        RenderPassExecutor::OnRenderingComponentCreation( this );
    }

    RenderingComponent::~RenderingComponent()
    {
        DestroyResource( _materialInstance );
        RenderPassExecutor::OnRenderingComponentDestruction( this );
    }

    void RenderingComponent::instantiateMaterial( const Handle<Material> material )
    {
        if ( material == INVALID_HANDLE<Material> )
        {
            return;
        }

        Handle<Material> newInstance = INVALID_HANDLE<Material>;
        const bool allowXMLOverrides = _parentSGN->getNode().type() == SceneNodeType::TYPE_SUBMESH;
        if ( Material::Clone(material, newInstance, (_parentSGN->name() + "_instance").c_str(), allowXMLOverrides) &&
             newInstance != INVALID_HANDLE<Material> )
        {
            DestroyResource( _materialInstance );
            _materialInstance = newInstance;
            _materialCoverageRevision = U64_MAX;
            ResourcePtr<Material> mat = Get(_materialInstance);

            DIVIDE_ASSERT( !mat->resourceName().empty() );

            EditorComponentField materialField = {};
            materialField._name = "Material";
            materialField._data = mat;
            materialField._type = EditorComponentFieldType::MATERIAL;
            materialField._readOnly = false;
            // should override any existing entry
            _editorComponent.registerField( MOV( materialField ) );

            EditorComponentField lockLodField = {};
            lockLodField._name = "Rendered LOD Level";
            lockLodField._type = EditorComponentFieldType::PUSH_TYPE;
            lockLodField._basicTypeSize = PushConstantSize::BYTE;
            lockLodField._basicType = PushConstantType::UINT;
            lockLodField._data = &_lodLevels[to_base( RenderStage::DISPLAY )];
            lockLodField._readOnly = true;
            lockLodField._serialise = false;
            _editorComponent.registerField( MOV( lockLodField ) );

            EditorComponentField lockLodLevelField = {};
            lockLodLevelField._name = "Lock LoD Level";
            lockLodLevelField._type = EditorComponentFieldType::PUSH_TYPE;
            lockLodLevelField._range = { 0.0f, to_F32( MAX_LOD_LEVEL ) };
            lockLodLevelField._basicType = PushConstantType::UINT;
            lockLodLevelField._basicTypeSize = PushConstantSize::BYTE;
            lockLodLevelField._data = &_lodLockLevels[to_base( RenderStage::DISPLAY )].second;
            lockLodLevelField._readOnly = false;
            _editorComponent.registerField( MOV( lockLodLevelField ) );

            EditorComponentField renderLodField = {};
            renderLodField._name = "Lock LoD";
            renderLodField._type = EditorComponentFieldType::SWITCH_TYPE;
            renderLodField._basicType = PushConstantType::BOOL;
            renderLodField._data = &_lodLockLevels[to_base( RenderStage::DISPLAY )].first;
            renderLodField._readOnly = false;
            _editorComponent.registerField( MOV( renderLodField ) );

            mat->properties().isStatic( _parentSGN->usageContext() == NodeUsageContext::NODE_STATIC );
            isInstanced( RequiresInstancedIndirection( isInstanced(), mat->properties().isInstanced(), _parentSGN->instanceCount() ) );
            mat->properties().isInstanced( isInstanced() );
            SetInstanceBufferDefine( *mat, _instanceShaderBuffer != nullptr );
        }
    }

    void RenderingComponent::setMinRenderRange( const F32 minRange ) noexcept
    {
        _renderRange.min = std::max( minRange, 0.f );
    }

    void RenderingComponent::setMaxRenderRange( const F32 maxRange ) noexcept
    {
        _renderRange.max = std::min( maxRange, 1.0f * g_renderRangeLimit );
    }

    void RenderingComponent::clearDrawPackages( const RenderStage stage, const RenderPassType pass )
    {
        SharedLock<SharedMutex> r_lock( _renderPackagesLock );
        auto& packagesPerPassType = _renderPackages[to_base( stage )];
        auto& packagesPerVariant = packagesPerPassType[to_base( pass )];
        for ( auto& packagesPerPassIndex : packagesPerVariant )
        {
            for ( auto& pacakgesPerIndex : packagesPerPassIndex )
            {
                for ( const auto& entry : pacakgesPerIndex )
                {
                    Clear( entry->_package );
                }
            }
        }
    }

    void RenderingComponent::updateReflectRefractDescriptors(const bool reflectState, const bool refractState )
    {
        LockGuard<SharedMutex> w_lock( _renderPackagesLock );
        _updateReflection = reflectState;
        _updateRefraction = refractState;
    }

    void RenderingComponent::clearDrawPackages()
    {
        SharedLock<SharedMutex> r_lock( _renderPackagesLock );
        for ( auto& packagesPerPassType : _renderPackages )
        {
            for ( auto& packagesPerVariant : packagesPerPassType )
            {
                for ( auto& packagesPerPassIndex : packagesPerVariant )
                {
                    for ( auto& pacakgesPerIndex : packagesPerPassIndex )
                    {
                        for ( const auto& entry : pacakgesPerIndex )
                        {
                            Clear( entry->_package );
                        }
                    }
                }
            }
        }
    }

    bool RenderingComponent::canDraw( const RenderStagePass& renderStagePass )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );
        PROFILE_TAG( "Node", (_parentSGN->name().c_str()) );

        if (renderStagePass._stage == RenderStage::SHADOW &&
            !IsShadowCaster(_parentSGN->getNode().type(), renderOptionEnabled(RenderOptions::CAST_SHADOWS)))
        {
            return false;
        }

        bool shaderJustFinishedLoading = false;
        bool materialReady = true;
        if ( _materialInstance != INVALID_HANDLE<Material> )
        {
            LockGuard<SharedMutex> w_lock( _drawCommands._dataLock );
            materialReady = Get(_materialInstance)->canDraw( renderStagePass, shaderJustFinishedLoading );
        }

        if ( materialReady )
        {
            if ( shaderJustFinishedLoading )
            {
                _parentSGN->SendEvent(
                {
                    ._type = ECS::CustomEvent::Type::NewShaderReady,
                    ._sourceCmp = this
                });
            }

            return renderOptionEnabled( RenderOptions::IS_VISIBLE );
        }

        return false;
    }

    bool RenderingComponent::shaderPending( const RenderStagePass& renderStagePass ) const
    {
        SharedLock<SharedMutex> r_lock( _drawCommands._dataLock );
        return _materialInstance != INVALID_HANDLE<Material> && Get(_materialInstance)->shaderPending( renderStagePass );
    }

    void RenderingComponent::onParentUsageChanged( const NodeUsageContext context ) const
    {
        if ( _materialInstance != INVALID_HANDLE<Material> )
        {
            Get(_materialInstance)->properties().isStatic( context == NodeUsageContext::NODE_STATIC );
        }
    }

    void RenderingComponent::rebuildMaterial()
    {
        if ( _materialInstance != INVALID_HANDLE<Material> )
        {
            Get(_materialInstance)->rebuild();
            rebuildDrawCommands( true );
        }

        const SceneGraphNode::ChildContainer& children = _parentSGN->getChildren();
        SharedLock<SharedMutex> r_lock( children._lock );
        const U32 childCount = children._count;
        for ( U32 i = 0u; i < childCount; ++i )
        {
            if ( children._data[i]->HasComponents( ComponentType::RENDERING ) )
            {
                children._data[i]->get<RenderingComponent>()->rebuildMaterial();
            }
        }
    }

    void RenderingComponent::setIndexBufferElementOffset(const size_t indexOffset) noexcept
    {
        _indexBufferOffsetCount.store( indexOffset, std::memory_order_relaxed );
    }

    void RenderingComponent::setLoDIndexOffset( const U8 lodIndex, size_t indexOffset, size_t indexCount ) noexcept
    {
        if ( lodIndex < _lodIndexOffsets.size() )
        {
            _lodIndexOffsets[lodIndex] = { indexOffset, indexCount };
        }
    }

    bool RenderingComponent::hasDrawCommands() noexcept
    {
        SharedLock<SharedMutex> r_lock( _drawCommands._dataLock );
        return !_drawCommands._data.empty();
    }

    void RenderingComponent::getMaterialData( NodeMaterialData& dataOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        if ( _materialInstance != INVALID_HANDLE<Material> )
        {
            Get(_materialInstance)->getData( _reflectionProbeIndex, dataOut );
        }
    }

    /// Called after the current node was rendered
    void RenderingComponent::postRender( const SceneRenderState& sceneRenderState, const RenderStagePass& renderStagePass, GFX::CommandBuffer& bufferInOut )
    {
        if ( renderStagePass._stage != RenderStage::DISPLAY ||
             renderStagePass._passType != RenderPassType::MAIN_PASS )
        {
            return;
        }

        if ( renderOptionEnabled( RenderOptions::RENDER_AXIS ) ||
             (sceneRenderState.isEnabledOption( SceneRenderState::RenderOptions::SELECTION_GIZMO ) && _parentSGN->hasFlag( SceneGraphNode::Flags::SELECTED )) ||
             sceneRenderState.isEnabledOption( SceneRenderState::RenderOptions::ALL_GIZMOS ) )
        {
            drawDebugAxis();
        }

        if ( renderOptionEnabled( RenderOptions::RENDER_SKELETON ) ||
             sceneRenderState.isEnabledOption( SceneRenderState::RenderOptions::RENDER_SKELETONS ) )
        {
            drawSkeleton();
        }

        if ( renderOptionEnabled( RenderOptions::RENDER_SELECTION ) )
        {
            drawSelectionGizmo();
        }


        SceneGraphNode* parent = _parentSGN->parent();
        if ( parent != nullptr && !parent->hasFlag( SceneGraphNode::Flags::PARENT_POST_RENDERED ) )
        {
            parent->setFlag( SceneGraphNode::Flags::PARENT_POST_RENDERED );
            if ( parent->HasComponents( ComponentType::RENDERING ) )
            {
                parent->get<RenderingComponent>()->postRender( sceneRenderState, renderStagePass, bufferInOut );
            }
        }
    }

    U8 RenderingComponent::getLoDLevel( const RenderStage renderStage ) const noexcept
    {
        const auto& [_, level] = _lodLockLevels[to_base( renderStage )];
        return CLAMPED( level, U8_ZERO, MAX_LOD_LEVEL );
    }

    U8 RenderingComponent::getLoDLevel( const F32 distSQtoCenter, const RenderStage renderStage, const vec4<U16> lodThresholds )
    {
        return getLoDLevelInternal( distSQtoCenter, renderStage, lodThresholds );
    }

    U8 RenderingComponent::getLoDLevelInternal( const F32 distSQtoCenter, const RenderStage renderStage, const vec4<U16> lodThresholds )
    {
        const auto& [state, level] = _lodLockLevels[to_base( renderStage )];

        if ( state )
        {
            return CLAMPED( level, U8_ZERO, MAX_LOD_LEVEL );
        }

        const F32 distSQtoCenterClamped = std::max( distSQtoCenter, EPSILON_F32 );
        for ( U8 i = 0u; i < MAX_LOD_LEVEL; ++i )
        {
            if ( distSQtoCenterClamped <= to_F32( SQUARED( lodThresholds[i] ) ) )
            {
                return i;
            }
        }

        return MAX_LOD_LEVEL;
    }

    void RenderingComponent::setInstanceShaderBuffer( ShaderBuffer* buffer, const U32 dataIndex ) noexcept
    {
        _instanceShaderBuffer = buffer;
        _indirectionData.setInstanceDataOffset( buffer != nullptr ? dataIndex : 0u );
        if ( _materialInstance != INVALID_HANDLE<Material> )
        {
            SetInstanceBufferDefine( *Get( _materialInstance ), buffer != nullptr );
        }
    }

    void RenderingComponent::ConfigureOITBlendStates( RTBlendStates& states )
    {
        BlendingSettings& accumulation = states._settings[to_base( GFXDevice::ScreenTargets::ACCUMULATION )];
        accumulation.enabled( true );
        accumulation.blendSrc( BlendProperty::ONE );
        accumulation.blendDest( BlendProperty::ONE );
        accumulation.blendOp( BlendOperation::ADD );

        BlendingSettings& normalsRevealage = states._settings[to_base( GFXDevice::ScreenTargets::NORMALS )];
        normalsRevealage.enabled( true );
        normalsRevealage.blendSrc( BlendProperty::ONE );
        normalsRevealage.blendDest( BlendProperty::ONE );
        normalsRevealage.blendOp( BlendOperation::ADD );
        // RGB sums coverage-weighted normals; alpha retains the product of uncovered fractions.
        normalsRevealage.blendSrcAlpha( BlendProperty::ZERO );
        normalsRevealage.blendDestAlpha( BlendProperty::INV_SRC_ALPHA );
        normalsRevealage.blendOpAlpha( BlendOperation::ADD );

        BlendingSettings& modulate = states._settings[to_base( GFXDevice::ScreenTargets::MODULATE )];
        modulate.enabled( true );
        modulate.blendSrc( BlendProperty::ZERO );
        modulate.blendDest( BlendProperty::INV_SRC_COLOR );
        modulate.blendOp( BlendOperation::ADD );
        modulate.blendSrcAlpha( BlendProperty::ZERO );
        modulate.blendDestAlpha( BlendProperty::ONE );
        modulate.blendOpAlpha( BlendOperation::ADD );
    }

    bool RenderingComponent::prepareDrawPackage( const CameraSnapshot& cameraSnapshot,
                                                 const SceneRenderState& sceneRenderState,
                                                 const RenderStagePass& renderStagePass,
                                                 GFX::MemoryBarrierCommand& postDrawMemCmd,
                                                 const bool refreshData )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        bool hasCommands = false;
        bool needsNewShader = false;
        U8 lodLevel = 0u;

        if ( refreshData )
        {
            const bool drawGeometry = renderOptionEnabled( RenderOptions::RENDER_GEOMETRY ) &&
                                      sceneRenderState.isEnabledOption( SceneRenderState::RenderOptions::RENDER_GEOMETRY );
            const bool drawWireframe = renderOptionEnabled( RenderOptions::RENDER_WIREFRAME ) &&
                                       sceneRenderState.isEnabledOption( SceneRenderState::RenderOptions::RENDER_WIREFRAME );
            {
                LockGuard<SharedMutex> w_lock( _drawCommands._dataLock );
                if ( _drawCommands._data.empty() )
                {
                    U8 drawCmdOptions = 0u;
                    if ( drawGeometry )
                    {
                        drawCmdOptions |= to_base( CmdRenderOptions::RENDER_GEOMETRY );
                    }
                    if ( drawWireframe )
                    {
                        drawCmdOptions |= to_base( CmdRenderOptions::RENDER_WIREFRAME );
                    }

                    _parentSGN->getNode().buildDrawCommands( _parentSGN, _drawCommands._data );
                    for ( GenericDrawCommand& cmd : _drawCommands._data )
                    {
                        enableOptions( cmd, drawCmdOptions );
                        isInstanced( RequiresInstancedIndirection( isInstanced(), false, cmd._cmd.instanceCount ) );
                    }
                }
                else
                {
                    for ( GenericDrawCommand& cmd : _drawCommands._data )
                    {
                        setOption( cmd, CmdRenderOptions::RENDER_GEOMETRY, drawGeometry );
                        setOption( cmd, CmdRenderOptions::RENDER_WIREFRAME, drawWireframe );
                    }
                }
                hasCommands = !_drawCommands._data.empty();

                if ( hasCommands && _materialInstance != INVALID_HANDLE<Material> )
                {
                    ResourcePtr<Material> mat = Get(_materialInstance);
                    if ( isInstanced() )
                    {
                        mat->properties().isInstanced( true );
                    }
                    needsNewShader = mat->properties().needsNewShader();
                }
            }

            if ( needsNewShader )
            {
                return false;
            }

            if ( hasCommands )
            {
                const BoundsComponent* bComp = _parentSGN->get<BoundsComponent>();
                const float3& cameraEye = renderStagePass._stage == RenderStage::NODE_PREVIEW
                                              ? cameraSnapshot._eye
                                              : sceneRenderState.parentScene().playerCamera()->snapshot()._eye;
                const SceneNodeRenderState& renderState = _parentSGN->getNode<>().renderState();
                if ( renderState.lod0OnCollision() && bComp->getBoundingBox().containsPoint( cameraEye ) )
                {
                    lodLevel = 0u;
                }
                else
                {
                    const BoundingBox& aabb = bComp->getBoundingBox();
                    const float3 LoDtarget = renderState.useBoundsCenterForLoD() ? aabb.getCenter() : aabb.nearestPoint( cameraEye );
                    const F32 distanceSQToCenter = LoDtarget.distanceSquared( cameraEye );
                    lodLevel = getLoDLevelInternal( distanceSQToCenter, renderStagePass._stage, sceneRenderState.lodThresholds( renderStagePass._stage ) );
                }
            }
        }
        else
        {
            SharedLock<SharedMutex> r_lock( _drawCommands._dataLock );
            hasCommands = !_drawCommands._data.empty();
            needsNewShader = hasCommands && _materialInstance != INVALID_HANDLE<Material> &&
                             Get(_materialInstance)->properties().needsNewShader();
        }

        if ( hasCommands && !needsNewShader )
        {
            RenderPackage& pkg = getDrawPackage( renderStagePass );
            if ( refreshData )
            {
                pkg.lodLevel( lodLevel );
                if ( renderStagePass._stage == RenderStage::DISPLAY || renderStagePass._stage == RenderStage::NODE_PREVIEW )
                {
                    _lodLevels[to_base( renderStagePass._stage )] = lodLevel;
                }
            }

            if ( pkg.pipelineCmd()._pipeline == nullptr )
            {
                // New packages will not have any reflect/refract data
                {
                    LockGuard<SharedMutex> w_lock( _renderPackagesLock );
                    _updateReflection = _updateRefraction = true;
                }

                SetNodeIndirectionIndex( pkg, RequiresUniformIndirection( isInstanced(), _instanceShaderBuffer != nullptr ),
                                        _indirectionBufferEntry );
                PipelineDescriptor pipelineDescriptor = {};

                if ( _materialInstance != INVALID_HANDLE<Material> )
                {
                    ResourcePtr<Material> mat = Get(_materialInstance);
                    pipelineDescriptor._stateBlock = mat->getOrCreateRenderStateBlock( renderStagePass );
                    pipelineDescriptor._shaderProgramHandle = mat->getProgramHandle( renderStagePass );
                    if ( pipelineDescriptor._shaderProgramHandle == INVALID_HANDLE<ShaderProgram> )
                    {
                        return false;
                    }
                    const ShaderProgram* const program = Get( pipelineDescriptor._shaderProgramHandle );
                    if ( program == nullptr )
                    {
                        Console::errorfn( LOCALE_STR( "MATERIAL_SHADER_RESOURCE_MISSING" ), mat->resourceName().c_str() );
                        return false;
                    }
                    pipelineDescriptor._primitiveTopology = mat->topology();
                    pipelineDescriptor._vertexFormat = mat->shaderAttributes();
                    pkg.descriptorSetCmd()._usage = DescriptorSetUsage::PER_DRAW;
                }
                else
                {
                    pipelineDescriptor._stateBlock = {};
                    pipelineDescriptor._shaderProgramHandle = _gfxContext.imShaders()->imWorldShader();
                    pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
                }
                if ( renderStagePass._passType == RenderPassType::TRANSPARENCY_PASS )
                {
                    BlendingSettings& state0 = pipelineDescriptor._blendStates._settings[to_U8( GFXDevice::ScreenTargets::ALBEDO )];
                    state0.enabled( true );
                    state0.blendSrc( BlendProperty::SRC_ALPHA );
                    state0.blendDest( BlendProperty::INV_SRC_ALPHA );
                    state0.blendOp( BlendOperation::ADD );
                }
                else if ( renderStagePass._passType == RenderPassType::OIT_PASS )
                {
                    ConfigureOITBlendStates( pipelineDescriptor._blendStates );
                }

                pipelineDescriptor._stateBlock._primitiveRestartEnabled = primitiveRestartRequired();
                pkg.pipelineCmd()._pipeline = _gfxContext.newPipeline( pipelineDescriptor );
            }

            if ( _materialInstance != INVALID_HANDLE<Material> )
            {
                const ShaderProgram* program = Get( pkg.pipelineCmd()._pipeline->descriptor()._shaderProgramHandle );
                if ( program == nullptr )
                {
                    Console::errorfn( "Material binding preparation failed: package shader is unavailable." );
                    return false;
                }
                pkg.descriptorSetCmd()._resources = Get( _materialInstance )->getResourceBindings( renderStagePass, program->descriptor() );
                if ( pkg.descriptorSetCmd()._resources == nullptr )
                {
                    return false;
                }
            }

            if ( !IsDepthPass( renderStagePass ) )
            {
                SharedLock<SharedMutex> r_lock( _renderPackagesLock );
                const auto updateBinding = [](DescriptorSet& targetSet, const U8 slot, DescriptorSetBindingData& data )
                {
                    for ( U8 i = 0u; i < targetSet._bindingCount; ++i )
                    {
                        DescriptorSetBinding& bindingEntry = targetSet._bindings[i];

                        if ( bindingEntry._slot == slot )
                        {
                            bindingEntry._data = data;
                            return;
                        }
                    }
                    AddBinding(targetSet, slot, ShaderStageVisibility::FRAGMENT )._data = data;
                };

                if ( _updateReflection )
                {
                    DescriptorSetBindingData data{};
                    if ( _reflectionPlanar.first != INVALID_HANDLE<Texture> && renderStagePass._stage != RenderStage::REFLECTION)
                    {
                        //ToDo: Find a way to render reflected items that also have reflections -Ionut
                        Set( data, _reflectionPlanar.first, _reflectionPlanar.second);
                    }
                    else if ( _reflectionCube.first != INVALID_HANDLE<Texture> && renderStagePass._stage != RenderStage::REFLECTION)
                    {
                        Set(data, _reflectionCube.first, _reflectionCube.second);
                    }
                    else
                    {
                        Set(data, Texture::DefaultTexture2D(), Texture::DefaultSampler());
                    }
                    updateBinding( pkg.descriptorSetCmd()._set, 10, data);
                }
                if ( _updateRefraction )
                {
                    DescriptorSetBindingData data{};
                    if ( _refractionPlanar.first != INVALID_HANDLE<Texture> && renderStagePass._stage != RenderStage::REFRACTION )
                    {
                        //ToDo: Find a way to render refracted items that also have refractions -Ionut
                        Set( data, _refractionPlanar.first, _refractionPlanar.second );
                    }
                    else if ( _refractionCube.first != INVALID_HANDLE<Texture> && renderStagePass._stage != RenderStage::REFRACTION)
                    {
                        Set(data, _refractionCube.first, _refractionCube.second);
                    }
                    else
                    {
                        Set( data, Texture::DefaultTexture2D(), Texture::DefaultSampler() );
                    }
                    updateBinding( pkg.descriptorSetCmd()._set, 11, data );
                }

            }
            Attorney::SceneGraphNodeComponent::prepareRender( _parentSGN, *this, pkg, postDrawMemCmd, cameraSnapshot, renderStagePass, refreshData );

            if ( _instanceShaderBuffer != nullptr )
            {
                if ( !DIVIDE_ASSERT( !_parentSGN->HasComponents( ComponentType::ANIMATION ),
                                     "Instance and animation buffers cannot share PER_DRAW slot 13" ) )
                {
                    return false;
                }

                DescriptorSet& set = pkg.descriptorSetCmd()._set;
                DescriptorSetBinding* instanceBinding = nullptr;
                for ( U8 i = 0u; i < set._bindingCount; ++i )
                {
                    if ( set._bindings[i]._slot == 13u )
                    {
                        instanceBinding = &set._bindings[i];
                        break;
                    }
                }
                if ( instanceBinding == nullptr )
                {
                    instanceBinding = &AddBinding( set, 13u, ShaderStageVisibility::VERTEX );
                }
                Set( instanceBinding->_data, _instanceShaderBuffer );
            }
        }

        return hasCommands && !needsNewShader;
    }

    RenderPackage& RenderingComponent::prepareDependency( const CameraSnapshot& cameraSnapshot,
                                                          const RenderStagePass& renderStagePass,
                                                          GFX::MemoryBarrierCommand& postDrawMemCmd )
    {
        RenderPackage& pkg = getDrawPackage( renderStagePass );
        Attorney::SceneGraphNodeComponent::prepareRender( _parentSGN, *this, pkg, postDrawMemCmd, cameraSnapshot, renderStagePass, true );
        return pkg;
    }

    void RenderingComponent::retrieveDrawCommands( const RenderStagePass& stagePass, const U32 cmdOffset, DrawCommandContainer& cmdsInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        RenderPackage& pkg = getDrawPackage( stagePass );
        if (stagePass._passType != RenderPassType::MAIN_PASS)
        {
            RenderStagePass preparedPass = stagePass;
            preparedPass._passType = RenderPassType::MAIN_PASS;
            pkg.drawCommandsOverride(getDrawPackage(preparedPass).drawCommandsOverride());
            pkg.lodLevel( getDrawPackage( preparedPass ).lodLevel() );
        }
        const bool uniformIndirection = RequiresUniformIndirection( isInstanced(), _instanceShaderBuffer != nullptr );
        SetNodeIndirectionIndex( pkg, uniformIndirection, _indirectionBufferEntry );
        pkg.stagePassBaseIndex( BaseIndex( stagePass ) );

        pkg.drawCmdOffset( cmdOffset + to_U32( cmdsInOut.size() ) );

        const auto& [offset, count] = _lodIndexOffsets[std::min( pkg.lodLevel(), to_U8( _lodIndexOffsets.size() - 1 ) )];
        const bool autoIndex = offset != 0u || count != 0u;

        SharedLock<SharedMutex> r_lock2( _drawCommands._dataLock );
        const GenericDrawCommandContainer& commands = pkg.drawCommandsOverride() != nullptr
            ? *pkg.drawCommandsOverride() : _drawCommands._data;
        for ( const GenericDrawCommand& gCmd : commands )
        {
            IndirectIndexedDrawCommand& iCmd = cmdsInOut.emplace_back(gCmd._cmd);

            iCmd.baseInstance = IndirectionBaseInstance( uniformIndirection, _indirectionBufferEntry );

            if ( autoIndex )
            {
                iCmd.firstIndex = to_U32( offset );
                iCmd.indexCount = to_U32( count );
            }

            iCmd.firstIndex += to_U32( _indexBufferOffsetCount.load( std::memory_order_relaxed ) );

        }
    }

    void RenderingComponent::getCommandBuffer( RenderPackage* const pkg, GFX::CommandBuffer& bufferInOut )
    {
        bufferInOut.add( GetCommandBuffer( *pkg ) );
        bufferInOut.add( pkg->pipelineCmd() );
        bufferInOut.add( pkg->descriptorSetCmd() );
        bufferInOut.add( pkg->pushConstantsCmd() );

        GFX::DrawCommand* drawCmd = nullptr;

        {
            SharedLock<SharedMutex> r_lock( _drawCommands._dataLock );
            drawCmd = GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
            drawCmd->_drawCommands = pkg->drawCommandsOverride() != nullptr
                ? *pkg->drawCommandsOverride() : _drawCommands._data;
        }

        U32 startOffset = pkg->drawCmdOffset();
        for ( GenericDrawCommand& gCmd : drawCmd->_drawCommands)
        {
            gCmd._commandOffset = startOffset++;
        }
    }

    RenderPackage& RenderingComponent::getDrawPackage( const RenderStagePass& renderStagePass )
    {
        PackagesPerPassType& packagesPerPassType = _renderPackages[to_base( renderStagePass._stage )];
        PackagesPerVariant& packagesPerVariant = packagesPerPassType[to_base( renderStagePass._passType )];
        PackagesPerPassIndex& packagesPerPassIndex = packagesPerVariant[to_base( renderStagePass._variant )];
        PackagesPerIndex& pacakgesPerIndex = packagesPerPassIndex[to_base( renderStagePass._pass )];

        {
            SharedLock<SharedMutex> r_lock( _renderPackagesLock );
            for ( const auto& entry : pacakgesPerIndex )
            {
                if ( entry->_index == renderStagePass._index )
                {
                    return entry->_package;
                }
            }
        }

        LockGuard<SharedMutex> w_lock( _renderPackagesLock );
        // check again
        for ( const auto& entry : pacakgesPerIndex )
        {
            if ( entry->_index == renderStagePass._index )
            {
                return entry->_package;
            }
        }

        auto entry = std::make_unique<PackageEntry>();
        entry->_index = renderStagePass._index;
        RenderPackage& package = entry->_package;
        pacakgesPerIndex.emplace_back( MOV(entry) );
        return package;
    }

    bool RenderingComponent::updateReflection( const ReflectorType reflectorType,
                                               const U16 reflectionIndex,
                                               const bool inBudget,
                                               Camera* camera,
                                               GFX::CommandBuffer& bufferInOut,
                                               GFX::MemoryBarrierCommand& memCmdInOut )
    {

        if ( inBudget && _materialInstance != INVALID_HANDLE<Material> && _reflectionCallback )
        {
            const RenderTargetID reflectRTID( reflectorType == ReflectorType::PLANAR
                                                             ? RenderTargetNames::REFLECT::PLANAR[reflectionIndex]
                                                             : RenderTargetNames::REFLECT::CUBE[reflectionIndex] );

            const RenderTargetID oitRTID( reflectorType == ReflectorType::PLANAR
                                                             ? RenderTargetNames::REFLECT::PLANAR_OIT[reflectionIndex]
                                                             : INVALID_RENDER_TARGET_ID );

            const U16 stageIndex = reflectorType == ReflectorType::CUBE
                                       ? to_U16( GetNodeReflectionCubeIndexOffset() + reflectionIndex )
                                       : reflectionIndex;
            RenderCbkParams params
            {
                _gfxContext,
                _parentSGN,
                reflectRTID,
                oitRTID,
                RenderTargetNames::REFLECT::UTILS.HI_Z,
                reflectorType,
                RefractorType::COUNT,
                stageIndex,
                camera
            };

            if ( _reflectionCallback( params, bufferInOut, memCmdInOut ) )
            {
                // Grab the updated reflection texture
                RTAttachment* targetAtt = _gfxContext.renderTargetPool().getRenderTarget(reflectRTID)->getAttachment(RTAttachmentType::COLOUR);

                LockGuard<SharedMutex> w_lock( _renderPackagesLock );
                // Grab our internal texture handle and swap our internal texture to the updated one
                if (reflectorType == ReflectorType::PLANAR)
                {
                    _reflectionPlanar = { targetAtt->texture(), targetAtt->_descriptor._sampler };
                    _reflectionCube = { INVALID_HANDLE<Texture>, {} };
                }
                else
                {
                    _reflectionCube = { targetAtt->texture(), targetAtt->_descriptor._sampler };
                    _reflectionPlanar = { INVALID_HANDLE<Texture>, {} };
                }

                // Inform the shader of our update
                _materialUpdateMask |= to_base(MaterialUpdateResult::NEW_REFLECTION);
                _updateReflection = true;

                // Return true so we can keep track of our budget
                return true;
            }
        }

        return false;
    }

    bool RenderingComponent::updateRefraction( const RefractorType refractorType, 
                                               const U16 refractionIndex,
                                               const bool inBudget,
                                               Camera* camera,
                                               GFX::CommandBuffer& bufferInOut,
                                               GFX::MemoryBarrierCommand& memCmdInOut )
    {
        // no default refraction system!
        if ( inBudget && _materialInstance != INVALID_HANDLE<Material> && _refractionCallback )
        {
            const RenderTargetID refractRTID(refractorType == RefractorType::PLANAR
                                                             ? RenderTargetNames::REFRACT::PLANAR[refractionIndex]
                                                             : RenderTargetNames::REFRACT::CUBE[refractionIndex] );

            const RenderTargetID oitRTID(refractorType == RefractorType::PLANAR
                                                             ? RenderTargetNames::REFRACT::PLANAR_OIT[refractionIndex]
                                                             : INVALID_RENDER_TARGET_ID);

            RenderCbkParams params
            {
                _gfxContext,
                _parentSGN,
                refractRTID,
                oitRTID,
                RenderTargetNames::REFRACT::UTILS.HI_Z,
                ReflectorType::COUNT,
                refractorType,
                refractionIndex,
                camera 
            };

            if ( _refractionCallback( params, bufferInOut, memCmdInOut ) )
            {
                // Grab the updated refraction texture
                RTAttachment* targetAtt = _gfxContext.renderTargetPool().getRenderTarget( refractRTID )->getAttachment( RTAttachmentType::COLOUR );

                LockGuard<SharedMutex> w_lock( _renderPackagesLock );
                // Grab our internal texture handle and swap our internal texture to the updated one
                if (refractorType == RefractorType::PLANAR)
                {
                    _refractionPlanar = { targetAtt->texture(), targetAtt->_descriptor._sampler };
                    _refractionCube = { INVALID_HANDLE<Texture>, {} };
                }
                else
                {
                    _refractionCube = { targetAtt->texture(), targetAtt->_descriptor._sampler };
                    _refractionPlanar = { INVALID_HANDLE<Texture>, {} };
                }

                // Inform the shader of our update
                _materialUpdateMask |= to_base( MaterialUpdateResult::NEW_REFRACTION );
                _updateRefraction = true;

                // Return true so we can keep track of our budget
                return true;
            }
        }

        return false;
    }

    void RenderingComponent::updateNearestProbes( const float3& position )
    {
        _envProbes.resize( 0 );
        _reflectionProbeIndex = SceneEnvironmentProbePool::SkyProbeLayerIndex();

        const SceneEnvironmentProbePool* probePool = _context.kernel().projectManager()->getEnvProbes();
        if ( probePool != nullptr )
        {
            probePool->lockProbeList();
            const auto& probes = probePool->getLocked();
            _envProbes.reserve( probes.size() );

            U8 idx = 0u;
            for ( const auto& probe : probes )
            {
                if ( ++idx == Config::MAX_REFLECTIVE_PROBES_PER_PASS )
                {
                    break;
                }
                _envProbes.push_back( probe );
            }
            probePool->unlockProbeList();

            if ( idx > 0u )
            {
                eastl::sort( begin( _envProbes ),
                             end( _envProbes ),
                             [&position]( const auto& a, const auto& b ) noexcept -> bool
                             {
                                 return a->distanceSqTo( position ) < b->distanceSqTo( position );
                             } );

                // We need to update this probe because we are going to use it. This will always lag one frame, but at least we keep updates separate from renders.
                for ( EnvironmentProbeComponent* probe : _envProbes )
                {
                    if ( probe->getBounds().containsPoint( position ) )
                    {
                        _reflectionProbeIndex = probe->poolIndex();
                        probe->queueRefresh();
                        break;
                    }
                }
            }
        }
    }

    /// Draw some kind of selection doodad. May differ if editor is running or not
    void RenderingComponent::drawSelectionGizmo()
    {
        if ( _selectionGizmoDirty )
        {
            _selectionGizmoDirty = false;

            UColour4 colour = UColour4( 64, 255, 128, 255 );
            if constexpr( Config::Build::ENABLE_EDITOR )
            {
                if ( _context.editor().inEditMode() )
                {
                    colour = UColour4( 255, 255, 255, 255 );
                }
            }
            //draw something else (at some point ...)
            BoundsComponent* bComp = _parentSGN->get<BoundsComponent>();
            DIVIDE_ASSERT( bComp != nullptr );
            _selectionGizmoDescriptor.box = bComp->getOBB();
            _selectionGizmoDescriptor.colour = colour;
        }

        _gfxContext.debugDrawOBB( _parentSGN->getGUID() + 12345, _selectionGizmoDescriptor );
    }

    /// Draw the axis arrow gizmo
    void RenderingComponent::drawDebugAxis()
    {
        if ( _axisGizmoLinesDescriptor._lines.empty() )
        {
            Line temp = {};
            temp._widthStart = 10.0f;
            temp._widthEnd = 10.0f;
            temp._positionStart = VECTOR3_ZERO;

            // Red X-axis
            temp._positionEnd = WORLD_X_AXIS * 4;
            temp._colourStart = UColour4( 255, 0, 0, 255 );
            temp._colourEnd = UColour4( 255, 0, 0, 255 );
            _axisGizmoLinesDescriptor._lines.push_back( temp );

            // Green Y-axis
            temp._positionEnd = WORLD_Y_AXIS * 4;
            temp._colourStart = UColour4( 0, 255, 0, 255 );
            temp._colourEnd = UColour4( 0, 255, 0, 255 );
            _axisGizmoLinesDescriptor._lines.push_back( temp );

            // Blue Z-axis
            temp._positionEnd = WORLD_Z_AXIS * 4;
            temp._colourStart = UColour4( 0, 0, 255, 255 );
            temp._colourEnd = UColour4( 0, 0, 255, 255 );
            _axisGizmoLinesDescriptor._lines.push_back( temp );
        }

        _axisGizmoLinesDescriptor.worldMatrix = GetMat4(_parentSGN->get<TransformComponent>()->getWorldOrientation());
        _axisGizmoLinesDescriptor.worldMatrix.scale(VECTOR3_UNIT * 10.f);
        _gfxContext.debugDrawLines( _parentSGN->getGUID() + 321, _axisGizmoLinesDescriptor );
    }

    void RenderingComponent::drawSkeleton()
    {
        const SceneNode& node = _parentSGN->getNode();
        if ( node.type() != SceneNodeType::TYPE_SUBMESH )
        {
            return;
        }

        // Get the animation component of any submesh. They should be synced anyway.
        if ( _parentSGN->HasComponents( ComponentType::ANIMATION ) )
        {
            // Get the skeleton lines from the submesh's animation component
            _skeletonLinesDescriptor._lines = _parentSGN->get<AnimationComponent>()->skeletonLines();
            _skeletonLinesDescriptor.worldMatrix.set( _parentSGN->get<TransformComponent>()->getWorldMatrix() );
            // Submit the skeleton lines to the GPU for rendering
            _gfxContext.debugDrawLines( _parentSGN->getGUID() + 213, _skeletonLinesDescriptor );
        }
    }

    void RenderingComponent::OnData( const ECS::CustomEvent& data )
    {
        SGNComponent::OnData( data );

        switch ( data._type )
        {
            case  ECS::CustomEvent::Type::TransformUpdated:
            {
                const TransformComponent* tComp = static_cast<TransformComponent*>(data._sourceCmp);
                assert( tComp != nullptr );
                updateNearestProbes( tComp->getWorldPosition() );

                _axisGizmoLinesDescriptor.worldMatrix = GetMat4( tComp->getWorldOrientation() );
                _axisGizmoLinesDescriptor.worldMatrix.setTranslation( tComp->getWorldPosition() );
            } break;
            case ECS::CustomEvent::Type::BoundsUpdated:
            {
                _selectionGizmoDirty = true;
            } break;
            default: break;
        }
    }
}
