

#include "Headers/MenuBar.h"
#include "Editor/Headers/Utils.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Editor/Headers/Editor.h"

#include "Managers/Headers/ProjectManager.h"

#include "Geometry/Shapes/Predefined/Headers/Box3D.h"
#include "Geometry/Shapes/Predefined/Headers/Sphere3D.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "Geometry/Shapes/Headers/Mesh.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/PostFX/Headers/PreRenderOperator.h"
#include "Rendering/Lighting/Headers/LightPool.h"

#include "GUI/Headers/GUI.h"

#include "Graphs/Headers/SceneGraph.h"
#include "Scenes/Headers/SceneEnvironmentProbePool.h"

#include "ECS/Components/Headers/EnvironmentProbeComponent.h"
#include "ECS/Systems/Headers/ECSManager.h"

#include "Core/Resources/Headers/ResourceCache.h"
#include "ECS/Systems/Headers/AnimationSystem.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Material/Headers/MaterialEnums.h"

#include <imgui_internal.h>
#include <ImGuiMisc/imguifilesystem/imguifilesystem.h>
#include <IconsForkAwesome.h>
#include <ECS/SystemManager.h>

namespace Divide
{
    namespace
    {
        I64 DefaultObjectIndex = 0u;
        SceneGraphNodeDescriptor g_nodeDescriptor;

        const string s_messages[] = {
            "Please wait while saving current scene! App may appear frozen or stuttery for up to 30 seconds ...",
            "Saved scene succesfully",
            "Failed to save the current scene"
        };

        struct SaveSceneParams
        {
            SceneEntry _targetScene = {};
            string _saveMessage = "";
            U32 _saveElementCount = 0u;
            U32 _saveProgress = 0u;
            bool _closePopup = false;
        } g_saveSceneParams;

        const char* EdgeMethodName( const PreRenderBatch::EdgeDetectionMethod method ) noexcept
        {
            switch ( method )
            {
                case PreRenderBatch::EdgeDetectionMethod::Depth: return "Depth";
                case PreRenderBatch::EdgeDetectionMethod::Luma: return "Luma";
                case PreRenderBatch::EdgeDetectionMethod::Colour: return "Colour";
                case PreRenderBatch::EdgeDetectionMethod::COUNT: break;
                default: break;
            }
            return "Unknown";
        }
    }


    MenuBar::MenuBar( PlatformContext& context, const bool mainMenu )
        : PlatformContextComponent( context )
        , _isMainMenu( mainMenu )
        , _sceneOpenDialog( true, true )
        , _sceneSaveDialog( true )
    {
    }

    void MenuBar::draw( const bool readOnly )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        const ImGuiContext& imguiContext = Attorney::EditorGeneralWidget::getImGuiContext( _context.editor(), Editor::ImGuiContextType::Editor );
        const bool modifierPressed = imguiContext.IO.KeyShift;

        if ( readOnly )
        {
            PushReadOnly(false);
        }

        if ( ImGui::BeginMainMenuBar() )
        {
            drawFileMenu( modifierPressed );
            drawEditMenu( modifierPressed );
            drawProjectMenu( modifierPressed );
            drawObjectMenu( modifierPressed );
            drawToolsMenu( modifierPressed );
            //drawWindowsMenu(modifierPressed);
            drawPostFXMenu( modifierPressed );
            drawDebugMenu( modifierPressed );
            drawHelpMenu( modifierPressed );

            ImGui::EndMainMenuBar();

            for ( vector<Handle<Texture>>::iterator it = std::begin( _previewTextures ); it != std::end( _previewTextures ); )
            {
                if ( Attorney::EditorGeneralWidget::modalTextureView( _context.editor(), Util::StringFormat( "Image Preview: {}", Get(*it)->resourceName().c_str() ).c_str(), *it, float2( 512, 512 ), true, false ) )
                {
                    it = _previewTextures.erase( it );
                }
                else
                {
                    ++it;
                }
            }

            if ( _closePopup )
            {
                Util::OpenCenteredPopup( "Confirm Close" );

                if ( ImGui::BeginPopupModal( "Confirm Close", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
                {
                    ImGui::Text( "Are you sure you want to close the editor? You have unsaved items!" );
                    ImGui::Separator();

                    if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
                    {
                        ImGui::CloseCurrentPopup();
                        _closePopup = false;
                    }
                    ImGui::SetItemDefaultFocus();
                    ImGui::SameLine();
                    if ( ImGui::Button( "Yes", ImVec2( 120, 0 ) ) )
                    {

                        ImGui::CloseCurrentPopup();
                        _closePopup = false;
                        _context.editor().toggle( false );
                    }
                    ImGui::EndPopup();
                }
            }

            if ( !_errorMsg.empty() )
            {
                Util::OpenCenteredPopup( "Error!" );
                if ( ImGui::BeginPopupModal( "Error!", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
                {
                    ImGui::Text( _errorMsg.c_str() );
                    if ( ImGui::Button( "Ok" ) )
                    {
                        ImGui::CloseCurrentPopup();
                        _errorMsg.clear();
                    }
                    ImGui::EndPopup();
                }
            }

            if ( _newScenePopup )
            {
                Util::OpenCenteredPopup( "Create New Scene" );
                if ( ImGui::BeginPopupModal( "Create New Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
                {
                    static char buf[256];
                    ImGui::Text( "This will automatically load the newly created scene!\nWARNING: All unsaved changes will be lost!" );
                    ImGui::Text( "New Scene Name:" ); ImGui::SameLine();
                    if ( ImGui::InputText( "##New Scene Name:", &buf[0], 254 ) )
                    {

                    }
                    ImGui::Separator();
                    if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
                    {
                        ImGui::CloseCurrentPopup();
                        _newScenePopup = false;
                    }
                    ImGui::SetItemDefaultFocus();
                    ImGui::SameLine();
                    if ( ImGui::Button( "Create", ImVec2( 120, 0 ) ) )
                    {
                        ImGui::CloseCurrentPopup();
                        _newScenePopup = false;
                        SceneEntry newScene;
                        newScene._name = buf;

                        if ( newScene._name.empty() )
                        {
                            _errorMsg.append( "Scenes must be named!. \n" );
                        }
                        else if ( !Util::CompareIgnoreCase( newScene._name.c_str(), Config::DEFAULT_SCENE_NAME ) )
                        {
                            if (!Attorney::EditorGeneralWidget::switchScene( _context.editor(), newScene, true ) )
                            {
                                DIVIDE_UNEXPECTED_CALL();
                            }
                        }
                        else
                        {
                            _errorMsg.append( "Tried to use a reserved name for a new scene! Try a different name. \n" );
                        }
                    }
                    ImGui::EndPopup();
                }
            }

            if ( _saveSceneAsPopup )
            {
                Util::OpenCenteredPopup( "Save Scene As ..." );
                if ( ImGui::BeginPopupModal( "Save Scene As ...", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
                {
                    static char buf[256];
                    ImGui::Text( "Target Name:" ); ImGui::SameLine();
                    if ( ImGui::InputText( "##Target Name:", &buf[0], 254 ) )
                    {

                    }
                    ImGui::Separator();
                    if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
                    {
                        ImGui::CloseCurrentPopup();
                        _saveSceneAsPopup = false;
                    }
                    ImGui::SetItemDefaultFocus();
                    ImGui::SameLine();
                    if ( ImGui::Button( "Save", ImVec2( 120, 0 ) ) )
                    {
                        ImGui::CloseCurrentPopup();
                        _saveSceneAsPopup = false;

                        SceneEntry newScene;
                        newScene._name = buf;

                        if ( newScene._name.empty() )
                        {
                            _errorMsg.append( "Scenes must be named!. \n" );
                        }
                        else if ( !Util::CompareIgnoreCase( newScene._name.c_str(), Config::DEFAULT_SCENE_NAME ) )
                        {
                            _errorMsg.append( "Can't overwrite the default scene!. \n" );
                        }

                        saveAndDrawModalPopup( newScene );
                    }

                    ImGui::EndPopup();
                }
            }

            if ( _quitPopup )
            {
                Util::OpenCenteredPopup( "Confirm Quit" );
                if ( ImGui::BeginPopupModal( "Confirm Quit", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
                {
                    ImGui::Text( "Are you sure you want to quit?" );
                    ImGui::NewLine();
                    static bool clearCache = false;
                    ImGui::Checkbox( "Clear All Cache Items", &clearCache );
                    ImGui::NewLine();
                    ImGui::Separator();

                    if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
                    {
                        ImGui::CloseCurrentPopup();
                        _quitPopup = false;
                    }
                    ImGui::SetItemDefaultFocus();
                    ImGui::SameLine();
                    if ( ImGui::Button( "Yes", ImVec2( 120, 0 ) ) )
                    {
                        ImGui::CloseCurrentPopup();
                        _quitPopup = false;
                        context().app().RequestShutdown( clearCache );
                    }
                    ImGui::EndPopup();
                }
            }
            if ( _restartPopup )
            {
                Util::OpenCenteredPopup( "Confirm Restart" );
                if ( ImGui::BeginPopupModal( "Confirm Restart", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
                {
                    ImGui::Text( "Are you sure you want to restart the application?" );
                    ImGui::NewLine();
                    static bool clearCache = false;
                    ImGui::Checkbox("Clear All Cache Items", &clearCache);
                    ImGui::NewLine();
                    ImGui::Separator();

                    if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
                    {
                        ImGui::CloseCurrentPopup();
                        _restartPopup = false;
                    }
                    ImGui::SetItemDefaultFocus();
                    ImGui::SameLine();
                    if ( ImGui::Button( "Yes", ImVec2( 120, 0 ) ) )
                    {
                        ImGui::CloseCurrentPopup();
                        _restartPopup = false;
                        context().app().RequestRestart(clearCache);
                    }
                    ImGui::EndPopup();
                }
            }
            if ( _savePopup )
            {
                Util::OpenCenteredPopup( "Saving Scene" );
                if ( ImGui::BeginPopupModal( "Saving Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
                {
                    constexpr U32 maxSize = 40u;
                    const U32 ident = MAP( g_saveSceneParams._saveProgress, 0u, g_saveSceneParams._saveElementCount, 0u, maxSize - 5u /*overestimate a bit*/ );

                    ImGui::Text( "Saving Scene!\n\n%s", g_saveSceneParams._saveMessage.c_str() );
                    ImGui::Separator();

                    string progress;
                    for ( U32 i = 0u; i < maxSize; ++i )
                    {
                        progress.append( i < ident ? "~" : " " );
                    }
                    ImGui::Text( "[%s]", progress.c_str() );
                    ImGui::Separator();

                    if ( g_saveSceneParams._closePopup )
                    {
                        _savePopup = false;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }

            static F32 sphereRadius = 1.f;
            static U32 resolution = 16u;
            static float3 sides{ 1.f, 1.f, 1.f };
            static bool doubleSided = true;

            const auto createPrimitive = [&]()
            {
                Scene* activeScene = _context.kernel().projectManager()->activeProject()->getActiveScene();

                g_nodeDescriptor._componentMask = to_U32( ComponentType::TRANSFORM ) |
                    to_U32( ComponentType::BOUNDS ) |
                    to_U32( ComponentType::RIGID_BODY ) |
                    to_U32( ComponentType::RENDERING ) |
                    to_U32( ComponentType::SELECTION ) |
                    to_U32( ComponentType::NAVIGATION );
                g_nodeDescriptor._usageContext = NodeUsageContext::NODE_DYNAMIC;

                if ( g_nodeDescriptor._name.empty() )
                {
                    Util::StringFormat( g_nodeDescriptor._name, "Primitive_{}", DefaultObjectIndex++ );
                }

                SceneNodeHandle handle{};
                Handle<Material> materialHandle = INVALID_HANDLE<Material>;

                switch ( _newPrimitiveType )
                {
                    case SceneNodeType::TYPE_SPHERE_3D:
                    {
                        ResourceDescriptor<Sphere3D> nodeDescriptor( (g_nodeDescriptor._name + "_n").c_str() );
                        nodeDescriptor.ID( resolution );
                        nodeDescriptor.enumValue( Util::FLOAT_TO_UINT( sphereRadius ) );

                        Handle<Sphere3D> handleTmp = CreateResource( nodeDescriptor );
                        materialHandle = Get( handleTmp )->getMaterialTpl();
                        handle = FromHandle(handleTmp);
                    } break;
                    case SceneNodeType::TYPE_BOX_3D:
                    {
                        ResourceDescriptor<Box3D> nodeDescriptor( (g_nodeDescriptor._name + "_n").c_str() );
                        nodeDescriptor.data(
                            {
                                Util::FLOAT_TO_UINT( sides.x ),
                                Util::FLOAT_TO_UINT( sides.y ),
                                Util::FLOAT_TO_UINT( sides.z )
                            }
                        );

                        const Handle<Box3D> handleTmp = CreateResource( nodeDescriptor );
                        materialHandle = Get( handleTmp )->getMaterialTpl();
                        handle = FromHandle(handleTmp);
                    } break;
                    case SceneNodeType::TYPE_QUAD_3D:
                    {
                        ResourceDescriptor<Quad3D> nodeDescriptor( (g_nodeDescriptor._name + "_n").c_str() );

                        P32 quadMask;
                        quadMask.i = 0;
                        quadMask.b[0] = doubleSided ? 0 : 1;
                        nodeDescriptor.mask( quadMask );
                        const float3 halfSides = sides * 0.5f;
                        const Handle<Quad3D> handleTmp = CreateResource( nodeDescriptor );
                        materialHandle = Get( handleTmp )->getMaterialTpl();
                        handle = FromHandle( handleTmp );

                        ResourcePtr<Quad3D> node = Get( handleTmp );
                        node->setCorner( Quad3D::CornerLocation::TOP_LEFT, float3( -halfSides.x, halfSides.y, 0 ) );
                        node->setCorner( Quad3D::CornerLocation::TOP_RIGHT, float3( halfSides.x, halfSides.y, 0 ) );
                        node->setCorner( Quad3D::CornerLocation::BOTTOM_LEFT, float3( -halfSides.x, -halfSides.y, 0 ) );
                        node->setCorner( Quad3D::CornerLocation::BOTTOM_RIGHT, float3( halfSides.x, -halfSides.y, 0 ) );
                    } break;

                    default:
                        DIVIDE_UNEXPECTED_CALL();
                        break;
                }

                ResourcePtr<Material> mat = Get( materialHandle );
                mat->properties().shadingMode( ShadingMode::PBR_MR );
                mat->properties().baseColour( FColour4( 0.4f, 0.4f, 0.4f, 1.0f ) );
                mat->properties().roughness( 0.5f );
                mat->properties().metallic( 0.5f );
                g_nodeDescriptor._nodeHandle = handle;

                activeScene->sceneGraph()->getRoot()->addChildNode( g_nodeDescriptor );
                Attorney::EditorGeneralWidget::registerUnsavedSceneChanges( _context.editor() );

                _newPrimitiveType = SceneNodeType::COUNT;
            };

            if ( _newPrimitiveType != SceneNodeType::COUNT )
            {
                if ( modifierPressed )
                {
                    createPrimitive();
                }
                else
                {
                    Util::OpenCenteredPopup( "Create Primitive" );
                    if ( ImGui::BeginPopupModal( "Create Primitive", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
                    {
                        ImGui::Text( "Create a new [ %s ]?", Names::sceneNodeType[to_base( _newPrimitiveType )] );
                        ImGui::Separator();

                        static char buf[64];
                        ImGui::Text( "Name:" ); ImGui::SameLine();
                        if ( ImGui::InputText( "##Name:", &buf[0], 61 ) )
                        {
                            g_nodeDescriptor._name = buf;
                        }

                        switch ( _newPrimitiveType )
                        {
                            case SceneNodeType::TYPE_SPHERE_3D:
                                ImGui::InputFloat( "Radius", &sphereRadius );
                                ImGui::InputScalar( "Resolution", ImGuiDataType_U32, &resolution );
                                break;
                            case SceneNodeType::TYPE_BOX_3D:
                                Util::DrawVec<F32, 3, false>( ImGuiDataType_Float, "Side length", Util::FieldLabels, sides._v, false, false, 0.1f, 0.001f, 10000.f );
                                break;
                            case SceneNodeType::TYPE_QUAD_3D:
                                Util::DrawVec<F32, 2, false>( ImGuiDataType_Float, "Side length", Util::FieldLabels, sides._v, false, false, 0.1f, 0.001f, 10000.f );
                                ImGui::Checkbox( "Double Sided", &doubleSided );
                                break;
                            default: break;
                        }
                        if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
                        {
                            ImGui::CloseCurrentPopup();
                            _newPrimitiveType = SceneNodeType::COUNT;
                        }
                        ImGui::SetItemDefaultFocus();
                        ImGui::SameLine();
                        if ( g_nodeDescriptor._name.empty() )
                        {
                            PushReadOnly();
                        }
                        if ( ImGui::Button( "Create", ImVec2( 120, 0 ) ) )
                        {
                            createPrimitive();
                            ImGui::CloseCurrentPopup();
                            _newPrimitiveType = SceneNodeType::COUNT;
                        }
                        if ( g_nodeDescriptor._name.empty() )
                        {
                            PopReadOnly();
                        }

                        std::memset( buf, 0, 64 * sizeof( char ) );
                        ImGui::EndPopup();
                    }
                }
            }

            if ( _debugObject != DebugObject::COUNT )
            {
                spawnDebugObject( _debugObject, modifierPressed );
                _debugObject = DebugObject::COUNT;
            }
        }

        if (readOnly)
        {
            PopReadOnly();
        }
    }

    void MenuBar::saveAndDrawModalPopup( const SceneEntry& targetScene )
    {
        _savePopup = true;

        g_saveSceneParams._targetScene = targetScene;
        g_saveSceneParams._closePopup = false;
        g_saveSceneParams._saveProgress = 0u;
        g_saveSceneParams._saveElementCount = Attorney::EditorGeneralWidget::saveItemCount( _context.editor() );

        const auto messageCbk = []( const std::string_view msg )
        {
            g_saveSceneParams._saveMessage = msg;
            ++g_saveSceneParams._saveProgress;
        };

        const auto closeDialog = [this]( const bool success )
        {
            Attorney::EditorGeneralWidget::showStatusMessage( _context.editor(), s_messages[success ? 1 : 2], Time::SecondsToMilliseconds<F32>( 6 ), !success );
            g_saveSceneParams._closePopup = true;
        };

        Attorney::EditorGeneralWidget::showStatusMessage( _context.editor(), s_messages[0], Time::SecondsToMilliseconds<F32>( 6 ), false );
        if ( !Attorney::EditorGeneralWidget::saveSceneChanges( _context.editor(), messageCbk, closeDialog ) )
        {
            _errorMsg.append( "Error occurred while saving the current scene!\n Try again or check the logs for errors!\n" );
        }
    }

    void MenuBar::drawFileMenu( [[maybe_unused]] const bool modifierPressed )
    {
        if ( ImGui::BeginMenu( "File" ) )
        {
            const bool hasUnsavedElements = Attorney::EditorGeneralWidget::hasUnsavedSceneChanges( _context.editor() );
            const bool isDefaultScene = Attorney::EditorGeneralWidget::isDefaultScene( _context.editor() );


            const auto& projectList = Attorney::EditorMenuBar::getProjectList( _context.editor() );
            if ( ImGui::BeginMenu( "Open Project", !projectList.empty() ) )
            {
                bool success = true;
                for ( size_t i = 0u; i < projectList.size(); ++i )
                {
                    if ( ImGui::MenuItem( projectList[i]._name.c_str() ) )
                    {
                        if (!Attorney::EditorMenuBar::openProject( _context.editor(), projectList[i] ))
                        {
                            success = false;
                            _errorMsg.append( Util::StringFormat( "Error occurred while trying to open project [ {} ]\n Try again or check the logs for errors!\n", projectList[i]._name.c_str()) );

                            for (const auto& backup : projectList)
                            {
                                if (backup._name == Config::DEFAULT_PROJECT_NAME)
                                {
                                    if (Attorney::EditorMenuBar::openProject( _context.editor(), projectList.front() ))
                                    {
                                        success = true;
                                    }
                                    break;
                                }
                            }
                        }
                        if (!success)
                        {
                            DIVIDE_UNEXPECTED_CALL_MSG(Util::StringFormat(LOCALE_STR("ERROR_PROJECT_LOAD"), projectList[i]._name).c_str());
                        }
                    }
                }
                ImGui::EndMenu();
            }

            if ( ImGui::MenuItem( "New Scene", "Ctrl+N", false, true ) )
            {
                if ( hasUnsavedElements && !isDefaultScene )
                {
                    saveAndDrawModalPopup( _context.kernel().projectManager()->activeProject()->getActiveScene()->entry() );
                }
                _newScenePopup = true;
            }

            const SceneEntries& sceneList = Attorney::EditorMenuBar::getSceneList( _context.editor() );
            if ( ImGui::BeginMenu("Open Scene", !sceneList.empty()) )
            {
                for ( size_t i = 0u; i < sceneList.size(); ++i )
                {
                    if ( ImGui::MenuItem( sceneList[i]._name.c_str() ) )
                    {
                        Attorney::EditorGeneralWidget::switchScene( _context.editor(), sceneList[i], false );
                    }
                }

                ImGui::EndMenu();
            }

            const auto& recentSceneList = Attorney::EditorMenuBar::getRecentSceneList( _context.editor() );
            if ( ImGui::BeginMenu( "Open Recent", !recentSceneList.empty() ) )
            {
                for ( size_t i = 0u; i < recentSceneList.size(); ++i )
                {
                    if ( ImGui::MenuItem( recentSceneList.get( i )._name.c_str() ) )
                    {
                        Attorney::EditorGeneralWidget::switchScene( _context.editor(), recentSceneList.get( i ), false );
                    }
                }

                ImGui::EndMenu();
            }

            if ( ImGui::MenuItem( modifierPressed ? (ICON_FK_FLOPPY_O" Save Scene (Forced)") : (ICON_FK_FLOPPY_O" Save Scene"), "", false, hasUnsavedElements || isDefaultScene || modifierPressed ) )
            {
                if ( isDefaultScene && !modifierPressed )
                {
                    _saveSceneAsPopup = true;
                }
                else
                {
                    saveAndDrawModalPopup( _context.kernel().projectManager()->activeProject()->getActiveScene()->entry() );
                }
            }
            if ( modifierPressed )
            {
                Util::AddUnderLine();
            }

            if ( ImGui::MenuItem( ICON_FK_FLOPPY_O" Save Scene As", "", false, !isDefaultScene ) )
            {
                _saveSceneAsPopup = true;
            }

            ImGui::Separator();
            if ( ImGui::BeginMenu( "Options" ) )
            {
                GFXDevice& gfx = _context.gfx();
                const Configuration& config = _context.config();

                if ( ImGui::BeginMenu( "MSAA" ) )
                {
                    for ( U8 i = 0; 1 << i <= DisplayManager::MaxMSAASamples(); ++i )
                    {
                        const U8 sampleCount = i == 0u ? 0u : 1 << i;
                        if ( sampleCount % 2 == 0 )
                        {
                            bool msaaEntryEnabled = config.rendering.MSAASamples == sampleCount;
                            if ( ImGui::MenuItem( Util::StringFormat( "{}x", to_U32( sampleCount ) ).c_str(), "", &msaaEntryEnabled ) )
                            {
                                gfx.setScreenMSAASampleCount( sampleCount );
                            }
                        }
                    }
                    ImGui::EndMenu();
                }

                for ( U8 type = 0; type < to_U8( ShadowType::COUNT ); ++type )
                {
                    const ShadowType sType = static_cast<ShadowType>(type);
                    if ( sType != ShadowType::CUBEMAP &&
                         ImGui::BeginMenu( Util::StringFormat( "{} ShadowMap MSAA", Names::shadowType[type] ).c_str() ) )
                    {
                        const U8 currentCount = sType == ShadowType::CSM
                            ? config.rendering.shadowMapping.csm.MSAASamples
                            : config.rendering.shadowMapping.spot.MSAASamples;

                        for ( U8 i = 0; 1 << i <= DisplayManager::MaxMSAASamples(); ++i )
                        {
                            const U8 sampleCount = i == 0u ? 0u : 1 << i;
                            if ( sampleCount % 2 == 0 )
                            {
                                bool msaaEntryEnabled = currentCount == sampleCount;
                                if ( ImGui::MenuItem( Util::StringFormat( "{}x", to_U32( sampleCount ) ).c_str(), "", &msaaEntryEnabled ) )
                                {
                                    gfx.setShadowMSAASampleCount( sType, sampleCount );
                                }
                            }
                        }
                        ImGui::EndMenu();
                    }
                }

                ImGui::EndMenu();
            }

            bool& options = Attorney::EditorMenuBar::optionWindowEnabled( _context.editor() );
            ImGui::MenuItem( "Editor options", "", &options );

            ImGui::Separator();
            if ( ImGui::BeginMenu( "Export Game" ) )
            {
                for ( auto platform : Editor::g_supportedExportPlatforms )
                {
                    PushReadOnly(true);
                    if ( ImGui::MenuItem( platform, "", false, true ) )
                    {
                        if ( hasUnsavedElements )
                        {
                            saveAndDrawModalPopup( _context.kernel().projectManager()->activeProject()->getActiveScene()->entry() );
                        }
                        Attorney::EditorGeneralWidget::showStatusMessage( _context.editor(), Util::StringFormat( "Exported game for [ {} ]!", platform ), Time::SecondsToMilliseconds<F32>( 3.0f ), false );
                        break;
                    }
                    PopReadOnly();
                }

                ImGui::EndMenu();
            }
            ImGui::Separator();
            if ( ImGui::MenuItem( "Close Editor" ) )
            {
                if ( hasUnsavedElements )
                {
                    _closePopup = true;
                }
                else
                {
                    _context.editor().toggle( false );
                }
            }

            if ( ImGui::MenuItem( "Restart" ) )
            {
                _restartPopup = true;
            }

            if ( ImGui::MenuItem( "Quit", "Alt+F4" ) )
            {
                _quitPopup = true;
            }

            ImGui::EndMenu();
        }
    }

    void MenuBar::drawEditMenu( [[maybe_unused]] const bool modifierPressed ) const
    {
        if ( ImGui::BeginMenu( "Edit" ) )
        {
            if ( ImGui::MenuItem( "Undo", "CTRL+Z", false, _context.editor().UndoStackSize() > 0 ) )
            {
                if ( !_context.editor().Undo() )
                {
                    Attorney::EditorGeneralWidget::showStatusMessage( _context.editor(), "ERROR: Undo failed!", Time::SecondsToMilliseconds<F32>( 3.0f ), true );
                }
            }

            if ( ImGui::MenuItem( "Redo", "CTRL+Y", false, _context.editor().RedoStackSize() > 0 ) )
            {
                if ( !_context.editor().Redo() )
                {
                    Attorney::EditorGeneralWidget::showStatusMessage( _context.editor(), "ERROR: Redo failed!", Time::SecondsToMilliseconds<F32>( 3.0f ), true );
                }
            }

            ImGui::Separator();

            if ( ImGui::MenuItem( "Cut", "CTRL+X", false, false ) )
            {
            }

            if ( ImGui::MenuItem( "Copy", "CTRL+C", false, false ) )
            {
            }

            if ( ImGui::MenuItem( "Paste", "CTRL+V", false, false ) )
            {
            }

            ImGui::Separator();
            ImGui::EndMenu();
        }
    }

    void MenuBar::drawProjectMenu( [[maybe_unused]] const bool modifierPressed ) const
    {
        if ( ImGui::BeginMenu( "Project" ) )
        {
            if ( ImGui::MenuItem( "Configuration", "", false, false ) )
            {
            }

            ImGui::EndMenu();
        }
    }
    void MenuBar::drawObjectMenu( const bool modifierPressed )
    {
        if ( ImGui::BeginMenu( "Object" ) )
        {
            if ( ImGui::BeginMenu( "New Primitive" ) )
            {
                if ( ImGui::MenuItem( "Sphere" ) )
                {
                    g_nodeDescriptor = {};
                    _newPrimitiveType = SceneNodeType::TYPE_SPHERE_3D;
                }
                if ( modifierPressed )
                {
                    Util::AddUnderLine();
                }
                if ( ImGui::MenuItem( "Box" ) )
                {
                    g_nodeDescriptor = {};
                    _newPrimitiveType = SceneNodeType::TYPE_BOX_3D;
                }
                if ( modifierPressed )
                {
                    Util::AddUnderLine();
                }
                if ( ImGui::MenuItem( "Plane" ) )
                {
                    g_nodeDescriptor = {};
                    _newPrimitiveType = SceneNodeType::TYPE_QUAD_3D;
                }
                if ( modifierPressed )
                {
                    Util::AddUnderLine();
                }
                ImGui::EndMenu();
            }
            if ( ImGui::BeginMenu( "Debug Objects" ) )
            {
                if ( ImGui::MenuItem( "Sponza" ) )
                {
                    g_nodeDescriptor = {};
                    _debugObject = DebugObject::SPONZA;
                }
                if ( modifierPressed )
                {
                    Util::AddUnderLine();
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

    }
    void MenuBar::drawToolsMenu( [[maybe_unused]] const bool modifierPressed )
    {
        if ( ImGui::BeginMenu( "Tools" ) )
        {
            const bool memEditorEnabled = Attorney::EditorMenuBar::memoryEditorEnabled( _context.editor() );
            if ( ImGui::MenuItem( "Memory Editor", nullptr, memEditorEnabled ) )
            {
                Attorney::EditorMenuBar::toggleMemoryEditor( _context.editor(), !memEditorEnabled );
                if ( !_context.editor().saveToXML() )
                {
                    Attorney::EditorGeneralWidget::showStatusMessage( _context.editor(), "ERROR: Save failed!", Time::SecondsToMilliseconds<F32>( 3.0f ), true );
                }
            }

            if ( ImGui::BeginMenu( "Render Targets" ) )
            {
                const GFXRTPool& pool = _context.gfx().renderTargetPool();
                for ( auto& rt : pool.getRenderTargets() )
                {
                    if ( ImGui::BeginMenu( rt->name().c_str() ) )
                    {
                        for ( U8 j = 0u; j < 2u; ++j )
                        {
                            const RTAttachmentType type = j == 0 ? RTAttachmentType::COLOUR : RTAttachmentType::DEPTH;
                            const U8 count = rt->getAttachmentCount( type );

                            for ( U8 k = 0; k < count; ++k )
                            {
                                RTAttachment* attachment = rt->getAttachment( type, static_cast<RTColourAttachmentSlot>(k) );
                                if ( attachment == nullptr )
                                {
                                    continue;
                                }
                                const Handle<Texture> tex = attachment->texture();
                                if ( tex == INVALID_HANDLE<Texture> )
                                {
                                    continue;
                                }

                                if ( ImGui::MenuItem( Get(tex)->resourceName().c_str() ) )
                                {
                                    _previewTextures.push_back( tex );
                                }
                            }
                        }
                        ImGui::EndMenu();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::drawWindowsMenu( [[maybe_unused]] const bool modifierPressed ) const
    {
        if ( ImGui::BeginMenu( "Window" ) )
        {
            bool& sampleWindowEnabled = Attorney::EditorMenuBar::sampleWindowEnabled( _context.editor() );
            if ( ImGui::MenuItem( "Sample Window", nullptr, &sampleWindowEnabled ) )
            {

            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::drawPostFXMenu( [[maybe_unused]] const bool modifierPressed ) const
    {
        if ( ImGui::BeginMenu( "PostFX" ) )
        {
            PostFX& postFX = _context.gfx().getRenderer().postFX();
            for ( U16 i = 0u; i < to_base( FilterType::FILTER_COUNT ); ++i )
            {
                const FilterType f = static_cast<FilterType>(i);

                bool filterEnabled = postFX.getFilterState( f );
                if ( ImGui::MenuItem( PostFX::FilterName( f ), nullptr, &filterEnabled ) )
                {
                    if ( filterEnabled )
                    {
                        postFX.pushFilter( f, true );
                    }
                    else
                    {
                        postFX.popFilter( f, true );
                    }
                }
            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::drawDebugMenu( [[maybe_unused]] const bool modifierPressed )
    {
        if ( ImGui::BeginMenu( "Debug" ) )
        {
            if ( ImGui::BeginMenu( "BRDF Settings" ) )
            {
                const MaterialDebugFlag debugFlag = _context.gfx().materialDebugFlag();
                bool debug = false;
                for ( U8 i = 0u; i < to_U8( MaterialDebugFlag::COUNT ); ++i )
                {
                    const MaterialDebugFlag flag = static_cast<MaterialDebugFlag>(i);
                    debug = debugFlag == flag;
                    if ( ImGui::MenuItem( TypeUtil::MaterialDebugFlagToString( flag ), "", &debug ) )
                    {
                        _context.gfx().materialDebugFlag( debug ? flag : MaterialDebugFlag::COUNT );
                    }
                }
                ImGui::EndMenu();
            }

            SceneEnvironmentProbePool* envProbPool = Attorney::EditorGeneralWidget::getActiveEnvProbePool( _context.editor() );
            LightPool& pool = Attorney::EditorGeneralWidget::getActiveLightPool( _context.editor() );

            if ( ImGui::BeginMenu( "Render Filters" ) )
            {
                Configuration::Debug::RenderFilter& renderFilters = _context.config().debug.renderFilter;
                bool configDirty = false;
                if ( ImGui::MenuItem( "MESHES", "", &renderFilters.meshes ) )
                {
                    configDirty = true;
                }
                if ( ImGui::MenuItem( "PARTICLES", "", &renderFilters.particles ) )
                {
                    configDirty = true;
                }
                if ( ImGui::MenuItem( "PRIMITIVES", "", &renderFilters.primitives ) )
                {
                    configDirty = true;
                }
                if ( ImGui::MenuItem( "SKY", "", &renderFilters.sky ) )
                {
                    configDirty = true;
                }
                if ( ImGui::MenuItem( "TERRAIN", "", &renderFilters.terrain ) )
                {
                    configDirty = true;
                }
                if ( ImGui::MenuItem( "VEGETATION", "", &renderFilters.vegetation ) )
                {
                    configDirty = true;
                }
                if ( ImGui::MenuItem( "WATER", "", &renderFilters.water ) )
                {
                    configDirty = true;
                }
                if ( ImGui::MenuItem( "DECALS", "", &renderFilters.decals ) )
                {
                    configDirty = true;
                }
                if ( configDirty )
                {
                    _context.config().changed( true );
                }
                ImGui::EndMenu();
            }

            if ( ImGui::BeginMenu( "Toggle Light Types" ) )
            {
                for ( U8 i = 0; i < to_U8( LightType::COUNT ); ++i )
                {
                    const LightType type = static_cast<LightType>(i);

                    bool state = pool.lightTypeEnabled( type );
                    if ( ImGui::MenuItem( TypeUtil::LightTypeToString( type ), "", &state ) )
                    {
                        pool.toggleLightType( type, state );
                    }
                }
                ImGui::EndMenu();
            }

            if ( ImGui::BeginMenu( "Select Env Probe" ) )
            {
                constexpr U8 MaxProbesPerPage = 32;
                bool debuggingSkyLight = SceneEnvironmentProbePool::DebuggingSkyLight();

                const auto PrintProbeEntry = [&envProbPool, debuggingSkyLight]( const EnvironmentProbeList& probes, const size_t j )
                {
                    EnvironmentProbeComponent* crtProbe = probes[j];
                    bool selected = !debuggingSkyLight &&
                        envProbPool->debugProbe() == crtProbe;
                    if ( ImGui::MenuItem( crtProbe->parentSGN()->name().c_str(), "", &selected ) )
                    {
                        envProbPool->debugProbe( selected ? crtProbe : nullptr );
                        SceneEnvironmentProbePool::DebuggingSkyLight( false );
                    }
                };

                if ( ImGui::MenuItem( "Sky Light", "", &debuggingSkyLight ) )
                {
                    envProbPool->debugProbe( nullptr );
                    SceneEnvironmentProbePool::DebuggingSkyLight( debuggingSkyLight );
                }

                envProbPool->lockProbeList();
                const EnvironmentProbeList& probes = envProbPool->getLocked();
                const size_t probeCount = probes.size();
                if ( probeCount > MaxProbesPerPage )
                {
                    const size_t pageCount = probeCount > MaxProbesPerPage ? probeCount / MaxProbesPerPage : 1;
                    const size_t remainder = probeCount > MaxProbesPerPage ? probeCount - pageCount * MaxProbesPerPage : 0;
                    for ( U8 p = 0; p < pageCount + 1; ++p )
                    {
                        const size_t start = p * MaxProbesPerPage;
                        const size_t end = start + (p < pageCount ? MaxProbesPerPage : remainder);
                        if ( ImGui::BeginMenu( Util::StringFormat( "{} - {}", start, end ).c_str() ) )
                        {
                            for ( size_t j = start; j < end; ++j )
                            {
                                PrintProbeEntry( probes, j );
                            }
                            ImGui::EndMenu();
                        }
                    }
                }
                else
                {
                    for ( size_t j = 0; j < probeCount; ++j )
                    {
                        PrintProbeEntry( probes, j );
                    }
                }
                envProbPool->unlockProbeList();
                ImGui::EndMenu();
            }

            if ( ImGui::BeginMenu( "Select Debug light" ) )
            {
                constexpr U8 MaxLightsPerPage = 32;
                const auto PrintLightEntry = [&pool]( const LightPool::LightList& lights, const size_t j )
                {
                    Light* crtLight = lights[j];
                    bool selected = pool.debugLight() == crtLight;
                    if ( ImGui::MenuItem( crtLight->sgn()->name().c_str(), "", &selected ) )
                    {
                        pool.debugLight( crtLight );
                    }
                };

                for ( U8 i = 0; i < to_U8( LightType::COUNT ); ++i )
                {
                    const LightType type = static_cast<LightType>(i);
                    const LightPool::LightList& lights = pool.getLights( type );
                    if ( !lights.empty() )
                    {
                        const size_t lightCount = lights.size();

                        if ( ImGui::BeginMenu( TypeUtil::LightTypeToString( type ) ) )
                        {
                            if ( lightCount > MaxLightsPerPage )
                            {
                                const size_t pageCount = lightCount > MaxLightsPerPage ? lightCount / MaxLightsPerPage : 1;
                                const size_t remainder = lightCount > MaxLightsPerPage ? lightCount - pageCount * MaxLightsPerPage : 0;
                                for ( U8 p = 0; p < pageCount + 1; ++p )
                                {
                                    const size_t start = p * MaxLightsPerPage;
                                    const size_t end = start + (p < pageCount ? MaxLightsPerPage : remainder);
                                    if ( ImGui::BeginMenu( Util::StringFormat( "{} - {}", start, end ).c_str() ) )
                                    {
                                        for ( size_t j = start; j < end; ++j )
                                        {
                                            PrintLightEntry( lights, j );
                                        }
                                        ImGui::EndMenu();
                                    }
                                }
                            }
                            else
                            {
                                for ( size_t j = 0; j < lightCount; ++j )
                                {
                                    PrintLightEntry( lights, j );
                                }
                            }

                            ImGui::EndMenu();
                        }
                    }
                    else
                    {
                        ImGui::Text( TypeUtil::LightTypeToString( type ) );
                    }
                }
                if ( ImGui::BeginMenu( "Shadow enabled" ) )
                {
                    for ( U8 i = 0; i < to_U8( LightType::COUNT ); ++i )
                    {
                        const LightType type = static_cast<LightType>(i);
                        const LightPool::LightList& lights = pool.getLights( type );
                        for ( Light* crtLight : lights )
                        {
                            if ( crtLight->castsShadows() )
                            {
                                bool selected = pool.debugLight() == crtLight;
                                if ( ImGui::MenuItem( crtLight->sgn()->name().c_str(), "", &selected ) )
                                {
                                    pool.debugLight( crtLight );
                                }
                            }
                        }
                    }
                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }

            bool lightImpostors = pool.lightImpostorsEnabled();
            if ( ImGui::MenuItem( "Draw Light Impostors", "", &lightImpostors ) )
            {
                pool.lightImpostorsEnabled( lightImpostors );
            }

            const ECSManager& ecsManager = _context.kernel().projectManager()->activeProject()->getActiveScene()->sceneGraph()->GetECSManager();
            bool playAnimations = ecsManager.ecsEngine().GetSystemManager()->GetSystem<AnimationSystem>()->getAnimationState();

            if ( ImGui::MenuItem( "Play animations", "", &playAnimations ) )
            {
                ecsManager.ecsEngine().GetSystemManager()->GetSystem<AnimationSystem>()->toggleAnimationState( playAnimations );
            }

            if ( ImGui::BeginMenu( "Debug Gizmos" ) )
            {
                ProjectManager* projectManager = context().kernel().projectManager().get();
                SceneRenderState& renderState = projectManager->activeProject()->getActiveScene()->state()->renderState();

                bool temp = renderState.isEnabledOption( SceneRenderState::RenderOptions::RENDER_AABB );
                if ( ImGui::MenuItem( "Show AABBs", "", &temp ) )
                {
                    Console::d_printfn( LOCALE_STR( "TOGGLE_SCENE_BOUNDING_BOXES" ), temp ? "On" : "Off" );
                    renderState.toggleOption( SceneRenderState::RenderOptions::RENDER_AABB, temp );
                }
                temp = renderState.isEnabledOption( SceneRenderState::RenderOptions::RENDER_OBB );
                if ( ImGui::MenuItem( "Show OBBs", "", &temp ) )
                {
                    Console::d_printfn( LOCALE_STR( "TOGGLE_SCENE_ORIENTED_BOUNDING_BOXES" ), temp ? "On" : "Off" );
                    renderState.toggleOption( SceneRenderState::RenderOptions::RENDER_OBB, temp );
                }
                temp = renderState.isEnabledOption( SceneRenderState::RenderOptions::RENDER_BSPHERES );
                if ( ImGui::MenuItem( "Show bounding spheres", "", &temp ) )
                {
                    Console::d_printfn( LOCALE_STR( "TOGGLE_SCENE_BOUNDING_SPHERES" ), temp ? "On" : "Off" );
                    renderState.toggleOption( SceneRenderState::RenderOptions::RENDER_BSPHERES, temp );
                }
                temp = renderState.isEnabledOption( SceneRenderState::RenderOptions::RENDER_SKELETONS );
                if ( ImGui::MenuItem( "Show skeletons", "", &temp ) )
                {
                    Console::d_printfn( LOCALE_STR( "TOGGLE_SCENE_SKELETONS" ), temp ? "On" : "Off" );
                    renderState.toggleOption( SceneRenderState::RenderOptions::RENDER_SKELETONS, temp );
                }
                temp = Attorney::EditorGeneralWidget::getSceneGizmoEnabled( _context.editor() );
                if ( ImGui::MenuItem( "Show scene axis", "", &temp ) )
                {
                    Console::d_printfn( LOCALE_STR( "TOGGLE_SCENE_AXIS_GIZMO" ) );
                    Attorney::EditorGeneralWidget::setSceneGizmoEnabled( _context.editor(), temp );
                }
                temp = renderState.isEnabledOption( SceneRenderState::RenderOptions::SELECTION_GIZMO );
                if ( ImGui::MenuItem( "Show selection axis", "", &temp ) )
                {
                    renderState.toggleOption( SceneRenderState::RenderOptions::SELECTION_GIZMO, temp );
                }
                temp = renderState.isEnabledOption( SceneRenderState::RenderOptions::ALL_GIZMOS );
                if ( ImGui::MenuItem( "Show all axis", "", &temp ) )
                {
                    renderState.toggleOption( SceneRenderState::RenderOptions::ALL_GIZMOS, temp );
                }
                temp = renderState.isEnabledOption( SceneRenderState::RenderOptions::RENDER_GEOMETRY );
                if ( ImGui::MenuItem( "Render geometry", "", &temp ) )
                {
                    renderState.toggleOption( SceneRenderState::RenderOptions::RENDER_GEOMETRY, temp );
                }
                temp = renderState.isEnabledOption( SceneRenderState::RenderOptions::RENDER_WIREFRAME );
                if ( ImGui::MenuItem( "Render wireframe", "", &temp ) )
                {
                    renderState.toggleOption( SceneRenderState::RenderOptions::RENDER_WIREFRAME, temp );
                }
                temp = renderState.isEnabledOption( SceneRenderState::RenderOptions::RENDER_CUSTOM_PRIMITIVES );
                if ( ImGui::MenuItem( "Render custom gismos", "", &temp ) )
                {
                    renderState.toggleOption( SceneRenderState::RenderOptions::RENDER_CUSTOM_PRIMITIVES, temp );
                }
                ImGui::EndMenu();
            }

            if ( ImGui::BeginMenu( "Edge Detection Method" ) )
            {

                PreRenderBatch& batch = _context.gfx().getRenderer().postFX().getFilterBatch();
                bool noneSelected = batch.edgeDetectionMethod() == PreRenderBatch::EdgeDetectionMethod::COUNT;
                if ( ImGui::MenuItem( "None", "", &noneSelected ) )
                {
                    batch.edgeDetectionMethod( PreRenderBatch::EdgeDetectionMethod::COUNT );
                }

                for ( U8 i = 0; i < to_U8( PreRenderBatch::EdgeDetectionMethod::COUNT ) + 1; ++i )
                {
                    const PreRenderBatch::EdgeDetectionMethod method = static_cast<PreRenderBatch::EdgeDetectionMethod>(i);

                    bool selected = batch.edgeDetectionMethod() == method;
                    if ( ImGui::MenuItem( EdgeMethodName( method ), "", &selected ) )
                    {
                        batch.edgeDetectionMethod( method );
                    }
                }

                ImGui::EndMenu();
            }

            if ( ImGui::BeginMenu( "Debug Views" ) )
            {
                vector<std::tuple<string, I16, I16, bool>> viewNames = {};
                _context.gfx().getDebugViewNames( viewNames );

                eastl::set<I16> groups = {};
                for ( auto [name, groupID, index, enabled] : viewNames )
                {
                    if ( groupID != -1 )
                    {
                        groups.insert( groupID );
                    }

                    const string label = groupID == -1 ? name : Util::StringFormat( "({}) {}", groupID, name.c_str() );
                    if ( ImGui::MenuItem( label.c_str(), "", &enabled ) )
                    {
                        _context.gfx().toggleDebugView( index, enabled );
                    }
                }
                if ( !groups.empty() )
                {
                    ImGui::Separator();
                    for ( const I16 group : groups )
                    {
                        bool groupEnabled = _context.gfx().getDebugGroupState( group );
                        if ( ImGui::MenuItem( Util::StringFormat( "Enable Group [ {} ]", group ).c_str(), "", &groupEnabled ) )
                        {
                            _context.gfx().toggleDebugGroup( group, groupEnabled );
                        }
                    }
                }
                ImGui::EndMenu();
            }

            bool state = context().gui().showDebugCursor();
            if ( ImGui::MenuItem( "Show CEGUI Debug Cursor", "", &state ) )
            {
                context().gui().showDebugCursor( state );
            }

            ImGui::EndMenu();
        }
    }

    void MenuBar::drawHelpMenu( [[maybe_unused]] const bool modifierPressed ) const
    {
        if ( ImGui::BeginMenu( "Help" ) )
        {
            bool& sampleWindowEnabled = Attorney::EditorMenuBar::sampleWindowEnabled( _context.editor() );
            if ( ImGui::MenuItem( "Sample Window", nullptr, &sampleWindowEnabled ) )
            {

            }
            ImGui::Separator();

            ImGui::Text( "Copyright(c) 2018 DIVIDE - Studio" );
            ImGui::Text( "Copyright(c) 2009 Ionut Cava" );
            ImGui::EndMenu();
        }
    }

    void MenuBar::spawnDebugObject( const DebugObject object, const bool modifierPressed ) const
    {
        if ( object == DebugObject::SPONZA)
        {
            ResourceDescriptor<Mesh> model( "Debug_Sponza" );
            model.assetLocation( Paths::g_modelsLocation );
            model.assetName( "sponza.obj" );
            model.flag( true );
            //model.waitForReady(true);

            Handle<Mesh> spawnMesh = CreateResource( model );
            if ( spawnMesh == INVALID_HANDLE<Mesh> )
            {
                Attorney::EditorGeneralWidget::showStatusMessage( _context.editor(), "ERROR: Couldn't load Sponza model!", Time::SecondsToMilliseconds<F32>( 3 ), true );
            }

            if ( Attorney::EditorGeneralWidget::modalModelSpawn( _context.editor(), spawnMesh, !modifierPressed, float3{0.1f}, VECTOR3_ZERO ) )
            {
            }
        }
        else
        {
            Attorney::EditorGeneralWidget::showStatusMessage( _context.editor(), "ERROR: Only Sponza model supported for nowl!", Time::SecondsToMilliseconds<F32>( 3 ), true );
        }
    }
} //namespace Divide
