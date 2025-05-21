

#include "Headers/Editor.h"

#include "Headers/Utils.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/Application.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "ECS/Components/Headers/TransformComponent.h"
#include "Editor/Widgets/DockedWindows/Headers/ContentExplorerWindow.h"
#include "Editor/Widgets/DockedWindows/Headers/NodePreviewWindow.h"
#include "Editor/Widgets/DockedWindows/Headers/OutputWindow.h"
#include "Editor/Widgets/DockedWindows/Headers/PostFXWindow.h"
#include "Editor/Widgets/DockedWindows/Headers/PropertyWindow.h"
#include "Editor/Widgets/DockedWindows/Headers/SceneViewWindow.h"
#include "Editor/Widgets/DockedWindows/Headers/SolutionExplorerWindow.h"
#include "Editor/Widgets/Headers/EditorOptionsWindow.h"
#include "Editor/Widgets/Headers/ImGuiExtensions.h"
#include "Editor/Widgets/Headers/MenuBar.h"
#include "Editor/Widgets/Headers/StatusBar.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"
#include "Platform/Video/Headers/CommandBufferPool.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Rendering/Camera/Headers/Camera.h"

#include <IconsForkAwesome.h>
#include <imgui_memory_editor/imgui_memory_editor.h>
#include <imgui_internal.h>

namespace Divide
{
    namespace
    {
        const string g_editorFontFile{ "Roboto-Medium.ttf" };
        const string g_editorFontFileBold{ "OpenSans-Bold.ttf" };
        const string g_editorIconFile{ FONT_ICON_FILE_NAME_FK };
        const string g_editorSaveFile{ "Editor.xml" };
        const string g_editorSaveFileBak{ "Editor.xml.bak" };

        WindowManager* g_windowManager = nullptr;

        struct ImGuiViewportData
        {
            DisplayWindow* _window = nullptr;
            bool _windowOwned = false;
        };

        IMGUICallbackData g_modalTextureData;

        inline void Reset( Editor::FocusedWindowState& state ) noexcept
        {
            state = {};
        }

        [[nodiscard]] inline bool SetFocus( Editor::FocusedWindowState& state ) noexcept
        {
            bool ret = false;
            if ( state._hoveredNodePreview != state._focusedNodePreview )
            {
                state._focusedNodePreview = state._hoveredNodePreview;
                ret = true;
            }
            if ( state._hoveredScenePreview != state._focusedScenePreview )
            {
                state._focusedScenePreview = state._hoveredScenePreview;
                ret = true;
            }

            return ret;
        }

        [[nodiscard]] inline bool Hovered( const Editor::FocusedWindowState& state ) noexcept
        {
            return state._hoveredNodePreview || state._hoveredScenePreview;
        }

        [[nodiscard]] inline bool Focused( const Editor::FocusedWindowState& state ) noexcept
        {
            return state._focusedNodePreview || state._focusedScenePreview;
        }
    } // namespace

    namespace ImGuiCustom
    {
        struct ImGuiAllocatorUserData
        {
            PlatformContext* _context = nullptr;
        };
#if defined(ENABLE_MIMALLOC)
        FORCE_INLINE void* MallocWrapper( const size_t size, [[maybe_unused]] void* user_data ) noexcept
        {
            // PlatformContext* user_data;
            return mi_new( size );
        }

        FORCE_INLINE void FreeWrapper( void* ptr, [[maybe_unused]] void* user_data ) noexcept
        {
            // PlatformContext* user_data;
            mi_free( ptr );
        }

        static ImGuiMemAllocFunc g_ImAllocatorAllocFunc = MallocWrapper;
        static ImGuiMemFreeFunc g_ImAllocatorFreeFunc = FreeWrapper;
#endif //ENABLE_MIMALLOC

        static ImGuiAllocatorUserData g_ImAllocatorUserData{};
    }; // namespace ImGuiCustom

    ImTextureID to_TexID( Handle<Texture> handle )
    {
        const U32 texData = handle._data;
        return ImTextureID(intptr_t(texData));
    }

    Handle<Texture> from_TexID( ImTextureID texID )
    {
        const intptr_t texData = intptr_t(texID);
        return Handle<Texture>{ ._data = to_U32(texData) };
    }

    namespace
    {
        std::array<Input::MouseButton, 5> g_editorButtons =
        {
            Input::MouseButton::MB_Left,
            Input::MouseButton::MB_Right,
            Input::MouseButton::MB_Middle,
            Input::MouseButton::MB_Button3,
            Input::MouseButton::MB_Button4,
        };
    }

    std::array<const char*, 3> Editor::g_supportedExportPlatforms = { "Windows",
                                                                        "Linux",
                                                                        "macOS" };

    PushConstantsStruct IMGUICallbackToPushConstants( const IMGUICallbackData& data, const bool isArrayTexture )
    {
        PushConstantsStruct pushConstants{};
        pushConstants.data[0]._vec[0]    = data._colourData;
        pushConstants.data[0]._vec[1].xy = data._depthRange;
        pushConstants.data[0]._vec[1].z  = to_F32( data._arrayLayer );
        pushConstants.data[0]._vec[1].w  = to_F32( data._mip );
        pushConstants.data[0]._vec[2].x  = isArrayTexture ? 1.f : 0.f;
        pushConstants.data[0]._vec[2].y  = data._isDepthTexture ? 1.f : 0.f;
        pushConstants.data[0]._vec[2].z  = data._flip ? 1.f : 0.f;
        pushConstants.data[0]._vec[2].w  = data._srgb ? 1.f : 0.f;
        return pushConstants;
    }

    Editor::Editor( PlatformContext& context, const ImGuiStyleEnum theme )
        : PlatformContextComponent( context )
        , FrameListener( "Editor", context.kernel().frameListenerMgr(), 9999 )
        , _editorUpdateTimer( Time::ADD_TIMER( "Editor Update Timer" ) )
        , _editorRenderTimer( Time::ADD_TIMER( "Editor Render Timer" ) )
        , _currentTheme( theme )
    {

#if defined(ENABLE_MIMALLOC)
        ImGui::SetAllocatorFunctions( ImGuiCustom::g_ImAllocatorAllocFunc,
                                      ImGuiCustom::g_ImAllocatorFreeFunc,
                                      &ImGuiCustom::g_ImAllocatorUserData );
#endif //ENABLE_MIMALLOC
        ImGuiFs::Dialog::ExtraWindowFlags |= ImGuiWindowFlags_NoSavedSettings;

        _menuBar = std::make_unique<MenuBar>( context, true );
        _statusBar = std::make_unique<StatusBar>( context );
        _optionsWindow = std::make_unique<EditorOptionsWindow>( context );

        _undoManager = std::make_unique<UndoManager>( 25 );
        g_windowManager = &context.app().windowManager();
        _memoryEditorData = std::make_pair( nullptr, 0 );
        _nodePreviewBGColour = { 0.35f, 0.32f, 0.45f };

        processInput(false);
    }

    Editor::~Editor()
    {
        close();
        g_windowManager = nullptr;
    }

    void Editor::idle() noexcept
    {
        if (_queueToggle.has_value())
        {
            toggleInternal(_queueToggle.value());
            _queueToggle = {};
        }
    }

    void Editor::createFontTexture(ImGuiIO& io, const F32 DPIScaleFactor )
    {
        constexpr F32 fontSize = 13.f;
        constexpr F32 fontSizeBold = 16.f;
        constexpr F32 iconSize = 16.f;

        if ( _fontTexture == INVALID_HANDLE<Texture> )
        {

            ResourceDescriptor<Texture> resDescriptor( "IMGUI_font_texture" );
            TextureDescriptor& texDescriptor = resDescriptor._propertyDescriptor;
            texDescriptor._mipMappingState = MipMappingState::OFF;

            _fontTexture = CreateResource( resDescriptor );
        }
        DIVIDE_ASSERT( _fontTexture != INVALID_HANDLE<Texture> );

        U8* pPixels = nullptr;
        I32 iWidth = 0;
        I32 iHeight = 0;
        const string textFontPath = ( Paths::g_fontsPath / g_editorFontFile ).string();
        const string textFontBoldPath = ( Paths::g_fontsPath / g_editorFontFileBold ).string();
        const string iconFontPath = ( Paths::g_fontsPath / g_editorIconFile ).string();

        ImFontConfig font_cfg;
        font_cfg.OversampleH = font_cfg.OversampleV = 1;
        font_cfg.PixelSnapH = true;
        font_cfg.SizePixels = fontSize * DPIScaleFactor;
        font_cfg.EllipsisChar = (ImWchar)0x0085;
        font_cfg.GlyphOffset.y = 1.0f * IM_TRUNC( font_cfg.SizePixels / fontSize ); // Add +1 offset per fontSize units
        ImFormatString( font_cfg.Name,
                        IM_ARRAYSIZE( font_cfg.Name ),
                        "%s, %dpx",
                        g_editorFontFile.c_str(),
                        (int)font_cfg.SizePixels );

        io.Fonts->Clear();
        io.Fonts->AddFontFromFileTTF( textFontPath.c_str(), fontSize * DPIScaleFactor, &font_cfg );

        font_cfg.MergeMode = true;
        font_cfg.SizePixels = iconSize * DPIScaleFactor;
        font_cfg.GlyphOffset.y = 1.0f * IM_TRUNC( font_cfg.SizePixels / iconSize ); // Add +1 offset per 16 units

        static const ImWchar icons_ranges[] = { ICON_MIN_FK, ICON_MAX_FK, 0 };
        io.Fonts->AddFontFromFileTTF( iconFontPath.c_str(), iconSize * DPIScaleFactor, &font_cfg, icons_ranges );

        font_cfg.MergeMode = false;
        font_cfg.SizePixels = fontSizeBold * DPIScaleFactor;
        font_cfg.GlyphOffset.y = 0.f; // 1.0f * IM_TRUNC(font_cfg.SizePixels / fontSizeBold);  // Add +1
        // offset per fontSize units
        io.Fonts->AddFontFromFileTTF( textFontBoldPath.c_str(), fontSizeBold * DPIScaleFactor, &font_cfg );

        io.Fonts->GetTexDataAsRGBA32( &pPixels, &iWidth, &iHeight );
        Get(_fontTexture)->createWithData( (Byte*)pPixels, iWidth * iHeight * 4u, vec2<U16>( iWidth, iHeight ), {});
        // Store our identifier as reloading data may change the handle!
        io.Fonts->SetTexID( to_TexID(_fontTexture) );
    }

    bool Editor::init( const vec2<U16> renderResolution )
    {
        if ( isInit() )
        {
            return false;
        }

        if ( createDirectory( Paths::g_saveLocation / Paths::Editor::g_saveLocation ) != FileError::NONE )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        _mainWindow = &_context.app().windowManager().getWindow( 0u );
        _render2DSnapshot = Camera::GetUtilityCamera( Camera::UtilityCamera::_2D_FLIP_Y )->snapshot();
        _editorCamera = Camera::CreateCamera( "Editor Camera", Camera::Mode::FREE_FLY );
        _editorCamera->fromCamera( *Camera::GetUtilityCamera( Camera::UtilityCamera::DEFAULT ) );
        _editorCamera->setGlobalAxis( true, false, false );
        _editorCamera->setEye( 0.f, 20.f, 60.f );
        _editorCamera->setRotation( 0.f, 15.f, 0.f );
        _editorCamera->speedFactor().turn = 45.f;

        _nodePreviewCamera = Camera::CreateCamera( "Node Preview Camera", Camera::Mode::ORBIT );
        _nodePreviewCamera->fromCamera( *Camera::GetUtilityCamera( Camera::UtilityCamera::DEFAULT ) );
        _nodePreviewCamera->setGlobalAxis(true, false, false);
        _nodePreviewCamera->speedFactor().turn = 125.f;
        _nodePreviewCamera->speedFactor().zoom = 175.f;

        IMGUI_CHECKVERSION();
        assert( _imguiContexts[to_base( ImGuiContextType::Editor )] == nullptr );

        ImGuiCustom::g_ImAllocatorUserData._context = &context();

        {
            ShaderModuleDescriptor vertModule = {};
            vertModule._moduleType = ShaderType::VERTEX;
            vertModule._sourceFile = "IMGUI.glsl";

            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "IMGUI.glsl";

            ResourceDescriptor<ShaderProgram> shaderResDescriptor( "IMGUI" );
            ShaderProgramDescriptor& shaderDescriptor = shaderResDescriptor._propertyDescriptor;
            shaderDescriptor._modules.push_back( vertModule );
            shaderDescriptor._modules.push_back( fragModule );
            shaderDescriptor._globalDefines.emplace_back("toggleChannel ivec4(PushData0[0])");
            shaderDescriptor._globalDefines.emplace_back("depthRange PushData0[1].xy");
            shaderDescriptor._globalDefines.emplace_back("layer uint(PushData0[1].z)");
            shaderDescriptor._globalDefines.emplace_back("mip uint(PushData0[1].w)");
            shaderDescriptor._globalDefines.emplace_back("textureType uint(PushData0[2].x)");
            shaderDescriptor._globalDefines.emplace_back("depthTexture uint(PushData0[2].y)");
            shaderDescriptor._globalDefines.emplace_back("flip uint(PushData0[2].z)");
            shaderDescriptor._globalDefines.emplace_back("convertToSRGB (uint(PushData0[2].w) == 1)");

            _imguiProgram = CreateResource( shaderResDescriptor );
        }
        {
            _infiniteGridPipelineDesc._primitiveTopology = PrimitiveTopology::TRIANGLES;
            _infiniteGridPipelineDesc._stateBlock._cullMode = CullMode::NONE;
            _infiniteGridPipelineDesc._stateBlock._depthWriteEnabled = false;

            ShaderModuleDescriptor vertModule = {};
            vertModule._moduleType = ShaderType::VERTEX;
            vertModule._sourceFile = "InfiniteGrid.glsl";

            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "InfiniteGrid.glsl";
            fragModule._defines.emplace_back( "axisWidth PushData0[0].x" );
            fragModule._defines.emplace_back( "gridScale PushData0[0].y" );


            ResourceDescriptor<ShaderProgram> shaderResDescriptor( "InfiniteGrid.Colour" );
            ShaderProgramDescriptor& shaderDescriptor = shaderResDescriptor._propertyDescriptor;
            shaderDescriptor._modules.push_back( vertModule );
            shaderDescriptor._modules.push_back( fragModule );

            _infiniteGridProgram = CreateResource( shaderResDescriptor );
            _infiniteGridPipelineDesc._shaderProgramHandle = _infiniteGridProgram;
            BlendingSettings& blend = _infiniteGridPipelineDesc._blendStates._settings[to_U8( GFXDevice::ScreenTargets::ALBEDO )];
            blend.enabled( true );
            blend.blendSrc( BlendProperty::SRC_ALPHA );
            blend.blendDest( BlendProperty::INV_SRC_ALPHA );
            blend.blendOp( BlendOperation::ADD );

            _axisGizmoPipelineDesc._stateBlock = _context.gfx().getNoDepthTestBlock();
            _axisGizmoPipelineDesc._shaderProgramHandle = _context.gfx().imShaders()->imWorldShaderNoTexture();
        }

        _infiniteGridPrimitive = _context.gfx().newIMP( "Editor Infinite Grid" );
        _infiniteGridPrimitive->setPipelineDescriptor( _infiniteGridPipelineDesc );

        _infiniteGridPrimitive->beginBatch( true, 6, 0 );
        _infiniteGridPrimitive->begin( PrimitiveTopology::TRIANGLES );
        _infiniteGridPrimitive->vertex( 1.f, 1.f, 0.f );
        _infiniteGridPrimitive->vertex( -1.f, -1.f, 0.f );
        _infiniteGridPrimitive->vertex( -1.f, 1.f, 0.f );
        _infiniteGridPrimitive->vertex( -1.f, -1.f, 0.f );
        _infiniteGridPrimitive->vertex( 1.f, 1.f, 0.f );
        _infiniteGridPrimitive->vertex( 1.f, -1.f, 0.f );
        _infiniteGridPrimitive->end();
        _infiniteGridPrimitive->endBatch();

        PipelineDescriptor pipelineDesc = {};
        pipelineDesc._stateBlock._cullMode = CullMode::NONE;
        pipelineDesc._stateBlock._depthTestEnabled = false;
        pipelineDesc._stateBlock._depthWriteEnabled = false;
        pipelineDesc._stateBlock._scissorTestEnabled = true;
        pipelineDesc._primitiveTopology = PrimitiveTopology::TRIANGLES;
        pipelineDesc._vertexFormat._vertexBindings.emplace_back()._strideInBytes = sizeof( ImDrawVert );
        AttributeDescriptor& descPos = pipelineDesc._vertexFormat._attributes[to_base( AttribLocation::GENERIC )];
        AttributeDescriptor& descUV = pipelineDesc._vertexFormat._attributes[to_base( AttribLocation::TEXCOORD )];
        AttributeDescriptor& descColour = pipelineDesc._vertexFormat._attributes[to_base( AttribLocation::COLOR )];

        descPos._vertexBindingIndex = descUV._vertexBindingIndex = descColour._vertexBindingIndex = 0u;
        descPos._componentsPerElement = descUV._componentsPerElement = 2u;
        descPos._dataType = descUV._dataType = GFXDataFormat::FLOAT_32;

        descColour._componentsPerElement = 4u;
        descColour._dataType = GFXDataFormat::UNSIGNED_BYTE;
        descColour._normalized = true;

        descPos._strideInBytes = to_U32( offsetof( ImDrawVert, pos ) );
        descUV._strideInBytes = to_U32( offsetof( ImDrawVert, uv ) );
        descColour._strideInBytes = to_U32( offsetof( ImDrawVert, col ) );

        pipelineDesc._shaderProgramHandle = _imguiProgram;

        BlendingSettings& blend = pipelineDesc._blendStates._settings[to_U8( GFXDevice::ScreenTargets::ALBEDO )];
        blend.enabled( true );
        blend.blendSrc( BlendProperty::SRC_ALPHA );
        blend.blendDest( BlendProperty::INV_SRC_ALPHA );
        blend.blendOp( BlendOperation::ADD );
        _editorPipeline = _context.gfx().newPipeline( pipelineDesc );

        const vector<WindowManager::MonitorData>& monitors = g_windowManager->monitorData();
        const WindowManager::MonitorData& mainMonitor = monitors[_mainWindow->initialDisplay()];

        ImGuiContext*& editorContext = _imguiContexts[to_base(ImGuiContextType::Editor)];
        editorContext = ImGui::CreateContext();
        ImGuiIO& io = _imguiContexts[to_base(ImGuiContextType::Editor)]->IO;
        createFontTexture(io, mainMonitor.dpi / PlatformDefaultDPI());
        initBasicImGUIState( io, true, _imguiStringBuffers[to_base(ImGuiContextType::Editor)] );
        editorContext->Viewports[0]->PlatformHandle = _mainWindow;

        ImGuiPlatformIO& platform_io = editorContext->PlatformIO;
        platform_io.Platform_CreateWindow = []( ImGuiViewport* viewport )
        {
            if ( g_windowManager != nullptr )
            {
                const DisplayWindow& window = g_windowManager->getWindow( 0u );
                WindowDescriptor winDescriptor = {};
                winDescriptor.title = "No Title Yet";
                winDescriptor.parentWindow = g_windowManager->mainWindow();
                winDescriptor.targetDisplay = to_U16( window.currentDisplayIndex() );
                winDescriptor.flags = to_U16( WindowDescriptor::Flags::HIDDEN );

                winDescriptor.flags |= viewport->Flags & ImGuiViewportFlags_NoDecoration
                                                       ? 0
                                                       : to_U16( WindowDescriptor::Flags::DECORATED );
                winDescriptor.flags |= viewport->Flags & ImGuiViewportFlags_NoDecoration
                                                       ? 0
                                                       : to_U16( WindowDescriptor::Flags::RESIZEABLE );
                winDescriptor.flags |= viewport->Flags & ImGuiViewportFlags_TopMost
                                                       ? to_U16( WindowDescriptor::Flags::ALWAYS_ON_TOP )
                                                       : 0;
                winDescriptor.flags |= viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon
                                                       ? to_U16( WindowDescriptor::Flags::NO_TASKBAR_ICON)
                                                       : 0;

                winDescriptor.dimensions.set( viewport->Size.x, viewport->Size.y );
                winDescriptor.position.set( viewport->Pos.x, viewport->Pos.y );
                winDescriptor.externalClose = true;
                winDescriptor.targetAPI = window.context().gfx().renderAPI();

                ErrorCode err = ErrorCode::NO_ERR;
                DisplayWindow* newWindow = g_windowManager->createWindow( winDescriptor, err );
                if ( err == ErrorCode::NO_ERR )
                {
                    DIVIDE_ASSERT( newWindow != nullptr );

                    newWindow->hidden( false );
                    newWindow->bringToFront();

                    newWindow->addEventListener(WindowEvent::CLOSE_REQUESTED,
                    {
                        ._cbk = [viewport]( [[maybe_unused]] const DisplayWindow::WindowEventArgs& args ) noexcept
                        {
                            viewport->PlatformRequestClose = true;
                            return true;
                        },
                        ._name = "Editor::CLOSE_REQUESTED"
                    });

                    newWindow->addEventListener(WindowEvent::MOVED,
                    {
                        ._cbk = [viewport]( [[maybe_unused]] const DisplayWindow::WindowEventArgs& args ) noexcept
                        {
                            viewport->PlatformRequestMove = true;
                            return true;
                        },
                        ._name = "Editor::MOVED"
                    });

                    newWindow->addEventListener(WindowEvent::RESIZED,
                    {
                        ._cbk = [viewport]( [[maybe_unused]] const DisplayWindow::WindowEventArgs&
                                    args ) noexcept
                        {
                            viewport->PlatformRequestResize = true;
                            return true;
                        },
                        ._name = "Editor::RESIZED"
                    });

                    viewport->PlatformHandle = (void*)newWindow;
                    viewport->PlatformUserData = IM_NEW( ImGuiViewportData )
                    {
                        newWindow, true
                    };
                }
                else
                {
                    DIVIDE_UNEXPECTED_CALL_MSG( "Editor::Platform_CreateWindow failed!" );
                    g_windowManager->destroyWindow( newWindow );
                }
            }
        };

        platform_io.Platform_DestroyWindow = []( ImGuiViewport* viewport )
        {
            if ( g_windowManager != nullptr )
            {
                if ( ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData )
                {
                    if ( data->_window && data->_windowOwned )
                    {
                        g_windowManager->destroyWindow( data->_window );
                    }
                    data->_window = nullptr;
                    IM_DELETE( data );
                }
                viewport->PlatformUserData = viewport->PlatformHandle = nullptr;
            }
        };

        platform_io.Platform_ShowWindow = []( ImGuiViewport* viewport )
        {
            if ( ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData )
            {
                data->_window->hidden( false );
            }
        };

        platform_io.Platform_SetWindowPos = []( ImGuiViewport* viewport,
                                                const ImVec2 pos )
        {
            if ( ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData )
            {
                data->_window->setPosition( (I32)pos.x, (I32)pos.y );
            }
        };

        platform_io.Platform_GetWindowPos = []( ImGuiViewport* viewport ) -> ImVec2
        {
            if ( const ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData )
            {
                const int2 pos = data->_window->getPosition();
                return ImVec2( (F32)pos.x, (F32)pos.y );
            }
            DIVIDE_UNEXPECTED_CALL_MSG( "Editor::Platform_GetWindowPos failed!" );
            return {};
        };

        platform_io.Platform_GetWindowSize = []( ImGuiViewport* viewport ) -> ImVec2
        {
            if ( const ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData )
            {
                const vec2<U16> dim = data->_window->getDimensions();
                return ImVec2( (F32)dim.width, (F32)dim.height );
            }
            DIVIDE_UNEXPECTED_CALL_MSG( "Editor::Platform_GetWindowSize failed!" );
            return {};
        };

        platform_io.Platform_GetWindowFocus = []( ImGuiViewport* viewport ) -> bool
        {
            if ( const ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData )
            {
                return data->_window->hasFocus();
            }
            DIVIDE_UNEXPECTED_CALL_MSG( "Editor::Platform_GetWindowFocus failed!" );
            return false;
        };

        platform_io.Platform_SetWindowAlpha = []( ImGuiViewport* viewport,
                                                  const float alpha )
        {
            if ( ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData )
            {
                data->_window->opacity( to_U8( alpha * 255 ) );
            }
        };

        platform_io.Platform_SetWindowSize = []( ImGuiViewport* viewport,
                                                 ImVec2 size )
        {
            if ( ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData )
            {
                WAIT_FOR_CONDITION( data->_window->setDimensions( to_U16( size.x ), to_U16( size.y ) ) );
            }
        };

        platform_io.Platform_SetWindowFocus = []( ImGuiViewport* viewport )
        {
            if ( const ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData )
            {
                data->_window->bringToFront();
            }
        };

        platform_io.Platform_SetWindowTitle = []( ImGuiViewport* viewport,
                                                  const char* title )
        {
            if ( const ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData )
            {
                data->_window->title( title );
            }
        };

        platform_io.Platform_RenderWindow = []( ImGuiViewport* viewport,
                                                void* platformContext )
        {
            if ( PlatformContext* context = (PlatformContext*)platformContext )
            {
                DisplayWindow* targetWindow = (DisplayWindow*)viewport->PlatformHandle;

                DIVIDE_ASSERT(targetWindow != nullptr);

                context->app().windowManager().drawToWindow( *targetWindow );
            }
        };

        platform_io.Renderer_RenderWindow = []( ImGuiViewport* viewport,
                                                void* platformContext )
        {
            if ( PlatformContext* context = (PlatformContext*)platformContext )
            {
                PROFILE_SCOPE("Editor:: Render Platform Window", Profiler::Category::GUI);

                DisplayWindow* targetWindow = (DisplayWindow*)viewport->PlatformHandle;
                DIVIDE_ASSERT( targetWindow != nullptr );

                Editor* editor = &context->editor();

                ImGuiContext* ctx = editor->_imguiContexts[to_base(ImGuiContextType::Editor)];
                ScopedImGuiContext ctxChange(ctx);

                ImDrawData* pDrawData = viewport->DrawData;
                const I32 fb_width = to_I32( pDrawData->DisplaySize.x * ctx->IO.DisplayFramebufferScale.x );
                const I32 fb_height = to_I32( pDrawData->DisplaySize.y * ctx->IO.DisplayFramebufferScale.y );
                const Rect<I32> targetViewport{0, 0, fb_width, fb_height};

                Handle<GFX::CommandBuffer> buffer = GFX::AllocateCommandBuffer("IMGUI Render Window");

                GFX::CommandBuffer& buf = *GFX::Get( buffer );

                GFX::MemoryBarrierCommand memCmd;
                editor->renderDrawList(pDrawData,
                                       2 + targetWindow->getGUID(),
                                       targetViewport,
                                       true,
                                       buf,
                                       memCmd);
                GFX::EnqueueCommand( buf, memCmd );

                context->gfx().flushCommandBuffer( MOV(buffer) );
            }
        };

        platform_io.Platform_SwapBuffers = []( ImGuiViewport* viewport,
                                               void* platformContext )
        {
            if ( g_windowManager != nullptr )
            {
                PlatformContext* context = (PlatformContext*)platformContext;
                DIVIDE_ASSERT( context != nullptr );
                DisplayWindow* targetWindow = (DisplayWindow*)viewport->PlatformHandle;
                DIVIDE_ASSERT( targetWindow != nullptr  );

                context->app().windowManager().flushWindow();
            }
        };

        platform_io.Platform_OnChangedViewport = []( ImGuiViewport* viewport )
        {
            static F32 previousDPIScale = 1.f;
            if ( ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData )
            {
                if ( viewport->DpiScale != previousDPIScale )
                {
                    previousDPIScale = viewport->DpiScale;
                    data->_window->context().editor()._queuedDPIValue = previousDPIScale;
                }
            }
        };

        const I32 monitorCount = to_I32( monitors.size() );

        platform_io.Monitors.resize( monitorCount );

        for ( I32 i = 0; i < monitorCount; ++i )
        {
            const WindowManager::MonitorData& monitor = monitors[i];
            ImGuiPlatformMonitor& imguiMonitor = platform_io.Monitors[i];

            // Warning: the validity of monitor DPI information on Windows depends on
            // the application DPI awareness settings, which generally needs to be set
            // in the manifest or at runtime.
            imguiMonitor.MainPos = ImVec2( to_F32( monitor.viewport.x ), to_F32( monitor.viewport.y ) );
            imguiMonitor.WorkPos = ImVec2( to_F32( monitor.drawableArea.x ), to_F32( monitor.drawableArea.y ) );

            imguiMonitor.MainSize = ImVec2( to_F32( monitor.viewport.z ), to_F32( monitor.viewport.w ) );
            imguiMonitor.WorkSize = ImVec2( to_F32( monitor.drawableArea.z ), to_F32( monitor.drawableArea.w ) );
            imguiMonitor.DpiScale = monitor.dpi / PlatformDefaultDPI();
        }

        ImGuiViewportData* data = IM_NEW( ImGuiViewportData )();
        data->_window = _mainWindow;
        data->_windowOwned = false;
        editorContext->Viewports[0]->PlatformUserData = data;

        ImGuiContext*& gizmoContext = _imguiContexts[to_base( ImGuiContextType::Gizmo )];
        gizmoContext = ImGui::CreateContext( io.Fonts );
        initBasicImGUIState( gizmoContext->IO, false, _imguiStringBuffers[to_base(ImGuiContextType::Gizmo)]);
        gizmoContext->Viewports[0]->PlatformHandle = _mainWindow;

        _gizmo = std::make_unique<Gizmo>( *this, gizmoContext, 0.3f );

        SDL_SetHint( SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1" );

        DockedWindow::Descriptor descriptor = {};
        descriptor.position = ImVec2( 0, 0 );
        descriptor.size = ImVec2( 300, 550 );
        descriptor.minSize = ImVec2( 200, 200 );
        descriptor.name = ICON_FK_HUBZILLA " Solution Explorer";
        descriptor.showCornerButton = true;
        _dockedWindows[to_base( WindowType::SolutionExplorer )] = std::make_unique<SolutionExplorerWindow>( *this, _context, descriptor );

        descriptor.position = ImVec2( 0, 0 );
        descriptor.minSize = ImVec2( 200, 200 );
        descriptor.name = ICON_FK_PICTURE_O " PostFX Settings";
        _dockedWindows[to_base( WindowType::PostFX )] = std::make_unique<PostFXWindow>( *this, _context, descriptor );

        descriptor.showCornerButton = false;
        descriptor.position = ImVec2( to_F32( renderResolution.width ) - 300, 0 );
        descriptor.name = ICON_FK_PENCIL_SQUARE_O " Property Explorer";
        _dockedWindows[to_base( WindowType::Properties )] = std::make_unique<PropertyWindow>( *this, _context, descriptor );

        descriptor.position = ImVec2( 0, 550.0f );
        descriptor.size = ImVec2( to_F32( renderResolution.width * 0.5f ),
                                  to_F32( renderResolution.height ) - 550 - 3 );
        descriptor.name = ICON_FK_FOLDER_OPEN " Content Explorer";
        descriptor.flags |= ImGuiWindowFlags_NoTitleBar;
        _dockedWindows[to_base( WindowType::ContentExplorer )] = std::make_unique<ContentExplorerWindow>( *this, descriptor );

        descriptor.position = ImVec2( to_F32( renderResolution.width * 0.5f ), 550 );
        descriptor.size = ImVec2( to_F32( renderResolution.width * 0.5f ),
                                  to_F32( renderResolution.height ) - 550 - 3 );
        descriptor.name = ICON_FK_PRINT " Application Output";
        _dockedWindows[to_base( WindowType::Output )] = std::make_unique<OutputWindow>( *this, descriptor );

        descriptor.position = ImVec2( 150, 150 );
        descriptor.size = ImVec2( 640, 480 );
        descriptor.name = ICON_FK_EYE " Node Preview";
        descriptor.minSize = ImVec2( 100, 100 );
        descriptor.flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        _dockedWindows[to_base( WindowType::NodePreview )] = std::make_unique<NodePreviewWindow>( *this, descriptor );

        descriptor.name = "Scene View ###AnimatedTitlePlayState";
        _dockedWindows[to_base( WindowType::SceneView )] = std::make_unique<SceneViewWindow>( *this, descriptor );

        _editorSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
        _editorSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
        _editorSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
        _editorSampler._anisotropyLevel = 0u;

        TextureDescriptor editorDescriptor
        {
            ._mipMappingState = MipMappingState::OFF
        };

        RenderTargetDescriptor editorDesc = {};
        editorDesc._attachments.emplace_back(InternalRTAttachmentDescriptor
        {
            editorDescriptor, _editorSampler, RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO
        });

        editorDesc._resolution = renderResolution;
        editorDesc._name = "Node_Preview";

        TextureDescriptor depthDescriptor
        {
            ._dataType = GFXDataFormat::FLOAT_32,
            ._packing = GFXImagePacking::DEPTH,
            ._baseFormat = GFXImageFormat::RED,
            ._mipMappingState = MipMappingState::OFF
        };

        editorDesc._attachments.emplace_back(InternalRTAttachmentDescriptor
        {
            depthDescriptor, _editorSampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0
        });

        _nodePreviewRTHandle = _context.gfx().renderTargetPool().allocateRT( editorDesc );

        PushImGuiContext(editorContext);
        ImGui::ResetStyle(_currentTheme);

        return loadFromXML();
    }

    void Editor::close()
    {
        if ( saveToXML() )
        {
            _context.config().save();
        }
        if ( _infiniteGridPrimitive && !_context.gfx().destroyIMP( _infiniteGridPrimitive ) )
        {
            DebugBreak();
        }
        if ( _axisGizmo && !_context.gfx().destroyIMP( _axisGizmo ) )
        {
            DebugBreak();
        }
        DestroyResource(_infiniteGridProgram);
        DestroyResource(_fontTexture);
        DestroyResource(_imguiProgram);

        _gizmo.reset();
        _imguiBuffers.clear();

        for ( ImGuiContext* context : _imguiContexts )
        {
            if ( context == nullptr )
            {
                continue;
            }

            ImGui::SetCurrentContext( context );
            ImGui::DestroyPlatformWindows();
            ImGui::DestroyContext( context );
        }
        _imguiContexts.fill( nullptr );

        for ( auto& window : _dockedWindows)
        {
            window.reset();
        }

        if ( !_context.gfx().renderTargetPool().deallocateRT( _nodePreviewRTHandle ) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        Camera::DestroyCamera( _editorCamera );
        Camera::DestroyCamera( _nodePreviewCamera );
    }

    void Editor::updateEditorFocus()
    {
        const bool editorHasFocus = hasFocus();

        if ( editorHasFocus )
        {
            _imguiContexts[to_base(ImGuiContextType::Editor)]->IO.ConfigFlags &= ~ImGuiConfigFlags_NavNoCaptureKeyboard;
        }
        else
        {
            _imguiContexts[to_base(ImGuiContextType::Editor)]->IO.ConfigFlags |= ImGuiConfigFlags_NavNoCaptureKeyboard;
        }

        _context.kernel().projectManager()->onChangeFocus( !editorHasFocus );
        Attorney::GizmoEditor::onSceneFocus( _gizmo.get(), !editorHasFocus );
    }

    void Editor::toggle( const bool state )
    {
        if ( running() == state )
        {
            return;
        }
        if ( isInit() )
        {
            toggleInternal(state);
        }
        else
        {
            _queueToggle = state;
        }
    }

    void Editor::toggleInternal( const bool state )
    {
        DIVIDE_ASSERT(state != running());

        auto& sMgr = _context.kernel().projectManager();
        Scene* activeScene = sMgr->activeProject()->getActiveScene();

        Reset( _windowFocusState );
        updateEditorFocus();

        SceneStatePerPlayer& playerState = activeScene->state()->playerState( sMgr->playerPass() );
        if ( !state )
        {
            _context.config().save();
            playerState.overrideCamera( nullptr );
            sceneGizmoEnabled( false );
            activeScene->state()->renderState().disableOption( SceneRenderState::RenderOptions::SELECTION_GIZMO );
            activeScene->state()->renderState().disableOption( SceneRenderState::RenderOptions::ALL_GIZMOS );
            if ( !_context.kernel().projectManager()->resetSelection( 0, true ) )
            {
                NOP();
            }
        }
        else
        {
            _stepQueue = 0;
            playerState.overrideCamera( editorCamera() );
            sceneGizmoEnabled( true );
            activeScene->state()->renderState().enableOption( SceneRenderState::RenderOptions::SELECTION_GIZMO );
            static_cast<ContentExplorerWindow*>( _dockedWindows[to_base( WindowType::ContentExplorer )].get() )->init();
            /*const Selections& selections = activeScene->getCurrentSelection();
            if (selections._selectionCount == 0)
            {
                SceneGraphNode* root = activeScene->sceneGraph().getRoot();
                _context.kernel().projectManager()->setSelected(0, { &root });
            }*/
      
            if ( !_axisGizmo )
            {
                _axisGizmo = _context.gfx().newIMP( "Editor Scene Axis Gizmo" );
                _axisGizmo->setPipelineDescriptor( _axisGizmoPipelineDesc );

                const auto addValAnd10Percent = []( const F32 val )
                {
                    return val + ((val * 10) / 100.f);
                };
                const auto addValMinus20Percent = []( const F32 val )
                {
                    return val - ((val * 20) / 100.f);
                };

                std::array<IM::ConeDescriptor, 6> descriptors;
                for ( IM::ConeDescriptor& descriptor : descriptors )
                {
                    descriptor.slices = 4u;
                    descriptor.noCull = true;
                }

                // Shafts
                descriptors[0].direction = WORLD_X_NEG_AXIS;
                descriptors[1].direction = WORLD_Y_NEG_AXIS;
                descriptors[2].direction = WORLD_Z_NEG_AXIS;

                descriptors[0].length = 2.0f;
                descriptors[1].length = 1.5f;
                descriptors[2].length = 2.0f;

                descriptors[0].root = VECTOR3_ZERO + float3( addValAnd10Percent( descriptors[0].length ), 0.f, 0.f );
                descriptors[1].root = VECTOR3_ZERO + float3( 0.f, addValAnd10Percent( descriptors[1].length ), 0.f );
                descriptors[2].root = VECTOR3_ZERO + float3( 0.f, 0.f, addValAnd10Percent( descriptors[2].length ) );

                descriptors[0].radius = 0.05f;
                descriptors[1].radius = 0.05f;
                descriptors[2].radius = 0.05f;

                descriptors[0].colour = UColour4( 255, 0, 0, 255 );
                descriptors[1].colour = UColour4( 0, 255, 0, 255 );
                descriptors[2].colour = UColour4( 0, 0, 255, 255 );

                // Arrow heads
                descriptors[3].direction = WORLD_X_NEG_AXIS;
                descriptors[4].direction = WORLD_Y_NEG_AXIS;
                descriptors[5].direction = WORLD_Z_NEG_AXIS;

                descriptors[3].length = 0.5f;
                descriptors[4].length = 0.5f;
                descriptors[5].length = 0.5f;

                descriptors[3].root = VECTOR3_ZERO + float3( addValMinus20Percent( descriptors[0].length ) + 0.50f, 0.f, 0.f );
                descriptors[4].root = VECTOR3_ZERO + float3( 0.f, addValMinus20Percent( descriptors[1].length ) + 0.50f, 0.f );
                descriptors[5].root = VECTOR3_ZERO + float3( 0.f, 0.f, addValMinus20Percent( descriptors[2].length ) + 0.50f );

                descriptors[3].radius = 0.15f;
                descriptors[4].radius = 0.15f;
                descriptors[5].radius = 0.15f;

                descriptors[3].colour = UColour4( 255, 0, 0, 255 );
                descriptors[4].colour = UColour4( 0, 255, 0, 255 );
                descriptors[5].colour = UColour4( 0, 0, 255, 255 );

                _axisGizmo->fromCones( descriptors );
            }
        }

        running(state);
        processInput(state);
    }

    void Editor::update( const U64 deltaTimeUS )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        static bool allGizmosEnabled = false;

        Time::ScopedTimer timer( _editorUpdateTimer );

        for ( ImGuiContext* context : _imguiContexts )
        {
            context->IO.DeltaTime = Time::MicrosecondsToSeconds<F32>( deltaTimeUS );

            ToggleCursor( !context->IO.MouseDrawCursor );

            if (context->IO.MouseDrawCursor || context->MouseCursor == ImGuiMouseCursor_None )
            {
                WindowManager::SetCursorStyle( CursorStyle::NONE );
            }
            else if ( !COMPARE(context->IO.MousePos.x, -1.f ) && !COMPARE(context->IO.MousePos.y, -1.f ) )
            {
                switch (context->MouseCursor )
                {
                    default:
                    case ImGuiMouseCursor_Arrow:
                        WindowManager::SetCursorStyle( CursorStyle::ARROW );
                        break;
                    case ImGuiMouseCursor_TextInput: // When hovering over InputText, etc.
                        WindowManager::SetCursorStyle( CursorStyle::TEXT_INPUT );
                        break;
                    case ImGuiMouseCursor_ResizeAll: // Unused
                        WindowManager::SetCursorStyle( CursorStyle::RESIZE_ALL );
                        break;
                    case ImGuiMouseCursor_ResizeNS: // Unused
                        WindowManager::SetCursorStyle( CursorStyle::RESIZE_NS );
                        break;
                    case ImGuiMouseCursor_ResizeEW: // When hovering over a column
                        WindowManager::SetCursorStyle( CursorStyle::RESIZE_EW );
                        break;
                    case ImGuiMouseCursor_ResizeNESW: // Unused
                        WindowManager::SetCursorStyle( CursorStyle::RESIZE_NESW );
                        break;
                    case ImGuiMouseCursor_ResizeNWSE: // When hovering over the
                        // bottom-right corner of a window
                        WindowManager::SetCursorStyle( CursorStyle::RESIZE_NWSE );
                        break;
                    case ImGuiMouseCursor_Hand:
                        WindowManager::SetCursorStyle( CursorStyle::HAND );
                        break;
                }
            }
        }

        nodePreviewWindowVisible(false);

        Attorney::GizmoEditor::update( _gizmo.get(), deltaTimeUS );

        if ( running() )
        {
            nodePreviewWindowVisible( _dockedWindows[to_base( WindowType::NodePreview )]->visible() );

            _statusBar->update( deltaTimeUS );
            _optionsWindow->update( deltaTimeUS );

            static_cast<ContentExplorerWindow*>(_dockedWindows[to_base( WindowType::ContentExplorer )].get())->update( deltaTimeUS );

            auto& sMgr = _context.kernel().projectManager();
            const bool scenePaused = (simulationPaused() && _stepQueue == 0);

            Scene* activeScene = sMgr->activeProject()->getActiveScene();
            const PlayerIndex idx = sMgr->playerPass();
            SceneStatePerPlayer& playerState = activeScene->state()->playerState( idx );

            if ( _isScenePaused != scenePaused )
            {
                _isScenePaused = scenePaused;

                _gizmo->enable( _isScenePaused );
                // ToDo: Find a way to keep current selection between running and editing
                // states. Maybe have 2 different selections flags?(i.e. in-editor and
                // in-game) - Ionut
                if ( !sMgr->resetSelection( 0, true ) )
                {
                    NOP();
                }
                if ( _isScenePaused )
                {
                    activeScene->state()->renderState().enableOption( SceneRenderState::RenderOptions::SELECTION_GIZMO );
                    if ( allGizmosEnabled )
                    {
                        activeScene->state()->renderState().enableOption( SceneRenderState::RenderOptions::ALL_GIZMOS );
                    }
                    sceneGizmoEnabled( true );
                }
                else
                {
                    allGizmosEnabled = activeScene->state()->renderState().isEnabledOption( SceneRenderState::RenderOptions::ALL_GIZMOS );
                    activeScene->state()->renderState().disableOption( SceneRenderState::RenderOptions::SELECTION_GIZMO );
                    activeScene->state()->renderState().disableOption( SceneRenderState::RenderOptions::ALL_GIZMOS );
                    sceneGizmoEnabled( false );
                }
            }

            static bool movedToNode = false;
            if ( !_isScenePaused || stepQueue() > 0 )
            {
                Attorney::ProjectManagerEditor::editorPreviewNode( sMgr.get(), -1 );
                playerState.overrideCamera( nullptr );
                nodePreviewCamera()->setTarget( nullptr );
                movedToNode = false;
            }
            else
            {
                if ( nodePreviewWindowVisible() )
                {
                    playerState.overrideCamera( nodePreviewCamera() );
                    Attorney::ProjectManagerEditor::editorPreviewNode( sMgr.get(), _previewNode == nullptr ? 0 : _previewNode->getGUID() );
                    if ( !movedToNode && _previewNode != nullptr )
                    {
                        teleportToNode( nodePreviewCamera(), _previewNode );
                        nodePreviewCamera()->setTarget( _previewNode->get<TransformComponent>() );
                        const F32 radius = SceneGraph::GetBounds( _previewNode )._sphere.radius;
                        nodePreviewCamera()->minRadius( radius * 0.75f );
                        nodePreviewCamera()->maxRadius( radius * 10.f );
                        nodePreviewCamera()->curRadius( radius );
                        movedToNode = true;
                    }
                }
                else
                {
                    playerState.overrideCamera( editorCamera() );
                    Attorney::ProjectManagerEditor::editorPreviewNode( sMgr.get(), -1 );
                    nodePreviewCamera()->setTarget( nullptr );
                    movedToNode = false;
                }
            }

            if ( _gridSettingsDirty )
            {
                PushConstantsStruct fastData{};
                fastData.data[0]._vec[0].xy.set( infiniteGridAxisWidth(), infiniteGridScale() );
                _infiniteGridPrimitive->setPushConstants( fastData );
                _gridSettingsDirty = false;
            }
        }
    }

    bool Editor::render()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos( viewport->WorkPos );
        ImGui::SetNextWindowSize( viewport->WorkSize );
        ImGui::SetNextWindowViewport( viewport->ID );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );

        constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin( "Editor", nullptr, windowFlags );
        ImGui::PopStyleVar( 3 );

        ImGuiStyle& style = ImGui::GetStyle();
        const F32 originalSize = style.WindowMinSize.x;
        style.WindowMinSize.x = 300.f;
        const ImGuiID dockspace_id = ImGui::GetID( "EditorDockspace" );
        ImGui::DockSpace( dockspace_id, ImVec2( 0.0f, 0.0f ), ImGuiDockNodeFlags_PassthruCentralNode );
        style.WindowMinSize.x = originalSize;

        const bool readOnly = Focused( _windowFocusState ) || _showOptionsWindow;

        _menuBar->draw( readOnly );
        for ( auto& window : _dockedWindows )
        {
            window->draw( readOnly );
        }
        _statusBar->draw( readOnly );

        if (readOnly)
        {
            PushReadOnly(true);
        }

        if ( _showMemoryEditor && !_showOptionsWindow )
        {
            if ( _memoryEditorData.first != nullptr && _memoryEditorData.second > 0 )
            {

                static MemoryEditor memEditor;
                memEditor.DrawWindow( "Memory Editor", _memoryEditorData.first, _memoryEditorData.second );

                if ( !memEditor.Open )
                {
                    _memoryEditorData = { nullptr, 0 };
                }
            }
        }

        if ( _showSampleWindow && !_showOptionsWindow )
        {
            ImGui::SetNextWindowPos( ImVec2( 650, 20 ), ImGuiCond_FirstUseEver );
            ImGui::ShowDemoWindow( &_showSampleWindow );
        }

        _optionsWindow->draw( _showOptionsWindow );
        renderModelSpawnModal();

        if (readOnly)
        {
            PopReadOnly();
        }

        ImGui::End();

        return true;
    }
    void Editor::infiniteGridAxisWidth( const F32 value ) noexcept
    {
        _infiniteGridAxisWidth = value;
        _gridSettingsDirty = true;
    }

    void Editor::infiniteGridScale( const F32 value ) noexcept
    {
        _infiniteGridScale = value;
        _gridSettingsDirty = true;
    }

    bool Editor::isNodeInView( const SceneGraphNode& node ) const noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        const I64 targetGUID = node.getGUID();

        const auto& visibleNodes = _context.kernel().projectManager()->getRenderedNodeList();
        const size_t nodeCount = visibleNodes.size();
        for ( size_t i = 0u; i < nodeCount; ++i )
        {
            if ( visibleNodes.node( i )._node->getGUID() == targetGUID )
            {
                return true;
            }
        }

        return false;
    }

    void Editor::postRender( const RenderStage stage,
                             const CameraSnapshot& cameraSnapshot,
                             const RenderTargetID target,
                             GFX::CommandBuffer& bufferInOut,
                             GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        const bool infiniteGridEnabled = stage == RenderStage::NODE_PREVIEW
                                                ? infiniteGridEnabledNode()
                                                : infiniteGridEnabledScene();

        if ( !sceneGizmoEnabled() && !infiniteGridEnabled )
        {
            return;
        }

        if ( running() && infiniteGridEnabled && _infiniteGridPrimitive && _isScenePaused )
        {
            _infiniteGridPrimitive->getCommandBuffer( bufferInOut, memCmdInOut );
        }

        // Debug axis form the axis arrow gizmo in the corner of the screen
        // This is toggleable, so check if it's actually requested
        if ( sceneGizmoEnabled() && _axisGizmo )
        {
            // Apply the inverse view matrix so that it cancels out in the shader
            // Submit the draw command, rendering it in a tiny viewport in the lower right corner
            const auto& rt = _context.gfx().renderTargetPool().getRenderTarget( target );
            const U16 windowWidth = rt->getWidth();
            //const U16 windowHeight = rt->getHeight();

            // We need to transform the gizmo so that it always remains axis aligned
            // Create a world matrix using a look at function with the eye position
            // backed up from the camera's view direction
            const mat4<F32>& viewMatrix = cameraSnapshot._viewMatrix;
            const mat4<F32> worldMatrix( Camera::LookAt( -viewMatrix.getForwardVec() * 5, VECTOR3_ZERO, viewMatrix.getUpVec() ) * cameraSnapshot._invViewMatrix );

            constexpr I32 viewportDim = 256;
            constexpr I32 viewportPadding = 6;

            GFX::EnqueueCommand<GFX::SetViewportCommand>( bufferInOut)->_viewport =
            {
                windowWidth - (viewportDim - viewportPadding),
                viewportPadding,
                viewportDim,
                viewportDim
            };

            _axisGizmo->getCommandBuffer( worldMatrix, bufferInOut, memCmdInOut );
        }
    }

    void Editor::drawScreenOverlay( const Camera* camera, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );
        Attorney::GizmoEditor::render( _gizmo.get(), camera, targetViewport, bufferInOut, memCmdInOut );
    }

    bool Editor::framePostRender( [[maybe_unused]] const FrameEvent& evt )
    {
        return true;
    }

    void Editor::getCommandBuffer( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        for ( auto& window : _dockedWindows )
        {
            window->backgroundUpdate();
        }

        if ( !running() )
        {
            return;
        }

        Time::ScopedTimer timer( _editorRenderTimer );
        ImGuiContext* ctx = _imguiContexts[to_base( ImGuiContextType::Editor )];

        if (ctx->IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) [[likely]]
        {
            bool found = false;
            for ( I32 n = 0; n < ctx->PlatformIO.Viewports.Size; n++ )
            {
                const ImGuiViewport* viewport = ctx->PlatformIO.Viewports[n];
                const DisplayWindow* window = static_cast<DisplayWindow*>(viewport->PlatformHandle);
                if ( window != nullptr && window->isHovered() && !(viewport->Flags & ImGuiViewportFlags_NoInputs) )
                {
                    ctx->IO.AddMouseViewportEvent( viewport->ID );
                    found = true;
                }
            }
            if ( !found )
            {
                ctx->IO.AddMouseViewportEvent( 0 );
            }
        }

        if ( _queuedDPIValue >= 0.f )
        {
            createFontTexture(ctx->IO, _queuedDPIValue );
            ctx->Style.ScaleAllSizes(_queuedDPIValue);
            _queuedDPIValue = -1.f;

        }

        ScopedImGuiContext ctxSwitch(ctx);
        ImGui::NewFrame();

        if ( render() ) [[likely]]
        {
            ImGui::Render();

            ImDrawData* pDrawData = ImGui::GetDrawData();
            const I32 fb_width = to_I32( pDrawData->DisplaySize.x * ctx->IO.DisplayFramebufferScale.x );
            const I32 fb_height = to_I32( pDrawData->DisplaySize.y * ctx->IO.DisplayFramebufferScale.y );
            const Rect<I32> targetViewport{0, 0, fb_width, fb_height};

            pDrawData->ScaleClipRects( ctx->IO.DisplayFramebufferScale );

            renderDrawList( pDrawData,
                            0,
                            targetViewport,
                            true,
                            bufferInOut,
                            memCmdInOut );
            
            if (ctx->IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) [[likely]]
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault( &context(), &context() );
            }
        }
    }

    bool Editor::frameEnded( [[maybe_unused]] const FrameEvent& evt ) noexcept
    {
        if (!_queuedModelSpawns.empty())
        {
            bool spawn = false;
            Scene* activeScene = _context.kernel().projectManager()->activeProject()->getActiveScene();
            SceneGraphNode& rootNode = *activeScene->sceneGraph()->getRoot();

            do
            {
                const QueueModelSpawn model = _queuedModelSpawns.front();
                if ( !model._showModal)
                {
                    _queuedModelSpawns.pop();
                    spawn = spawnGeometry(rootNode, model);
                }


            } while(spawn && !_queuedModelSpawns.empty());
        }

        if ( running() && _stepQueue > 0 )
        {
            --_stepQueue;
        }

        return true;
    }

    Rect<I32> Editor::scenePreviewRect( const bool globalCoords ) const noexcept
    {
        if ( !isInit() )
        {
            return {};
        }

        const WindowType type = _windowFocusState._focusedNodePreview ? WindowType::NodePreview : WindowType::SceneView;
        const NodePreviewWindow* viewWindow = static_cast<NodePreviewWindow*>(_dockedWindows[to_base( type )].get());
        return viewWindow->sceneRect( globalCoords );
    }

    GenericVertexData* Editor::getOrCreateIMGUIBuffer( const I64 bufferGUID, const U32 maxVertices, const U32 maxIndices, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        for (const auto&[id, ptr] : _imguiBuffers)
        {
            if (id == bufferGUID)
            {
                GenericVertexData* buffer = ptr.get();
                buffer->incQueue();
                return buffer;
            }
        }

        auto& newBuffer = _imguiBuffers.emplace_back(std::make_pair(bufferGUID, nullptr)).second;

        newBuffer = _context.gfx().newGVD( Config::MAX_FRAMES_IN_FLIGHT + 1u, Util::StringFormat("IMGUI_{}", bufferGUID).c_str() );

        GenericVertexData::SetBufferParams params = {};
        params._bindConfig = { 0u, 0u };
        params._useRingBuffer = true;
        params._initialData = { nullptr, 0 };

        params._bufferParams._elementCount = maxVertices;
        params._bufferParams._elementSize = sizeof( ImDrawVert );
        params._bufferParams._updateFrequency = BufferUpdateFrequency::OFTEN;

        memCmdInOut._bufferLocks.push_back( newBuffer->setBuffer( params )); //Pos, UV and Colour

        GenericVertexData::IndexBuffer idxBuff{};
        idxBuff.smallIndices = sizeof(ImDrawIdx) == sizeof(U16);
        idxBuff.dynamic = true;
        idxBuff.count = maxIndices;
        idxBuff.useRingBuffer = true;
        memCmdInOut._bufferLocks.push_back( newBuffer->setIndexBuffer( idxBuff ));

        return newBuffer.get();
    }

    // Needs to be rendered immediately. *IM*GUI. IMGUI::NewFrame invalidates this data
    void Editor::renderDrawList( ImDrawData* pDrawData,
                                 I64 bufferGUID,
                                 const Rect<I32>& targetViewport,
                                 const bool editorPass,
                                 GFX::CommandBuffer& bufferInOut,
                                 GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        constexpr U32 MaxVertices = (1 << 16);
        constexpr U32 MaxIndices = MaxVertices * 3u;
        static ImDrawVert vertices[MaxVertices];
        static ImDrawIdx indices[MaxIndices];

        DIVIDE_ASSERT( bufferGUID != -1 );

        const I32 fb_width  = targetViewport.sizeX;
        const I32 fb_height = targetViewport.sizeY;

        // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
        if ( pDrawData->CmdListsCount == 0  || fb_width <= 0 || fb_height <= 0 )
        {
            return;
        }

        // ref: https://gist.github.com/floooh/10388a0afbe08fce9e617d8aefa7d302
        U32 numVertices = 0, numIndices = 0;
        for ( I32 n = 0; n < pDrawData->CmdListsCount; ++n )
        {
            const ImDrawList* cl = pDrawData->CmdLists[n];
            const U32 clNumVertices = to_U32(cl->VtxBuffer.size());
            const U32 clNumIndices = to_U32(cl->IdxBuffer.size());

            if ( (numVertices + clNumVertices) > MaxVertices || (numIndices + clNumIndices) > MaxIndices )
            {
                break;
            }

            memcpy( &vertices[numVertices], cl->VtxBuffer.Data, clNumVertices * sizeof( ImDrawVert ) );
            memcpy( &indices[numIndices],   cl->IdxBuffer.Data, clNumIndices  * sizeof( ImDrawIdx )  );

            numVertices += clNumVertices;
            numIndices += clNumIndices;
        }

        GenericVertexData* buffer = getOrCreateIMGUIBuffer( bufferGUID, MaxVertices, MaxIndices, memCmdInOut);
        DIVIDE_ASSERT( buffer != nullptr );

        memCmdInOut._bufferLocks.emplace_back(buffer->updateBuffer( 0u, 0u, numVertices, vertices ));
        memCmdInOut._bufferLocks.emplace_back(buffer->updateIndexBuffer( 0u, numIndices, indices ));

        if ( editorPass )
        {
            const ImVec4 windowBGColour = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];

            auto beginRenderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut );
            beginRenderPassCmd->_target = SCREEN_TARGET_ID;
            beginRenderPassCmd->_name = "Render IMGUI [ External ]";
            beginRenderPassCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = {{windowBGColour.x, windowBGColour.y, windowBGColour.z, 1.f}, true};
            beginRenderPassCmd->_descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;
        }
        else
        {
            auto scopeCmd = GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut );
            scopeCmd->_scopeName = "Render IMGUI [ Internal ]";
            scopeCmd->_scopeId = numVertices;
        }

        GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut)->_pipeline = _editorPipeline;

        static IMGUICallbackData defaultData{
            ._flip = false
        };

        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData = IMGUICallbackToPushConstants( defaultData, false );
        GFX::EnqueueCommand<GFX::SetViewportCommand>( bufferInOut)->_viewport = targetViewport;

        const F32 scale[] = {
            2.f / pDrawData->DisplaySize.x,
            2.f / pDrawData->DisplaySize.y
        };

        const F32 translate[] = {
            -1.f - pDrawData->DisplayPos.x * scale[0],
             1.f + pDrawData->DisplayPos.y * scale[1]
        };
        
        CameraSnapshot& snapshot = GFX::EnqueueCommand<GFX::SetCameraCommand>( bufferInOut)->_cameraSnapshot;
        snapshot = _render2DSnapshot;

        mat4<F32>& projection = snapshot._projectionMatrix;
        projection.m[0][0] = scale[0];
        projection.m[1][1] = -scale[1];
        projection.m[2][2] = -1.f;
        projection.m[3][0] = translate[0];
        projection.m[3][1] = translate[1];

        const ImVec2 clip_off = pDrawData->DisplayPos;         // (0,0) unless using multi-viewports
        const ImVec2 clip_scale = pDrawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

        const bool flipClipY = _context.gfx().renderAPI() == RenderAPI::OpenGL;

        ImTextureID crtImguiTexID = nullptr;

        U32 baseVertex = 0u;
        U32 indexOffset = 0u;

        GFX::DrawCommand* drawCommand = nullptr;
        Rect<I32> prevClipRect{-1};

        bool newCommand = true;
        for ( I32 n = 0; n < pDrawData->CmdListsCount; ++n )
        {
            const ImDrawList* cmd_list = pDrawData->CmdLists[n];
            for ( const ImDrawCmd& pcmd : cmd_list->CmdBuffer )
            {
                if ( pcmd.UserCallback )
                {
                    static_cast<IMGUICallbackData*>(pcmd.UserCallbackData)->_cmdBuffer = &bufferInOut;
                    pcmd.UserCallback( cmd_list, &pcmd );
                    newCommand = true;
                }
                else
                {
                    ImVec2 clip_min( (pcmd.ClipRect.x - clip_off.x)* clip_scale.x, (pcmd.ClipRect.y - clip_off.y)* clip_scale.y );
                    ImVec2 clip_max( (pcmd.ClipRect.z - clip_off.x)* clip_scale.x, (pcmd.ClipRect.w - clip_off.y)* clip_scale.y );

                    // Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
                    if (clip_min.x < 0.f) { clip_min.x = 0.f; }
                    if (clip_min.y < 0.f) { clip_min.y = 0.f; }
                    if (clip_max.x > fb_width) { clip_max.x = to_F32(fb_width); }
                    if (clip_max.y > fb_height) { clip_max.y = to_F32(fb_height); }
                    if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    {
                        continue;
                    }

                    Rect<I32> clipRect;
                    clipRect.sizeX = to_I32( clip_max.x - clip_min.x );
                    clipRect.sizeY = to_I32( clip_max.y - clip_min.y );
                    clipRect.offsetX = to_I32( clip_min.x );
                    clipRect.offsetY = flipClipY ? to_I32( fb_height - clip_max.y ) : to_I32( clip_min.y );

                    if ( prevClipRect != clipRect )
                    {
                        prevClipRect = clipRect;
                        GFX::EnqueueCommand<GFX::SetScissorCommand>( bufferInOut )->_rect = clipRect;
                        newCommand = true;
                    }
                    else
                    {
                        NOP();
                    }

                    ImTextureID imguiTexID = pcmd.GetTexID();

                    if ( imguiTexID != crtImguiTexID )
                    {
                        const Handle<Texture> tex = from_TexID( imguiTexID );

                        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
                        cmd->_usage = DescriptorSetUsage::PER_DRAW;

                        {
                            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
                            Set(binding._data, (tex == INVALID_HANDLE<Texture> ? Texture::DefaultTexture2D() : tex), _editorSampler );
                        }
                        {
                            DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
                            Set(binding._data, Texture::DefaultTexture2DArray(), Texture::DefaultSampler());
                        }
                        crtImguiTexID = imguiTexID;
                        newCommand = true;
                    }

                    if ( newCommand )
                    {
                        drawCommand = GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut );
                        newCommand = false;
                    }

                    auto drawCmd = &drawCommand->_drawCommands.emplace_back();
                    drawCmd->_sourceBuffer = buffer->handle();
                    drawCmd->_cmd.indexCount = pcmd.ElemCount;
                    drawCmd->_cmd.firstIndex = indexOffset + pcmd.IdxOffset;
                    drawCmd->_cmd.baseVertex = baseVertex + pcmd.VtxOffset;
                    
                }
            }

            indexOffset += cmd_list->IdxBuffer.size();
            baseVertex += cmd_list->VtxBuffer.size();
        }

        if ( editorPass )
        {
            GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut );
        }
        else
        {
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
        }
    }

    void Editor::selectionChangeCallback( const PlayerIndex idx, const vector<SceneGraphNode*>& nodes ) const
    {
        if ( idx != 0 )
        {
            return;
        }

        Attorney::GizmoEditor::updateSelections( _gizmo.get(), nodes );
    }

    void Editor::copyPlayerCamToEditorCam() noexcept
    {
        _editorCamera->fromCamera( *Attorney::ProjectManagerEditor::playerCamera( _context.kernel().projectManager().get(), 0, true ) );
    }

    void Editor::setEditorCamLookAt( const float3& eye,
                                     const float3& fwd,
                                     const float3& up )
    {
        _editorCamera->lookAt( eye, fwd, up );
    }

    void Editor::setEditorCameraSpeed( const float3& speed ) noexcept
    {
        _editorCamera->speedFactor( speed );
    }

    bool Editor::Undo() const
    {
        if ( _undoManager->Undo() )
        {
            showStatusMessage(
                Util::StringFormat( "Undo: {}", _undoManager->lasActionName().c_str() ),
                Time::SecondsToMilliseconds<F32>( 2.0f ),
                false );
            return true;
        }

        showStatusMessage( "Nothing to Undo", Time::SecondsToMilliseconds<F32>( 2.0f ), true );
        return false;
    }

    bool Editor::Redo() const
    {
        if ( _undoManager->Redo() )
        {
            showStatusMessage(
                Util::StringFormat( "Redo: {}", _undoManager->lasActionName().c_str() ),
                Time::SecondsToMilliseconds<F32>( 2.0f ),
                false );
            return true;
        }

        showStatusMessage( "Nothing to Redo", Time::SecondsToMilliseconds<F32>( 2.0f ), true );
        return false;
    }

    /// Key pressed: return true if input was consumed
    bool Editor::onKeyDownInternal( Input::KeyEvent& argInOut )
    {
        if ( !hasFocus() || !simulationPaused() )
        {
            return false;
        }

        if ( _gizmo->onKeyDown(argInOut) )
        {
            return true;
        }

        ImGuiIO& io = _imguiContexts[to_base( ImGuiContextType::Editor )]->IO;

        if ( argInOut._key == Input::KeyCode::KC_LCONTROL || argInOut._key == Input::KeyCode::KC_RCONTROL )
        {
            io.AddKeyEvent( ImGuiMod_Ctrl, true );
        }
        if ( argInOut._key == Input::KeyCode::KC_LSHIFT || argInOut._key == Input::KeyCode::KC_RSHIFT )
        {
            io.AddKeyEvent( ImGuiMod_Shift, true );
        }
        if ( argInOut._key == Input::KeyCode::KC_LMENU || argInOut._key == Input::KeyCode::KC_RMENU )
        {
            io.AddKeyEvent( ImGuiMod_Alt, true );
        }
        if ( argInOut._key == Input::KeyCode::KC_LWIN || argInOut._key == Input::KeyCode::KC_RWIN )
        {
            io.AddKeyEvent( ImGuiMod_Super, true );
        }
        const ImGuiKey imguiKey = DivideKeyToImGuiKey( argInOut._key );
        io.AddKeyEvent( imguiKey, true );
        io.SetKeyEventNativeData( imguiKey, argInOut.sym, argInOut.scancode, argInOut.scancode );

        return wantsKeyboard();
    }

    // Key released: return true if input was consumed
    bool Editor::onKeyUpInternal( Input::KeyEvent& argInOut )
    {
        if ( !hasFocus() || !simulationPaused() )
        {
            return false;
        }

        if ( _gizmo->onKeyUp(argInOut ) )
        {
            return true;
        }

        ImGuiIO& io = _imguiContexts[to_base( ImGuiContextType::Editor )]->IO;

        bool ret = false;
        if ( io.KeyCtrl )
        {
            if ( argInOut._key == Input::KeyCode::KC_Z )
            {
                if ( Undo() )
                {
                    ret = true;
                }
            }
            else if ( argInOut._key == Input::KeyCode::KC_Y )
            {
                if ( Redo() )
                {
                    ret = true;
                }
            }
        }

        if ( argInOut._key == Input::KeyCode::KC_LCONTROL || argInOut._key == Input::KeyCode::KC_RCONTROL )
        {
            io.AddKeyEvent( ImGuiMod_Ctrl, false );
        }
        if ( argInOut._key == Input::KeyCode::KC_LSHIFT || argInOut._key == Input::KeyCode::KC_RSHIFT )
        {
            io.AddKeyEvent( ImGuiMod_Shift, false );
        }
        if ( argInOut._key == Input::KeyCode::KC_LMENU || argInOut._key == Input::KeyCode::KC_RMENU )
        {
            io.AddKeyEvent( ImGuiMod_Alt, false );
        }
        if ( argInOut._key == Input::KeyCode::KC_LWIN || argInOut._key == Input::KeyCode::KC_RWIN )
        {
            io.AddKeyEvent( ImGuiMod_Super, false );
        }
        const ImGuiKey imguiKey = DivideKeyToImGuiKey( argInOut._key );
        io.AddKeyEvent( imguiKey, false );
        io.SetKeyEventNativeData( imguiKey, argInOut.sym, argInOut.scancode, argInOut.scancode );

        return wantsKeyboard() || ret;
    }

    ImGuiViewport* Editor::FindViewportByPlatformHandle( ImGuiContext* context, const DisplayWindow* window )
    {
        if ( window != nullptr )
        {
            for ( I32 i = 0; i != context->Viewports.Size; i++ )
            {
                const DisplayWindow* it = static_cast<DisplayWindow*>(context->Viewports[i]->PlatformHandleRaw);

                if ( it != nullptr && it->getGUID() == window->getGUID() )
                {
                    return context->Viewports[i];
                }
            }
        }

        return nullptr;
    }

    void Editor::updateFocusState( const ImVec2 mousePos )
    {
        DisplayWindow* focusedWindow = g_windowManager->getFocusedWindow();
        if ( focusedWindow == nullptr )
        {
            focusedWindow = g_windowManager->mainWindow();
            assert( focusedWindow != nullptr );
        }

        Rect<I32> viewportSize( -1 );

        ImGuiViewport* viewport = FindViewportByPlatformHandle( _imguiContexts[to_base( ImGuiContextType::Editor )], focusedWindow );
        if ( viewport == nullptr )
        {
            viewportSize = 
            {
                0u,
                0u,
                focusedWindow->getDrawableSize().x,
                focusedWindow->getDrawableSize().y
            };
        }
        else
        {
            viewportSize =
            {
                viewport->Pos.x,
                viewport->Pos.y,
                viewport->Size.x,
                viewport->Size.y
            };
        }

        const SceneViewWindow* sceneView = static_cast<SceneViewWindow*>(_dockedWindows[to_base( WindowType::SceneView )].get());
        _windowFocusState._hoveredScenePreview = sceneView->hovered() && sceneView->sceneRect( true ).contains( mousePos.x, mousePos.y );

        const NodePreviewWindow* nodeView = static_cast<SceneViewWindow*>(_dockedWindows[to_base( WindowType::NodePreview )].get());
        _windowFocusState._hoveredNodePreview = nodeView->hovered() && nodeView->sceneRect( true ).contains( mousePos.x, mousePos.y );

        _windowFocusState._globalMousePos = mousePos;

        const float2 tempMousePos = COORD_REMAP( int2( mousePos.x, mousePos.y ),
                                                    scenePreviewRect( true ),
                                                    Rect<I32>( 0, 0, viewportSize.z, viewportSize.w ) );
        _windowFocusState._scaledMousePos = ImVec2( tempMousePos.x, tempMousePos.y );
    }

    void Editor::remapAbsolutePosition(Input::MouseEvent& eventInOut) const noexcept
    {
        int2 absPositionIn = { eventInOut.state().X.abs, eventInOut.state().Y.abs };

        const Rect<I32> renderingViewport = _mainWindow->renderingViewport();
        CLAMP_IN_RECT(absPositionIn.x, absPositionIn.y, renderingViewport);

        if (!hasFocus())
        {
            const Rect<I32> previewRect = scenePreviewRect(false);
            absPositionIn = COORD_REMAP(absPositionIn, previewRect, renderingViewport);
            if (!previewRect.contains(absPositionIn))
            {
                CLAMP_IN_RECT(absPositionIn.x, absPositionIn.y, renderingViewport);
            }
        }

        const vec2<U16> resolution = _context.gfx().renderingResolution();
        absPositionIn = COORD_REMAP(absPositionIn, renderingViewport, { 0, 0, to_I32(resolution.width), to_I32(resolution.height) });

        Input::Attorney::MouseEventConsumer::setAbsolutePosition(eventInOut, absPositionIn);
        eventInOut._simulationPaused = simulationPaused();
    }

    bool Editor::mouseMoved(Input::MouseMoveEvent& argInOut)
    {
        if (!InputAggregatorInterface::mouseMoved(argInOut))
        {
            if (_mouseCaptured)
            {
                WindowManager::SetCaptureMouse(false);
                _mouseCaptured = false;
            }

            remapAbsolutePosition(argInOut);
            return false;
        }

        if (!argInOut._wheelEvent && _mouseCaptured)
        {
            WindowManager::SetCaptureMouse(ImGui::IsAnyMouseDown());
        }

        return true;
    }

    bool Editor::mouseButtonPressed(Input::MouseButtonEvent& argInOut)
    {
        if (!InputAggregatorInterface::mouseButtonPressed(argInOut))
        {
            remapAbsolutePosition(argInOut);
            return false;
        }

        return true;
    }

    bool Editor::mouseButtonReleased(Input::MouseButtonEvent& argInOut)
    {
        if (!InputAggregatorInterface::mouseButtonReleased(argInOut))
        {
            remapAbsolutePosition(argInOut);
            return false;
        }

        return true;
    }

    /// Mouse moved: return true if input was consumed
    bool Editor::mouseMovedInternal( Input::MouseMoveEvent& argInOut)
    {
        if ( !argInOut._wheelEvent )
        {
            if ( WindowManager::IsRelativeMouseMode() )
            {
                return false;
            }

            ImVec2 tempCoords{};

            _mouseCaptured = false;
            for ( const ImGuiContext* ctx : _imguiContexts )
            {
                if ( ctx->IO.WantSetMousePos )
                {
                    // Only one override at a time per context
                    assert( !_mouseCaptured);

                    _mouseCaptured = true;
                    tempCoords = ctx->IO.MousePos;
                    WindowManager::SetGlobalCursorPosition( to_I32( tempCoords.x ),
                                                            to_I32( tempCoords.y ) );
                    break;
                }
            }
            if ( !_mouseCaptured)
            {
                int2 posGlobal{};
                WindowManager::GetMouseState( posGlobal, true );
                tempCoords = { to_F32( posGlobal.x ), to_F32( posGlobal.y ) };
            }

            updateFocusState( tempCoords );

            { // Update Editor State
                ImGuiContext* ctx = _imguiContexts[to_base( ImGuiContextType::Editor )];
                if ( !ctx->IO.WantSetMousePos )
                {
                    ctx->IO.AddMousePosEvent( _windowFocusState._globalMousePos.x,
                                              _windowFocusState._globalMousePos.y );
                }
            }
            { // Update Gizmo State
                ImGuiContext* ctx = _imguiContexts[to_base( ImGuiContextType::Gizmo )];
                if ( !ctx->IO.WantSetMousePos )
                {
                    ctx->IO.AddMousePosEvent( _windowFocusState._scaledMousePos.x,
                                              _windowFocusState._scaledMousePos.y );
                }
            }
        }
        else
        {
            for ( ImGuiContext* ctx : _imguiContexts )
            {
                if ( argInOut.state().HWheel > 0 )
                {
                    ctx->IO.AddMouseWheelEvent( ctx->IO.MouseWheelH + 1, ctx->IO.MouseWheel );
                }
                if ( argInOut.state().HWheel < 0 )
                {
                    ctx->IO.AddMouseWheelEvent( ctx->IO.MouseWheelH - 1, ctx->IO.MouseWheel );
                }
                if ( argInOut.state().VWheel > 0 )
                {
                    ctx->IO.AddMouseWheelEvent( ctx->IO.MouseWheelH, ctx->IO.MouseWheel + 1 );
                }
                if ( argInOut.state().VWheel < 0 )
                {
                    ctx->IO.AddMouseWheelEvent( ctx->IO.MouseWheelH, ctx->IO.MouseWheel - 1 );
                }
            }
        }

        if (_gizmo->needsMouse())
        {
            return true;
        }

        return wantsMouse();
    }

    /// Mouse button pressed: return true if input was consumed
    bool Editor::mouseButtonPressedInternal( Input::MouseButtonEvent& argInOut)
    {
        if ( WindowManager::IsRelativeMouseMode() )
        {
            return false;
        }

        for ( ImGuiContext* ctx : _imguiContexts )
        {
            for ( size_t i = 0; i < g_editorButtons.size(); ++i )
            {
                if (argInOut.button() == g_editorButtons[i] )
                {
                    ctx->IO.AddMouseButtonEvent( to_I32( i ), true );
                    break;
                }
            }
        }

        if ( !hasFocus() && 
             _gizmo->onMouseButtonPressed(argInOut))
        {
            return true;
        }

        return wantsMouse();
    }

    /// Mouse button released: return true if input was consumed
    bool Editor::mouseButtonReleasedInternal( Input::MouseButtonEvent& argInOut)
    {
        if ( WindowManager::IsRelativeMouseMode() )
        {
            return false;
        }

        if ( SetFocus( _windowFocusState ) )
        {
            updateEditorFocus();
        }

        for ( ImGuiContext* ctx : _imguiContexts )
        {
            for ( size_t i = 0; i < g_editorButtons.size(); ++i )
            {
                if (argInOut.button() == g_editorButtons[i] )
                {
                    ctx->IO.AddMouseButtonEvent( to_I32( i ), false );
                    break;
                }
            }
        }

        if (_gizmo->onMouseButtonReleased(argInOut))
        {
            return true;
        }

        return wantsMouse();
    }

    bool Editor::joystickButtonPressedInternal( [[maybe_unused]] Input::JoystickEvent& argInOut) noexcept
    {
        return wantsJoystick();
    }

    bool Editor::joystickButtonReleasedInternal( [[maybe_unused]] Input::JoystickEvent& argInOut) noexcept
    {
        return wantsJoystick();
    }

    bool Editor::joystickAxisMovedInternal( [[maybe_unused]] Input::JoystickEvent& argInOut) noexcept
    {
        return wantsJoystick();
    }

    bool Editor::joystickPovMovedInternal( [[maybe_unused]] Input::JoystickEvent& argInOut) noexcept
    {
        return wantsJoystick();
    }

    bool Editor::joystickBallMovedInternal( [[maybe_unused]] Input::JoystickEvent& argInOut) noexcept
    {
        return wantsJoystick();
    }

    bool Editor::joystickAddRemoveInternal( [[maybe_unused]] Input::JoystickEvent& argInOut) noexcept
    {
        return wantsJoystick();
    }

    bool Editor::joystickRemapInternal( [[maybe_unused]] Input::JoystickEvent& argInOut) noexcept
    {
        return wantsJoystick();
    }

    bool Editor::wantsJoystick() const noexcept
    {
        return !_windowFocusState._focusedNodePreview && hasFocus();
    }

    bool Editor::wantsMouse() const
    {
        // We don't need editor controls in the node preview window. We want direct camera controls
        if ( !_windowFocusState._focusedNodePreview && hasFocus() )
        {
            for ( const ImGuiContext* ctx : _imguiContexts )
            {
                if ( ctx->IO.WantCaptureMouseUnlessPopupClose )
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool Editor::wantsKeyboard() const noexcept
    {
        if ( !_windowFocusState._focusedNodePreview && hasFocus() )
        {
            for ( const ImGuiContext* ctx : _imguiContexts )
            {
                if ( ctx->IO.WantCaptureKeyboard )
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool Editor::hasFocus() const
    {
        return running() && !Focused( _windowFocusState );
    }

    bool Editor::isHovered() const
    {
        return running() && !Hovered( _windowFocusState );
    }

    bool Editor::onTextEventInternal( Input::TextEvent& argInOut)
    {
        if ( !hasFocus() )
        {
            return false;
        }

        bool wantsCapture = false;
        for ( ImGuiContext* ctx : _imguiContexts )
        {
            ctx->IO.AddInputCharactersUTF8( argInOut._text.c_str() );
            wantsCapture = ctx->IO.WantCaptureKeyboard || wantsCapture;
        }

        return wantsCapture;
    }
     
    void Editor::onWindowSizeChange( const SizeChangeParams& params )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        const U16 w = params.width;
        const U16 h = params.height;

        if ( w < 1 || h < 1 ||
            !params.isMainWindow ||
            !isInit())
        {
            return;
        }

        const vec2<U16> displaySize = _mainWindow->getDrawableSize();

        for ( ImGuiContext* ctx : _imguiContexts )
        {
            ctx->IO.DisplaySize.x = to_F32( w );
            ctx->IO.DisplaySize.y = to_F32( h );

            ctx->IO.DisplayFramebufferScale = 
            {
                ctx->IO.DisplaySize.x > 1u ? to_F32( displaySize.width )  / ctx->IO.DisplaySize.x : 1.f,
                ctx->IO.DisplaySize.y > 1u ? to_F32( displaySize.height ) / ctx->IO.DisplaySize.y : 1.f
            };
        }

        Attorney::GizmoEditor::onWindowSizeChange(_gizmo.get(), params);
    }

    void Editor::onResolutionChange( const SizeChangeParams& params )
    {
        const U16 w = params.width;
        const U16 h = params.height;

        // Avoid resolution change on minimize so we don't thrash render targets
        if (w < 1 || h < 1 ||
            !isInit())
        {
            //Console::errorfn("onResolutionChange FAIL (isInit: {}) {}-{}", isInit() ? "true" : "false", w, h);
            return;
        }

        // Avoid resolution change on minimize so we don't thrash render targets
        if (_nodePreviewRTHandle._rt->getResolution() != vec2<U16>( w, h ) )
        {
            _nodePreviewRTHandle._rt->resize( w, h );
        }

        _render2DSnapshot = Camera::GetUtilityCamera( Camera::UtilityCamera::_2D_FLIP_Y )->snapshot();

        Attorney::GizmoEditor::onResolutionChange(_gizmo.get(), params);
    }

    bool Editor::saveSceneChanges( const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback ) const
    {
        if ( _context.kernel().projectManager()->saveActiveScene( false, true, msgCallback, finishCallback ) )
        {
            if ( saveToXML() )
            {
                _context.config().save();
                return true;
            }
        }

        return false;
    }

    bool Editor::openProject(const ProjectID& projectID)
    {
        return _context.kernel().projectManager()->loadProject(projectID, true) == ErrorCode::NO_ERR;
    }

    bool Editor::switchScene( const SceneEntry& scene, bool createIfNotExists )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        static CircularBuffer<SceneEntry, 10> tempBuffer;

        if ( Util::IsEmptyOrNull( scene._name.c_str() ) )
        {
            return false;
        }

        SwitchSceneTarget target
        {
            ._targetScene = scene,
            ._unloadPreviousScene = true,
            ._loadInSeparateThread = true,
            ._deferToStartOfFrame = true,
            ._createIfNotExist = createIfNotExists
        };

        if ( !_context.kernel().projectManager()->activeProject()->switchScene( target ) )
        {
            Console::errorfn( LOCALE_STR( "ERROR_SCENE_LOAD" ), scene._name.c_str() );
            showStatusMessage( Util::StringFormat( LOCALE_STR( "ERROR_SCENE_LOAD" ), scene._name.c_str() ),
                               Time::SecondsToMilliseconds<F32>( 3.f ),
                               true );
            return false;
        }

        tempBuffer.reset();
        for ( size_t i = 0u; i < _recentSceneList.size(); ++i )
        {
            const SceneEntry& crtEntry = _recentSceneList.get( i );
            if ( crtEntry != scene )
            {
                tempBuffer.put( crtEntry );
            }
        }
        tempBuffer.put( scene );
        _recentSceneList.reset();
        size_t i = tempBuffer.size();
        while ( i-- )
        {
            _recentSceneList.put( tempBuffer.get( i ) );
        }

        return true;
    }

    void Editor::onNodeSpatialChange([[maybe_unused]] const SceneGraphNode& node)
    {
        // Don't update the gizmo if it the source of the spatial change
        if (!_gizmo->isUsing())
        {
            Attorney::GizmoEditor::updateSelections(_gizmo.get());
        }
    }

    void Editor::onChangeScene( Scene* newScene )
    {
        _lastOpenSceneName = newScene == nullptr ? "" : newScene->resourceName().c_str();
    }

    U32 Editor::saveItemCount() const noexcept
    {
        U32 ret = 10u; // All of the scene stuff (settings, music, etc)

        const auto& graph = _context.kernel().projectManager()->activeProject()->getActiveScene()->sceneGraph();
        ret += to_U32( graph->getTotalNodeCount() );

        return ret;
    }

    bool Editor::isDefaultScene() const noexcept
    {
        Scene* activeScene = _context.kernel().projectManager()->activeProject()->getActiveScene();
        return activeScene->getGUID() == Scene::DEFAULT_SCENE_GUID;
    }

    bool Editor::modalTextureView( const std::string_view modalName,
                                   const Handle<Texture> tex,
                                   const float2 dimensions,
                                   const bool preserveAspect,
                                   const bool useModal ) const
    {
        if ( tex == INVALID_HANDLE<Texture> )
        {
            return false;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        static std::array<bool, 4> state = { true, true, true, true };

        const ImDrawCallback toggleColours =  []( [[maybe_unused]] const ImDrawList* parent_list, const ImDrawCmd* imCmd ) -> void
        {
            static SamplerDescriptor defaultSampler {};

            IMGUICallbackData* data = static_cast<IMGUICallbackData*>(imCmd->UserCallbackData);

            assert( data->_cmdBuffer != nullptr );
            GFX::CommandBuffer& buffer = *(data->_cmdBuffer);

            bool isArrayTexture = false;
            if ( data->_texture != INVALID_HANDLE<Texture> )
            {
                Texture* tex = Get(data->_texture);

                const TextureType texType = tex->descriptor()._texType;
                const bool isTextureArray = IsArrayTexture( texType );
                const bool isTextureCube = IsCubeTexture( texType );

                if ( isTextureArray || isTextureCube )
                {
                    isArrayTexture = true;

                    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( buffer );
                    cmd->_usage = DescriptorSetUsage::PER_DRAW;
                    {
                        DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
                        const ImageView texView = Get(Texture::DefaultTexture2D())->getView(TextureType::TEXTURE_2D,
                                                                                            { 0u, 1u },
                                                                                            { 0u, 1u });
                        Set( binding._data, texView, Texture::DefaultSampler() );
                    }
                    {
                        DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );

                        if ( isTextureCube )
                        {
                            const ImageView texView = tex->getView( TextureType::TEXTURE_2D_ARRAY,
                                                                                    { 0u, tex->mipCount() },
                                                                                    { 0u, to_U16( tex->depth() * 6u) });

                            Set( binding._data, texView, defaultSampler );
                        }
                        else
                        {
                            Set( binding._data, tex->getView(), defaultSampler );
                        }
                    }
                }
            }

            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( buffer )->_fastData = IMGUICallbackToPushConstants( *data, isArrayTexture );
        };

        bool closed = false;
        bool opened = false;

        DIVIDE_ASSERT( !modalName.empty() );

        if ( useModal )
        {
            Util::OpenCenteredPopup( modalName.data() );
            opened = ImGui::BeginPopupModal( modalName.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize );
        }
        else
        {
            Util::CenterNextWindow();
            opened = ImGui::Begin( modalName.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize );
        }

        if ( opened )
        {
            assert( tex != INVALID_HANDLE<Texture> );

            static IMGUICallbackData defaultData{
                ._flip = false
            };

            Texture* texPtr = Get(tex);

            g_modalTextureData._texture = tex;
            g_modalTextureData._isDepthTexture = IsDepthTexture( texPtr->descriptor()._packing );
            
            const U8 numChannels = NumChannels( texPtr->descriptor()._baseFormat );

            assert( numChannels > 0 );

            bool isArray = false;
            if ( g_modalTextureData._isDepthTexture )
            {
                ImGui::Text( "Depth: " );
                ImGui::SameLine();
                ImGui::ToggleButton( "Depth", &state[0] );
                ImGui::SameLine();
                ImGui::Text( "Range: " );
                ImGui::SameLine();
                ImGui::DragFloatRange2( "",
                                        &g_modalTextureData._depthRange[0],
                                        &g_modalTextureData._depthRange[1],
                                        0.005f,
                                        0.f,
                                        1.f );
            }
            else
            {
                ImGui::Text( "R: " );
                ImGui::SameLine();
                ImGui::ToggleButton( "R", &state[0] );

                if ( numChannels > 1 )
                {
                    ImGui::SameLine();
                    ImGui::Text( "G: " );
                    ImGui::SameLine();
                    ImGui::ToggleButton( "G", &state[1] );

                    if ( numChannels > 2 )
                    {
                        ImGui::SameLine();
                        ImGui::Text( "B: " );
                        ImGui::SameLine();
                        ImGui::ToggleButton( "B", &state[2] );
                    }

                    if ( numChannels > 3 )
                    {
                        ImGui::SameLine();
                        ImGui::Text( "A: " );
                        ImGui::SameLine();
                        ImGui::ToggleButton( "A", &state[3] );
                    }
                }
            }
            ImGui::SameLine();
            ImGui::Text( "Flip: " );
            ImGui::SameLine();
            ImGui::ToggleButton( "Flip", &g_modalTextureData._flip );
            if ( IsArrayTexture( texPtr->descriptor()._texType ) )
            {
                isArray = true;
                U32 maxLayers = texPtr->depth();
                if ( IsCubeTexture( texPtr->descriptor()._texType ) )
                {
                    maxLayers *= 6u;
                }
                maxLayers -= 1u;
                U32 minLayers = 0u;
                ImGui::Text( "Layer: " );
                ImGui::SameLine();
                ImGui::SliderScalar( "##modalTextureLayerSelect",
                                     ImGuiDataType_U32,
                                     &g_modalTextureData._arrayLayer,
                                     &minLayers,
                                     &maxLayers );
            }
            U16 maxMip = texPtr->mipCount();
            if ( maxMip > 1u )
            {
                maxMip -= 1u;
                U16 minMip = 0u;
                ImGui::Text( "Mip: " );
                ImGui::SameLine();
                ImGui::SliderScalar( "##modalTextureMipSelect",
                                     ImGuiDataType_U16,
                                     &g_modalTextureData._mip,
                                     &minMip,
                                     &maxMip );
            }

            const bool nonDefaultColours = g_modalTextureData._isDepthTexture || !state[0] || !state[1] || !state[2] || !state[3] || g_modalTextureData._flip || isArray;
            g_modalTextureData._colourData.set( state[0] ? 1 : 0, state[1] ? 1 : 0, state[2] ? 1 : 0, state[3] ? 1 : 0 );

            if ( nonDefaultColours )
            {
                ImGui::GetWindowDrawList()->AddCallback( toggleColours, &g_modalTextureData );
            }

            F32 aspect = 1.0f;
            if ( preserveAspect )
            {
                const U16 w = texPtr->width();
                const U16 h = texPtr->height();
                aspect = w / to_F32( h );
            }

            static F32 zoom = 1.0f;
            static ImVec2 zoomCenter( 0.5f, 0.5f );
            ImGui::ImageZoomAndPan( to_TexID(tex),
                                    ImVec2( dimensions.width, dimensions.height / aspect ),
                                    aspect,
                                    zoom,
                                    zoomCenter,
                                    2,
                                    3,
                                    ImVec2( 16.f, 1.025f ) );

            if ( nonDefaultColours )
            {
                // Reset draw data
                ImGui::GetWindowDrawList()->AddCallback( toggleColours, &defaultData );
            }

            ImGui::Text( "Mouse: Wheel = scroll | CTRL + Wheel = zoom | Hold Wheel "
                         "Button = pan" );

            if ( ImGui::Button( "Close" ) )
            {
                zoom = 1.0f;
                zoomCenter = ImVec2( 0.5f, 0.5f );
                if ( useModal )
                {
                    ImGui::CloseCurrentPopup();
                }
                g_modalTextureData._texture = INVALID_HANDLE<Texture>;
                closed = true;
            }

            if ( useModal )
            {
                ImGui::EndPopup();
            }
            else
            {
                ImGui::End();
            }
        }
        else if ( !useModal )
        {
            ImGui::End();
        }

        return closed;
    }

    bool Editor::modalModelSpawn( const Handle<Mesh> mesh,
                                  const bool showSpawnModalFirst,
                                  const float3& scale,
                                  const float3& position )
    {
        if ( mesh == INVALID_HANDLE<Mesh> )
        {
            return false;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        _queuedModelSpawns.push(QueueModelSpawn
        {
            ._mesh = mesh,
            .transform 
            {
                ._translation = position,
                ._scale = scale,
            },
            ._showModal = showSpawnModalFirst
        });

        if (!showSpawnModalFirst)
        {
            const Camera * playerCam = Attorney::ProjectManagerCameraAccessor::playerCamera(_context.kernel().projectManager().get());
            _queuedModelSpawns.back().transform._orientation = playerCam->snapshot()._orientation;
        }

        return true;
    }

    void Editor::renderModelSpawnModal()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        static bool showModal = false;
        static QueueModelSpawn modelSpawn;
        static bool uniformScaling = true;
        using ReturnTypeOfName = std::remove_cvref_t<std::invoke_result_t<decltype(&Resource::resourceName), Resource>>;
        static char inputBuf[ReturnTypeOfName::kMaxSize + 2] = {};

        if ( !showModal )
        {
            if ( _queuedModelSpawns.empty() || !_queuedModelSpawns.front()._showModal )
            {
                return;
            }
            modelSpawn = _queuedModelSpawns.front();
            _queuedModelSpawns.pop();
            showModal = true;
            modelSpawn._showModal = false;
        }

        Util::OpenCenteredPopup( "Spawn Entity" );
        if ( ImGui::BeginPopupModal( "Spawn Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            DIVIDE_ASSERT(modelSpawn._mesh != INVALID_HANDLE<Mesh> );
            if ( Util::IsEmptyOrNull( inputBuf ) )
            {
                strcpy( &inputBuf[0], Get(modelSpawn._mesh)->resourceName().c_str() );
            }
            ImGui::Text( "Spawn [ %s ]?", inputBuf );
            ImGui::Separator();

            ImGui::Text( "Scale:" ); ImGui::SameLine();
            if ( uniformScaling )
            {
                if ( ImGui::InputFloat("##Scale:", &modelSpawn.transform._scale.x, 0.f, 0.f, "%.5f") )
                {
                    modelSpawn.transform._scale.set(std::max(modelSpawn.transform._scale.x, EPSILON_F32));
                }
            }
            else
            {
                if ( ImGui::InputFloat3( "##Scale:", modelSpawn.transform._scale._v, "%.5f" ) )
                {
                    modelSpawn.transform._scale = Max(modelSpawn.transform._scale, VECTOR3_EPSILON);
                }
            }
            if (ImGui::Checkbox("Uniform scaling", &uniformScaling))
            {
            }

            ImGui::Text( "Position:" ); ImGui::SameLine();
            if ( ImGui::InputFloat3( "##Position:", modelSpawn.transform._translation._v ) )
            {
            }

            vec3<Angle::DEGREES_F> rotation = Angle::to_DEGREES(modelSpawn.transform._orientation.getEuler());
            ImGui::Text( "Rotation (euler):" ); ImGui::SameLine();
            if ( ImGui::InputFloat3( "##Rotation (euler):", &rotation.x.value ) )
            {
                modelSpawn.transform._orientation.fromEuler(Angle::to_RADIANS(rotation));
            }
            ImGui::Text( "Name:" ); ImGui::SameLine();
            if ( ImGui::InputText( "##Name:",
                                   inputBuf,
                                   IM_ARRAYSIZE( inputBuf ),
                                   ImGuiInputTextFlags_EnterReturnsTrue ) )
            {
            }

            ImGui::Separator();
            if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
            {
                ImGui::CloseCurrentPopup();
                showModal = false;
                inputBuf[0] = '\0';
            }

            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if ( ImGui::Button( "Yes", ImVec2( 120, 0 ) ) )
            {
                ImGui::CloseCurrentPopup();
                showModal = false;
                inputBuf[0] = '\0';

                _queuedModelSpawns.push(modelSpawn);
            }

            ImGui::EndPopup();
        }
    }

    void Editor::showStatusMessage( const string& message,
                                    const F32 durationMS,
                                    bool error ) const
    {
        _statusBar->showMessage( message, durationMS, error );
    }

    bool Editor::spawnGeometry( SceneGraphNode& root, const QueueModelSpawn& model ) const
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Scene);

        constexpr U32 normalMask = to_base( ComponentType::TRANSFORM ) |
                                   to_base( ComponentType::BOUNDS ) |
                                   to_base( ComponentType::NETWORKING ) |
                                   to_base( ComponentType::RENDERING ) |
                                   to_base( ComponentType::SELECTION);

        if (model._mesh != INVALID_HANDLE<Mesh>)
        {
            SceneGraphNodeDescriptor nodeDescriptor = {};
            nodeDescriptor._name = Get(model._mesh)->resourceName().c_str();
            nodeDescriptor._componentMask = normalMask;
            nodeDescriptor._nodeHandle = FromHandle(model._mesh);

            const SceneGraphNode* node = root.addChildNode( nodeDescriptor );
            if ( node != nullptr )
            {
                node->get<TransformComponent>()->setTransform(model.transform);
                return true;
            }
        }

        return false;
    }

    const ProjectIDs& Editor::getProjectList() const noexcept
    {
        return _context.kernel().projectManager()->availableProjects();
    }

    const SceneEntries& Editor::getSceneList() const noexcept
    {
        return _context.kernel().projectManager()->activeProject()->getSceneEntries();
    }

    LightPool& Editor::getActiveLightPool() const
    {
        Scene* activeScene = _context.kernel().projectManager()->activeProject()->getActiveScene();
        return *activeScene->lightPool();
    }

    SceneEnvironmentProbePool* Editor::getActiveEnvProbePool() const noexcept
    {
        return Attorney::ProjectManagerEditor::getEnvProbes( _context.kernel().projectManager().get() );
    }

    BoundingSphere Editor::teleportToNode( Camera* camera, const SceneGraphNode* sgn ) const
    {
        return Attorney::ProjectManagerCameraAccessor::moveCameraToNode( _context.kernel().projectManager().get(), camera, sgn );
    }

    void Editor::onRemoveComponent( const EditorComponent& comp ) const
    {
        for ( auto& window : _dockedWindows )
        {
            window->onRemoveComponent( comp );
        }
    }

    void Editor::saveNode( const SceneGraphNode* sgn ) const
    {
        if ( Attorney::ProjectManagerEditor::saveNode( _context.kernel().projectManager().get(), sgn ) )
        {
            bool savedParent = false, savedScene = false;
            // Save the parent as well (if it isn't the root node) as this node may be
            // one that's been newly added
            if ( sgn->parent() != nullptr && sgn->parent()->parent() != nullptr )
            {
                savedParent = Attorney::ProjectManagerEditor::saveNode( _context.kernel().projectManager().get(), sgn->parent() );
            }
            if ( unsavedSceneChanges() )
            {
                savedScene = saveSceneChanges( {}, {} );
            }

            showStatusMessage(
                Util::StringFormat(
                    "Saved node [ {} ] to file! (Saved parent: {}) (Saved scene: {})",
                    sgn->name().c_str(),
                    savedParent ? "Yes" : "No",
                    savedScene ? "Yes" : "No" ),
                Time::SecondsToMilliseconds<F32>( 3 ),
                false );
        }
    }

    void Editor::loadNode( SceneGraphNode* sgn ) const
    {
        if ( Attorney::ProjectManagerEditor::loadNode( _context.kernel().projectManager().get(), sgn ) )
        {
            showStatusMessage( Util::StringFormat( "Reloaded node [ {} ] from file!",
                                                   sgn->name().c_str() ),
                               Time::SecondsToMilliseconds<F32>( 3 ),
                               false );
        }
    }

    void Editor::queueRemoveNode( const I64 nodeGUID )
    {
        Scene* activeScene = _context.kernel().projectManager()->activeProject()->getActiveScene();
        activeScene->sceneGraph()->removeNode( nodeGUID );
        unsavedSceneChanges( true );
    }

    bool Editor::addComponent( SceneGraphNode* selection,
                               const ComponentType newComponentType ) const
    {
        if ( selection != nullptr && newComponentType != ComponentType::COUNT )
        {
            selection->AddComponents( to_U32( newComponentType ), true );
            return selection->componentMask() & to_U32( newComponentType );
        }

        return false;
    }

    bool Editor::addComponent( const Selections& selections,
                               const ComponentType newComponentType ) const
    {
        bool ret = false;
        if ( selections._selectionCount > 0 )
        {
            Scene* activeScene = context().kernel().projectManager()->activeProject()->getActiveScene();

            for ( U8 i = 0; i < selections._selectionCount; ++i )
            {
                SceneGraphNode* sgn = activeScene->sceneGraph()->findNode( selections._selections[i] );
                ret = addComponent( sgn, newComponentType ) || ret;
            }
        }

        return ret;
    }

    bool Editor::removeComponent( SceneGraphNode* selection,
                                  const ComponentType newComponentType ) const
    {
        if ( selection != nullptr && newComponentType != ComponentType::COUNT )
        {
            selection->RemoveComponents( to_U32( newComponentType ) );
            return !( selection->componentMask() & to_U32( newComponentType ) );
        }

        return false;
    }

    bool Editor::removeComponent( const Selections& selections,
                                  const ComponentType newComponentType ) const
    {
        bool ret = false;
        if ( selections._selectionCount > 0 )
        {
            Scene* activeScene = context().kernel().projectManager()->activeProject()->getActiveScene();

            for ( U8 i = 0; i < selections._selectionCount; ++i )
            {
                SceneGraphNode* sgn = activeScene->sceneGraph()->findNode( selections._selections[i] );
                ret = removeComponent( sgn, newComponentType ) || ret;
            }
        }

        return ret;
    }

    bool Editor::saveToXML() const
    {
        boost::property_tree::ptree pt;
        const ResourcePath editorPath = Paths::g_xmlDataLocation / Paths::Editor::g_saveLocation;

        pt.put( "editor.showMemEditor", _showMemoryEditor );
        pt.put( "editor.showSampleWindow", _showSampleWindow );
        pt.put( "editor.themeIndex", to_I32( _currentTheme ) );
        pt.put( "editor.textEditor", _externalTextEditorPath.string() );
        pt.put( "editor.lastOpenSceneName", _lastOpenSceneName );
        pt.put( "editor.grid.<xmlattr>.enabled_scene", infiniteGridEnabledScene() );
        pt.put( "editor.grid.<xmlattr>.enabled_node", infiniteGridEnabledNode() );
        pt.put( "editor.grid.<xmlattr>.axisWidth", infiniteGridAxisWidth() );
        pt.put( "editor.grid.<xmlattr>.scale", infiniteGridScale() );
        pt.put( "editor.nodeBGColour.<xmlattr>.r", nodePreviewBGColour().r );
        pt.put( "editor.nodeBGColour.<xmlattr>.g", nodePreviewBGColour().g );
        pt.put( "editor.nodeBGColour.<xmlattr>.b", nodePreviewBGColour().b );

        for ( size_t i = 0u; i < _recentSceneList.size(); ++i )
        {
            pt.add( "editor.recentScene.entry", _recentSceneList.get( i )._name.c_str() );
        }

        if ( createDirectory( editorPath ) == FileError::NONE )
        {
            if ( copyFile( editorPath,
                           g_editorSaveFile,
                           editorPath,
                           g_editorSaveFileBak,
                           true )
                 == FileError::NONE )
            {
                XML::writeXML( editorPath / g_editorSaveFile, pt );
                return true;
            }
        }

        return false;
    }

    bool Editor::loadFromXML()
    {
        static boost::property_tree::ptree g_emptyPtree;

        boost::property_tree::ptree pt;
        const ResourcePath editorPath = Paths::g_xmlDataLocation / Paths::Editor::g_saveLocation;
        if ( !fileExists( editorPath / g_editorSaveFile ) )
        {
            if ( fileExists( editorPath / g_editorSaveFileBak ) )
            {
                if ( copyFile( editorPath,
                               g_editorSaveFileBak,
                               editorPath,
                               g_editorSaveFile,
                               true )
                     != FileError::NONE )
                {
                    return false;
                }
            }
        }

        if ( fileExists( editorPath / g_editorSaveFile ) )
        {
            XML::readXML(editorPath / g_editorSaveFile, pt );
            _showMemoryEditor = pt.get( "editor.showMemEditor", false );
            _showSampleWindow = pt.get( "editor.showSampleWindow", false );
            _currentTheme = static_cast<ImGuiStyleEnum>(pt.get( "themeIndex", to_I32( _currentTheme ) ));
            ImGui::ResetStyle( _currentTheme );
            _externalTextEditorPath = ResourcePath { pt.get<string>( "editor.textEditor", "notepad" ) };
            if ( _lastOpenSceneName == pt.get<string>( "editor.lastOpenSceneName", "" ) )
            {
                NOP();
            }
            else
            {
                NOP();
            }
            for ( const auto& [tag, data] :
                  pt.get_child( "editor.recentScene", g_emptyPtree ) )
            {
                if ( tag == "<xmlcomment>" )
                {
                    continue;
                }
                const std::string name = data.get_value<std::string>();
                if ( !name.empty() )
                {
                    _recentSceneList.put( SceneEntry { ._name = name.c_str() } );
                }
            }

            infiniteGridEnabledScene( pt.get( "editor.grid.<xmlattr>.enabled_scene",
                                              infiniteGridEnabledScene() ) );
            infiniteGridEnabledNode( pt.get( "editor.grid.<xmlattr>.enabled_node", infiniteGridEnabledNode() ) );
            infiniteGridAxisWidth( pt.get( "editor.grid.<xmlattr>.axisWidth", infiniteGridAxisWidth() ) );
            infiniteGridScale( pt.get( "editor.grid.<xmlattr>.scale", infiniteGridScale() ) );
            _nodePreviewBGColour.set(
                pt.get( "editor.nodeBGColour.<xmlattr>.r", nodePreviewBGColour().r ),
                pt.get( "editor.nodeBGColour.<xmlattr>.g", nodePreviewBGColour().g ),
                pt.get( "editor.nodeBGColour.<xmlattr>.b", nodePreviewBGColour().b ) );
            return true;
        }

        return false;
    }

    void Editor::initBasicImGUIState(ImGuiIO& io, const bool enableViewportSupport, string& clipboardStringBuffer) noexcept
    {
        if (enableViewportSupport)
        {
            io.ConfigViewportsNoDecoration = true;
            io.ConfigViewportsNoTaskBarIcon = true;
            io.ConfigDockingTransparentPayload = true;
            io.ConfigViewportsNoAutoMerge = false;

            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking

            io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
            io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports; // We can create multi-viewports on the Platform side (optional)
        }

        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos; // We can honor io.WantSetMousePos requests (optional, rarely used)

        io.BackendPlatformName = Config::ENGINE_NAME;
        io.BackendRendererName = _context.gfx().renderAPI() == RenderAPI::Vulkan ? "Vulkan" : "OpenGL";
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.DisplaySize.x = to_F32(_mainWindow->getDimensions().width);
        io.DisplaySize.y = to_F32(_mainWindow->getDimensions().height);

        const vec2<U16> display_size = _mainWindow->getDrawableSize();
        io.DisplayFramebufferScale.x = io.DisplaySize.x > 0 ? (F32)display_size.width  / io.DisplaySize.x : 1.f;
        io.DisplayFramebufferScale.y = io.DisplaySize.y > 0 ? (F32)display_size.height / io.DisplaySize.y : 1.f;

        io.SetClipboardTextFn = []([[maybe_unused]] void* user_data, const char* text)
        {
            SetClipboardText(text);
        };

        io.GetClipboardTextFn = []( void* user_data)
        {
            string* clipboardStringBuffer = (string*)user_data;
            clipboardStringBuffer->assign(GetClipboardText());
            return clipboardStringBuffer->c_str();
        };

        io.ClipboardUserData = &clipboardStringBuffer;
    }

    namespace Util::detail
    {
        static std::stack<ImGuiContext*> g_imguiContexts;
    }; // namespace Util::detail

    void PushReadOnly( const bool fade, const F32 fadedAlpha)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, fade ? CLAMPED_01(fadedAlpha) : 1.f);
        ImGui::BeginDisabled();
    }

    void PopReadOnly()
    {
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
    }

    void PushImGuiContext(ImGuiContext* ctx)
    {
        Util::detail::g_imguiContexts.push(ImGui::GetCurrentContext());
        ImGui::SetCurrentContext(ctx);
    }

    void PopImGuiContext()
    {
        DIVIDE_ASSERT(!Util::detail::g_imguiContexts.empty());
        ImGui::SetCurrentContext(Util::detail::g_imguiContexts.top());
        Util::detail::g_imguiContexts.pop();
    }
} // namespace Divide
