

#include "config.h"

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

#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Headers/Mesh.h"

#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/Headers/LightPool.h"
#include "Rendering/RenderPass/Headers/NodeBufferedData.h"
#include "Rendering/RenderPass/Headers/RenderPassExecutor.h"

namespace Divide
{

    namespace
    {
        constexpr I16 g_renderRangeLimit = I16_MAX;
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

        instantiateMaterial( parentSGN->getNode().getMaterialTpl() );

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

        if ( Material::Clone(material, _materialInstance, (_parentSGN->name() + "_instance").c_str()) &&
             _materialInstance != INVALID_HANDLE<Material> )
        {
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
            mat->properties().isInstanced( mat->properties().isInstanced() || isInstanced() );
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
                for ( PackageEntry& entry : pacakgesPerIndex )
                {
                    Clear( entry._package );
                }
            }
        }
    }

    void RenderingComponent::updateReflectRefractDescriptors(const bool reflectState, const bool refractState )
    {
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
                        for ( PackageEntry& entry : pacakgesPerIndex )
                        {
                            Clear( entry._package );
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

        // Can we render without a material? Maybe. IDK.
        bool shaderJustFinishedLoading = false;
        if ( _materialInstance == INVALID_HANDLE<Material> || Get(_materialInstance)->canDraw( renderStagePass, shaderJustFinishedLoading ) )
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
        _indexBufferOffsetCount = indexOffset;
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

        // Draw bounding box if needed and only in the final stage to prevent Shadow/PostFX artifacts
        drawBounds( _drawAABB || sceneRenderState.isEnabledOption( SceneRenderState::RenderOptions::RENDER_AABB ),
                    _drawOBB  || sceneRenderState.isEnabledOption( SceneRenderState::RenderOptions::RENDER_OBB ),
                    _drawBS   || sceneRenderState.isEnabledOption( SceneRenderState::RenderOptions::RENDER_BSPHERES ) );

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

    bool RenderingComponent::prepareDrawPackage( const CameraSnapshot& cameraSnapshot,
                                                 const SceneRenderState& sceneRenderState,
                                                 const RenderStagePass& renderStagePass,
                                                 GFX::MemoryBarrierCommand& postDrawMemCmd,
                                                 const bool refreshData )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        bool hasCommands = hasDrawCommands();

        if ( refreshData )
        {
            if ( !hasCommands )
            {
                U8 drawCmdOptions = 0u;
                if (renderOptionEnabled(RenderOptions::RENDER_GEOMETRY) && sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_GEOMETRY))
                {
                    drawCmdOptions |= to_base(CmdRenderOptions::RENDER_GEOMETRY);
                }

                if (renderOptionEnabled(RenderOptions::RENDER_WIREFRAME) && sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_WIREFRAME))
                {
                    drawCmdOptions |= to_base(CmdRenderOptions::RENDER_WIREFRAME);
                }

                LockGuard<SharedMutex> w_lock( _drawCommands._dataLock );
                _parentSGN->getNode().buildDrawCommands( _parentSGN, _drawCommands._data );
                hasCommands = !_drawCommands._data.empty();

                if (hasCommands)
                {
                    for ( GenericDrawCommand& cmd : _drawCommands._data )
                    {
                        enableOptions(cmd, drawCmdOptions);
                        if ( cmd._cmd.instanceCount > 1 )
                        {
                            isInstanced( true );
                        }
                    }
                }
            }
            else
            {
                LockGuard<SharedMutex> w_lock( _drawCommands._dataLock );
                for ( GenericDrawCommand& cmd : _drawCommands._data )
                {
                    setOption(cmd, CmdRenderOptions::RENDER_GEOMETRY,  renderOptionEnabled(RenderOptions::RENDER_GEOMETRY)  && sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_GEOMETRY ));
                    setOption(cmd, CmdRenderOptions::RENDER_WIREFRAME, renderOptionEnabled(RenderOptions::RENDER_WIREFRAME) && sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_WIREFRAME));
                }
            }

            if ( hasCommands )
            {
                const BoundsComponent* bComp = _parentSGN->get<BoundsComponent>();
                const float3& cameraEye = cameraSnapshot._eye;
                const SceneNodeRenderState& renderState = _parentSGN->getNode<>().renderState();
                if ( renderState.lod0OnCollision() && bComp->getBoundingBox().containsPoint( cameraEye ) )
                {
                    _lodLevels[to_base( renderStagePass._stage )] = 0u;
                }
                else
                {
                    const BoundingBox& aabb = bComp->getBoundingBox();
                    const float3 LoDtarget = renderState.useBoundsCenterForLoD() ? aabb.getCenter() : aabb.nearestPoint( cameraEye );
                    const F32 distanceSQToCenter = LoDtarget.distanceSquared( cameraEye );
                    _lodLevels[to_base( renderStagePass._stage )] = getLoDLevelInternal( distanceSQToCenter, renderStagePass._stage, sceneRenderState.lodThresholds( renderStagePass._stage ) );
                }
            }
        }

        if ( hasCommands )
        {
            RenderPackage& pkg = getDrawPackage( renderStagePass );

            if ( pkg.pipelineCmd()._pipeline == nullptr )
            {
                // New packages will not have any reflect/refract data
                _updateReflection = _updateRefraction = true;

                if ( isInstanced() )
                {
                    pkg.pushConstantsCmd()._uniformData->set( _ID( "INDIRECT_DATA_IDX" ), PushConstantType::UINT, 0u );
                    if ( _materialInstance != INVALID_HANDLE<Material> )
                    {
                        Get(_materialInstance)->properties().isInstanced( true );
                    }
                }
                PipelineDescriptor pipelineDescriptor = {};

                if ( _materialInstance != INVALID_HANDLE<Material> )
                {
                    ResourcePtr<Material> mat = Get(_materialInstance);
                    pipelineDescriptor._stateBlock = mat->getOrCreateRenderStateBlock( renderStagePass );
                    pipelineDescriptor._shaderProgramHandle = mat->getProgramHandle( renderStagePass );
                    pipelineDescriptor._primitiveTopology = mat->topology();
                    pipelineDescriptor._vertexFormat = mat->shaderAttributes();
                    pkg.descriptorSetCmd()._usage = DescriptorSetUsage::PER_DRAW;
                    pkg.descriptorSetCmd()._set = mat->getDescriptorSet( renderStagePass );
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
                    BlendingSettings& state0 = pipelineDescriptor._blendStates._settings[to_U8( GFXDevice::ScreenTargets::ACCUMULATION )];
                    state0.enabled( true );
                    state0.blendSrc( BlendProperty::ONE );
                    state0.blendDest( BlendProperty::ONE );
                    state0.blendOp( BlendOperation::ADD );

                    BlendingSettings& state1 = pipelineDescriptor._blendStates._settings[to_U8( GFXDevice::ScreenTargets::REVEALAGE )];
                    state1.enabled( true );
                    state1.blendSrc( BlendProperty::ZERO );
                    state1.blendDest( BlendProperty::INV_SRC_COLOR );
                    state1.blendOp( BlendOperation::ADD );

                    BlendingSettings& state2 = pipelineDescriptor._blendStates._settings[to_U8( GFXDevice::ScreenTargets::MODULATE )];
                    state2.enabled( true );
                    state2.blendSrc( BlendProperty::ZERO );
                    state2.blendDest( BlendProperty::INV_SRC_COLOR );
                    state2.blendOp( BlendOperation::ADD );
                }

                pipelineDescriptor._stateBlock._primitiveRestartEnabled = primitiveRestartRequired();
                pkg.pipelineCmd()._pipeline = _gfxContext.newPipeline( pipelineDescriptor );
            }

            if ( !IsDepthPass( renderStagePass ) )
            {
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
        }

        return hasCommands;
    }

    void RenderingComponent::retrieveDrawCommands( const RenderStagePass& stagePass, const U32 cmdOffset, DrawCommandContainer& cmdsInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        RenderPackage& pkg = getDrawPackage( stagePass );
        if ( isInstanced() )
        {
            pkg.pushConstantsCmd()._uniformData->set( _ID( "INDIRECT_DATA_IDX" ), PushConstantType::UINT, _indirectionBufferEntry);
        }
        pkg.stagePassBaseIndex( BaseIndex( stagePass ) );

        pkg.drawCmdOffset( cmdOffset + to_U32( cmdsInOut.size() ) );

        const auto& [offset, count] = _lodIndexOffsets[std::min( _lodLevels[to_U8( stagePass._stage )], to_U8( _lodIndexOffsets.size() - 1 ) )];
        const bool autoIndex = offset != 0u || count != 0u;

        SharedLock<SharedMutex> r_lock2( _drawCommands._dataLock );
        for ( const GenericDrawCommand& gCmd : _drawCommands._data )
        {
            IndirectIndexedDrawCommand& iCmd = cmdsInOut.emplace_back(gCmd._cmd);

            iCmd.baseInstance = isInstanced() ? 0u : (_indirectionBufferEntry + 1u); //Make sure to substract 1 in the shader!

            if ( autoIndex )
            {
                iCmd.firstIndex = to_U32( offset );
                iCmd.indexCount = to_U32( count );
            }

            iCmd.firstIndex += to_U32(_indexBufferOffsetCount);
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
            drawCmd = GFX::EnqueueCommand(bufferInOut, GFX::DrawCommand{ _drawCommands._data });
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
            for ( PackageEntry& entry : pacakgesPerIndex )
            {
                if ( entry._index == renderStagePass._index )
                {
                    return entry._package;
                }
            }
        }

        LockGuard<SharedMutex> w_lock( _renderPackagesLock );
        // check again
        for ( PackageEntry& entry : pacakgesPerIndex )
        {
            if ( entry._index == renderStagePass._index )
            {
                return entry._package;
            }
        }

        PackageEntry& entry = pacakgesPerIndex.emplace_back();
        entry._index = renderStagePass._index;
        return entry._package;
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

            const U16 reflectionIndexWithOffset = GetNodeReflectionIndexOffset() + // Offset by environment probe count
                                                  (reflectorType == ReflectorType::PLANAR ? Config::MAX_REFLECTIVE_CUBE_NODES_IN_VIEW : 0u) + // Ofset by cube reflectors if target is PLANAR
                                                  reflectionIndex; //Offset by our budget ID
            RenderCbkParams params
            {
                _gfxContext,
                _parentSGN,
                reflectRTID,
                oitRTID,
                RenderTargetNames::REFLECT::UTILS.HI_Z,
                reflectorType,
                RefractorType::COUNT,
                reflectionIndexWithOffset,
                camera
            };

            if ( _reflectionCallback( params, bufferInOut, memCmdInOut ) )
            {
                // Grab the updated reflection texture
                RTAttachment* targetAtt = _gfxContext.renderTargetPool().getRenderTarget(reflectRTID)->getAttachment(RTAttachmentType::COLOUR);

                // Grab our internal texture handle and swap our internal texture to the updated one
                if (reflectorType == ReflectorType::PLANAR)
                {
                    _reflectionPlanar = { targetAtt->texture(), targetAtt->_descriptor._sampler };
                    _reflectionCube = { INVALID_HANDLE<Texture>, {} };
                }
                else
                {
                    _refractionCube = { targetAtt->texture(), targetAtt->_descriptor._sampler };
                    _reflectionPlanar = { INVALID_HANDLE<Texture>, {} };
                }

                // Inform the shader of our update
                _materialUpdateMask |= to_base(MaterialUpdateResult::NEW_REFLECTION);

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

            const U16 refractionIndexWithOffset = GetNodeRefractionIndexOffset() + // Offset by environment probe count
                                                  (refractorType == RefractorType::PLANAR ? Config::MAX_REFRACTIVE_CUBE_NODES_IN_VIEW : 0u) + // Ofset by cube refractors if target is PLANAR
                                                  refractionIndex; //Offset by our budget ID
            RenderCbkParams params
            {
                _gfxContext,
                _parentSGN,
                refractRTID,
                oitRTID,
                RenderTargetNames::REFRACT::UTILS.HI_Z,
                ReflectorType::COUNT,
                refractorType,
                refractionIndexWithOffset,
                camera 
            };

            if ( _refractionCallback( params, bufferInOut, memCmdInOut ) )
            {
                // Grab the updated refraction texture
                RTAttachment* targetAtt = _gfxContext.renderTargetPool().getRenderTarget( refractRTID )->getAttachment( RTAttachmentType::COLOUR );

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

        _axisGizmoLinesDescriptor.worldMatrix = mat4<F32>(GetMatrix(_parentSGN->get<TransformComponent>()->getWorldOrientation()));
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

    void RenderingComponent::drawBounds( const bool AABB, const bool OBB, const bool Sphere )
    {
        if ( !AABB && !Sphere && !OBB )
        {
            return;
        }

        const SceneNode& node = _parentSGN->getNode();
        const bool isSubMesh = node.type() == SceneNodeType::TYPE_SUBMESH;

        if ( AABB )
        {
            const BoundingBox& bb = _parentSGN->get<BoundsComponent>()->getBoundingBox();
            IM::BoxDescriptor descriptor;
            descriptor.min = bb.getMin();
            descriptor.max = bb.getMax();
            descriptor.colour = isSubMesh ? UColour4( 0, 0, 255, 255 ) : UColour4( 255, 0, 255, 255 );
            _gfxContext.debugDrawBox( _parentSGN->getGUID() + 123, descriptor );
        }

        if ( OBB )
        {
            const auto& obb = _parentSGN->get<BoundsComponent>()->getOBB();
            IM::OBBDescriptor descriptor;
            descriptor.box = obb;
            descriptor.colour = isSubMesh ? UColour4( 128, 0, 255, 255 ) : UColour4( 255, 0, 128, 255 );

            _gfxContext.debugDrawOBB( _parentSGN->getGUID() + 123, descriptor );
        }

        if ( Sphere )
        {
            const BoundingSphere& bs = _parentSGN->get<BoundsComponent>()->getBoundingSphere();
            IM::SphereDescriptor descriptor;
            descriptor.center = bs.getCenter();
            descriptor.radius = bs.getRadius();
            descriptor.colour = isSubMesh ? UColour4( 0, 255, 0, 255 ) : UColour4( 255, 255, 0, 255 );
            descriptor.slices = 16u;
            descriptor.stacks = 16u;
            _gfxContext.debugDrawSphere( _parentSGN->getGUID() + 123, descriptor );
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

                _axisGizmoLinesDescriptor.worldMatrix = mat4<F32>( GetMatrix( tComp->getWorldOrientation() ) );
                _axisGizmoLinesDescriptor.worldMatrix.setTranslation( tComp->getWorldPosition() );
            } break;
            case ECS::CustomEvent::Type::DrawBoundsChanged:
            {
                const BoundsComponent* bComp = static_cast<BoundsComponent*>(data._sourceCmp);
                toggleBoundsDraw( bComp->showAABB(), bComp->showBS(), bComp->showOBB(), false );
            } break;
            case ECS::CustomEvent::Type::BoundsUpdated:
            {
                _selectionGizmoDirty = true;
            } break;
            default: break;
        }
    }
}
