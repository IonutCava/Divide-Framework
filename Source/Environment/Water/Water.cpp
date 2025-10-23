

#include "Headers/Water.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/Configuration.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/File/Headers/FileManagement.h"

#include "Managers/Headers/ProjectManager.h"
#include "Managers/Headers/RenderPassManager.h"

#include "Rendering/Camera/Headers/Camera.h"

#include "Geometry/Material/Headers/Material.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"
#include "ECS/Components/Headers/NavigationComponent.h"

namespace Divide
{

    namespace
    {
        // how far to offset the clipping planes for reflections in order to avoid artefacts at water/geometry intersections with high wave noise factors
        constexpr F32 g_reflectionPlaneCorrectionHeight = -1.0f;
    }

    WaterPlane::WaterPlane( const ResourceDescriptor<WaterPlane>& descriptor )
        : SceneNode( descriptor,
                     GetSceneNodeType<WaterPlane>(),
                     to_base( ComponentType::TRANSFORM ) )
    {
        _fogStartEnd = { 648.f, 1300.f };
        _noiseTile = { 15.0f, 15.0f };
        _noiseFactor = { 0.1f, 0.1f };
        _refractionTint = { 0.f, 0.567f, 0.845f };
        _waterDistanceFogColour = { 0.9f, 0.9f, 1.f };
        _dimensions = { 500u, 500u, 500u };
        // The water doesn't cast shadows, doesn't need ambient occlusion and doesn't have real "depth"
        renderState().addToDrawExclusionMask( RenderStage::SHADOW );
    }

    WaterPlane::~WaterPlane()
    {
        Camera::DestroyCamera( _reflectionCam );
    }

    void WaterPlane::onEditorChange( std::string_view ) noexcept
    {

        if ( _fogStartEnd.y <= _fogStartEnd.y )
        {
            _fogStartEnd.y = _fogStartEnd.x + 0.1f;
        }
    }

    bool WaterPlane::load( PlatformContext& context )
    {
        if ( _plane != INVALID_HANDLE<Quad3D> )
        {
            return false;
        }

        setState( ResourceState::RES_LOADING );

        _reflectionCam = Camera::CreateCamera( resourceName() + "_reflectionCam", Camera::Mode::STATIC );

        const auto name = resourceName();

        ResourceDescriptor<Quad3D> waterPlane( "waterPlane" );
        waterPlane.flag( true );  // No default material
        waterPlane.waitForReady( true );

        _plane = CreateResource( waterPlane );

        SamplerDescriptor defaultSampler = {};
        defaultSampler._wrapU = TextureWrap::REPEAT;
        defaultSampler._wrapV = TextureWrap::REPEAT;
        defaultSampler._wrapW = TextureWrap::REPEAT;
        defaultSampler._anisotropyLevel = 4u;

        std::atomic_uint loadTasks = 0u;

        ResourceDescriptor<Texture> waterTexture( "waterTexture_" + name );
        waterTexture.assetName( "terrain_water_NM_old.jpg" );
        waterTexture.assetLocation( Paths::g_imagesLocation );
        waterTexture.waitForReady( false );

        TextureDescriptor& texDescriptor = waterTexture._propertyDescriptor;
        texDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
        texDescriptor._textureOptions._alphaChannelTransparency = false;

        Handle<Texture> waterNM = CreateResource( waterTexture, loadTasks );

        ResourceDescriptor<Material> waterMaterial( "waterMaterial_" + name );
        Handle<Material> waterMatHandle = CreateResource( waterMaterial );
        ResourcePtr<Material> waterMat = Get(waterMatHandle);

        waterMat->updatePriorirty( Material::UpdatePriority::Medium );
        waterMat->properties().shadingMode( ShadingMode::BLINN_PHONG );
        waterMat->properties().bumpMethod( BumpMethod::NORMAL );
        waterMat->properties().isStatic( true );
        waterMat->addShaderDefine( "ENABLE_TBN" );

        WAIT_FOR_CONDITION( loadTasks.load() == 0u );

        waterMat->setTexture( TextureSlot::NORMALMAP, waterNM, defaultSampler, TextureOperation::REPLACE );
        waterMat->computeShaderCBK( []( [[maybe_unused]] Material* material, const RenderStagePass stagePass )
        {
            ShaderModuleDescriptor vertModule = {};
            vertModule._moduleType = ShaderType::VERTEX;
            vertModule._sourceFile = "water.glsl";

            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "water.glsl";

            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._name = "waterColour";
            if ( IsDepthPass( stagePass ) )
            {
                if ( stagePass._stage == RenderStage::DISPLAY )
                {
                    shaderDescriptor._name = "waterPrePass";
                    shaderDescriptor._globalDefines.emplace_back( "PRE_PASS" );
                    shaderDescriptor._modules.push_back( vertModule );
                    shaderDescriptor._modules.push_back( fragModule );
                }
                else
                {
                    shaderDescriptor._name = "waterDepthPass";
                    shaderDescriptor._modules.push_back( vertModule );
                }
            }
            else
            {
                shaderDescriptor._modules.push_back( vertModule );
                shaderDescriptor._modules.push_back( fragModule );
            }

            shaderDescriptor._globalDefines.emplace_back( "_refractionTint PushData0[0].xyz" );
            shaderDescriptor._globalDefines.emplace_back( "_specularShininess PushData0[0].w" );
            shaderDescriptor._globalDefines.emplace_back( "_waterDistanceFogColour PushData0[1].xyz" );
            shaderDescriptor._globalDefines.emplace_back( "_noiseTile PushData0[2].xy" );
            shaderDescriptor._globalDefines.emplace_back( "_noiseFactor PushData0[2].zw)" );
            shaderDescriptor._globalDefines.emplace_back( "_fogStartEndDistances PushData0[3].xy" );

            return shaderDescriptor;
        } );

        waterMat->properties().roughness( 0.01f );
        waterMat->setPipelineLayout( PrimitiveTopology::TRIANGLE_STRIP, Get(_plane)->geometryBuffer()->generateAttributeMap() );

        setMaterialTpl( waterMatHandle );

        const F32 halfWidth = _dimensions.width * 0.5f;
        const F32 halfLength = _dimensions.height * 0.5f;

        setBounds( BoundingBox( float3( -halfWidth, -_dimensions.depth, -halfLength ), float3( halfWidth, 0, halfLength ) ) );

        return SceneNode::load( context );
    }

    void WaterPlane::postLoad( SceneGraphNode* sgn )
    {
        PlatformContext& pContext = sgn->context();
        registerEditorComponent( pContext );
        DIVIDE_ASSERT( _editorComponent != nullptr );

        EditorComponentField blurReflectionField = {};
        blurReflectionField._name = "Blur reflections";
        blurReflectionField._data = &_blurReflections;
        blurReflectionField._type = EditorComponentFieldType::PUSH_TYPE;
        blurReflectionField._readOnly = false;
        blurReflectionField._basicType = PushConstantType::BOOL;

        _editorComponent->registerField( MOV( blurReflectionField ) );

        EditorComponentField blurRefractionField = {};
        blurRefractionField._name = "Blur refractions";
        blurRefractionField._data = &_blurRefractions;
        blurRefractionField._type = EditorComponentFieldType::PUSH_TYPE;
        blurRefractionField._readOnly = false;
        blurRefractionField._basicType = PushConstantType::BOOL;

        _editorComponent->registerField(MOV(blurRefractionField));

        EditorComponentField blurKernelSizeField = {};
        blurKernelSizeField._name = "Blur kernel size";
        blurKernelSizeField._data = &_blurKernelSize;
        blurKernelSizeField._type = EditorComponentFieldType::SLIDER_TYPE;
        blurKernelSizeField._readOnly = false;
        blurKernelSizeField._basicType = PushConstantType::UINT;
        blurKernelSizeField._basicTypeSize = PushConstantSize::WORD;
        blurKernelSizeField._range = { 2.f, 20.f };
        blurKernelSizeField._step = 1.f;

        _editorComponent->registerField( MOV( blurKernelSizeField ) );

        EditorComponentField reflPlaneOffsetField = {};
        reflPlaneOffsetField._name = "Reflection Plane Offset";
        reflPlaneOffsetField._data = &_reflPlaneOffset;
        reflPlaneOffsetField._range = { -5.0f, 5.0f };
        reflPlaneOffsetField._type = EditorComponentFieldType::PUSH_TYPE;
        reflPlaneOffsetField._readOnly = false;
        reflPlaneOffsetField._basicType = PushConstantType::FLOAT;

        _editorComponent->registerField( MOV( reflPlaneOffsetField ) );

        EditorComponentField refrPlaneOffsetField = {};
        refrPlaneOffsetField._name = "Refraction Plane Offset";
        refrPlaneOffsetField._data = &_refrPlaneOffset;
        refrPlaneOffsetField._range = { -5.0f, 5.0f };
        refrPlaneOffsetField._type = EditorComponentFieldType::PUSH_TYPE;
        refrPlaneOffsetField._readOnly = false;
        refrPlaneOffsetField._basicType = PushConstantType::FLOAT;

        _editorComponent->registerField( MOV( refrPlaneOffsetField ) );

        EditorComponentField fogDistanceField = {};
        fogDistanceField._name = "Fog start/end distances";
        fogDistanceField._data = &_fogStartEnd;
        fogDistanceField._range = { 0.0f, 4096.0f };
        fogDistanceField._type = EditorComponentFieldType::PUSH_TYPE;
        fogDistanceField._readOnly = false;
        fogDistanceField._basicType = PushConstantType::VEC2;

        _editorComponent->registerField( MOV( fogDistanceField ) );

        EditorComponentField waterFogField = {};
        waterFogField._name = "Water fog colour";
        waterFogField._data = &_waterDistanceFogColour;
        waterFogField._type = EditorComponentFieldType::PUSH_TYPE;
        waterFogField._readOnly = false;
        waterFogField._basicType = PushConstantType::FCOLOUR3;

        _editorComponent->registerField( MOV( waterFogField ) );

        EditorComponentField noiseTileSizeField = {};
        noiseTileSizeField._name = "Noise tile factor";
        noiseTileSizeField._data = &_noiseTile;
        noiseTileSizeField._range = { 0.0f, 1000.0f };
        noiseTileSizeField._type = EditorComponentFieldType::PUSH_TYPE;
        noiseTileSizeField._readOnly = false;
        noiseTileSizeField._basicType = PushConstantType::VEC2;

        _editorComponent->registerField( MOV( noiseTileSizeField ) );

        EditorComponentField noiseFactorField = {};
        noiseFactorField._name = "Noise factor";
        noiseFactorField._data = &_noiseFactor;
        noiseFactorField._range = { 0.0f, 10.0f };
        noiseFactorField._type = EditorComponentFieldType::PUSH_TYPE;
        noiseFactorField._readOnly = false;
        noiseFactorField._basicType = PushConstantType::VEC2;

        _editorComponent->registerField( MOV( noiseFactorField ) );

        EditorComponentField refractionTintField = {};
        refractionTintField._name = "Refraction tint";
        refractionTintField._data = &_refractionTint;
        refractionTintField._type = EditorComponentFieldType::PUSH_TYPE;
        refractionTintField._readOnly = false;
        refractionTintField._basicType = PushConstantType::FCOLOUR3;

        _editorComponent->registerField( MOV( refractionTintField ) );

        EditorComponentField specularShininessField = {};
        specularShininessField._name = "Specular Shininess";
        specularShininessField._data = &_specularShininess;
        specularShininessField._type = EditorComponentFieldType::PUSH_TYPE;
        specularShininessField._readOnly = false;
        specularShininessField._range = { 0.01f, Material::MAX_SHININESS };
        specularShininessField._basicType = PushConstantType::FLOAT;

        _editorComponent->registerField( MOV( specularShininessField ) );

        _editorComponent->onChangedCbk( [this]( const std::string_view field ) noexcept
        {
            onEditorChange( field );
        });
        NavigationComponent* nComp = sgn->get<NavigationComponent>();
        if ( nComp != nullptr )
        {
            nComp->navigationContext( NavigationComponent::NavigationContext::NODE_OBSTACLE );
        }

        RigidBodyComponent* rComp = sgn->get<RigidBodyComponent>();
        if ( rComp != nullptr )
        {
            rComp->physicsCollisionGroup( PhysicsGroup::GROUP_STATIC );
        }

        const F32 halfWidth = _dimensions.width * 0.5f;
        const F32 halfLength = _dimensions.height * 0.5f;

        Get(_plane)->setCorner( Quad3D::CornerLocation::TOP_LEFT, float3( -halfWidth, 0, -halfLength ) );
        Get(_plane)->setCorner( Quad3D::CornerLocation::TOP_RIGHT, float3( halfWidth, 0, -halfLength ) );
        Get(_plane)->setCorner( Quad3D::CornerLocation::BOTTOM_LEFT, float3( -halfWidth, 0, halfLength ) );
        Get(_plane)->setCorner( Quad3D::CornerLocation::BOTTOM_RIGHT, float3( halfWidth, 0, halfLength ) );
        Get(_plane)->setNormal( Quad3D::CornerLocation::CORNER_ALL, WORLD_Y_AXIS );
        _boundingBox.set( float3( -halfWidth, -_dimensions.depth, -halfLength ), float3( halfWidth, 0, halfLength ) );

        RenderingComponent* renderable = sgn->get<RenderingComponent>();

        // If the reflector is reasonably sized, we should keep LoD fixed so that we always update reflections
        if ( sgn->context().config().rendering.lodThresholds.x < std::max( halfWidth, halfLength ) )
        {
            renderable->lockLoD( 0u );
        }

        renderable->setReflectionCallback( [this]( RenderCbkParams& params, GFX::CommandBuffer& commandsInOut, GFX::MemoryBarrierCommand& memCmdInOut )
                                           {
                                               return updateReflection( params, commandsInOut, memCmdInOut );
                                           } );

        renderable->setRefractionCallback( [this]( RenderCbkParams& params, GFX::CommandBuffer& commandsInOut, GFX::MemoryBarrierCommand& memCmdInOut )
                                           {
                                               return updateRefraction( params, commandsInOut, memCmdInOut );
                                           } );

        renderable->toggleRenderOption( RenderingComponent::RenderOptions::CAST_SHADOWS, false );

        SceneNode::postLoad( sgn );
    }

    bool WaterPlane::postLoad()
    {
        return SceneNode::postLoad();
    }

    bool WaterPlane::unload()
    {
        DestroyResource(_plane);
        return SceneNode::unload();
    }

    void WaterPlane::sceneUpdate( const U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState )
    {
        sceneState.waterBodies().emplace_back(WaterBodyData
        {
            ._positionW = sgn->get<TransformComponent>()->getWorldPosition(),
            ._extents = { to_F32( _dimensions.width ),
                          to_F32( _dimensions.depth ),
                          to_F32( _dimensions.height ),
                          0.f}
        });

        SceneNode::sceneUpdate( deltaTimeUS, sgn, sceneState );
    }

    void WaterPlane::prepareRender( SceneGraphNode* sgn,
                                    RenderingComponent& rComp,
                                    RenderPackage& pkg,
                                    GFX::MemoryBarrierCommand& postDrawMemCmd,
                                    const RenderStagePass renderStagePass,
                                    const CameraSnapshot& cameraSnapshot,
                                    const bool refreshData )
    {
        PushConstantsStruct& fastData = pkg.pushConstantsCmd()._fastData;
        fastData.data[0]._vec[0].set( refractionTint(), specularShininess() );
        fastData.data[0]._vec[1].set( waterDistanceFogColour(), 0.f );
        fastData.data[0]._vec[2].set( noiseTile(), noiseFactor() );
        fastData.data[0]._vec[3].xy = fogStartEnd();

        VertexBuffer* waterBuffer = Get(_plane)->geometryBuffer();
        waterBuffer->commitData(postDrawMemCmd);
        rComp.setIndexBufferElementOffset(waterBuffer->firstIndexOffsetCount());

        SceneNode::prepareRender( sgn, rComp, pkg, postDrawMemCmd, renderStagePass, cameraSnapshot, refreshData );
    }

    bool WaterPlane::PointUnderwater( const SceneGraphNode* sgn, const float3& point ) noexcept
    {
        return sgn->get<BoundsComponent>()->getBoundingBox().containsPoint( point );
    }

    void WaterPlane::buildDrawCommands( SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut )
    {
        VertexBuffer* waterBuffer = Get(_plane)->geometryBuffer();

        GenericDrawCommand& cmd = cmdsOut.emplace_back();
        toggleOption( cmd, CmdRenderOptions::RENDER_INDIRECT );
        cmd._sourceBuffers = waterBuffer->handles().data();
        cmd._sourceBuffersCount = waterBuffer->handles().size();
        cmd._cmd.indexCount = to_U32(waterBuffer->getIndexCount() );

        SceneNode::buildDrawCommands( sgn, cmdsOut );
    }

    /// update water refraction
    bool WaterPlane::updateRefraction( RenderCbkParams& renderParams, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut ) const
    {
        DIVIDE_ASSERT(renderParams._refractType != RefractorType::COUNT);
        if (renderParams._refractType == RefractorType::CUBE)
        {
            return false;
        }

        // If we are above water, process the plane's refraction.
        // If we are below, we render the scene normally
        const bool underwater = PointUnderwater( renderParams._sgn, renderParams._camera->snapshot()._eye );
        Plane<F32> refractionPlane;
        updatePlaneEquation( renderParams._sgn, refractionPlane, underwater, refrPlaneOffset() );
        refractionPlane._distance += g_reflectionPlaneCorrectionHeight;

        RenderPassParams params = {};
        SetDefaultDrawDescriptor( params );

        params._sourceNode = renderParams._sgn;
        params._targetHIZ = renderParams._hizTarget;
        params._targetOIT = renderParams._oitTarget;
        params._minExtents.set( 1.0f );
        params._stagePass = {
            ._stage = RenderStage::REFRACTION,
            ._passType = RenderPassType::COUNT,
            ._index = renderParams._passIndex,
            ._variant = static_cast<RenderStagePass::VariantType>(renderParams._refractType)
        };
        params._target = renderParams._renderTarget;
        params._clippingPlanes.set( 0, refractionPlane );
        params._passName = "Refraction";
        params._clearDescriptorMainPass[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;
        params._clearDescriptorMainPass[to_base(RTColourAttachmentSlot::SLOT_0)] = { DefaultColours::BLUE, true };

        if ( !underwater )
        {
            params._drawMask &= ~(1u << to_base(RenderPassParams::Flags::DRAW_DYNAMIC_NODES));
        }

        auto& passManager = renderParams._context.context().kernel().renderPassManager();
        passManager->doCustomPass( renderParams._camera, params, bufferInOut, memCmdInOut );

        if (_blurRefractions)
        {
            RenderTarget* refractTarget = renderParams._context.renderTargetPool().getRenderTarget(renderParams._renderTarget);
            RenderTargetHandle refractionTargetHandle{
                refractTarget,
                renderParams._renderTarget
            };

            RenderTarget* refractBlurTarget = renderParams._context.renderTargetPool().getRenderTarget(RenderTargetNames::REFRACT::UTILS.BLUR);
            RenderTargetHandle refractionBlurBuffer{
                refractBlurTarget,
                RenderTargetNames::REFRACT::UTILS.BLUR
            };

            renderParams._context.blurTarget(refractionTargetHandle,
                                             refractionBlurBuffer,
                                             refractionTargetHandle,
                                             RTAttachmentType::COLOUR,
                                             RTColourAttachmentSlot::SLOT_0,
                                             _blurKernelSize,
                                             true,
                                             1,
                                             bufferInOut);
        }

        const PlatformContext& context = passManager->parent().platformContext();
        const RenderTarget* rt = context.gfx().renderTargetPool().getRenderTarget( params._target );

        GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
        computeMipMapsCommand._texture = rt->getAttachment( RTAttachmentType::COLOUR )->texture();
        computeMipMapsCommand._usage = ImageUsage::SHADER_READ;
        GFX::EnqueueCommand( bufferInOut, computeMipMapsCommand );

        return true;
    }

    /// Update water reflections
    bool WaterPlane::updateReflection( RenderCbkParams& renderParams, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut ) const
    {
        DIVIDE_ASSERT(renderParams._reflectType != ReflectorType::COUNT);
        if (renderParams._reflectType == ReflectorType::CUBE)
        {
            return false;
        }

        // If we are above water, process the plane's refraction.
        // If we are below, we render the scene normally
        const bool underwater = PointUnderwater( renderParams._sgn, renderParams._camera->snapshot()._eye );
        if ( underwater )
        {
            //ToDo: Validate that this is correct -Ionut.
            return false;
        }

        Plane<F32> reflectionPlane;
        updatePlaneEquation( renderParams._sgn, reflectionPlane, !underwater, reflPlaneOffset() );

        // Reset reflection cam
        renderParams._camera->updateLookAt();
        _reflectionCam->fromCamera( *renderParams._camera );
        if ( !underwater )
        {
            reflectionPlane._distance += g_reflectionPlaneCorrectionHeight;
            _reflectionCam->setReflection( reflectionPlane );
        }

        RenderPassParams params = {};
        SetDefaultDrawDescriptor( params );

        params._sourceNode = renderParams._sgn;
        params._targetHIZ = renderParams._hizTarget;
        params._targetOIT = renderParams._oitTarget;
        params._minExtents.set( 1.5f );
        params._stagePass = 
        {
            ._stage = RenderStage::REFLECTION,
            ._passType = RenderPassType::COUNT,
            ._index = renderParams._passIndex,
            ._variant = static_cast<RenderStagePass::VariantType>(renderParams._reflectType)
        };
        params._target = renderParams._renderTarget;
        params._clippingPlanes.set( 0, reflectionPlane );
        params._passName = "Reflection";
        params._clearDescriptorMainPass[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;
        params._clearDescriptorMainPass[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::BLUE, true };

        params._drawMask &= ~(1u << to_base(RenderPassParams::Flags::DRAW_DYNAMIC_NODES));

        auto& passManager = renderParams._context.context().kernel().renderPassManager();
        passManager->doCustomPass( _reflectionCam, params, bufferInOut, memCmdInOut );

        if ( _blurReflections )
        {
            RenderTarget* reflectTarget = renderParams._context.renderTargetPool().getRenderTarget( renderParams._renderTarget );
            RenderTargetHandle reflectionTargetHandle{
                reflectTarget,
                renderParams._renderTarget
            };

            RenderTarget* reflectBlurTarget = renderParams._context.renderTargetPool().getRenderTarget( RenderTargetNames::REFLECT::UTILS.BLUR );
            RenderTargetHandle reflectionBlurBuffer{
                reflectBlurTarget,
                RenderTargetNames::REFLECT::UTILS.BLUR
            };

            renderParams._context.blurTarget( reflectionTargetHandle,
                                              reflectionBlurBuffer,
                                              reflectionTargetHandle,
                                              RTAttachmentType::COLOUR,
                                              RTColourAttachmentSlot::SLOT_0,
                                              _blurKernelSize,
                                              true,
                                              1,
                                              bufferInOut );
        }

        const PlatformContext& context = passManager->parent().platformContext();
        const RenderTarget* rt = context.gfx().renderTargetPool().getRenderTarget( params._target );

        GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
        computeMipMapsCommand._texture = rt->getAttachment( RTAttachmentType::COLOUR )->texture();
        computeMipMapsCommand._usage = ImageUsage::SHADER_READ;
        GFX::EnqueueCommand( bufferInOut, computeMipMapsCommand );

        return true;
    }

    void WaterPlane::updatePlaneEquation( const SceneGraphNode* sgn, Plane<F32>& plane, const bool reflection, const F32 offset ) const
    {
        const F32 waterLevel = sgn->get<TransformComponent>()->getWorldPosition().y * (reflection ? -1.f : 1.f);
        const quatf& orientation = sgn->get<TransformComponent>()->getWorldOrientation();

        plane.set( Normalized( float3( orientation * (reflection ? WORLD_Y_AXIS : WORLD_Y_NEG_AXIS) ) ), offset + waterLevel );
    }

    const vec3<U16>& WaterPlane::getDimensions() const noexcept
    {
        return _dimensions;
    }

    void WaterPlane::saveToXML( boost::property_tree::ptree& pt ) const
    {
        pt.put( "dimensions.<xmlattr>.width", _dimensions.width );
        pt.put( "dimensions.<xmlattr>.length", _dimensions.height );
        pt.put( "dimensions.<xmlattr>.depth", _dimensions.depth );

        SceneNode::saveToXML( pt );
    }

    void WaterPlane::loadFromXML( const boost::property_tree::ptree& pt )
    {
        _dimensions.width = pt.get<U16>( "dimensions.<xmlattr>.width", _dimensions.width );
        _dimensions.height = pt.get<U16>( "dimensions.<xmlattr>.length", _dimensions.height );
        _dimensions.depth = pt.get<U16>( "dimensions.<xmlattr>.depth", _dimensions.depth );

        SceneNode::loadFromXML( pt );
    }

} //namespace Divide
