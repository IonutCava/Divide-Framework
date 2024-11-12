

#include "Headers/PostFX.h"
#include "Headers/PreRenderOperator.h"

#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Core/Resources/Headers/ResourceCache.h"

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

    PostFX::PostFX( PlatformContext& context )
        : PlatformContextComponent( context )
        , _preRenderBatch( context.gfx(), *this )
    {
        std::atomic_uint loadTasks = 0u;

        context.paramHandler().setParam<bool>( _ID( "postProcessing.enableVignette" ), false );

        Console::printfn( LOCALE_STR( "START_POST_FX" ) );

        _uniformData.set( _ID( "_noiseTile" ), PushConstantType::FLOAT, 0.1f );
        _uniformData.set( _ID( "_noiseFactor" ), PushConstantType::FLOAT, 0.02f );
        _uniformData.set( _ID( "_fadeActive" ), PushConstantType::BOOL, false );
        _uniformData.set( _ID( "_zPlanes" ), PushConstantType::VEC2, vec2<F32>( 0.01f, 500.0f ) );

        TextureDescriptor texDescriptor{};
        texDescriptor._textureOptions._isNormalMap = true;
        texDescriptor._textureOptions._useDDSCache = true;
        texDescriptor._textureOptions._outputSRGB = false;
        texDescriptor._textureOptions._alphaChannelTransparency = false;

        ResourceDescriptor<Texture> textureWaterCaustics( "Underwater Normal Map", texDescriptor );
        textureWaterCaustics.assetName( "terrain_water_NM.jpg" );
        textureWaterCaustics.assetLocation( Paths::g_imagesLocation );
        textureWaterCaustics.waitForReady( false );
        _underwaterTexture = CreateResource( textureWaterCaustics, loadTasks );

        texDescriptor._textureOptions._isNormalMap = false;
        ResourceDescriptor<Texture> noiseTexture( "noiseTexture", texDescriptor );
        noiseTexture.assetName( "bruit_gaussien.jpg" );
        noiseTexture.assetLocation( Paths::g_imagesLocation );
        noiseTexture.waitForReady( false );
        _noise = CreateResource( noiseTexture, loadTasks );

        ResourceDescriptor<Texture> borderTexture( "borderTexture", texDescriptor );
        borderTexture.assetName( "vignette.jpeg" );
        borderTexture.assetLocation(  Paths::g_imagesLocation );
        borderTexture.waitForReady( false );
        _screenBorder = CreateResource( borderTexture, loadTasks );

        _noiseTimer = 0.0;
        _tickInterval = 1.0f / 24.0f;
        _randomNoiseCoefficient = 0;
        _randomFlashCoefficient = 0;

        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "baseVertexShaders.glsl";
        vertModule._variant = "FullScreenQuad";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "postProcessing.glsl";

        ResourceDescriptor<ShaderProgram> postFXShader( "postProcessing" );
        ShaderProgramDescriptor& postFXShaderDescriptor = postFXShader._propertyDescriptor;
        postFXShaderDescriptor._modules.push_back( vertModule );
        postFXShaderDescriptor._modules.push_back( fragModule );
        _postProcessingShader = CreateResource( postFXShader, loadTasks );

        PipelineDescriptor pipelineDescriptor;
        pipelineDescriptor._stateBlock = context.gfx().get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _postProcessingShader;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        _drawPipeline = context.gfx().newPipeline( pipelineDescriptor );
        WAIT_FOR_CONDITION( loadTasks.load() == 0 );
    }

    PostFX::~PostFX()
    {
        // Destroy our post processing system
        Console::printfn( LOCALE_STR( "STOP_POST_FX" ) );
        DestroyResource(_screenBorder);
        DestroyResource(_noise);
        DestroyResource(_underwaterTexture);
        DestroyResource(_postProcessingShader);
    }

    void PostFX::updateResolution( const U16 newWidth, const U16 newHeight )
    {
        if ( (_resolutionCache.width == newWidth && _resolutionCache.height == newHeight) ||
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
        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "PostFX: PrePass";
        GFX::EnqueueCommand<GFX::PushCameraCommand>( bufferInOut )->_cameraSnapshot = _setCameraCmd._cameraSnapshot;

        _preRenderBatch.prePass( idx, cameraSnapshot, _filterStack | _overrideFilterStack, bufferInOut );

        GFX::EnqueueCommand<GFX::PopCameraCommand>( bufferInOut );
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }

    void PostFX::apply( const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, GFX::CommandBuffer& bufferInOut )
    {
        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "PostFX: Apply";
        GFX::EnqueueCommand( bufferInOut, _setCameraCmd );

        _preRenderBatch.execute( idx, cameraSnapshot, _filterStack | _overrideFilterStack, bufferInOut );

        GFX::BeginRenderPassCommand beginRenderPassCmd{};
        beginRenderPassCmd._target = RenderTargetNames::SCREEN;
        std::ranges::fill(beginRenderPassCmd._descriptor._drawMask, false);
        beginRenderPassCmd._descriptor._drawMask[to_base(GFXDevice::ScreenTargets::ALBEDO)] = true;
        beginRenderPassCmd._name = "DO_POSTFX_PASS";
        beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { VECTOR4_ZERO, true };
        GFX::EnqueueCommand( bufferInOut, beginRenderPassCmd );
        GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = _drawPipeline;

        if ( _filtersDirty )
        {
            _uniformData.set( _ID( "vignetteEnabled" ), PushConstantType::BOOL, getFilterState( FilterType::FILTER_VIGNETTE ) );
            _uniformData.set( _ID( "noiseEnabled" ), PushConstantType::BOOL, getFilterState( FilterType::FILTER_NOISE ) );
            _uniformData.set( _ID( "underwaterEnabled" ), PushConstantType::BOOL, getFilterState( FilterType::FILTER_UNDERWATER ) );
            _uniformData.set( _ID( "lutCorrectionEnabled" ), PushConstantType::BOOL, getFilterState( FilterType::FILTER_LUT_CORECTION ) );
            _filtersDirty = false;
        };

        _uniformData.set( _ID( "_zPlanes" ), PushConstantType::VEC2, cameraSnapshot._zPlanes );
        _uniformData.set( _ID( "_invProjectionMatrix" ), PushConstantType::VEC2, cameraSnapshot._invProjectionMatrix );

        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_uniformData = &_uniformData;
        const auto& rtPool = context().gfx().renderTargetPool();
        const auto& prbAtt = _preRenderBatch.getOutput( false )._rt->getAttachment( RTAttachmentType::COLOUR );
        const auto& linDepthDataAtt = _preRenderBatch.getLinearDepthRT()._rt->getAttachment( RTAttachmentType::COLOUR );
        const auto& ssrDataAtt = rtPool.getRenderTarget( RenderTargetNames::SSR_RESULT )->getAttachment( RTAttachmentType::COLOUR );
        const auto& velocityAtt = rtPool.getRenderTarget( RenderTargetNames::SCREEN )->getAttachment( RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::VELOCITY );

        const SamplerDescriptor defaultSampler =
        {
            ._wrapU = TextureWrap::REPEAT,
            ._wrapV = TextureWrap::REPEAT,
            ._wrapW = TextureWrap::REPEAT
        };

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 6u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, velocityAtt->texture(), defaultSampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 5u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, ssrDataAtt->texture(), defaultSampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 4u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, linDepthDataAtt->texture(), defaultSampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 3u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, _underwaterTexture, defaultSampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 2u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, _noise, defaultSampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, _screenBorder, defaultSampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, prbAtt->texture(), prbAtt->_descriptor._sampler );
        }
        GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut )->_drawCommands.emplace_back();

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

            _uniformData.set( _ID( "randomCoeffNoise" ), PushConstantType::FLOAT, _randomNoiseCoefficient );
            _uniformData.set( _ID( "randomCoeffFlash" ), PushConstantType::FLOAT, _randomFlashCoefficient );
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

            _uniformData.set( _ID( "_fadeStrength" ), PushConstantType::FLOAT, fadeStrength );

            _fadeActive = fadeStrength > EPSILON_D64;
            if ( !_fadeActive )
            {
                _uniformData.set( _ID( "_fadeActive" ), PushConstantType::BOOL, false );
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
        _uniformData.set( _ID( "_fadeColour" ), PushConstantType::VEC4, Util::ToFloatColour( targetColour ) );
        _uniformData.set( _ID( "_fadeActive" ), PushConstantType::BOOL, true );
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
        _uniformData.set( _ID( "_fadeActive" ), PushConstantType::BOOL, true );
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
