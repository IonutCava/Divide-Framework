

#include "Headers/Gizmo.h"
#include "Editor/Headers/Utils.h"
#include "Editor/Headers/Editor.h"
#include "Managers/Headers/ProjectManager.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "Rendering/Camera/Headers/Camera.h"

#include <imgui_internal.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4505) //Unreferenced local function has been removed
#endif
#include <ImGuizmo.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace Divide
{
    namespace
    {
        constexpr U8 g_maxSelectedNodes = 12;
        using TransformCache = std::array<TransformValues, g_maxSelectedNodes + 1>;
        using NodeCache = std::array<SceneGraphNode*, g_maxSelectedNodes + 1>;

        TransformCache g_transformCache;
        NodeCache g_selectedNodesCache;
        UndoEntry<std::pair<TransformCache, NodeCache>> g_undoEntry;
    }

    Gizmo::Gizmo( Editor& parent, ImGuiContext* targetContext, const F32 clipSpaceSize )
        : _parent( parent )
        , _imguiContext( targetContext )
        , _clipSpaceSize(clipSpaceSize)
    {
        g_undoEntry._name = "Gizmo Manipulation";
        _selectedNodes.reserve( g_maxSelectedNodes );

        ScopedImGuiContext ctx(_imguiContext);
        ImGuizmo::SetGizmoSizeClipSpace(_clipSpaceSize);
    }

    Gizmo::~Gizmo()
    {
        _imguiContext = nullptr;
    }

    ImGuiContext& Gizmo::getContext() noexcept
    {
        assert( _imguiContext != nullptr );
        return *_imguiContext;
    }

    const ImGuiContext& Gizmo::getContext() const noexcept
    {
        assert( _imguiContext != nullptr );
        return *_imguiContext;
    }

    void Gizmo::enable( const bool state ) noexcept
    {
        _enabled = state;
    }

    void Gizmo::update( [[maybe_unused]] const U64 deltaTimeUS )
    {
        if ( _shouldRegisterUndo )
        {
            _parent.registerUndoEntry( g_undoEntry );
            _shouldRegisterUndo = false;
        }
    }

    void Gizmo::onResolutionChange(const SizeChangeParams& params) noexcept
    {
        onWindowSizeChange(params);
    }

    void Gizmo::onWindowSizeChange([[maybe_unused]] const SizeChangeParams& params) noexcept
    {
        const DisplayWindow* mainWindow = static_cast<DisplayWindow*>(ImGui::GetMainViewport()->PlatformHandle);
        const vec2<U16> size = mainWindow->getDrawableSize();

        ScopedImGuiContext ctx(_imguiContext);
        ImGuizmo::SetRect(0.f, 0.f, to_F32(size.width), to_F32(size.height));
    }

    void Gizmo::render( const Camera* camera, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        const auto GetSnapValues = []( const TransformSettings& settings ) -> const F32*
        {
            if ( settings.useSnap )
            {
                if ( IsTranslationOperation( settings ) )
                {
                    return &settings.snapTranslation[0];
                }
                else if ( IsRotationOperation( settings ) )
                {
                    return &settings.snapRotation[0];
                }
                else if ( IsScaleOperation( settings ) )
                {
                    return &settings.snapScale[0];
                }
            }

            return nullptr;
        };

        if ( !isActive() || _selectedNodes.empty() )
        {
            return;
        }

        ScopedImGuiContext ctx(_imguiContext);
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        const F32* viewMatrix = camera->viewMatrix();
        const F32* projectionMatrix = camera->projectionMatrix();

        const bool dirty = Manipulate( viewMatrix,
                                       projectionMatrix,
                                       _transformSettings.currentGizmoOperation,
                                       _transformSettings.currentGizmoMode,
                                       _workMatrix,
                                       _deltaMatrix,
                                       GetSnapValues( _transformSettings ) );

        if ( dirty && ImGuizmo::IsUsing() )
        {
            ImGuizmo::DrawGrid(viewMatrix, projectionMatrix, _gridMatrix, 100.f);

            if ( _selectedNodes.size() == 1 )
            {
                renderSingleSelection();
            }
            else
            {
                renderMultipleSelections();
            }

        }

        ImGui::Render();
        Attorney::EditorGizmo::renderDrawList( _parent, ImGui::GetDrawData(), 1, targetViewport, bufferInOut, memCmdInOut);
    }

    void Gizmo::applyTransforms( const SelectedNode& node, const vec3<F32>& position, const vec3<Angle::DEGREES_F>& euler, const vec3<F32>& scale )
    {
        bool updateGridMatrix = false;

        switch ( _transformSettings.currentGizmoOperation )
        {
            case ImGuizmo::TRANSLATE:   node.tComp->translate( position );    updateGridMatrix = true; break;
            case ImGuizmo::TRANSLATE_X: node.tComp->translateX( position.x ); updateGridMatrix = true; break;
            case ImGuizmo::TRANSLATE_Y: node.tComp->translateY( position.y ); updateGridMatrix = true; break;
            case ImGuizmo::TRANSLATE_Z: node.tComp->translateZ( position.z ); updateGridMatrix = true; break;
            case ImGuizmo::SCALE:   node.tComp->scale( Max( scale, vec3<F32>( EPSILON_F32 ) ) ); break;
            case ImGuizmo::SCALE_XU:
            case ImGuizmo::SCALE_X: node.tComp->scaleX( std::max( scale.x, EPSILON_F32 ) ); break;
            case ImGuizmo::SCALE_YU:
            case ImGuizmo::SCALE_Y: node.tComp->scaleY( std::max( scale.y, EPSILON_F32 ) ); break;
            case ImGuizmo::SCALE_ZU:
            case ImGuizmo::SCALE_Z:  node.tComp->scaleZ( std::max( scale.z, EPSILON_F32 ) ); break;
            case ImGuizmo::ROTATE:   node.tComp->rotate( -euler );    updateGridMatrix = true; break;
            case ImGuizmo::ROTATE_X: node.tComp->rotateX( -euler.x ); updateGridMatrix = true; break;
            case ImGuizmo::ROTATE_Y: node.tComp->rotateY( -euler.y ); updateGridMatrix = true; break;
            case ImGuizmo::ROTATE_Z: node.tComp->rotateZ( -euler.z ); updateGridMatrix = true; break;

            case ImGuizmo::BOUNDS: break;
            case ImGuizmo::ROTATE_SCREEN: break;
        }

        if (updateGridMatrix)
        {
            _gridMatrix.fromEuler(node.tComp->getWorldOrientation().getEuler());
            _gridMatrix.setTranslation(node.tComp->getWorldPosition());
        }
    }

    void Gizmo::renderSingleSelection()
    {
        const SelectedNode& node = _selectedNodes.front();
        if ( !_wasUsed )
        {
            g_transformCache.back() = node._initialValues;
            g_selectedNodesCache.back() = node.tComp->parentSGN();
            g_undoEntry._oldVal = { g_transformCache, g_selectedNodesCache };
        }

        static vec3<F32> position, euler, scale;
        ImGuizmo::DecomposeMatrixToComponents( _deltaMatrix, position._v, euler._v, scale._v );

        position = (_localToWorldMatrix * vec4<F32>( position, 1.f )).xyz;

        applyTransforms( node, position, euler, scale );

        g_transformCache.back() = node.tComp->getLocalValues();
        _wasUsed = true;

        g_undoEntry._newVal = { g_transformCache, g_selectedNodesCache };
        g_undoEntry._dataSetter = []( const std::pair<TransformCache, NodeCache>& data )
        {
            const SceneGraphNode* node = data.second.back();
            assert( node != nullptr );
            TransformComponent* tComp = node->get<TransformComponent>();
            tComp->setTransform( data.first.back() );
        };
    }

    void Gizmo::renderMultipleSelections()
    {
        //ToDo: This is still very buggy! -Ionut
        assert( _selectedNodes.front().tComp != nullptr );

        if ( !_wasUsed )
        {
            U32 selectionCounter = 0u;
            for ( const SelectedNode& node : _selectedNodes )
            {
                g_transformCache[selectionCounter] = node._initialValues;
                g_selectedNodesCache[selectionCounter] = node.tComp->parentSGN();
                if ( ++selectionCounter == g_maxSelectedNodes )
                {
                    break;
                }
            }

            g_undoEntry._oldVal = { g_transformCache, g_selectedNodesCache };
        }

        static vec3<F32> position, euler, scale;
        ImGuizmo::DecomposeMatrixToComponents( _deltaMatrix, position._v, euler._v, scale._v );
        position = (_localToWorldMatrix * vec4<F32>( position, 1.f )).xyz;

        U32 selectionCounter = 0u;
        for ( const SelectedNode& node : _selectedNodes )
        {
            applyTransforms( node, position, euler, scale );

            g_transformCache[selectionCounter] = node.tComp->getLocalValues();
            if ( ++selectionCounter == g_maxSelectedNodes )
            {
                break;
            }
        }
        _wasUsed = true;


        g_undoEntry._newVal = { g_transformCache, g_selectedNodesCache };
        g_undoEntry._dataSetter = []( const std::pair<TransformCache, NodeCache>& data )
        {
            for ( U32 i = 0u; i < g_maxSelectedNodes; ++i )
            {
                const SceneGraphNode* node = data.second[i];
                if ( node != nullptr )
                {
                    TransformComponent* tComp = node->get<TransformComponent>();
                    if ( tComp != nullptr )
                    {
                        tComp->setTransform( data.first[i] );
                    }
                }
            }
        };
    }

    void Gizmo::updateSelections()
    {
        updateSelectionsInternal();
    }

    void Gizmo::updateSelections( const vector<SceneGraphNode*>& nodes )
    {
        _selectedNodes.resize( 0 );

        U32 selectionCounter = 0u;
        for ( const SceneGraphNode* node : nodes )
        {
            TransformComponent* tComp = node->get<TransformComponent>();
            if ( tComp != nullptr )
            {
                _selectedNodes.push_back( SelectedNode{ tComp, tComp->getLocalValues() } );
                if ( ++selectionCounter == g_maxSelectedNodes )
                {
                    break;
                }
            }
        }
        updateSelectionsInternal();
    }

    void Gizmo::updateSelectionsInternal()
    {
        _gridMatrix.identity();
        _workMatrix.identity();
        _localToWorldMatrix.identity();
        _deltaMatrix.identity();

        if (_selectedNodes.size() == 1u )
        {
            const TransformComponent* tComp = _selectedNodes.front().tComp;
            _workMatrix = tComp->getWorldMatrix();
            SceneGraphNode* parent = tComp->parentSGN()->parent();
            if ( parent != nullptr )
            {
                parent->get<TransformComponent>()->getWorldMatrix().getInverse( _localToWorldMatrix );
            }
            const BoundsComponent* bComp = tComp->parentSGN()->get<BoundsComponent>();
            if ( bComp != nullptr )
            {
                const vec3<F32>& boundsCenter = bComp->getBoundingSphere().getCenter();
                _workMatrix.setTranslation(boundsCenter);
            }
        }
        else
        {
            BoundingBox nodesBB = {};
            for ( const SelectedNode& node : _selectedNodes )
            {
                const TransformComponent* tComp = node.tComp;
                const BoundsComponent* bComp = tComp->parentSGN()->get<BoundsComponent>();
                if ( bComp != nullptr )
                {
                    nodesBB.add( bComp->getBoundingSphere().getCenter() );
                }
                else
                {
                    nodesBB.add( tComp->getWorldPosition() );
                }
            }

            _workMatrix.setScale( VECTOR3_UNIT );
            _workMatrix.setTranslation( nodesBB.getCenter() );
        }
    }

    void Gizmo::setTransformSettings( const TransformSettings& settings ) noexcept
    {
        _transformSettings = settings;
    }

    const TransformSettings& Gizmo::getTransformSettings() const noexcept
    {
        return _transformSettings;
    }

    void Gizmo::onSceneFocus( [[maybe_unused]] const bool state ) noexcept
    {
        ImGuiIO& io = _imguiContext->IO;
        _wasUsed = false;
        io.KeyCtrl = io.KeyShift = io.KeyAlt = io.KeySuper = false;
    }

    void Gizmo::onKeyInternal(const Input::KeyEvent& arg, bool pressed)
    {
        ImGuiIO& io = _imguiContext->IO;

        if (arg._key == Input::KeyCode::KC_LCONTROL || arg._key == Input::KeyCode::KC_RCONTROL)
        {
            io.AddKeyEvent(ImGuiMod_Ctrl, pressed);
        }
        if (arg._key == Input::KeyCode::KC_LSHIFT || arg._key == Input::KeyCode::KC_RSHIFT)
        {
            io.AddKeyEvent(ImGuiMod_Shift, pressed);
        }
        if (arg._key == Input::KeyCode::KC_LMENU || arg._key == Input::KeyCode::KC_RMENU)
        {
            io.AddKeyEvent(ImGuiMod_Alt, pressed);
        }
        if (arg._key == Input::KeyCode::KC_LWIN || arg._key == Input::KeyCode::KC_RWIN)
        {
            io.AddKeyEvent(ImGuiMod_Super, pressed);
        }
        const ImGuiKey imguiKey = DivideKeyToImGuiKey(arg._key);
        io.AddKeyEvent(imguiKey, pressed);
        io.SetKeyEventNativeData(imguiKey, arg.sym, arg.scancode, arg.scancode);
    }

    bool Gizmo::onKeyDown(Input::KeyEvent& argInOut)
    {
        _wasUsed = false;
        if (!isActive())
        {
            return false;
        }

        onKeyInternal(argInOut, true);
        return false;
    }

    bool Gizmo::onKeyUp(Input::KeyEvent& argInOut)
    {
        if (_wasUsed)
        {
            _shouldRegisterUndo = true;
            _wasUsed = false;
        }

        if (!isActive())
        {
            return false;
        }

        onKeyInternal(argInOut, false);

        bool ret = false;
        if ( _imguiContext->IO.KeyCtrl )
        {
            TransformSettings settings = _parent.getTransformSettings();
            if (argInOut._key == Input::KeyCode::KC_T )
            {
                settings.currentGizmoOperation = ImGuizmo::TRANSLATE;
                ret = true;
            }
            else if (argInOut._key == Input::KeyCode::KC_R )
            {
                settings.currentGizmoOperation = ImGuizmo::ROTATE;
                ret = true;
            }
            else if (argInOut._key == Input::KeyCode::KC_S )
            {
                settings.currentGizmoOperation = ImGuizmo::SCALE;
                ret = true;
            }
            if ( ret )
            {
                _parent.setTransformSettings( settings );
            }
        }

        return ret;
    }

    bool Gizmo::onMouseButtonPressed(Input::MouseButtonEvent& argInOut) noexcept
    {
        _wasUsed = false;
        if ( isActive() )
        {
            ScopedImGuiContext ctx(_imguiContext);
            return ImGuizmo::IsOver();
        }

        return false;
    }

    bool Gizmo::onMouseButtonReleased(Input::MouseButtonEvent& argInOut) noexcept
    {
        if (_wasUsed)
        {
            _shouldRegisterUndo = true;
            _wasUsed = false;
            return true;
        }

        return false;
    }

    bool Gizmo::needsMouse() const
    {
        if ( isActive() )
        {
            ScopedImGuiContext ctx(_imguiContext);
            return ImGuizmo::IsUsingAny() || ImGuizmo::IsOver();
        }

        return false;
    }

    bool Gizmo::isHovered() const noexcept
    {
        if ( isActive() )
        {
            ScopedImGuiContext ctx(_imguiContext);
            return ImGuizmo::IsOver();
        }

        return false;
    }

    bool Gizmo::isUsing() const noexcept
    {
        if (isActive())
        {
            ScopedImGuiContext ctx(_imguiContext);
            return ImGuizmo::IsUsingAny();
        }

        return false;
    }

    bool Gizmo::isEnabled() const noexcept
    {
        return _enabled;
    }

    bool Gizmo::isActive() const noexcept
    {
        return isEnabled() && !_selectedNodes.empty();
    }

} //namespace Divide
