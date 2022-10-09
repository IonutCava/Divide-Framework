#include "stdafx.h"

#include "Headers/InfinitePlane.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Managers/Headers/SceneManager.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "Rendering/Camera/Headers/Camera.h"

#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"

namespace Divide
{

    InfinitePlane::InfinitePlane( GFXDevice& context, ResourceCache* parentCache, const size_t descriptorHash, const Str256& name, vec2<U32> dimensions )
        : SceneNode( parentCache, descriptorHash, name, ResourcePath{ name }, {}, SceneNodeType::TYPE_INFINITEPLANE, to_base( ComponentType::TRANSFORM ) | to_base( ComponentType::BOUNDS ) ),
        _context( context ),
        _dimensions( dimensions )
    {
        _renderState.addToDrawExclusionMask( RenderStage::SHADOW );
        _renderState.addToDrawExclusionMask( RenderStage::REFLECTION );
        _renderState.addToDrawExclusionMask( RenderStage::REFRACTION );
    }

    bool InfinitePlane::load()
    {
        if ( _plane != nullptr )
        {
            return false;
        }

        setState( ResourceState::RES_LOADING );

        ResourceDescriptor infinitePlane( "infinitePlane" );
        infinitePlane.flag( true );  // No default material
        infinitePlane.waitForReady( true );
        infinitePlane.ID( 150u );
        infinitePlane.data().set( Util::FLOAT_TO_UINT( _dimensions.x * 2.f ),
                                 0u,
                                 Util::FLOAT_TO_UINT( _dimensions.y * 2.f ) );

        _plane = CreateResource<Quad3D>( _parentCache, infinitePlane );

        ResourceDescriptor planeMaterialDescriptor( "infinitePlaneMaterial" );
        Material_ptr planeMaterial = CreateResource<Material>( _parentCache, planeMaterialDescriptor );
        planeMaterial->properties().shadingMode( ShadingMode::BLINN_PHONG );
        planeMaterial->properties().baseColour( FColour4( DefaultColours::WHITE.rgb * 0.5f, 1.0f ) );
        planeMaterial->properties().roughness( 1.0f );
        planeMaterial->properties().isStatic( true );

        SamplerDescriptor albedoSampler = {};
        albedoSampler.wrapUVW( TextureWrap::REPEAT );
        albedoSampler.anisotropyLevel( 8 );

        TextureDescriptor miscTexDescriptor( TextureType::TEXTURE_2D_ARRAY );
        miscTexDescriptor.srgb( true );
        miscTexDescriptor.textureOptions()._alphaChannelTransparency = false;

        ResourceDescriptor textureWaterCaustics( "Plane Water Caustics" );
        textureWaterCaustics.assetLocation( Paths::g_assetsLocation + Paths::g_imagesLocation );
        textureWaterCaustics.assetName( ResourcePath{ "terrain_water_caustics.jpg" } );
        textureWaterCaustics.propertyDescriptor( miscTexDescriptor );

        planeMaterial->setTexture( TextureSlot::UNIT0, CreateResource<Texture>( _parentCache, textureWaterCaustics ), albedoSampler.getHash(), TextureOperation::REPLACE );

        planeMaterial->computeShaderCBK( []( [[maybe_unused]] Material* material, const RenderStagePass stagePass )
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

        planeMaterial->setPipelineLayout( PrimitiveTopology::TRIANGLE_STRIP, _plane->geometryBuffer()->generateAttributeMap() );

        setMaterialTpl( planeMaterial );

        _boundingBox.set( vec3<F32>( -(_dimensions.x * 1.5f), -0.5f, -(_dimensions.y * 1.5f) ),
                         vec3<F32>( _dimensions.x * 1.5f, 0.5f, _dimensions.y * 1.5f ) );

        return SceneNode::load();
    }

    void InfinitePlane::postLoad( SceneGraphNode* sgn )
    {
        assert( _plane != nullptr );

        RenderingComponent* renderable = sgn->get<RenderingComponent>();
        if ( renderable )
        {
            renderable->toggleRenderOption( RenderingComponent::RenderOptions::CAST_SHADOWS, false );
        }

        SceneNode::postLoad( sgn );
    }

    void InfinitePlane::sceneUpdate( const U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        TransformComponent* tComp = sgn->get<TransformComponent>();

        const vec3<F32>& newEye = sceneState.parentScene().playerCamera()->snapshot()._eye;
        if ( newEye.xz().distanceSquared( tComp->getWorldPosition().xz() ) > SQUARED( 2 ) )
        {
            tComp->setPosition( newEye.x, tComp->getWorldPosition().y, newEye.z );
        }
    }

    void InfinitePlane::buildDrawCommands( SceneGraphNode* sgn, vector_fast<GFX::DrawCommand>& cmdsOut )
    {
        //infinite plane
        GenericDrawCommand planeCmd = {};
        planeCmd._cmd.firstIndex = 0u;
        planeCmd._cmd.indexCount = to_U32( _plane->geometryBuffer()->getIndexCount() );
        planeCmd._sourceBuffer = _plane->geometryBuffer()->handle();
        cmdsOut.emplace_back( GFX::DrawCommand{ planeCmd } );

        SceneNode::buildDrawCommands( sgn, cmdsOut );
    }

} //namespace Divide