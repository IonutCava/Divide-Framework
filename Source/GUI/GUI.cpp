

#include "Headers/GUI.h"
#include "Headers/SceneGUIElements.h"

#include "Headers/GUIButton.h"
#include "Headers/GUIConsole.h"
#include "Headers/GUIMessageBox.h"
#include "Headers/GUIText.h"
#include "Scenes/Headers/Scene.h"

#include "GUI/CEGUIAddons/Renderer/Headers/CEGUIRenderer.h"
#include "GUI/CEGUIAddons/Renderer/Headers/DVDTextureTarget.h"

#include "Core/Headers/NonCopyable.h"
#include "Core/Debugging/Headers/DebugInterface.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Rendering/Camera/Headers/Camera.h"

#include "Platform/Audio/Headers/SFXDevice.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"

#define FONTSTASH_IMPLEMENTATION
#include "Platform/Video/Headers/fontstash.h"

namespace Divide
{
    struct DVDFONSContext final : private NonCopyable
    {
        DVDFONSContext() = default;
        ~DVDFONSContext()
        {
            if ( _impl != nullptr )
            {
                fonsDeleteInternal( _impl );
            }
        }

        U32  _writeOffset = 0u;
        U32  _bufferSizeFactor = 1024u;
        bool _bufferNeedsResize = false;

        GFXDevice* _parent{ nullptr };
        GenericVertexData_ptr _fontRenderingBuffer{};
        Handle<Texture> _fontRenderingTexture{INVALID_HANDLE<Texture>};
        I32 _width{ 1u };

        GFX::CommandBuffer* _commandBuffer{ nullptr };
        GFX::MemoryBarrierCommand* _memCmd{ nullptr };

        FONScontext* _impl{ nullptr };
    };

    namespace
    {
        GUIMessageBox* g_assertMsgBox = nullptr;

        void RefreshBufferSize( DVDFONSContext* dvd )
        {
            GenericVertexData::SetBufferParams params = {};
            params._bindConfig = { 0u, 0u };
            params._useRingBuffer = true;
            params._initialData = { nullptr, 0 };

            params._bufferParams._elementCount = FONS_VERTEX_COUNT * dvd->_bufferSizeFactor;
            params._bufferParams._elementSize = sizeof( FONSvert );
            params._bufferParams._updateFrequency = BufferUpdateFrequency::OFTEN;

            const auto lock = dvd->_fontRenderingBuffer->setBuffer( params ); //Pos, UV and Colour
            DIVIDE_UNUSED( lock );
        }


        I32 FONSRenderCreate( void* userPtr, int width, int height )
        {
            DVDFONSContext* dvd = (DVDFONSContext*)userPtr;

            dvd->_fontRenderingBuffer = dvd->_parent->newGVD( Config::MAX_FRAMES_IN_FLIGHT + 1u, "GUIFontBuffer" );

            RefreshBufferSize(dvd);

            ResourceDescriptor<Texture> resDescriptor( "FONTSTASH_font_texture" );
            TextureDescriptor& texDescriptor = resDescriptor._propertyDescriptor;
            texDescriptor._baseFormat = GFXImageFormat::RED;
            texDescriptor._mipMappingState = MipMappingState::OFF;
            texDescriptor._allowRegionUpdates = true;

            dvd->_fontRenderingTexture = CreateResource( resDescriptor );
            if ( dvd->_fontRenderingTexture != INVALID_HANDLE<Texture>)
            {
                Get(dvd->_fontRenderingTexture)->createWithData( nullptr, 0u, vec2<U16>( width, height), {});

                if ( dvd->_fontRenderingBuffer )
                {
                    return 1;
                }
            }

            return 0;
        }
    }

    void DIVIDE_ASSERT_MSG_BOX( const char* failMessage ) noexcept
    {
        if constexpr( Config::Assert::SHOW_MESSAGE_BOX )
        {
            if ( g_assertMsgBox )
            {
                g_assertMsgBox->setTitle( "Assertion Failed!" );
                g_assertMsgBox->setMessage( failMessage );
                g_assertMsgBox->setMessageType( GUIMessageBox::MessageType::MESSAGE_ERROR );
                g_assertMsgBox->show();
            }
        }
    }

    GUI::GUI( Kernel& parent )
        : GUIInterface( *this )
        , KernelComponent( parent )
        , FrameListener( "GUI", parent.frameListenerMgr(), 2 )
        , _textRenderInterval( Time::MillisecondsToMicroseconds( 10 ) )
        , _ceguiInput( *this )
    {
        // 500ms
        _ceguiInput.setInitialDelay( 0.500f );
    }

    GUI::~GUI()
    {
        destroy();
    }

    void GUI::onChangeScene( Scene* newScene )
    {
        assert( newScene != nullptr );
        SharedLock<SharedMutex> r_lock( _guiStackLock );
        if ( _activeScene != nullptr && _activeScene->getGUID() != newScene->getGUID() )
        {
            const GUIMapPerScene::const_iterator it = _guiStack.find( _activeScene->getGUID() );
            if ( it != std::cend( _guiStack ) )
            {
                it->second->onDisable();
            }
        }

        const GUIMapPerScene::const_iterator it = _guiStack.find( newScene->getGUID() );
        if ( it != std::cend( _guiStack ) )
        {
            it->second->onEnable();
        }
        else
        {
            SceneGUIElements* elements = Attorney::SceneGUI::guiElements( newScene );
            insert( _guiStack, newScene->getGUID(), elements );
            elements->onEnable();
        }

        _activeScene = newScene;
        recreateDefaultMessageBox();
    }

    void GUI::onUnloadScene( Scene* const scene )
    {
        assert( scene != nullptr );
        LockGuard<SharedMutex> w_lock( _guiStackLock );
        const GUIMapPerScene::const_iterator it = _guiStack.find( scene->getGUID() );
        if ( it != std::cend( _guiStack ) )
        {
            _guiStack.erase( it );
        }
    }

    void GUI::drawText( const TextElementBatch& batch, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut, const bool pushCamera )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( _fonsContext == nullptr )
        {
            return;
        }

        _fonsContext->_commandBuffer = &bufferInOut;
        _fonsContext->_memCmd = &memCmdInOut;

        static const SamplerDescriptor sampler = {
            ._minFilter = TextureFilter::LINEAR,
            ._magFilter = TextureFilter::LINEAR,
            ._mipSampling = TextureMipSampling::NONE,
            ._wrapU = TextureWrap::CLAMP_TO_EDGE,
            ._wrapV = TextureWrap::CLAMP_TO_EDGE,
            ._wrapW = TextureWrap::CLAMP_TO_EDGE,
            ._anisotropyLevel = 0u
        };

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Draw Text";

        if ( pushCamera )
        {
            GFX::EnqueueCommand<GFX::PushCameraCommand>( bufferInOut )->_cameraSnapshot = Camera::GetUtilityCamera( Camera::UtilityCamera::_2D )->snapshot();
        }

        GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = _textRenderPipeline;

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, _fonsContext->_fontRenderingTexture, sampler );

        size_t drawCount = 0;
        size_t previousStyle = 0;


        fonsClearState( _fonsContext->_impl );
        for ( const TextElement& entry : batch.data() )
        {
            if ( previousStyle != entry.textLabelStyleHash() )
            {
                const TextLabelStyle& textLabelStyle = TextLabelStyle::get( entry.textLabelStyleHash() );
                const UColour4& colour = textLabelStyle.colour();
                // Retrieve the font from the font cache
                const I32 font = getFont( TextLabelStyle::fontName( textLabelStyle.font() ) );
                // The font may be invalid, so skip this text label
                if ( font != FONS_INVALID )
                {
                    fonsSetFont( _fonsContext->_impl, font );
                }
                fonsSetBlur( _fonsContext->_impl, textLabelStyle.blurAmount() );
                fonsSetBlur( _fonsContext->_impl, textLabelStyle.spacing() );
                fonsSetAlign( _fonsContext->_impl, textLabelStyle.alignFlag() );
                fonsSetSize( _fonsContext->_impl, to_F32( textLabelStyle.fontSize() ) );
                fonsSetColour( _fonsContext->_impl, colour.r, colour.g, colour.b, colour.a );
                previousStyle = entry.textLabelStyleHash();
            }

            F32 textX = entry.position()._x._scale * targetViewport.sizeX + entry.position()._x._offset;
            F32 textY = targetViewport.sizeY - (entry.position()._y._scale * targetViewport.sizeY + entry.position()._y._offset);

            textX += targetViewport.offsetX;
            textY += targetViewport.offsetY;

            F32 lh = 0;
            fonsVertMetrics( _fonsContext->_impl, nullptr, nullptr, &lh );

            const TextElement::TextType& text = entry.text();
            const size_t lineCount = text.size();

            
            for ( size_t i = 0; i < lineCount; ++i )
            {
                fonsDrawText( _fonsContext->_impl,
                              textX,
                              textY - lh * i,
                              text[i].c_str(),
                              nullptr );
            }
            drawCount += lineCount;

            // Register each label rendered as a draw call
            _fonsContext->_parent->registerDrawCalls( to_U32( drawCount ) );
        }

        if ( pushCamera )
        {
            GFX::EnqueueCommand( bufferInOut, GFX::PopCameraCommand{} );
        }

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }

    void GUI::preDraw( [[maybe_unused]] GFXDevice& context, [[maybe_unused]] const Rect<I32>& viewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        if ( !_init || !_activeScene )
        {
            return;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Pre-Render GUI";
        if ( _ceguiRenderer != nullptr )
        {
            GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Render CEGUI";

            _ceguiRenderer->beginRendering( bufferInOut, memCmdInOut );
            _renderTextureTarget->clear();
            _ceguiContext->draw();
            _ceguiRenderer->endRendering();

            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
        }

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }

    void GUI::draw( GFXDevice& context, const Rect<I32>& viewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        thread_local TextElementBatch textBatch;

        if ( !_init || !_activeScene )
        {
            return;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Render GUI";

        //Set a 2D camera for rendering
        GFX::EnqueueCommand<GFX::SetCameraCommand>( bufferInOut )->_cameraSnapshot = Camera::GetUtilityCamera( Camera::UtilityCamera::_2D )->snapshot();

        GFX::EnqueueCommand<GFX::SetViewportCommand>( bufferInOut )->_viewport = viewport;

        const GUIMap& elements = _guiElements[to_base( GUIType::GUI_TEXT )];

        efficient_clear(textBatch.data());

        textBatch.data().reserve( elements.size() );
        for ( const GUIMap::value_type& guiStackIterator : elements )
        {
            const GUIText& textLabel = static_cast<GUIText&>(*guiStackIterator.second.first);
            if ( textLabel.visible() && !textLabel.text().empty() )
            {
                textBatch.data().push_back( textLabel );
            }
        }

        {
            SharedLock<SharedMutex> r_lock( _guiStackLock );
            // scene specific
            const GUIMapPerScene::const_iterator it = _guiStack.find( _activeScene->getGUID() );
            if ( it != std::cend( _guiStack ) )
            {
                const auto& batch = it->second->updateAndGetText();
                textBatch.data().insert(textBatch.data().cend(), batch.data().cbegin(), batch.data().cend());
            }
        }

        if ( !textBatch.data().empty() )
        {
            drawText( textBatch, viewport, bufferInOut, memCmdInOut );
        }

        if ( _ceguiRenderer != nullptr )
        {
            context.drawTextureInViewport( Get(_renderTextureTarget->getAttachmentTex())->getView(), _renderTextureTarget->getSampler(), viewport, false, false, true, bufferInOut);
        }

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }

    void GUI::update( const U64 deltaTimeUS )
    {
        if ( !_init )
        {
            return;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        if ( _ceguiRenderer != nullptr )
        {
            _ceguiInput.update( deltaTimeUS );
            auto& ceguiSystem = CEGUI::System::getSingleton();
            ceguiSystem.injectTimePulse( Time::MicrosecondsToSeconds<F32>( deltaTimeUS ) );
            ceguiSystem.getDefaultGUIContext().injectTimePulse( Time::MicrosecondsToSeconds<F32>( deltaTimeUS ) );
        }

        if ( _console )
        {
            _console->update( deltaTimeUS );
        }
    }

    bool GUI::frameStarted( [[maybe_unused]] const FrameEvent& evt )
    {
        if ( _fonsContext != nullptr )
        {
            _fonsContext->_fontRenderingBuffer->incQueue();
            _fonsContext->_writeOffset = 0u;

            if ( _fonsContext->_bufferNeedsResize )
            {
                ++_fonsContext->_bufferSizeFactor;
                RefreshBufferSize( _fonsContext.get() );
                _fonsContext->_bufferNeedsResize = false;
            }
        }

        return true;
    }

    ErrorCode GUI::init( PlatformContext& context )
    {
        if ( _init )
        {
            Console::d_errorfn( LOCALE_STR( "ERROR_GUI_DOUBLE_INIT" ) );
            return ErrorCode::GUI_INIT_ERROR;
        }


        _ceguiInput.init( parent().platformContext().config() );

        _console = std::make_unique<GUIConsole>( *this, context );

        GUIButton::soundCallback( [&context]( const Handle<AudioDescriptor>& sound )
                                  {
                                      context.sfx().playSound( sound );
                                  } );


        _fonsContext = std::make_unique<DVDFONSContext>();
        _fonsContext->_width = 512;
        _fonsContext->_parent = &context.gfx();

        FONSparams params;
        memset( &params, 0, sizeof params );
        params.width = _fonsContext->_width;
        params.height = 512;
        params.renderCreate = FONSRenderCreate;
        params.renderResize = []( void* userPtr, int width, int height )
        {
            const DVDFONSContext* dvd = (DVDFONSContext*)userPtr;

            if ( dvd->_fontRenderingTexture != INVALID_HANDLE<Texture>)
            {
                Get(dvd->_fontRenderingTexture)->createWithData( nullptr, 0u, vec2<U16>( width, height), {} );
                return 1;
            }

            return FONSRenderCreate( userPtr, width, height );
        };
        params.renderUpdate = []( void* userPtr, int* rect, const unsigned char* data )
        {
            const DVDFONSContext* dvd = (DVDFONSContext*)userPtr;

            if ( dvd->_fontRenderingTexture == INVALID_HANDLE<Texture> )
            {
                FONSRenderCreate( userPtr, dvd->_width, dvd->_width );
            }

            const I32 w = rect[2] - rect[0];
            const I32 h = rect[3] - rect[1];

            const PixelAlignment pixelUnpackAlignment =
            {
                ._alignment = 1u,
                ._rowLength = to_size( dvd->_width ),
                ._skipPixels = to_size( rect[0] ),
                ._skipRows = to_size( rect[1] )
            };

            Get(dvd->_fontRenderingTexture)->replaceData( (const Divide::Byte*)data, sizeof( U8 ) * w * h, vec3<U16>{rect[0], rect[1], 0}, vec3<U16>{w, h, 1u}, pixelUnpackAlignment );
        };
        params.renderDraw = []( void* userPtr, const FONSvert* verts, int nverts )
        {
            if (nverts <= 0)
            {
                return;
            }

            DVDFONSContext* dvd = (DVDFONSContext*)userPtr;
            if ( dvd->_fontRenderingTexture == INVALID_HANDLE<Texture> || !dvd->_fontRenderingBuffer || !dvd->_commandBuffer )
            {
                return;
            }

            dvd->_writeOffset = (dvd->_writeOffset + 1u) % dvd->_bufferSizeFactor;
            if ( dvd->_writeOffset == 0u )
            {
                // Wrapped around. Dangerous to write data. Wait till next frame
                dvd->_bufferNeedsResize = true;
                return;
            }

            const U32 elementOffset = dvd->_writeOffset * FONS_VERTEX_COUNT;

            const BufferLock lock = dvd->_fontRenderingBuffer->updateBuffer( 0u, elementOffset, nverts, (Divide::bufferPtr)verts );
            dvd->_memCmd->_bufferLocks.emplace_back( lock );

            GenericDrawCommand drawCmd
            {
                ._cmd = 
                {
                    .vertexCount = to_U32(nverts),
                    .baseVertex = elementOffset
                },
                ._sourceBuffer = dvd->_fontRenderingBuffer->handle()
            };
            GFX::EnqueueCommand(*dvd->_commandBuffer, GFX::DrawCommand{MOV(drawCmd)});
            
        };
        params.renderDelete = [](void* userPtr)
        {
            DVDFONSContext* dvd = (DVDFONSContext*)userPtr;
            dvd->_fontRenderingBuffer.reset();
            DestroyResource(dvd->_fontRenderingTexture);
        };
        params.userPtr = _fonsContext.get();

        _fonsContext->_impl = fonsCreateInternal( &params );

        std::atomic_uint loadTasks = 0;
        {
            ShaderModuleDescriptor vertModule = {};
            vertModule._moduleType = ShaderType::VERTEX;
            vertModule._sourceFile = "ImmediateModeEmulation.glsl";
            vertModule._variant = "GUI";
            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "ImmediateModeEmulation.glsl";
            fragModule._variant = "GUI";

            ResourceDescriptor<ShaderProgram> immediateModeShader( "ImmediateModeEmulationGUI" );
            immediateModeShader.waitForReady( true );
            ShaderProgramDescriptor& shaderDescriptor = immediateModeShader._propertyDescriptor;
            shaderDescriptor._modules.push_back( vertModule );
            shaderDescriptor._modules.push_back( fragModule );

            _textRenderShader = CreateResource( immediateModeShader, loadTasks );
            PipelineDescriptor descriptor = {};
            descriptor._shaderProgramHandle = _textRenderShader;
            descriptor._stateBlock = context.gfx().get2DStateBlock();
            descriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
            descriptor._vertexFormat._vertexBindings.emplace_back()._strideInBytes = 2 * sizeof( F32 ) + 2 * sizeof( F32 ) + 4 * sizeof( U8 );
            AttributeDescriptor& descPos = descriptor._vertexFormat._attributes[to_base( AttribLocation::POSITION )]; //vec2
            AttributeDescriptor& descUV = descriptor._vertexFormat._attributes[to_base( AttribLocation::TEXCOORD )];  //vec2
            AttributeDescriptor& descColour = descriptor._vertexFormat._attributes[to_base( AttribLocation::COLOR )]; //vec4

            descPos._vertexBindingIndex = 0u;
            descPos._componentsPerElement = 2u;
            descPos._dataType = GFXDataFormat::FLOAT_32;

            descUV._vertexBindingIndex = 0u;
            descUV._componentsPerElement = 2u;
            descUV._dataType = GFXDataFormat::FLOAT_32;

            descColour._vertexBindingIndex = 0u;
            descColour._componentsPerElement = 4u;
            descColour._normalized = true;

            descPos._strideInBytes = 0u;
            descUV._strideInBytes = 2 * sizeof( F32 );
            descColour._strideInBytes = 2 * sizeof( F32 ) + 2 * sizeof( F32 );

            BlendingSettings& blend = descriptor._blendStates._settings[0u];
            descriptor._blendStates._blendColour = DefaultColours::BLACK_U8;

            blend.enabled( true );
            blend.blendSrc( BlendProperty::SRC_ALPHA );
            blend.blendDest( BlendProperty::INV_SRC_ALPHA );
            blend.blendOp( BlendOperation::ADD );
            blend.blendSrcAlpha( BlendProperty::ONE );
            blend.blendDestAlpha( BlendProperty::ZERO );
            blend.blendOpAlpha( BlendOperation::COUNT );

            _textRenderPipeline = context.gfx().newPipeline( descriptor );
        }
        {
            ShaderModuleDescriptor vertModule = {};
            vertModule._moduleType = ShaderType::VERTEX;
            vertModule._sourceFile = "ImmediateModeEmulation.glsl";
            vertModule._variant = "CEGUI";
            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "ImmediateModeEmulation.glsl";
            fragModule._variant = "CEGUI";


            ResourceDescriptor<ShaderProgram> ceguiShader( "CEGUIShader" );
            ceguiShader.waitForReady( true );

            ShaderProgramDescriptor& shaderDescriptor = ceguiShader._propertyDescriptor;
            shaderDescriptor._modules.push_back( fragModule );
            shaderDescriptor._modules.push_back( vertModule );
            _ceguiRenderShader = CreateResource( ceguiShader, loadTasks );
        }

        const Configuration::GUI& guiConfig = parent().platformContext().config().gui;
        if ( guiConfig.cegui.enabled )
        {
            const vec2<U16> renderSize = context.gfx().renderingResolution();
            const CEGUI::Sizef size( static_cast<float>(renderSize.width), static_cast<float>(renderSize.height) );
            _ceguiRenderer = &CEGUI::CEGUIRenderer::create( context.gfx(), _ceguiRenderShader, size );

            const string logFile = (Paths::g_logPath / "CEGUI.log").string();
            CEGUI::System::create( *_ceguiRenderer, nullptr, nullptr, nullptr, nullptr, "", logFile.c_str() );
            if constexpr ( Config::Build::IS_DEBUG_BUILD )
            {
                CEGUI::Logger::getSingleton().setLoggingLevel( CEGUI::Informative );
            }

            CEGUI::DefaultResourceProvider* rp = static_cast<CEGUI::DefaultResourceProvider*>(CEGUI::System::getSingleton().getResourceProvider());

            const CEGUI::String CEGUIInstallSharePath( (Paths::g_GUILocation.string() + Paths::g_pathSeparator).c_str() );
            rp->setResourceGroupDirectory( "schemes", CEGUIInstallSharePath + "schemes/" );
            rp->setResourceGroupDirectory( "imagesets", CEGUIInstallSharePath + "imagesets/" );
            rp->setResourceGroupDirectory( "fonts", CEGUIInstallSharePath + "fonts/" );
            rp->setResourceGroupDirectory( "layouts", CEGUIInstallSharePath + "layouts/" );
            rp->setResourceGroupDirectory( "looknfeels", CEGUIInstallSharePath + "looknfeel/" );
            rp->setResourceGroupDirectory( "lua_scripts", CEGUIInstallSharePath + "lua_scripts/" );
            rp->setResourceGroupDirectory( "schemas", CEGUIInstallSharePath + "xml_schemas/" );
            rp->setResourceGroupDirectory( "animations", CEGUIInstallSharePath + "animations/" );

            // set the default resource groups to be used
            CEGUI::ImageManager::setImagesetDefaultResourceGroup( "imagesets" );
            CEGUI::Font::setDefaultResourceGroup( "fonts" );
            CEGUI::Scheme::setDefaultResourceGroup( "schemes" );
            CEGUI::WidgetLookManager::setDefaultResourceGroup( "looknfeels" );
            CEGUI::WindowManager::setDefaultResourceGroup( "layouts" );
            CEGUI::ScriptModule::setDefaultResourceGroup( "lua_scripts" );
            // setup default group for validation schemas
            CEGUI::XMLParser* parser = CEGUI::System::getSingleton().getXMLParser();
            if ( parser->isPropertyPresent( "SchemaDefaultResourceGroup" ) )
            {
                parser->setProperty( "SchemaDefaultResourceGroup", "schemas" );
            }
            CEGUI::FontManager::getSingleton().createFromFile( "DejaVuSans-10.font" );
            CEGUI::FontManager::getSingleton().createFromFile( "DejaVuSans-12.font" );
            CEGUI::FontManager::getSingleton().createFromFile( "DejaVuSans-10-NoScale.font" );
            CEGUI::FontManager::getSingleton().createFromFile( "DejaVuSans-12-NoScale.font" );
            CEGUI::SchemeManager::getSingleton().createFromFile( (defaultGUIScheme() + ".scheme").c_str() );

            _renderTextureTarget = static_cast<CEGUI::DVDTextureTarget*>(_ceguiRenderer->createTextureTarget());
            _renderTextureTarget->declareRenderSize( size );

            _ceguiContext = &CEGUI::System::getSingleton().createGUIContext( *_renderTextureTarget );
            _rootSheet = CEGUI::WindowManager::getSingleton().createWindow( "DefaultWindow", "root_window" );
            _rootSheet->setMousePassThroughEnabled( true );
            _rootSheet->setUsingAutoRenderingSurface( false );
            _rootSheet->setPixelAligned( false );

            _ceguiContext->setRootWindow( _rootSheet );
            _ceguiContext->setDefaultTooltipType( (defaultGUIScheme() + "/Tooltip").c_str() );

            _console->createCEGUIWindow();
            CEGUI::System::getSingleton().notifyDisplaySizeChanged( size );
        }

        recreateDefaultMessageBox();

        _init = true;
        if ( _fonsContext == nullptr )
        {
            Console::errorfn( LOCALE_STR( "ERROR_FONT_INIT" ) );
            return ErrorCode::FONT_INIT_ERROR;
        }

        return ErrorCode::NO_ERR;
    }

    void GUI::recreateDefaultMessageBox()
    {
        _defaultMsgBox.reset();

        if ( _ceguiRenderer != nullptr )
        {
            _defaultMsgBox = std::make_unique<GUIMessageBox>( "AssertMsgBox",
                                                              "Assertion failure",
                                                              "Assertion failed with message: ",
                                                              vec2<I32>( 0 ),
                                                              _context->rootSheet() );
        }
        g_assertMsgBox = _defaultMsgBox.get();
    }

    void GUI::destroy()
    {
        if ( _init )
        {

            _fonsContext.reset();
            _fonts.clear();

            Console::printfn( LOCALE_STR( "STOP_GUI" ) );
            _console.reset();
            _defaultMsgBox.reset();
            g_assertMsgBox = nullptr;

            {
                LockGuard<SharedMutex> w_lock( _guiStackLock );
                assert( _guiStack.empty() );
                for ( U8 i = 0; i < to_base( GUIType::COUNT ); ++i )
                {
                    for ( auto& [nameHash, entry] : _guiElements[i] )
                    {
                        delete entry.first;
                    }
                    _guiElements[i].clear();
                }
            }

            if ( _ceguiRenderer != nullptr )
            {
                // Close CEGUI
                try
                {
                    CEGUI::System::destroy();
                }
                catch ( ... )
                {
                    Console::d_errorfn( LOCALE_STR( "ERROR_CEGUI_DESTROY" ) );
                }
                CEGUI::CEGUIRenderer::destroy( *_ceguiRenderer );
                _ceguiRenderer = nullptr;
            }

            DestroyResource(_textRenderShader);
            DestroyResource(_ceguiRenderShader);
            _init = false;
        }
    }

    void GUI::showDebugCursor( const bool state )
    {
        _showDebugCursor = state;

        if ( _rootSheet != nullptr )
        {
            _rootSheet->setMouseCursor( state ? "GWEN/Tree.Plus" : "" );
        }
    }

    void GUI::onResolutionChange( const SizeChangeParams& params )
    {
        if ( !params.isMainWindow )
        {
            return;
        }

        if ( _ceguiRenderer != nullptr )
        {
            const CEGUI::Sizef windowSize( params.width, params.height );
            CEGUI::System::getSingleton().notifyDisplaySizeChanged( windowSize );
            if ( _rootSheet )
            {
                const Rect<I32>& renderViewport = { 0, 0, params.width, params.height };
                _rootSheet->setSize( CEGUI::USize( CEGUI::UDim( 0.0f, to_F32( renderViewport.z ) ),
                                                   CEGUI::UDim( 0.0f, to_F32( renderViewport.w ) ) ) );
                _rootSheet->setPosition( CEGUI::UVector2( CEGUI::UDim( 0.0f, to_F32( renderViewport.x ) ),
                                                          CEGUI::UDim( 0.0f, to_F32( renderViewport.y ) ) ) );
            }
        }
    }

    void GUI::setCursorPosition( const I32 x, const I32 y )
    {
        if ( _ceguiRenderer != nullptr )
        {
            getCEGUIContext()->injectMousePosition( to_F32( x ), to_F32( y ) );
        }
    }

    // Return true if input was consumed
    bool GUI::onKeyDown( const Input::KeyEvent& key )
    {
        return _ceguiInput.onKeyDown( key );
    }

    // Return true if input was consumed
    bool GUI::onKeyUp( const Input::KeyEvent& key )
    {
        return _ceguiInput.onKeyUp( key );
    }

    // Return true if input was consumed
    bool GUI::mouseMoved( const Input::MouseMoveEvent& arg )
    {
        return _ceguiInput.mouseMoved( arg );
    }

    // Return true if input was consumed
    bool GUI::mouseButtonPressed( const Input::MouseButtonEvent& arg )
    {
        return _ceguiInput.mouseButtonPressed( arg );
    }

    // Return true if input was consumed
    bool GUI::mouseButtonReleased( const Input::MouseButtonEvent& arg )
    {
        return _ceguiInput.mouseButtonReleased( arg );
    }

    // Return true if input was consumed
    bool GUI::joystickAxisMoved( const Input::JoystickEvent& arg )
    {
        return _ceguiInput.joystickAxisMoved( arg );
    }

    // Return true if input was consumed
    bool GUI::joystickPovMoved( const Input::JoystickEvent& arg )
    {
        return _ceguiInput.joystickPovMoved( arg );
    }

    // Return true if input was consumed
    bool GUI::joystickButtonPressed( const Input::JoystickEvent& arg )
    {
        return _ceguiInput.joystickButtonPressed( arg );
    }

    // Return true if input was consumed
    bool GUI::joystickButtonReleased( const Input::JoystickEvent& arg )
    {
        return _ceguiInput.joystickButtonReleased( arg );
    }

    // Return true if input was consumed
    bool GUI::joystickBallMoved( const Input::JoystickEvent& arg )
    {
        return _ceguiInput.joystickBallMoved( arg );
    }

    // Return true if input was consumed
    bool GUI::joystickAddRemove( const Input::JoystickEvent& arg )
    {
        return _ceguiInput.joystickAddRemove( arg );
    }

    bool GUI::joystickRemap( const Input::JoystickEvent& arg )
    {
        return _ceguiInput.joystickRemap( arg );
    }

    bool GUI::onTextEvent( [[maybe_unused]] const Input::TextEvent& arg ) noexcept
    {
        return false;
    }

    GUIElement* GUI::getSceneGUIElementImpl( const I64 sceneID, const U64 elementName, const GUIType type ) const
    {
        if ( sceneID != 0 )
        {
            SharedLock<SharedMutex> r_lock( _guiStackLock );
            const GUIMapPerScene::const_iterator it = _guiStack.find( sceneID );
            if ( it != std::cend( _guiStack ) )
            {
                return it->second->getGUIElement<GUIElement>( elementName );
            }
        }
        else
        {
            return GUIInterface::getGUIElementImpl( elementName, type );
        }

        return nullptr;
    }

    GUIElement* GUI::getSceneGUIElementImpl( const I64 sceneID, const I64 elementID, const GUIType type ) const
    {
        if ( sceneID != 0 )
        {
            SharedLock<SharedMutex> r_lock( _guiStackLock );
            const GUIMapPerScene::const_iterator it = _guiStack.find( sceneID );
            if ( it != std::cend( _guiStack ) )
            {
                return it->second->getGUIElement<GUIElement>( elementID );
            }
        }
        else
        {
            return GUIInterface::getGUIElementImpl( elementID, type );
        }

        return nullptr;
    }
    CEGUI::GUIContext* GUI::getCEGUIContext() noexcept
    {
        return _ceguiContext;
    }

    /// Try to find the requested font in the font cache. Load on cache miss.
    I32 GUI::getFont( const Str<64>& fontName )
    {
        if ( _fontCache.first.compare( fontName ) != 0 )
        {
            _fontCache.first = fontName;
            const U64 fontNameHash = _ID( fontName.c_str() );
            // Search for the requested font by name
            const auto& it = _fonts.find( fontNameHash );
            // If we failed to find it, it wasn't loaded yet
            if ( it == std::cend( _fonts ) )
            {
                // Fonts are stored in the general asset directory -> in the GUI
                // subfolder -> in the fonts subfolder
                const string fontPath = (Paths::g_fontsPath / fontName.c_str()).string();
                // We use FontStash to load the font file
                _fontCache.second = fonsAddFont( _fonsContext->_impl, fontName.c_str(), fontPath.c_str());
                // If the font is invalid, inform the user, but map it anyway, to avoid
                // loading an invalid font file on every request
                if ( _fontCache.second == FONS_INVALID )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_FONT_FILE" ), fontName.c_str() );
                }
                // Save the font in the font cache
                hashAlg::insert( _fonts, fontNameHash, _fontCache.second );

            }
            else
            {
                _fontCache.second = it->second;
            }
        }

        // Return the font
        return _fontCache.second;
    }

};
