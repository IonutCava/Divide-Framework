

#include "Headers/InfinitePlane.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Managers/Headers/ProjectManager.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "Rendering/Camera/Headers/Camera.h"

#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"

namespace Divide
{

    InfinitePlane::InfinitePlane( PlatformContext& context, const ResourceDescriptor<InfinitePlane>& descriptor )
        : SceneNode( descriptor,
                     GetSceneNodeType<InfinitePlane>(),
                     to_base( ComponentType::TRANSFORM ) | to_base( ComponentType::BOUNDS ) )
        , _context( context.gfx() )
        , _dimensions( descriptor.data().xy )
    {
        _renderState.addToDrawExclusionMask( RenderStage::SHADOW );
        _renderState.addToDrawExclusionMask( RenderStage::REFLECTION );
        _renderState.addToDrawExclusionMask( RenderStage::REFRACTION );
    }

    bool InfinitePlane::load( PlatformContext& context )
    {
        if ( _plane != INVALID_HANDLE<Quad3D> )
        {
            return false;
        }

        setState( ResourceState::RES_LOADING );

        ResourceDescriptor<Quad3D> infinitePlane( "infinitePlane" );
        infinitePlane.flag( true );  // No default material
        infinitePlane.waitForReady( true );
        infinitePlane.ID( 150u );
        infinitePlane.data().set( Util::FLOAT_TO_UINT( _dimensions.x * 2.f ),
                                 0u,
                                 Util::FLOAT_TO_UINT( _dimensions.y * 2.f ) );

        _plane = CreateResource( infinitePlane );

        ResourceDescriptor<Material> planeMaterialDescriptor( "infinitePlaneMaterial" );
        Handle<Material> planeMaterial = CreateResource( planeMaterialDescriptor );
        ResourcePtr<Material> planeMaterialPtr = Get(planeMaterial);

        planeMaterialPtr->properties().shadingMode( ShadingMode::BLINN_PHONG );
        planeMaterialPtr->properties().baseColour( FColour4( DefaultColours::WHITE.rgb * 0.5f, 1.0f ) );
        planeMaterialPtr->properties().roughness( 1.0f );
        planeMaterialPtr->properties().isStatic( true );

        SamplerDescriptor albedoSampler = {};
        albedoSampler._wrapU = TextureWrap::REPEAT;
        albedoSampler._wrapV = TextureWrap::REPEAT;
        albedoSampler._wrapW = TextureWrap::REPEAT;
        albedoSampler._anisotropyLevel = 8u;

        ResourceDescriptor<Texture> textureWaterCaustics( "Plane Water Caustics" );
        textureWaterCaustics.assetLocation( Paths::g_imagesLocation );
        textureWaterCaustics.assetName( "terrain_water_caustics.jpg" );

        TextureDescriptor& miscTexDescriptor = textureWaterCaustics._propertyDescriptor;
        miscTexDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
        miscTexDescriptor._packing = GFXImagePacking::NORMALIZED_SRGB;
        miscTexDescriptor._textureOptions._alphaChannelTransparency = false;

        planeMaterialPtr->setTexture( TextureSlot::UNIT0, textureWaterCaustics, albedoSampler, TextureOperation::REPLACE );

        planeMaterialPtr->computeShaderCBK( []( [[maybe_unused]] Material* material, const RenderStagePass stagePass )
        {
            ShaderModuleDescriptor vertModule = {};
            vertModule._moduleType = ShaderType::VERTEX;
            vertModule._sourceFile = "terrainPlane.glsl";
            vertModule._defines.emplace_back( "UNDERWATER_TILE_SCALE 100" );

            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "terrainPlane.glsl";

            ShaderProgramDescriptor shaderDescriptor = {};
            if ( IsDepthPass( stagePass ) && stagePass._stage != RenderStage::DISPLAY )
            {
                shaderDescriptor._modules.push_back( vertModule );
                shaderDescriptor._name = "terrainPlane_Depth";
            }
            else
            {
                shaderDescriptor._modules.push_back( vertModule );
                shaderDescriptor._modules.push_back( fragModule );
                shaderDescriptor._name = "terrainPlane_Colour";
            }

            return shaderDescriptor;
        } );

        planeMaterialPtr->setPipelineLayout( PrimitiveTopology::TRIANGLE_STRIP, Get(_plane)->geometryBuffer()->generateAttributeMap() );

        setMaterialTpl( planeMaterial );

        _boundingBox.set( vec3<F32>( -(_dimensions.x * 1.5f), -0.5f, -(_dimensions.y * 1.5f) ),
                         vec3<F32>( _dimensions.x * 1.5f, 0.5f, _dimensions.y * 1.5f ) );

        return SceneNode::load( context );
    }

    void InfinitePlane::postLoad( SceneGraphNode* sgn )
    {
        assert( _plane != INVALID_HANDLE<Quad3D> );

        RenderingComponent* renderable = sgn->get<RenderingComponent>();
        if ( renderable )
        {
            renderable->toggleRenderOption( RenderingComponent::RenderOptions::CAST_SHADOWS, false );
        }

        SceneNode::postLoad( sgn );
    }

    bool InfinitePlane::postLoad()
    {
        return SceneNode::postLoad();
    }

    bool InfinitePlane::unload()
    {
        DestroyResource(_plane);
        return SceneNode::unload();
    }

    void InfinitePlane::sceneUpdate( [[maybe_unused]] const U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        TransformComponent* tComp = sgn->get<TransformComponent>();

        const vec3<F32>& newEye = sceneState.parentScene().playerCamera()->snapshot()._eye;
        if ( newEye.xz().distanceSquared( tComp->getWorldPosition().xz() ) > SQUARED( 2 ) )
        {
            tComp->setPosition( newEye.x, tComp->getWorldPosition().y, newEye.z );
        }
    }

    void InfinitePlane::buildDrawCommands( SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut )
    {
        //infinite plane
        GenericDrawCommand& cmd = cmdsOut.emplace_back();
        toggleOption( cmd, CmdRenderOptions::RENDER_INDIRECT );

        ResourcePtr<Quad3D> planePtr = Get( _plane );

        cmd._cmd.firstIndex = 0u;
        cmd._cmd.indexCount = to_U32(planePtr->geometryBuffer()->getIndexCount() );
        cmd._sourceBuffer = planePtr->geometryBuffer()->handle();

        SceneNode::buildDrawCommands( sgn, cmdsOut );
    }

} //namespace Divide
