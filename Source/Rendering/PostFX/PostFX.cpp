

#include "Headers/PostFX.h"
#include "Headers/PreRenderOperator.h"

#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Time/Headers/ApplicationTimer.h"

#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "Managers/Headers/ProjectManager.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"

#include "Platform/File/Headers/FileManagement.h"

#include "Rendering/Camera/Headers/Camera.h"

namespace Divide
{

    const char* PostFX::FilterName( const FilterType filter ) noexcept
    {
        switch ( filter )
        {
            case FilterType::FILTER_SS_ANTIALIASING:  return "SS_ANTIALIASING";
            case FilterType::FILTER_SS_REFLECTIONS:  return "SS_REFLECTIONS";
            case FilterType::FILTER_SS_AMBIENT_OCCLUSION:  return "SS_AMBIENT_OCCLUSION";
            case FilterType::FILTER_DEPTH_OF_FIELD:  return "DEPTH_OF_FIELD";
            case FilterType::FILTER_MOTION_BLUR:  return "MOTION_BLUR";
            case FilterType::FILTER_BLOOM: return "BLOOM";
            case FilterType::FILTER_LUT_CORECTION:  return "LUT_CORRECTION";
            case FilterType::FILTER_UNDERWATER: return "UNDERWATER";
            case FilterType::FILTER_NOISE: return "NOISE";
            case FilterType::FILTER_VIGNETTE: return "VIGNETTE";
            default: break;
        }

        return "Unknown";
    };

    PostFX::PostFX( PlatformContext& context, ResourceCache* cache )
        : PlatformContextComponent( context ),
        _preRenderBatch( context.gfx(), *this, cache )
    {
        std::atomic_uint loadTasks = 0u;

        context.paramHandler().setParam<bool>( _ID( "postProcessing.enableVignette" ), false );

        std::ranges::fill(_postFXTarget._drawMask, false);
        _postFXTarget._drawMask[to_base( GFXDevice::ScreenTargets::ALBEDO )] = true;

        Console::printfn( LOCALE_STR( "START_POST_FX" ) );

        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "baseVertexShaders.glsl";
        vertModule._variant = "FullScreenQuad";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "postProcessing.glsl";

        ShaderProgramDescriptor postFXShaderDescriptor = {};
        postFXShaderDescriptor._modules.push_back( vertModule );
        postFXShaderDescriptor._modules.push_back( fragModule );

        _drawConstantsCmd._constants.set( _ID( "_noiseTile" ), PushConstantType::FLOAT, 0.1f );
        _drawConstantsCmd._constants.set( _ID( "_noiseFactor" ), PushConstantType::FLOAT, 0.02f );
        _drawConstantsCmd._constants.set( _ID( "_fadeActive" ), PushConstantType::BOOL, false );
        _drawConstantsCmd._constants.set( _ID( "_zPlanes" ), PushConstantType::VEC2, vec2<F32>( 0.01f, 500.0f ) );

        TextureDescriptor texDescriptor( TextureType::TEXTURE_2D, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA );

        ImageTools::ImportOptions options;
        options._isNormalMap = true;
        options._useDDSCache = true;
        options._outputSRGB = false;
        options._alphaChannelTransparency = false;

        texDescriptor.textureOptions( options );

        ResourceDescriptor textureWaterCaustics( "Underwater Normal Map" );
        textureWaterCaustics.assetName( "terrain_water_NM.jpg" );
        textureWaterCaustics.assetLocation( Paths::g_imagesLocation );
        textureWaterCaustics.propertyDescriptor( texDescriptor );
        textureWaterCaustics.waitForReady( false );
        _underwaterTexture = CreateResource<Texture>( cache, textureWaterCaustics, loadTasks );

        options._isNormalMap = false;
        texDescriptor.textureOptions( options );
        ResourceDescriptor noiseTexture( "noiseTexture" );
        noiseTexture.assetName( "bruit_gaussien.jpg" );
        noiseTexture.assetLocation( Paths::g_imagesLocation );
        noiseTexture.propertyDescriptor( texDescriptor );
        noiseTexture.waitForReady( false );
        _noise = CreateResource<Texture>( cache, noiseTexture, loadTasks );

        ResourceDescriptor borderTexture( "borderTexture" );
        borderTexture.assetName( "vignette.jpeg" );
        borderTexture.assetLocation(  Paths::g_imagesLocation );
        borderTexture.propertyDescriptor( texDescriptor );
        borderTexture.waitForReady( false );
        _screenBorder = CreateResource<Texture>( cache, borderTexture ), loadTasks;

        _noiseTimer = 0.0;
        _tickInterval = 1.0f / 24.0f;
        _randomNoiseCoefficient = 0;
        _randomFlashCoefficient = 0;

        ResourceDescriptor postFXShader( "postProcessing" );
        postFXShader.propertyDescriptor( postFXShaderDescriptor );
        _postProcessingShader = CreateResource<ShaderProgram>( cache, postFXShader, loadTasks );
        _postProcessingShader->addStateCallback( ResourceState::RES_LOADED, [this, &context]( CachedResource* )
                                                 {
                                                     PipelineDescriptor pipelineDescriptor;
                                                     pipelineDescriptor._stateBlock = context.gfx().get2DStateBlock();
                                                     pipelineDescriptor._shaderProgramHandle = _postProcessingShader->handle();
                                                     pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

                                                     _drawPipeline = context.gfx().newPipeline( pipelineDescriptor );
                                                 } );

        WAIT_FOR_CONDITION( loadTasks.load() == 0 );
    }

    PostFX::~PostFX()
    {
        // Destroy our post processing system
        Console::printfn( LOCALE_STR( "STOP_POST_FX" ) );
    }

    void PostFX::updateResolution( const U16 newWidth, const U16 newHeight )
    {
        if ( _resolutionCache.width == newWidth &&
             _resolutionCache.height == newHeight ||
             newWidth < 1 || newHeight < 1 )
        {
            return;
        }

        _resolutionCache.set( newWidth, newHeight );

        _preRenderBatch.reshape( newWidth, newHeight );
        _setCameraCmd._cameraSnapshot = Camera::GetUtilityCamera( Camera::UtilityCamera::_2D )->snapshot();
    }

    void PostFX::prePass( const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, GFX::CommandBuffer& bufferInOut )
    {
        static GFX::BeginDebugScopeCommand s_beginScopeCmd{ "PostFX: PrePass" };
        GFX::EnqueueCommand( bufferInOut, s_beginScopeCmd );
        GFX::EnqueueCommand<GFX::PushCameraCommand>( bufferInOut )->_cameraSnapshot = _setCameraCmd._cameraSnapshot;

        _preRenderBatch.prePass( idx, cameraSnapshot, _filterStack | _overrideFilterStack, bufferInOut );

        GFX::EnqueueCommand<GFX::PopCameraCommand>( bufferInOut );
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }

    void PostFX::apply( const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, GFX::CommandBuffer& bufferInOut )
    {
        static GFX::BeginDebugScopeCommand s_beginScopeCmd{ "PostFX: Apply" };

        GFX::EnqueueCommand( bufferInOut, s_beginScopeCmd );
        GFX::EnqueueCommand( bufferInOut, _setCameraCmd );

        _preRenderBatch.execute( idx, cameraSnapshot, _filterStack | _overrideFilterStack, bufferInOut );

        GFX::BeginRenderPassCommand beginRenderPassCmd{};
        beginRenderPassCmd._target = RenderTargetNames::SCREEN;
        beginRenderPassCmd._descriptor = _postFXTarget;
        beginRenderPassCmd._name = "DO_POSTFX_PASS";
        beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { VECTOR4_ZERO, true };
        GFX::EnqueueCommand( bufferInOut, beginRenderPassCmd );
        GFX::EnqueueCommand( bufferInOut, GFX::BindPipelineCommand{ _drawPipeline } );

        if ( _filtersDirty )
        {
            _drawConstantsCmd._constants.set( _ID( "vignetteEnabled" ), PushConstantType::BOOL, getFilterState( FilterType::FILTER_VIGNETTE ) );
            _drawConstantsCmd._constants.set( _ID( "noiseEnabled" ), PushConstantType::BOOL, getFilterState( FilterType::FILTER_NOISE ) );
            _drawConstantsCmd._constants.set( _ID( "underwaterEnabled" ), PushConstantType::BOOL, getFilterState( FilterType::FILTER_UNDERWATER ) );
            _drawConstantsCmd._constants.set( _ID( "lutCorrectionEnabled" ), PushConstantType::BOOL, getFilterState( FilterType::FILTER_LUT_CORECTION ) );
            _filtersDirty = false;
        };

        _drawConstantsCmd._constants.set( _ID( "_zPlanes" ), PushConstantType::VEC2, cameraSnapshot._zPlanes );
        _drawConstantsCmd._constants.set( _ID( "_invProjectionMatrix" ), PushConstantType::VEC2, cameraSnapshot._invProjectionMatrix );

        GFX::EnqueueCommand( bufferInOut, _drawConstantsCmd );
        const auto& rtPool = context().gfx().renderTargetPool();
        const auto& prbAtt = _preRenderBatch.getOutput( false )._rt->getAttachment( RTAttachmentType::COLOUR );
        const auto& linDepthDataAtt = _preRenderBatch.getLinearDepthRT()._rt->getAttachment( RTAttachmentType::COLOUR );
        const auto& ssrDataAtt = rtPool.getRenderTarget( RenderTargetNames::SSR_RESULT )->getAttachment( RTAttachmentType::COLOUR );
        const auto& velocityAtt = rtPool.getRenderTarget( RenderTargetNames::SCREEN )->getAttachment( RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::VELOCITY );

        const SamplerDescriptor defaultSampler = {
            ._wrapU = TextureWrap::REPEAT,
            ._wrapV = TextureWrap::REPEAT,
            ._wrapW = TextureWrap::REPEAT
        };

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 6u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, velocityAtt->texture()->getView(), defaultSampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 5u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, ssrDataAtt->texture()->getView(), defaultSampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 4u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, linDepthDataAtt->texture()->getView(), defaultSampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 3u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, _underwaterTexture->getView(), defaultSampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 2u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, _noise->getView(), defaultSampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, _screenBorder->getView(), defaultSampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, prbAtt->texture()->getView(), prbAtt->_descriptor._sampler );
        }
        GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut );

        GFX::EnqueueCommand( bufferInOut, GFX::EndRenderPassCommand{} );

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }

    void PostFX::idle( [[maybe_unused]] const Configuration& config, [[maybe_unused]] const U64 deltaTimeUSGame )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Update states
        if ( getFilterState( FilterType::FILTER_NOISE ) )
        {
            _noiseTimer += Time::MicrosecondsToMilliseconds<D64>( deltaTimeUSGame );
            if ( _noiseTimer > _tickInterval )
            {
                _noiseTimer = 0.0;
                _randomNoiseCoefficient = Random( 1000 ) * 0.001f;
                _randomFlashCoefficient = Random( 1000 ) * 0.001f;
            }

            _drawConstantsCmd._constants.set( _ID( "randomCoeffNoise" ), PushConstantType::FLOAT, _randomNoiseCoefficient );
            _drawConstantsCmd._constants.set( _ID( "randomCoeffFlash" ), PushConstantType::FLOAT, _randomFlashCoefficient );
        }
    }

    void PostFX::update( [[maybe_unused]] const U64 deltaTimeUSFixed, const U64 deltaTimeUSApp )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        if ( _fadeActive )
        {
            _currentFadeTimeMS += Time::MicrosecondsToMilliseconds<D64>( deltaTimeUSApp );
            F32 fadeStrength = to_F32( std::min( _currentFadeTimeMS / _targetFadeTimeMS, 1.0 ) );
            if ( !_fadeOut )
            {
                fadeStrength = 1.0f - fadeStrength;
            }

            if ( fadeStrength > 0.99 )
            {
                if ( _fadeWaitDurationMS < EPSILON_D64 )
                {
                    if ( _fadeOutComplete )
                    {
                        _fadeOutComplete();
                        _fadeOutComplete = DELEGATE<void>();
                    }
                }
                else
                {
                    _fadeWaitDurationMS -= Time::MicrosecondsToMilliseconds<D64>( deltaTimeUSApp );
                }
            }

            _drawConstantsCmd._constants.set( _ID( "_fadeStrength" ), PushConstantType::FLOAT, fadeStrength );

            _fadeActive = fadeStrength > EPSILON_D64;
            if ( !_fadeActive )
            {
                _drawConstantsCmd._constants.set( _ID( "_fadeActive" ), PushConstantType::BOOL, false );
                if ( _fadeInComplete )
                {
                    _fadeInComplete();
                    _fadeInComplete = DELEGATE<void>();
                }
            }
        }

        _preRenderBatch.update( deltaTimeUSApp );
    }

    void PostFX::setFadeOut( const UColour3& targetColour, const D64 durationMS, const D64 waitDurationMS, DELEGATE<void> onComplete )
    {
        _drawConstantsCmd._constants.set( _ID( "_fadeColour" ), PushConstantType::VEC4, Util::ToFloatColour( targetColour ) );
        _drawConstantsCmd._constants.set( _ID( "_fadeActive" ), PushConstantType::BOOL, true );
        _targetFadeTimeMS = durationMS;
        _currentFadeTimeMS = 0.0;
        _fadeWaitDurationMS = waitDurationMS;
        _fadeOut = true;
        _fadeActive = true;
        _fadeOutComplete = MOV( onComplete );
    }

    // clear any fading effect currently active over the specified time interval
    // set durationMS to instantly clear the fade effect
    void PostFX::setFadeIn( const D64 durationMS, DELEGATE<void> onComplete )
    {
        _targetFadeTimeMS = durationMS;
        _currentFadeTimeMS = 0.0;
        _fadeOut = false;
        _fadeActive = true;
        _drawConstantsCmd._constants.set( _ID( "_fadeActive" ), PushConstantType::BOOL, true );
        _fadeInComplete = MOV( onComplete );
    }

    void PostFX::setFadeOutIn( const UColour3& targetColour, const D64 durationFadeOutMS, const D64 waitDurationMS )
    {
        if ( waitDurationMS > 0.0 )
        {
            setFadeOutIn( targetColour, waitDurationMS * 0.5, waitDurationMS * 0.5, durationFadeOutMS );
        }
    }

    void PostFX::setFadeOutIn( const UColour3& targetColour, const D64 durationFadeOutMS, const D64 durationFadeInMS, const D64 waitDurationMS )
    {
        setFadeOut( targetColour, durationFadeOutMS, waitDurationMS, [this, durationFadeInMS]()
                    {
                        setFadeIn( durationFadeInMS );
                    } );
    }

};
