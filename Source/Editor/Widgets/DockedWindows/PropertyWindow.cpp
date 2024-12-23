

#include "Editor/Headers/UndoManager.h"
#include "Editor/Headers/Utils.h"
#include "Headers/PropertyWindow.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Editor/Headers/Editor.h"
#include "Geometry/Material/Headers/Material.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Graphs/Headers/SceneNode.h"
#include "Managers/Headers/ProjectManager.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Rendering/Camera/Headers/Camera.h"

#include "ECS/Components/Headers/DirectionalLightComponent.h"
#include "ECS/Components/Headers/EnvironmentProbeComponent.h"
#include "ECS/Components/Headers/PointLightComponent.h"
#include "ECS/Components/Headers/SpotLightComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"

#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"
#include "Core/Math/BoundingVolumes/Headers/OBB.h"
#include "Editor/Widgets/Headers/ImGuiExtensions.h"

#include <IconsForkAwesome.h>
#include <imgui_internal.h>

DISABLE_MSVC_WARNING_PUSH(4127) // conditional expression is constant

namespace Divide
{
    namespace
    {
        hashMap<U64, std::tuple<Frustum, FColour3, bool>> g_debugFrustums;
        std::array<U64, 1024> s_openProperties = {};

        // Separate activate is used for stuff that does continuous value changes, e.g. colour selectors, but you only want to register the old val once
        template<typename T, bool SeparateActivate, typename Pred>
        void RegisterUndo( Editor& editor, PushConstantType Type, const T& oldVal, const T& newVal, const char* name, Pred&& dataSetter )
        {
            UndoEntry<T> undo = {};
            if ( !SeparateActivate || ImGui::IsItemActivated() )
            {
                undo._oldVal = oldVal;
            }

            if ( ImGui::IsItemDeactivatedAfterEdit() )
            {
                undo._type = Type;
                undo._name = name;
                undo._newVal = newVal;
                undo._dataSetter = dataSetter;
                editor.registerUndoEntry( undo );
            }
        }

        bool IsRequiredComponentType( SceneGraphNode* selection, const ComponentType componentType )
        {
            if ( selection != nullptr )
            {
                return selection->getNode().requiredComponentMask() & to_U32( componentType );
            }

            return false;
        }

        template<typename Pred>
        void ApplyToMaterials( const Material& baseMaterial, Material* instanceRoot, Pred&& predicate )
        {
            if ( instanceRoot != nullptr )
            {
                predicate( baseMaterial, instanceRoot );

                //ToDo: Not thread safe 
                instanceRoot->lockInstancesForRead();
                const auto& instances = instanceRoot->getInstancesLocked();
                for ( Handle<Material> mat : instances )
                {
                    ApplyToMaterials( baseMaterial, Get(mat), predicate );
                }
                instanceRoot->unlockInstancesForRead();
            }
        }

        template<typename Pred>
        void ApplyAllButton( I32& id, const bool readOnly, const Material& material, Pred&& predicate )
        {
            ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - 40 );
            ImGui::PushID( 4321234 + id++ );
            if ( readOnly )
            {
                PushReadOnly();
            }
            if ( ImGui::SmallButton( "A" ) )
            {
                ApplyToMaterials( material, Get(material.baseMaterial()), MOV( predicate ) );
            }
            if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
            {
                ImGui::SetTooltip( "Apply to all instances" );
            }
            if ( readOnly )
            {
                PopReadOnly();
            }
            ImGui::PopID();
        }

        bool PreviewTextureButton( I32& id, const Handle<Texture> tex, const bool readOnly )
        {
            bool ret = false;
            ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - 15 );
            ImGui::PushID( 4321234 + id++ );
            if ( readOnly )
            {
                PushReadOnly();
            }
            if ( ImGui::SmallButton( "T" ) )
            {
                ret = true;
            }
            if ( tex != INVALID_HANDLE<Texture> && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
            {
                ImGui::SetTooltip( Util::StringFormat( "Preview texture : {}", Get(tex)->assetName()).c_str() );
            }
            if ( readOnly )
            {
                PopReadOnly();
            }
            ImGui::PopID();
            return ret;
        }
    }

    PropertyWindow::PropertyWindow( Editor& parent, PlatformContext& context, const Descriptor& descriptor )
        : DockedWindow( parent, descriptor )
        , PlatformContextComponent( context )
    {
    }

    void PropertyWindow::onRemoveComponent( const EditorComponent& comp )
    {
        if ( _lockedComponent._editorComp == nullptr )
        {
            return;
        }

        if ( comp.getGUID() == _lockedComponent._editorComp->getGUID() )
        {
            _lockedComponent = { nullptr, nullptr };
        }
    }

    bool PropertyWindow::drawCamera( Camera* cam )
    {
        bool sceneChanged = false;
        if ( cam == nullptr )
        {
            return false;
        }

        const char* camName = cam->resourceName().c_str();
        const U64 camID = _ID( camName );
        ImGui::PushID( to_I32( camID ) * 54321 );

        if ( ImGui::CollapsingHeader( camName, ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            const char* modeStr = TypeUtil::CameraModeToString( cam->mode() );
            ImGui::PushID( modeStr );
            ImGui::LabelText( "", "Camera Mode: [ %s ]", modeStr );
            ImGui::PopID();

            ImGui::Separator();
            Util::PushNarrowLabelWidth();
            {
                constexpr const char* CamMoveLabels[] = {
                       "R", "U", "F"
                };


                float3 eye = cam->snapshot()._eye;
                EditorComponentField camField = {};
                camField._name = "Eye";
                camField._labels = CamMoveLabels;
                camField._basicType = PushConstantType::VEC3;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = eye._v;
                camField._dataSetter = [cam]( const void* val, [[maybe_unused]] void* user_data) noexcept
                {
                    cam->setEye( *static_cast<const float3*>(val) );
                };
                sceneChanged = processField( camField ) || sceneChanged;
            }
            {
                constexpr const char* CamRotateLabels[] = {
                    "P", "Y", "R"
                };

                vec3<Angle::DEGREES_F> euler = cam->euler();
                EditorComponentField camField = {};
                camField._name = "Euler";
                camField._labels = CamRotateLabels;
                camField._tooltip = "Change camera orientation using euler angles( degrees).\nP = Pitch, Y = Yaw, R = Roll";
                camField._basicType = PushConstantType::VEC3;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = euler._v;
                camField._dataSetter = [cam]( const void* e, [[maybe_unused]] void* user_data) noexcept
                {
                    cam->setRotation( *static_cast<const vec3<Angle::DEGREES_F>*>(e) );
                };
                sceneChanged = processField( camField ) || sceneChanged;
            }
            {
                constexpr const char* CamSpeedLabels[] = {
                    "T", "M", "Z"
                };

                float3 speed = cam->speedFactor();

                EditorComponentField camField = {};
                camField._name = "Speed";
                camField._range = { 0.001f, std::max(Camera::MAX_CAMERA_MOVE_SPEED, Camera::MAX_CAMERA_TURN_SPEED)};
                camField._labels = CamSpeedLabels;
                camField._basicType = PushConstantType::VEC3;
                camField._type = EditorComponentFieldType::SLIDER_TYPE;
                camField._readOnly = false;
                camField._tooltip = "Change camera speed factor. (units / second) \nT = Turn speed, M = Move speed, Z = Zoom speed";
                camField._data = speed._v;
                camField._resetValue = 5.f;
                camField._dataSetter = [cam]( const void* e, [[maybe_unused]] void* user_data) noexcept
                {
                    const float3 speed = *static_cast<const float3*>(e);
                    cam->speedFactor( speed );
                };
                sceneChanged = processField( camField ) || sceneChanged;
            }
            {
                float3 fwd = cam->viewMatrix().getForwardDirection();
                EditorComponentField camField = {};
                camField._name = "Forward";
                camField._basicType = PushConstantType::VEC3;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = true;
                camField._data = fwd._v;
                sceneChanged = processField( camField ) || sceneChanged;
            }
            {
                F32 aspect = cam->snapshot()._aspectRatio;
                EditorComponentField camField = {};
                camField._name = "Aspect";
                camField._basicType = PushConstantType::FLOAT;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = &aspect;
                camField._dataSetter = [cam]( const void* a, [[maybe_unused]] void* user_data) noexcept
                {
                    cam->setAspectRatio( *static_cast<const F32*>(a) );
                };
                sceneChanged = processField( camField ) || sceneChanged;
            }
            {
                F32 horizontalFoV = cam->getHorizontalFoV();
                EditorComponentField camField = {};
                camField._name = "FoV (horizontal)";
                camField._basicType = PushConstantType::FLOAT;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = &horizontalFoV;
                camField._dataSetter = [cam]( const void* fov, [[maybe_unused]] void* user_data) noexcept
                {
                    cam->setHorizontalFoV( *static_cast<const F32*>(fov) );
                };
                sceneChanged = processField( camField ) || sceneChanged;
            }
            {
                float2 zPlanes = cam->snapshot()._zPlanes;
                EditorComponentField camField = {};
                camField._name = "zPlanes";
                camField._basicType = PushConstantType::VEC2;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = zPlanes._v;
                camField._dataSetter = [cam]( const void* planes, [[maybe_unused]] void* user_data)
                {
                    if ( cam->snapshot()._isOrthoCamera )
                    {
                        cam->setProjection( cam->orthoRect(), *static_cast<const float2*>(planes) );
                    }
                    else
                    {
                        cam->setProjection( cam->snapshot()._aspectRatio, cam->snapshot()._fov, *static_cast<const float2*>(planes) );
                    }
                };
                sceneChanged = processField( camField ) || sceneChanged;
            }
            if ( cam->snapshot()._isOrthoCamera )
            {
                float4 orthoRect = cam->orthoRect();
                EditorComponentField camField = {};
                camField._name = "Ortho";
                camField._basicType = PushConstantType::VEC4;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = orthoRect._v;
                camField._dataSetter = [cam]( const void* rect, [[maybe_unused]] void* user_data)
                {
                    cam->setProjection( *static_cast<const float4*>(rect), cam->snapshot()._zPlanes );
                };
                sceneChanged = processField( camField ) || sceneChanged;
            }
            {
                mat4<F32> viewMatrix = cam->viewMatrix();
                EditorComponentField worldMatrixField = {};
                worldMatrixField._name = "View Matrix";
                worldMatrixField._basicType = PushConstantType::MAT4;
                worldMatrixField._type = EditorComponentFieldType::PUSH_TYPE;
                worldMatrixField._readOnly = true;
                worldMatrixField._data = &viewMatrix;
                if ( processBasicField( worldMatrixField ) )
                {
                    // Value changed
                }
            }
            {
                mat4<F32> projMatrix = cam->projectionMatrix();
                EditorComponentField projMatrixField;
                projMatrixField._basicType = PushConstantType::MAT4;
                projMatrixField._type = EditorComponentFieldType::PUSH_TYPE;
                projMatrixField._readOnly = true;
                projMatrixField._name = "Projection Matrix";
                projMatrixField._data = &projMatrix;
                if ( processBasicField( projMatrixField ) )
                {
                    // Value changed
                }
            }
            {
                ImGui::Separator();
                bool drawFustrum = g_debugFrustums.find( camID ) != eastl::cend( g_debugFrustums );
                ImGui::PushID( to_I32( camID ) * 123456 );
                if ( ImGui::Checkbox( "Draw debug frustum", &drawFustrum ) )
                {
                    if ( drawFustrum )
                    {
                        g_debugFrustums[camID] = { cam->getFrustum(), DefaultColours::GREEN, true };
                    }
                    else
                    {
                        g_debugFrustums.erase( camID );
                    }
                }
                if ( drawFustrum )
                {
                    auto& [frust, colour, realtime] = g_debugFrustums[camID];
                    ImGui::Checkbox( "Update realtime", &realtime );
                    const bool update = realtime || ImGui::Button( "Update frustum" );
                    if ( update )
                    {
                        frust = cam->getFrustum();
                    }
                    ImGui::PushID( cam->resourceName().c_str() );
                    ImGui::ColorEdit3( "Frust Colour", colour._v, ImGuiColorEditFlags_DefaultOptions_ );
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
            {
                F32 radius = cam->maxRadius();
                EditorComponentField camField = {};
                camField._name = "MAX Radius";
                camField._basicType = PushConstantType::FLOAT;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = &radius;
                camField._dataSetter = [cam]( const void* radius, [[maybe_unused]] void* user_data) noexcept
                {
                    cam->maxRadius( *static_cast<const F32*>(radius) );
                };
                sceneChanged = processField( camField ) || sceneChanged;
            }
            {
                F32 radius = cam->maxRadius();
                EditorComponentField camField = {};
                camField._name = "MIN Radius";
                camField._basicType = PushConstantType::FLOAT;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = &radius;
                camField._dataSetter = [cam]( const void* radius, [[maybe_unused]] void* user_data) noexcept
                {
                    cam->maxRadius( *static_cast<const F32*>(radius) );
                };
                sceneChanged = processField( camField ) || sceneChanged;
            }
            {
                F32 radius = cam->curRadius();
                EditorComponentField camField = {};
                camField._name = "Current Radius";
                camField._basicType = PushConstantType::FLOAT;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = &radius;
                camField._dataSetter = [cam]( const void* radius, [[maybe_unused]] void* user_data) noexcept
                {
                    cam->curRadius( *static_cast<const F32*>(radius) );
                };
                sceneChanged = processField( camField ) || sceneChanged;
            }

            Util::PopNarrowLabelWidth();
        }
        ImGui::PopID();

        return sceneChanged;
    }

    void PropertyWindow::backgroundUpdateInternal()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        I64 guid = 12344231;
        for ( const auto& it : g_debugFrustums )
        {
            const auto& [frustum, colour, realtime] = it.second;
            IM::FrustumDescriptor descriptor;
            descriptor.frustum = frustum;
            descriptor.colour = Util::ToByteColour( colour );
            _context.gfx().debugDrawFrustum( guid++, descriptor );
        }
    }

    bool PropertyWindow::printComponent( SceneGraphNode* sgnNode, EditorComponent* comp, const F32 xOffset, const F32 smallButtonWidth )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        bool sceneChanged = false;

        bool isLockedField = false;
        bool fieldWasOpen = false;

        if ( _lockedComponent._editorComp != nullptr && _lockedComponent._editorComp->getGUID() == comp->getGUID() )
        {
            fieldWasOpen = true;
            isLockedField = true;
        }

        // always keep transforms open by default for convenience
        const bool fieldAlwaysOpen = comp->componentType() == ComponentType::TRANSFORM || comp->componentType() == ComponentType::COUNT;

        const string fieldNameStr = fieldWasOpen ? Util::StringFormat( "{} ({})", comp->name().c_str(), _lockedComponent._parentSGN->name().c_str() ) : comp->name().c_str();
        const char* fieldName = fieldNameStr.c_str();
        const U64 fieldHash = _ID( fieldName );
        if ( !isLockedField )
        {
            for ( const U64 p : s_openProperties )
            {
                if ( p == fieldHash )
                {
                    fieldWasOpen = true;
                    break;
                }
            }
        }
        if ( comp->fields().empty() )
        {
            PushReadOnly();
            ImGui::CollapsingHeader( fieldName, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet );
            PopReadOnly();
        }
        else
        {
            if ( ImGui::CollapsingHeader( fieldName, ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ((fieldWasOpen || fieldAlwaysOpen) ? ImGuiTreeNodeFlags_DefaultOpen : 0u) ) )
            {
                if ( !fieldWasOpen )
                {
                    for ( U64& p : s_openProperties )
                    {
                        if ( p == 0u )
                        {
                            p = fieldHash;
                            break;
                        }
                    }
                }

                ImGui::NewLine();
                ImGui::SameLine( xOffset );
                if ( ImGui::Button( ICON_FK_SEARCH" INSPECT", ImVec2( smallButtonWidth, 20 ) ) )
                {
                    Attorney::EditorGeneralWidget::inspectMemory( _context.editor(), std::make_pair( comp, sizeof( EditorComponent ) ) );
                }
                if ( !isLockedField && comp->componentType() != ComponentType::COUNT && !IsRequiredComponentType( sgnNode, comp->componentType() ) )
                {
                    ImGui::SameLine();
                    if ( ImGui::Button( ICON_FK_MINUS" REMOVE", ImVec2( smallButtonWidth, 20 ) ) )
                    {
                        Attorney::EditorGeneralWidget::inspectMemory( _context.editor(), std::make_pair( nullptr, 0 ) );

                        if ( Attorney::EditorGeneralWidget::removeComponent( _context.editor(), sgnNode, comp->componentType() ) )
                        {
                            sceneChanged = true;
                            return true;
                        }
                    }
                }
                ImGui::SameLine( ImGui::GetWindowSize().x - 80.f );
                bool fieldLocked = _lockedComponent._editorComp != nullptr && _lockedComponent._editorComp->getGUID() == comp->getGUID();
                ImGui::PushID( to_I32( fieldHash ) );
                if ( ImGui::Checkbox( ICON_FK_LOCK"  ", &fieldLocked ) )
                {
                    if ( !fieldLocked )
                    {
                        _lockedComponent = { nullptr, nullptr };
                    }
                    else
                    {
                        _lockedComponent = { comp, sgnNode };
                    }
                }
                ImGui::PopID();
                if ( ImGui::IsItemHovered() )
                {
                    ImGui::SetTooltip( "Always keep this component visible in the editor regardless of the currently selected scene node" );
                    skipAutoTooltip( true );
                }
                ImGui::Separator();
                vector<EditorComponentField>& fields = Attorney::EditorComponentEditor::fields( *comp );
                for ( EditorComponentField& field : fields )
                {
                    if ( processField( field ) && !field._readOnly )
                    {
                        Attorney::EditorComponentEditor::onChanged( *comp, field );
                        sceneChanged = true;
                    }
                    ImGui::Spacing();
                }
                const U32 componentMask = sgnNode->componentMask();
                if ( componentMask & to_base(ComponentType::ENVIRONMENT_PROBE ) )
                {
                    const EnvironmentProbeComponent* probe = sgnNode->get<EnvironmentProbeComponent>();
                    if ( probe != nullptr )
                    {
                       ImGui::Text("Probe ID: %d", probe->getGUID());
                    }
                }
                Light* light = nullptr;
                if ( componentMask & to_base(ComponentType::SPOT_LIGHT ) )
                {
                    light = sgnNode->get<SpotLightComponent>();
                }
                else if ( componentMask & to_base(ComponentType::POINT_LIGHT ) )
                {
                    light = sgnNode->get<PointLightComponent>();
                }
                else if ( componentMask & to_base(ComponentType::DIRECTIONAL_LIGHT ) )
                {
                    light = sgnNode->get<DirectionalLightComponent>();
                }
                if ( light != nullptr )
                {
                    if ( light->castsShadows() )
                    {
                        if ( ImGui::CollapsingHeader( "Light Shadow Settings", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth ) )
                        {
                            ImGui::Text( "Shadow Offset: %d", to_U32( light->getShadowArrayOffset() ) );

                            switch ( light->getLightType() )
                            {
                                case LightType::POINT:
                                {
                                    for ( U8 face = 0u; face < 6u; ++face )
                                    {
                                        Camera* shadowCamera = ShadowMap::shadowCameras( ShadowType::CUBEMAP )[face];
                                        if ( drawCamera( shadowCamera ) )
                                        {
                                            sceneChanged = true;
                                        }
                                    }

                                } break;

                                case LightType::SPOT:
                                {
                                    Camera* shadowCamera = ShadowMap::shadowCameras( ShadowType::SINGLE ).front();
                                    if ( drawCamera( shadowCamera ) )
                                    {
                                        sceneChanged = true;
                                    }
                                } break;

                                case LightType::DIRECTIONAL:
                                {
                                    DirectionalLightComponent* dirLight = static_cast<DirectionalLightComponent*>(light);
                                    for ( U8 split = 0u; split < dirLight->csmSplitCount(); ++split )
                                    {
                                        Camera* shadowCamera = ShadowMap::shadowCameras( ShadowType::CSM )[split];
                                        if ( drawCamera( shadowCamera ) )
                                        {
                                            sceneChanged = true;
                                        }
                                    }
                                } break;

                                default:
                                case LightType::COUNT:
                                {
                                    DIVIDE_UNEXPECTED_CALL();
                                } break;
                            }
                        }
                    }

                    if ( ImGui::CollapsingHeader( "Scene Shadow Settings", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth ) )
                    {
                        auto& projectManager = context().kernel().projectManager();
                        auto& activeSceneState = projectManager->activeProject()->getActiveScene()->state();

                        {
                            F32 bleedBias = activeSceneState->lightBleedBias();
                            EditorComponentField tempField = {};
                            tempField._name = "Light bleed bias";
                            tempField._basicType = PushConstantType::FLOAT;
                            tempField._type = EditorComponentFieldType::PUSH_TYPE;
                            tempField._readOnly = false;
                            tempField._data = &bleedBias;
                            tempField._format = "%.6f";
                            tempField._range = { 0.0f, 1.0f };
                            tempField._dataSetter = [&activeSceneState]( const void* bias, [[maybe_unused]] void* user_data) noexcept
                            {
                                activeSceneState->lightBleedBias( *static_cast<const F32*>(bias) );
                            };
                            sceneChanged = processField( tempField ) || sceneChanged;
                        }
                        {
                            F32 shadowVariance = activeSceneState->minShadowVariance();
                            EditorComponentField tempField = {};
                            tempField._name = "Minimum variance";
                            tempField._basicType = PushConstantType::FLOAT;
                            tempField._type = EditorComponentFieldType::PUSH_TYPE;
                            tempField._readOnly = false;
                            tempField._data = &shadowVariance;
                            tempField._range = { 0.00001f, 0.99999f };
                            tempField._format = "%.6f";
                            tempField._dataSetter = [&activeSceneState]( const void* variance, [[maybe_unused]] void* user_data) noexcept
                            {
                                activeSceneState->minShadowVariance( *static_cast<const F32*>(variance) );
                            };
                            sceneChanged = processField( tempField ) || sceneChanged;
                        }
                    }
                }
            }
            else
            {
                for ( U64& p : s_openProperties )
                {
                    if ( p == fieldHash )
                    {
                        p = 0u;
                        break;
                    }
                }
            }
        }
        return sceneChanged;
    }

    void PropertyWindow::drawInternal()
    {
        PROFILE_SCOPE_AUTO( Divide::Profiler::Category::GUI );

        constexpr F32 buttonWidth = 105.0f;
        constexpr F32 smallButtonWidth = 90.0f;

        skipAutoTooltip( false );

        bool sceneChanged = false;
        F32 xOffset = ImGui::GetWindowSize().x * 0.5f - (smallButtonWidth * 2);
        I64 lockedGUID = -1;
        if ( _lockedComponent._editorComp != nullptr )
        {
            lockedGUID = _lockedComponent._editorComp->getGUID();
            sceneChanged = printComponent( _lockedComponent._parentSGN, _lockedComponent._editorComp, xOffset, smallButtonWidth );
        }

        const Selections crtSelections = selections();
        const bool hasSelections = crtSelections._selectionCount > 0u;

        bool lockSolutionExplorer = false;
        Camera* selectedCamera = Attorney::EditorPropertyWindow::getSelectedCamera( _parent );
        if ( selectedCamera != nullptr )
        {
            sceneChanged = drawCamera( selectedCamera );
        }
        else if ( hasSelections )
        {
            for ( U8 i = 0u; i < crtSelections._selectionCount; ++i )
            {
                SceneGraphNode* sgnNode = node( crtSelections._selections[i] );
                if ( sgnNode != nullptr )
                {
                    ImGui::PushID( sgnNode->name().c_str() );

                    bool enabled = sgnNode->hasFlag( SceneGraphNode::Flags::ACTIVE );
                    if ( ImGui::Checkbox( Util::StringFormat( "{} {}", getIconForNode( sgnNode ), sgnNode->name().c_str() ).c_str(), &enabled ) )
                    {
                        if ( enabled )
                        {
                            sgnNode->setFlag( SceneGraphNode::Flags::ACTIVE );
                        }
                        else
                        {
                            sgnNode->clearFlag( SceneGraphNode::Flags::ACTIVE );
                        }
                        sceneChanged = true;
                    }

                    ImGui::SameLine( ImGui::GetWindowSize().x - 80.f );
                    bool selectionLocked = sgnNode->hasFlag( SceneGraphNode::Flags::SELECTION_LOCKED );
                    if ( ImGui::Checkbox( ICON_FK_LOCK"  ", &selectionLocked ) )
                    {
                        if ( selectionLocked )
                        {
                            sgnNode->setFlag( SceneGraphNode::Flags::SELECTION_LOCKED );
                        }
                        else
                        {
                            sgnNode->clearFlag( SceneGraphNode::Flags::SELECTION_LOCKED );
                        }
                    }
                    if ( ImGui::IsItemHovered() )
                    {
                        ImGui::SetTooltip( "When ticked, prevents selection of a different node" );
                        skipAutoTooltip( true );
                    }
                    if ( selectionLocked )
                    {
                        lockSolutionExplorer = true;
                    }
                    ImGui::Separator();

                    // Root
                    if ( sgnNode->parent() == nullptr )
                    {
                        ImGui::Separator();
                    }

                    EditorComponent* nodeComp = sgnNode->node()->editorComponent();
                    if ( nodeComp != nullptr && lockedGUID != nodeComp->getGUID() && printComponent( sgnNode, nodeComp, xOffset, smallButtonWidth))
                    {
                        sceneChanged = true;
                    }

                    vector<EditorComponent*>& editorComp = Attorney::SceneGraphNodeEditor::editorComponents( sgnNode );
                    for ( EditorComponent* comp : editorComp )
                    {
                        if ( lockedGUID != comp->getGUID() && printComponent( sgnNode, comp, xOffset, smallButtonWidth ) )
                        {
                            sceneChanged = true;
                        }
                    }
                    ImGui::Separator();
                    ImGui::NewLine();
                    ImGui::NewLine();
                    ImGui::SameLine( ImGui::GetWindowSize().x * 0.5f - (buttonWidth * 0.5f) );
                    if ( ImGui::Button( ICON_FK_FLOPPY_O" Save Node", ImVec2( buttonWidth, 25 ) ) )
                    {
                        Attorney::EditorPropertyWindow::saveNode( _parent, sgnNode );
                    }
                }
                ImGui::PopID();
            }

            //ToDo: Speed this up. Also, how do we handle adding stuff like RenderingComponents and creating materials and the like?
            const auto validComponentToAdd = [this, &crtSelections]( const ComponentType type ) -> bool
            {
                if ( type == ComponentType::COUNT )
                {
                    return false;
                }

                if ( type == ComponentType::SCRIPT )
                {
                    return true;
                }

                bool missing = false;
                for ( U8 i = 0u; i < crtSelections._selectionCount; ++i )
                {
                    const SceneGraphNode* sgn = node( crtSelections._selections[i] );
                    if ( sgn != nullptr && !( sgn->componentMask() & to_U32( type ) ) )
                    {
                        missing = true;
                        break;
                    }
                }

                return missing;
            };

            xOffset = ImGui::GetWindowSize().x - buttonWidth - 20.0f;
            ImGui::NewLine();
            ImGui::Separator();
            ImGui::NewLine();
            ImGui::SameLine( xOffset );
            if ( ImGui::Button( ICON_FK_PLUS" ADD NEW", ImVec2( buttonWidth, 25 ) ) )
            {
                Util::OpenCenteredPopup( "COMP_SELECTION_GROUP" );
            }
            static ComponentType selectedType = ComponentType::COUNT;

            if ( ImGui::BeginPopup( "COMP_SELECTION_GROUP" ) )
            {
                for ( auto i = 1u; i < to_base( ComponentType::COUNT ) + 1; ++i )
                {
                    const U32 componentBit = 1 << i;
                    const ComponentType type = static_cast<ComponentType>(componentBit);
                    if ( type == ComponentType::COUNT || !validComponentToAdd( type ) )
                    {
                        continue;
                    }

                    if ( ImGui::Selectable( TypeUtil::ComponentTypeToString( type ) ) )
                    {
                        selectedType = type;
                    }
                }
                ImGui::EndPopup();
            }
            if ( selectedType != ComponentType::COUNT )
            {
                Util::OpenCenteredPopup( "Add new component" );
            }

            if ( ImGui::BeginPopupModal( "Add new component", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
            {
                ImGui::Text( "Add new %s component?", TypeUtil::ComponentTypeToString( selectedType ) );
                ImGui::Separator();

                if ( ImGui::Button( "OK", ImVec2( 120, 0 ) ) )
                {
                    if ( Attorney::EditorGeneralWidget::addComponent( _context.editor(), crtSelections, selectedType ) )
                    {
                        sceneChanged = true;
                    }
                    selectedType = ComponentType::COUNT;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
                {
                    selectedType = ComponentType::COUNT;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        else
        {
            ImGui::Separator();
            ImGui::Text( "Please select a scene node \n to inspect its properties" );
            ImGui::Separator();
        }

        if ( _previewTexture != INVALID_HANDLE<Texture> )
        {
            if ( Attorney::EditorGeneralWidget::modalTextureView( _context.editor(), Util::StringFormat( "Image Preview: {}", Get(_previewTexture)->resourceName().c_str() ).c_str(), _previewTexture, float2( 512, 512 ), true, false ) )
            {
                _previewTexture = INVALID_HANDLE<Texture>;
            }
        }

        Attorney::EditorPropertyWindow::lockSolutionExplorer( _parent, lockSolutionExplorer );

        if ( sceneChanged )
        {
            Attorney::EditorGeneralWidget::registerUnsavedSceneChanges( _context.editor() );
        }
    }

    const Selections& PropertyWindow::selections() const
    {
        Scene* activeScene = context().kernel().projectManager()->activeProject()->getActiveScene();
        return activeScene->getCurrentSelection();
    }

    SceneGraphNode* PropertyWindow::node( const I64 guid ) const
    {
        Scene* activeScene = context().kernel().projectManager()->activeProject()->getActiveScene();
        return activeScene->sceneGraph()->findNode( guid );
    }

    bool PropertyWindow::processField( EditorComponentField& field )
    {
        if ( field._labels == nullptr )
        {
            field._labels = Util::FieldLabels;
        }

        const auto printFieldName = [&field]()
        {
            ImGui::Text( "[%s]", field._name.c_str() );
        };
        bool ret = false;
        switch ( field._type )
        {
            case EditorComponentFieldType::SEPARATOR:
            {
                printFieldName();
                ImGui::Separator();
            }break;
            case EditorComponentFieldType::BUTTON:
            {
                if ( field._readOnly )
                {
                    PushReadOnly();
                }
                if ( field._range.y - field._range.x > 1.0f )
                {
                    ret = ImGui::Button( field._name.c_str(), ImVec2( field._range.x, field._range.y ) );
                }
                else
                {
                    ret = ImGui::Button( field._name.c_str() );
                }
                if ( field._readOnly )
                {
                    PopReadOnly();
                }
            }break;
            case EditorComponentFieldType::SLIDER_TYPE:
            case EditorComponentFieldType::PUSH_TYPE:
            case EditorComponentFieldType::SWITCH_TYPE:
            {
                ImGui::PushItemWidth( ImGui::GetContentRegionAvail().x * 0.5f );
                {
                    ret = processBasicField( field );
                }ImGui::PopItemWidth();
            } break;
            case EditorComponentFieldType::DROPDOWN_TYPE:
            {
                if ( field._readOnly )
                {
                    PushReadOnly();
                }

                printFieldName();
                const U32 entryStart = to_U32( field._range.offset );
                const U32 entryCount = to_U32( field._range.count );
                static UndoEntry<I32> typeUndo = {};
                if ( entryCount > 0u && entryStart <= entryCount )
                {
                    ImGui::PushID( field._name.c_str() );

                    const U32 crtMode = field.get<U32>();
                    ret = ImGui::BeginCombo( "", field.getDisplayName( crtMode ) );
                    if ( ret )
                    {
                        for ( U32 n = entryStart; n < entryCount; ++n )
                        {
                            const bool isSelected = crtMode == n;
                            if ( ImGui::Selectable( field.getDisplayName( n ), isSelected ) )
                            {
                                typeUndo._type = PushConstantType::UINT;
                                typeUndo._name = "Drop Down Selection";
                                typeUndo._oldVal = crtMode;
                                typeUndo._newVal = n;
                                typeUndo._dataSetter = [&field]( const U32& data )
                                {
                                    field.set( data );
                                };

                                field.set( n );
                                _context.editor().registerUndoEntry( typeUndo );
                                break;
                            }
                            if ( isSelected )
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
                if ( field._readOnly )
                {
                    PopReadOnly();
                }
            }break;
            case EditorComponentFieldType::BOUNDING_BOX:
            {
                printFieldName();
                BoundingBox bb{};
                field.get<BoundingBox>( bb );

                F32* bbMin = bb._min._v;
                F32* bbMax = bb._max._v;
                float3 halfExtent = bb.getHalfExtent();
                float3 bbCenter = bb.getCenter();
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Min ";
                    bbField._basicType = PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = field._readOnly;
                    bbField._data = bbMin;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._dataSetter = [&field]( const void* val, [[maybe_unused]] void* user_data)
                    {
                        BoundingBox aabb{};
                        field.get<BoundingBox>( aabb );
                        aabb.setMin( *static_cast<const float3*>(val) );
                        field.set<BoundingBox>( aabb );
                    };
                    ret = processField( bbField ) || ret;
                }
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Max ";
                    bbField._basicType = PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = field._readOnly;
                    bbField._data = bbMax;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._dataSetter = [&field]( const void* val, [[maybe_unused]] void* user_data)
                    {
                        BoundingBox aabb{};
                        field.get<BoundingBox>( aabb );
                        aabb.setMax( *static_cast<const float3*>(val) );
                        field.set<BoundingBox>( aabb );
                    };
                    ret = processField( bbField ) || ret;
                }
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Half Extents ";
                    bbField._basicType = PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = true;
                    bbField._data = halfExtent;
                    bbField._hexadecimal = field._hexadecimal;
                    ret = processField( bbField ) || ret;
                }
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Center ";
                    bbField._basicType = PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = true;
                    bbField._data = bbCenter;
                    bbField._hexadecimal = field._hexadecimal;
                    ret = processField( bbField ) || ret;
                }
            }break;
            case EditorComponentFieldType::ORIENTED_BOUNDING_BOX:
            {
                printFieldName();
                OBB obb = {};
                field.get<OBB>( obb );
                float3 position = obb.position();
                float3 hExtents = obb.halfExtents();
                OBB::OBBAxis axis = obb.axis();
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Center ";
                    bbField._basicType = PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = true;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._data = position._v;
                    ret = processField( bbField ) || ret;
                }
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Half Extents ";
                    bbField._basicType = PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = true;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._data = hExtents._v;
                    ret = processField( bbField ) || ret;
                }
                for ( U8 i = 0; i < 3; ++i )
                {
                    EditorComponentField bbField = {};
                    Util::StringFormatTo( bbField._name, "Axis [ {} ]", i );
                    bbField._basicType = PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = true;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._data = axis[i]._v;
                    ret = processField( bbField ) || ret;
                }
            }break;
            case EditorComponentFieldType::BOUNDING_SPHERE:
            {
                printFieldName();

                BoundingSphere bs = {};
                field.get<BoundingSphere>( bs );
                F32* center = bs._sphere.center._v;
                F32& radius = bs._sphere.radius;
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Center ";
                    bbField._basicType = PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = field._readOnly;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._data = center;
                    bbField._dataSetter = [&field]( const void* c, [[maybe_unused]] void* user_data)
                    {
                        BoundingSphere bSphere = {};
                        field.get<BoundingSphere>( bSphere );
                        bSphere.setCenter( *static_cast<const float3*>(c) );
                        field.set<BoundingSphere>( bSphere );
                    };
                    ret = processField( bbField ) || ret;
                }
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Radius ";
                    bbField._basicType = PushConstantType::FLOAT;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = field._readOnly;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._data = &radius;
                    bbField._dataSetter = [&field]( const void* r, [[maybe_unused]] void* user_data)
                    {
                        BoundingSphere bSphere = {};
                        field.get<BoundingSphere>( bSphere );
                        bSphere.setRadius( *static_cast<const F32*>(r) );
                        field.set<BoundingSphere>( bSphere );
                    };
                    ret = processField( bbField ) || ret;
                }
            }break;
            case EditorComponentFieldType::TRANSFORM:
            {
                assert( !field._dataSetter && "Need direct access to memory" );
                ret = processTransform( field.getPtr<TransformComponent>(), field._readOnly, field._hexadecimal );
            }break;

            case EditorComponentFieldType::MATERIAL:
            {
                assert( !field._dataSetter && "Need direct access to memory" );
                ret = processMaterial( field.getPtr<Material>(), field._readOnly, field._hexadecimal );
            }break;
            default:
            case EditorComponentFieldType::COUNT: break;
        }

        if ( !skipAutoTooltip() && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
        {
            ImGui::SetTooltip( field._tooltip.empty() ? field._name.c_str() : field._tooltip.c_str() );
        }

        return ret;
    }

    bool PropertyWindow::processTransform( TransformComponent* transform, const bool readOnly, const bool hex )
    {
        if ( transform == nullptr )
        {
            return false;
        }

        DIVIDE_ASSERT(!hex, "PropertyWindow::processTransform error: Hex fields not supported!");

        Util::PushNarrowLabelWidth();
        bool ret = false;
        const bool transformReadOnly = readOnly || transform->editorLockPosition();
        const bool rotationReadOnly = readOnly || transform->editorLockRotation();
        const bool scaleReadOnly = readOnly || transform->editorLockScale();

        const TransformValues transformValues = transform->getLocalValues();
        float3 pos = transformValues._translation;
        vec3<Angle::DEGREES_F> rot = Angle::to_DEGREES( transformValues._orientation.getEuler() );
        float3 scale = transformValues._scale;

        const vec3<Angle::DEGREES_F> oldRot = rot;
        if ( Util::DrawVec<F32, 3, true>( ImGuiDataType_Float, "Position", pos._v, transformReadOnly ).wasChanged )
        {
            ret = true;
            RegisterUndo<float3, false>( _parent,
                                            PushConstantType::VEC3,
                                            transformValues._translation,
                                            pos,
                                            "Transform position",
                                            [transform]( const float3& val )
                                            {
                                                transform->setPosition( val );
                                            } );
            transform->setPosition( pos );
        }
        if ( Util::DrawVec<F32, 3, true>( ImGuiDataType_Float, "Rotation", &rot.x.value, rotationReadOnly ).wasChanged )
        {
            ret = true;
            RegisterUndo<vec3<Angle::DEGREES_F>, false>( _parent,
                                                         PushConstantType::VEC3,
                                                         oldRot,
                                                         rot,
                                                         "Transform rotation",
                                                         [transform]( const vec3<Angle::DEGREES_F>& val )
                                                         {
                                                             transform->setRotationEuler( val );
                                                         } );
            transform->setRotationEuler( rot );
        }
        TransformComponent::ScalingMode scalingMode = transform->scalingMode();
        bool nonUniformScalingEnabled = scalingMode != TransformComponent::ScalingMode::UNIFORM;

        bool scaleChanged = false;
        if ( nonUniformScalingEnabled )
        {
            scaleChanged = Util::DrawVec<F32, 3, true>( ImGuiDataType_Float, "Scale", scale._v, scaleReadOnly, 1.f ).wasChanged;
        }
        else
        {
            if ( Util::DrawVec<F32, 1, true>( ImGuiDataType_Float, "Scale", scale._v, scaleReadOnly, 1.f ).wasChanged )
            {
                scaleChanged = true;
                scale.z = scale.y = scale.x;
            }
        }

        if ( scaleChanged )
        {
            ret = true;
            // Scale is tricky as it may invalidate everything if it's set wrong!
            for ( U8 i = 0; i < 3; ++i )
            {
                scale[i] = std::max( EPSILON_F32, scale[i] );
            }
            RegisterUndo<float3, false>( _parent,
                                            PushConstantType::VEC3,
                                            transformValues._scale,
                                            scale,
                                            "Transform scale",
                                            [transform]( const float3& val )
                                            {
                                                transform->setScale( val );
                                            } );
            transform->setScale( scale );
        }

        if ( ImGui::Checkbox( "Non-uniform scaling", &nonUniformScalingEnabled ) )
        {
            const TransformComponent::ScalingMode newMode = nonUniformScalingEnabled ? TransformComponent::ScalingMode::NON_UNIFORM : TransformComponent::ScalingMode::UNIFORM;
            RegisterUndo<bool, false>( _parent,
                                       PushConstantType::UINT,
                                       to_U32( scalingMode ),
                                       to_U32( newMode ),
                                       "Non-uniform scaling",
                                       [transform]( const bool& oldVal ) noexcept
                                       {
                                           transform->scalingMode( static_cast<TransformComponent::ScalingMode>(oldVal) );
                                       } );

            transform->scalingMode( newMode );
            ret = true;
        }
        if ( ImGui::IsItemHovered() )
        {
            ImGui::SetTooltip( "Toggle per-axis independent scale values.\nAllow shear/tear/squash/etc.\nBreaks the scene hierarchy in many ways but should be fine for leaf nodes" );
            skipAutoTooltip( true );
        }

        bool parentRelativeRotations = transform->rotationMode() == TransformComponent::RotationMode::RELATIVE_TO_PARENT;
        if (ImGui::Checkbox("Parent-relative rotations", &parentRelativeRotations))
        {
            const TransformComponent::RotationMode newMode = parentRelativeRotations ? TransformComponent::RotationMode::RELATIVE_TO_PARENT : TransformComponent::RotationMode::LOCAL;
            RegisterUndo<bool, false>( _parent,
                                       PushConstantType::UINT,
                                       to_U32( scalingMode ),
                                       to_U32( newMode ),
                                       "Parent-relative rotations",
                                       [transform]( const bool& oldVal ) noexcept
                                       {
                                           transform->rotationMode( static_cast<TransformComponent::RotationMode>(oldVal) );
                                       } );

            transform->rotationMode( newMode );
            ret = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("If enabled, the child objects will rotate AROUND the parent object's center as opposed to their own local center.\nExample: Solar sytem: In parent-relative mode, rotating the sun parent object will rotate the child planet objects around it.");
            skipAutoTooltip(true);
        }

        if ( ImGui::Button( ICON_FK_UNDO" RESET", ImVec2( 90.f, 20 ) ) )
        {
            transform->reset();
            const TransformValues newTransformValues = transform->getLocalValues();
            RegisterUndo<TransformValues, false>( _parent,
                                                  PushConstantType::COUNT,
                                                  transformValues,
                                                  newTransformValues,
                                                  "Transform Reset",
                                                  [transform]( const TransformValues& oldVal ) noexcept
                                                  {
                                                      transform->setTransform(oldVal);
                                                  } );

            ret = true;
        }
        Util::PopNarrowLabelWidth();
        return ret;
    }

    bool PropertyWindow::processMaterial( Material* material, bool readOnly, bool hex )
    {
        if ( material == nullptr )
        {
            return false;
        }

        if ( readOnly )
        {
            PushReadOnly();
        }

        bool ret = false;
        static RenderStagePass currentStagePass{ RenderStage::DISPLAY, RenderPassType::MAIN_PASS };
        {
            const char* crtStageName = TypeUtil::RenderStageToString( currentStagePass._stage );
            const char* crtPassName = TypeUtil::RenderPassTypeToString( currentStagePass._passType );
            if ( ImGui::BeginCombo( "Stage", crtStageName, ImGuiComboFlags_PopupAlignLeft ) )
            {
                for ( U8 n = 0; n < to_U8( RenderStage::COUNT ); ++n )
                {
                    const RenderStage mode = static_cast<RenderStage>(n);
                    const bool isSelected = currentStagePass._stage == mode;

                    if ( ImGui::Selectable( TypeUtil::RenderStageToString( mode ), isSelected ) )
                    {
                        currentStagePass._stage = mode;
                    }
                    if ( isSelected )
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if ( ImGui::BeginCombo( "PassType", crtPassName, ImGuiComboFlags_PopupAlignLeft ) )
            {
                for ( U8 n = 0; n < to_U8( RenderPassType::COUNT ); ++n )
                {
                    const RenderPassType pass = static_cast<RenderPassType>(n);
                    const bool isSelected = currentStagePass._passType == pass;

                    if ( ImGui::Selectable( TypeUtil::RenderPassTypeToString( pass ), isSelected ) )
                    {
                        currentStagePass._passType = pass;
                    }
                    if ( isSelected )
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            {
                constexpr U8 min = 0u, max = to_U8( RenderStagePass::VariantType::COUNT );
                ImGui::SliderScalar( "Variant", ImGuiDataType_U8, &currentStagePass._variant, &min, &max );
            }
            {
                constexpr U8 min = 0u, max = to_U8( RenderStagePass::PassIndex::COUNT );
                ImGui::SliderScalar( "Pass", ImGuiDataType_U16, &currentStagePass._pass, &min, &max );
            }

            ImGui::InputScalar( "Index", ImGuiDataType_U16, &currentStagePass._index, nullptr, nullptr, (hex ? "%08X" : nullptr), (hex ? ImGuiInputTextFlags_CharsHexadecimal : 0u) );
        }

        RenderStateBlock stateBlock{};
        string shaderName = "None";
        ShaderProgram* program = nullptr;
        if ( currentStagePass._stage != RenderStage::COUNT && currentStagePass._passType != RenderPassType::COUNT )
        {
            const Handle<ShaderProgram> shaderHandle = material->computeAndGetProgramHandle( currentStagePass );
            if ( shaderHandle != INVALID_HANDLE<ShaderProgram> )
            {
                program = Get( shaderHandle );
                shaderName = program->resourceName().c_str();
            }
            stateBlock = material->getOrCreateRenderStateBlock( currentStagePass );
        }

        ImGui::Text("Skinning Mode: %s", TypeUtil::SkinningModeToString(material->properties().skinningMode()));

        if ( ImGui::CollapsingHeader( ("Program: " + shaderName).c_str() ) )
        {
            if ( program != nullptr )
            {
                const ShaderProgramDescriptor& descriptor = program->descriptor();
                for ( const ShaderModuleDescriptor& module : descriptor._modules )
                {
                    const char* stages[] = { "PS", "VS", "GS", "HS", "DS","CS" };
                    if ( ImGui::CollapsingHeader( Util::StringFormat( "{}: File [ {} ] Variant [ {} ]",
                                                                      stages[to_base( module._moduleType )],
                                                                      module._sourceFile.data(),
                                                                      module._variant.empty() ? "-" : module._variant.c_str() ).c_str() ) )
                    {
                        ImGui::Text( "Defines: " );
                        ImGui::Separator();
                        for ( const auto& [text, appendPrefix] : module._defines )
                        {
                            ImGui::Text( text.c_str() );
                        }
                        if ( ImGui::Button( "Open Source File" ) )
                        {
                            const ResourcePath& textEditor = Attorney::EditorGeneralWidget::externalTextEditorPath( _context.editor() );
                            if ( textEditor.empty() )
                            {
                                Attorney::EditorGeneralWidget::showStatusMessage( _context.editor(), "ERROR: No text editor specified!", Time::SecondsToMilliseconds<F32>( 3 ), true );
                            }
                            else
                            {
                                if ( openFile( textEditor.string(), Paths::Shaders::GLSL::g_GLSLShaderLoc, module._sourceFile.c_str() ) != FileError::NONE )
                                {
                                    Attorney::EditorGeneralWidget::showStatusMessage( _context.editor(), "ERROR: Couldn't open specified source file!", Time::SecondsToMilliseconds<F32>( 3 ), true );
                                }
                            }
                        }
                    }
                    if ( !descriptor._globalDefines.empty() )
                    {
                        ImGui::Text( "Global Defines: " );
                        ImGui::Separator();
                        for ( const auto& [text, appendPrefix] : descriptor._globalDefines )
                        {
                            ImGui::Text( text.c_str() );
                        }
                    }
                }

                ImGui::Separator();
                if ( ImGui::Button( "Rebuild from source" ) && !readOnly )
                {
                    Attorney::EditorGeneralWidget::showStatusMessage( _context.editor(), "Rebuilding shader from source ...", Time::SecondsToMilliseconds<F32>( 3 ), false );
                    bool skipped = false;
                    if ( !program->recompile( skipped ) )
                    {
                        Attorney::EditorGeneralWidget::showStatusMessage( _context.editor(), "ERROR: Failed to rebuild shader from source!", Time::SecondsToMilliseconds<F32>( 3 ), true );
                    }
                    else
                    {
                        Attorney::EditorGeneralWidget::showStatusMessage( _context.editor(), skipped ? "Rebuilt shader not needed!" : "Rebuilt shader from source!", Time::SecondsToMilliseconds<F32>( 3 ), false );
                        ret = true;
                    }
                }
                ImGui::Separator();
            }
        }

        const size_t stateHash = GetHash(stateBlock);
        static bool renderStateWasOpen = false;
        if ( !ImGui::CollapsingHeader( Util::StringFormat( "Render State: {}", stateHash ).c_str(), (renderStateWasOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0u) | ImGuiTreeNodeFlags_SpanAvailWidth ) )
        {
            renderStateWasOpen = false;
        }
        else if ( stateHash > 0 )
        {
            renderStateWasOpen = true;

            bool changed = false;
            {
                P32 colourWrite = stateBlock._colourWrite;
                constexpr const char* const names[] = { "R", "G", "B", "A" };

                for ( U8 i = 0; i < 4; ++i )
                {
                    if ( i > 0 )
                    {
                        ImGui::SameLine();
                    }

                    bool val = colourWrite.b[i] == 1;
                    if ( ImGui::Checkbox( names[i], &val ) )
                    {
                        RegisterUndo<bool, false>( _parent, PushConstantType::BOOL, !val, val, "Colour Mask", [&stateBlock, material, i]( const bool& oldVal )
                                                   {
                                                       stateBlock._colourWrite.b[i] = oldVal;
                                                       material->setRenderStateBlock( stateBlock, currentStagePass._stage, currentStagePass._passType, currentStagePass._variant );
                                                   } );
                        colourWrite.b[i] = val ? 1 : 0;
                        stateBlock._colourWrite = colourWrite;
                        changed = true;
                    }
                }
            }

            F32 zBias = stateBlock._zBias;
            F32 zUnits = stateBlock._zUnits;
            {
                EditorComponentField tempField = {};
                tempField._name = "ZBias";
                tempField._basicType = PushConstantType::FLOAT;
                tempField._type = EditorComponentFieldType::PUSH_TYPE;
                tempField._readOnly = readOnly;
                tempField._data = &zBias;
                tempField._range = { 0.0f, 1000.0f };
                const RenderStagePass tempPass = currentStagePass;
                tempField._dataSetter = [material, &stateBlock, tempPass]( const void* data, [[maybe_unused]] void* user_data)
                {
                    stateBlock._zBias = *static_cast<const F32*>(data);
                    material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                };
                changed = processField( tempField ) || changed;
            }
            {
                EditorComponentField tempField = {};
                tempField._name = "ZUnits";
                tempField._basicType = PushConstantType::FLOAT;
                tempField._type = EditorComponentFieldType::PUSH_TYPE;
                tempField._readOnly = readOnly;
                tempField._data = &zUnits;
                tempField._range = { 0.0f, 65536.0f };
                const RenderStagePass tempPass = currentStagePass;
                tempField._dataSetter = [material, &stateBlock, tempPass]( const void* data, [[maybe_unused]] void* user_data)
                {
                    stateBlock._zUnits = *static_cast<const F32*>(data);
                    material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                };
                changed = processField( tempField ) || changed;
            }

            ImGui::Text( "Tessellation control points: %d", stateBlock._tessControlPoints );
            {
                CullMode cMode = stateBlock._cullMode;

                static UndoEntry<I32> cullUndo = {};
                const char* crtMode = TypeUtil::CullModeToString( cMode );
                if ( ImGui::BeginCombo( "Cull Mode", crtMode, ImGuiComboFlags_PopupAlignLeft ) )
                {
                    for ( U8 n = 0; n < to_U8( CullMode::COUNT ); ++n )
                    {
                        const CullMode mode = static_cast<CullMode>(n);
                        const bool isSelected = cMode == mode;

                        if ( ImGui::Selectable( TypeUtil::CullModeToString( mode ), isSelected ) )
                        {
                            cullUndo._type = PushConstantType::INT;
                            cullUndo._name = "Cull Mode";
                            cullUndo._oldVal = to_I32( cMode );
                            cullUndo._newVal = to_I32( mode );
                            const RenderStagePass tempPass = currentStagePass;
                            cullUndo._dataSetter = [material, &stateBlock, tempPass]( const I32& data )
                            {
                                stateBlock._cullMode = static_cast<CullMode>(data);
                                material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                            };
                            _context.editor().registerUndoEntry( cullUndo );

                            cMode = mode;
                            stateBlock._cullMode = mode;
                            changed = true;
                        }
                        if ( isSelected )
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            {
                static UndoEntry<I32> fillUndo = {};
                FillMode fMode = stateBlock._fillMode;
                const char* crtMode = TypeUtil::FillModeToString( fMode );
                if ( ImGui::BeginCombo( "Fill Mode", crtMode, ImGuiComboFlags_PopupAlignLeft ) )
                {
                    for ( U8 n = 0; n < to_U8( FillMode::COUNT ); ++n )
                    {
                        const FillMode mode = static_cast<FillMode>(n);
                        const bool isSelected = fMode == mode;

                        if ( ImGui::Selectable( TypeUtil::FillModeToString( mode ), isSelected ) )
                        {
                            fillUndo._type = PushConstantType::INT;
                            fillUndo._name = "Fill Mode";
                            fillUndo._oldVal = to_I32( fMode );
                            fillUndo._newVal = to_I32( mode );
                            const RenderStagePass tempPass = currentStagePass;
                            fillUndo._dataSetter = [material, &stateBlock, tempPass]( const I32& data )
                            {
                                stateBlock._fillMode = static_cast<FillMode>(data);
                                material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                            };
                            _context.editor().registerUndoEntry( fillUndo );

                            fMode = mode;
                            stateBlock._fillMode = mode;
                            changed = true;
                        }
                        if ( isSelected )
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            U32 stencilReadMask = stateBlock._stencilMask;
            U32 stencilWriteMask = stateBlock._stencilWriteMask;
            if ( ImGui::InputScalar( "Stencil mask", ImGuiDataType_U32, &stencilReadMask, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal ) )
            {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<U32, false>( _parent, PushConstantType::UINT, stateBlock._stencilMask, stencilReadMask, "Stencil mask", [material, &stateBlock, tempPass]( const U32& oldVal )
                                          {
                                              stateBlock._stencilMask = oldVal;
                                              material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                                          } );

                stateBlock._stencilMask = stencilReadMask;
                changed = true;
            }

            if ( ImGui::InputScalar( "Stencil write mask", ImGuiDataType_U32, &stencilWriteMask, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal ) )
            {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<U32, false>( _parent, PushConstantType::UINT, stateBlock._stencilWriteMask, stencilWriteMask, "Stencil write mask", [material, &stateBlock, tempPass]( const U32& oldVal )
                                          {
                                              stateBlock._stencilWriteMask = oldVal;
                                              material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                                          } );

                stateBlock._stencilWriteMask = stencilWriteMask;
                changed = true;
            }
            {
                static UndoEntry<I32> depthUndo = {};
                const char* crtMode = TypeUtil::ComparisonFunctionToString( stateBlock._zFunc );
                if ( ImGui::BeginCombo( "Depth function", crtMode, ImGuiComboFlags_PopupAlignLeft ) )
                {
                    for ( U8 n = 0; n < to_U8( ComparisonFunction::COUNT ); ++n )
                    {
                        const ComparisonFunction func = static_cast<ComparisonFunction>(n);
                        const bool isSelected = stateBlock._zFunc == func;

                        if ( ImGui::Selectable( TypeUtil::ComparisonFunctionToString( func ), isSelected ) )
                        {
                            depthUndo._type = PushConstantType::INT;
                            depthUndo._name = "Depth function";
                            depthUndo._oldVal = to_I32( stateBlock._zFunc );
                            depthUndo._newVal = to_I32( func );
                            const RenderStagePass tempPass = currentStagePass;
                            depthUndo._dataSetter = [material, &stateBlock, tempPass]( const I32& data )
                            {
                                stateBlock._zFunc = static_cast<ComparisonFunction>(data);
                                material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                            };
                            _context.editor().registerUndoEntry( depthUndo );

                            stateBlock._zFunc = func;
                            changed = true;
                        }
                        if ( isSelected )
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            bool stencilDirty = false;
            U32 stencilRef = stateBlock._stencilRef;
            bool stencilEnabled = stateBlock._stencilEnabled;
            StencilOperation sFailOp = stateBlock._stencilFailOp;
            StencilOperation sZFailOp = stateBlock._stencilZFailOp;
            StencilOperation sPassOp = stateBlock._stencilPassOp;
            ComparisonFunction sFunc = stateBlock._stencilFunc;

            if ( ImGui::InputScalar( "Stencil reference mask", ImGuiDataType_U32, &stencilRef, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal ) )
            {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<U32, false>( _parent, PushConstantType::UINT, stateBlock._stencilRef, stencilRef, "Stencil reference mask", [material, &stateBlock, tempPass]( const U32& oldVal )
                                          {
                                              stateBlock._stencilRef = oldVal;
                                              material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                                          } );
                stencilDirty = true;
            }

            {
                static UndoEntry<I32> stencilUndo = {};
                const char* crtMode = TypeUtil::StencilOperationToString( sFailOp );
                if ( ImGui::BeginCombo( "Stencil fail op", crtMode, ImGuiComboFlags_PopupAlignLeft ) )
                {
                    for ( U8 n = 0; n < to_U8( StencilOperation::COUNT ); ++n )
                    {
                        const StencilOperation op = static_cast<StencilOperation>(n);
                        const bool isSelected = sFailOp == op;

                        if ( ImGui::Selectable( TypeUtil::StencilOperationToString( op ), isSelected ) )
                        {
                            stencilUndo._type = PushConstantType::INT;
                            stencilUndo._name = "Stencil fail op";
                            stencilUndo._oldVal = to_I32( sFailOp );
                            stencilUndo._newVal = to_I32( op );
                            const RenderStagePass tempPass = currentStagePass;
                            stencilUndo._dataSetter = [material, &stateBlock, tempPass]( const I32& data )
                            {
                                stateBlock._stencilFailOp = static_cast<StencilOperation>(data);
                                material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                            };
                            _context.editor().registerUndoEntry( stencilUndo );

                            sFailOp = op;
                            stencilDirty = true;
                        }
                        if ( isSelected )
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            {
                static UndoEntry<I32> stencilUndo = {};
                const char* crtMode = TypeUtil::StencilOperationToString( sZFailOp );
                if ( ImGui::BeginCombo( "Stencil depth fail op", crtMode, ImGuiComboFlags_PopupAlignLeft ) )
                {
                    for ( U8 n = 0; n < to_U8( StencilOperation::COUNT ); ++n )
                    {
                        const StencilOperation op = static_cast<StencilOperation>(n);
                        const bool isSelected = sZFailOp == op;

                        if ( ImGui::Selectable( TypeUtil::StencilOperationToString( op ), isSelected ) )
                        {
                            stencilUndo._type = PushConstantType::INT;
                            stencilUndo._name = "Stencil depth fail op";
                            stencilUndo._oldVal = to_I32( sZFailOp );
                            stencilUndo._newVal = to_I32( op );
                            const RenderStagePass tempPass = currentStagePass;
                            stencilUndo._dataSetter = [material, &stateBlock, tempPass]( const I32& data )
                            {
                                stateBlock._stencilZFailOp = static_cast<StencilOperation>(data);
                                material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                            };
                            _context.editor().registerUndoEntry( stencilUndo );

                            sZFailOp = op;
                            stencilDirty = true;
                        }
                        if ( isSelected )
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            {
                static UndoEntry<I32> stencilUndo = {};
                const char* crtMode = TypeUtil::StencilOperationToString( sPassOp );
                if ( ImGui::BeginCombo( "Stencil pass op", crtMode, ImGuiComboFlags_PopupAlignLeft ) )
                {
                    for ( U8 n = 0; n < to_U8( StencilOperation::COUNT ); ++n )
                    {
                        const StencilOperation op = static_cast<StencilOperation>(n);
                        const bool isSelected = sPassOp == op;

                        if ( ImGui::Selectable( TypeUtil::StencilOperationToString( op ), isSelected ) )
                        {

                            stencilUndo._type = PushConstantType::INT;
                            stencilUndo._name = "Stencil pass op";
                            stencilUndo._oldVal = to_I32( sPassOp );
                            stencilUndo._newVal = to_I32( op );
                            const RenderStagePass tempPass = currentStagePass;
                            stencilUndo._dataSetter = [material, &stateBlock, tempPass]( const I32& data )
                            {
                                stateBlock._stencilPassOp = static_cast<StencilOperation>(data);
                                material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                            };
                            _context.editor().registerUndoEntry( stencilUndo );

                            sPassOp = op;
                            stencilDirty = true;
                        }
                        if ( isSelected )
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            {
                static UndoEntry<I32> stencilUndo = {};
                const char* crtMode = TypeUtil::ComparisonFunctionToString( sFunc );
                if ( ImGui::BeginCombo( "Stencil function", crtMode, ImGuiComboFlags_PopupAlignLeft ) )
                {
                    for ( U8 n = 0; n < to_U8( ComparisonFunction::COUNT ); ++n )
                    {
                        const ComparisonFunction mode = static_cast<ComparisonFunction>(n);
                        const bool isSelected = sFunc == mode;

                        if ( ImGui::Selectable( TypeUtil::ComparisonFunctionToString( mode ), isSelected ) )
                        {

                            stencilUndo._type = PushConstantType::INT;
                            stencilUndo._name = "Stencil function";
                            stencilUndo._oldVal = to_I32( sFunc );
                            stencilUndo._newVal = to_I32( mode );
                            const RenderStagePass tempPass = currentStagePass;
                            stencilUndo._dataSetter = [material, &stateBlock, tempPass]( const I32& data )
                            {
                                stateBlock._stencilFunc = static_cast<ComparisonFunction>(data);
                                material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                            };
                            _context.editor().registerUndoEntry( stencilUndo );

                            sFunc = mode;
                            stencilDirty = true;
                        }
                        if ( isSelected )
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            bool frontFaceCCW = stateBlock._frontFaceCCW;
            if ( ImGui::Checkbox( "CCW front face", &frontFaceCCW ) )
            {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<bool, false>( _parent, PushConstantType::BOOL, !frontFaceCCW, frontFaceCCW, "CCW front face", [material, &stateBlock, tempPass]( const bool& oldVal )
                                           {
                                               stateBlock._frontFaceCCW = oldVal;
                                               material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                                           } );

                stateBlock._frontFaceCCW = frontFaceCCW;
                changed = true;
            }

            bool scissorEnabled = stateBlock._scissorTestEnabled;
            if ( ImGui::Checkbox( "Scissor test", &scissorEnabled ) )
            {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<bool, false>( _parent, PushConstantType::BOOL, !scissorEnabled, scissorEnabled, "Scissor test", [material, &stateBlock, tempPass]( const bool& oldVal )
                                           {
                                               stateBlock._scissorTestEnabled = oldVal;
                                               material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                                           } );

                stateBlock._scissorTestEnabled = scissorEnabled;
                changed = true;
            }

            bool depthTestEnabled = stateBlock._depthTestEnabled;
            if ( ImGui::Checkbox( "Depth test", &depthTestEnabled ) )
            {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<bool, false>( _parent, PushConstantType::BOOL, !depthTestEnabled, depthTestEnabled, "Depth test", [material, &stateBlock, tempPass]( const bool& oldVal )
                                           {
                                               stateBlock._depthTestEnabled = oldVal;
                                               material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                                           } );

                stateBlock._depthTestEnabled = depthTestEnabled;
                changed = true;
            } 
            
            bool depthWriteEnabled = stateBlock._depthWriteEnabled;
            if ( ImGui::Checkbox( "Depth write", &depthWriteEnabled ) )
            {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<bool, false>( _parent, PushConstantType::BOOL, !depthWriteEnabled, depthWriteEnabled, "Depth write", [material, &stateBlock, tempPass]( const bool& oldVal )
                                           {
                                               stateBlock._depthWriteEnabled = oldVal;
                                               material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                                           } );

                stateBlock._depthWriteEnabled = depthWriteEnabled;
                changed = true;
            }

            if ( ImGui::Checkbox( "Stencil test", &stencilEnabled ) )
            {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<bool, false>( _parent, PushConstantType::BOOL, !stencilEnabled, stencilEnabled, "Stencil test", [material, &stateBlock, tempPass]( const bool& oldVal )
                                           {
                                               stateBlock._stencilEnabled = oldVal;
                                               material->setRenderStateBlock( stateBlock, tempPass._stage, tempPass._passType, tempPass._variant );
                                           } );

                stencilDirty = true;
            }

            if ( stencilDirty )
            {
                stateBlock._stencilRef = stencilRef;
                stateBlock._stencilEnabled = stencilEnabled;
                stateBlock._stencilFailOp = sFailOp;
                stateBlock._stencilZFailOp = sZFailOp;
                stateBlock._stencilPassOp = sPassOp;
                stateBlock._stencilFunc = sFunc;
                changed = true;
            }

            if ( changed && !readOnly )
            {
                material->setRenderStateBlock( stateBlock, currentStagePass._stage, currentStagePass._passType, currentStagePass._variant );
                ret = true;
            }
        }

        static bool shadingModeWasOpen = false;
        const ShadingMode crtMode = material->properties().shadingMode();
        const char* crtModeName = TypeUtil::ShadingModeToString( crtMode );
        if ( !ImGui::CollapsingHeader( crtModeName, (shadingModeWasOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0u) | ImGuiTreeNodeFlags_SpanAvailWidth ) )
        {
            shadingModeWasOpen = false;
        }
        else
        {
            skipAutoTooltip( true );

            shadingModeWasOpen = true;
            {
                static UndoEntry<I32> modeUndo = {};
                ImGui::PushItemWidth( 250 );
                if ( ImGui::BeginCombo( "[Shading Mode]", crtModeName, ImGuiComboFlags_PopupAlignLeft ) )
                {
                    for ( U8 n = 0; n < to_U8( ShadingMode::COUNT ); ++n )
                    {
                        const ShadingMode mode = static_cast<ShadingMode>(n);
                        const bool isSelected = crtMode == mode;

                        if ( ImGui::Selectable( TypeUtil::ShadingModeToString( mode ), isSelected ) )
                        {

                            modeUndo._type = PushConstantType::INT;
                            modeUndo._name = "Shading Mode";
                            modeUndo._oldVal = to_I32( crtMode );
                            modeUndo._newVal = to_I32( mode );
                            modeUndo._dataSetter = [material, mode]( [[maybe_unused]] const I32& data )
                            {
                                material->properties().shadingMode( mode );
                            };
                            _context.editor().registerUndoEntry( modeUndo );

                            material->properties().shadingMode( mode );
                        }
                        if ( isSelected )
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
            }
            I32 id = 0;

            ApplyAllButton( id, false, *material, []( const Material& baseMaterial, Material* matInstance )
                            {
                                matInstance->properties().shadingMode( baseMaterial.properties().shadingMode() );
                            } );

            bool fromTexture = false;
            Handle<Texture> texture = INVALID_HANDLE<Texture>;
            { //Base colour
                ImGui::Separator();
                ImGui::PushItemWidth( 250 );
                FColour4 diffuse = material->getBaseColour( fromTexture, texture );
                if ( Util::colourInput4( _parent, "[Albedo]", diffuse, readOnly, [material]( const FColour4& col )
                                         {
                                             material->properties().baseColour( col ); return true;
                                         } ) )
                {
                    ret = true;
                }
                                         ImGui::PopItemWidth();
                                         if ( fromTexture && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
                                         {
                                             ImGui::SetTooltip( "Albedo is sampled from a texture. Base colour possibly unused!" );
                                             skipAutoTooltip( true );
                                         }
                                         ApplyAllButton( id, fromTexture || readOnly, *material, []( const Material& baseMaterial, Material* matInstance )
                                                         {
                                                             matInstance->properties().baseColour( baseMaterial.properties().baseColour() );
                                                         } );
                                         if ( PreviewTextureButton( id, texture, !fromTexture ) )
                                         {
                                             _previewTexture = texture;
                                         }
                                         ImGui::Separator();
            }
            { //Second texture
                Handle<Texture> detailTex = material->getTexture( TextureSlot::UNIT1 );
                const bool ro = detailTex == INVALID_HANDLE<Texture>;
                ImGui::PushID( 4321234 + id++ );
                if ( ro || readOnly )
                {
                    PushReadOnly();
                }

                bool showTexture = false;
                if ( ImGui::Button( "Detail Texture" ) )
                {
                    ret = true;
                    showTexture = true;
                }
                if ( ro || readOnly )
                {
                    PopReadOnly();
                }
                if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
                {
                    if ( ro )
                    {
                        ImGui::SetTooltip( "No detail texture specified!" );
                    }
                    else
                    {
                        ImGui::SetTooltip( Util::StringFormat( "Preview texture : {}", Get(detailTex)->assetName() ).c_str() );
                    }
                    skipAutoTooltip( true );
                }
                ImGui::PopID();
                if ( showTexture && !ro )
                {
                    _previewTexture = detailTex;
                }
            }
            { //Normal
                Handle<Texture> normalTex = material->getTexture( TextureSlot::NORMALMAP );
                const bool ro = normalTex == INVALID_HANDLE<Texture>;
                ImGui::PushID( 4321234 + id++ );
                if ( ro || readOnly )
                {
                    PushReadOnly();
                }
                bool showTexture = false;
                if ( ImGui::Button( "Normal Map" ) )
                {
                    ret = true;
                    showTexture = true;
                }
                if ( ro || readOnly )
                {
                    PopReadOnly();
                }
                if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
                {
                    if ( ro )
                    {
                        ImGui::SetTooltip( "No normal map specified!" );
                    }
                    else
                    {
                        ImGui::SetTooltip( Util::StringFormat( "Preview texture : {}", Get(normalTex)->assetName() ).c_str() );
                    }
                    skipAutoTooltip( true );
                }
                ImGui::PopID();
                if ( showTexture && !ro )
                {
                    _previewTexture = normalTex;
                }
            }
            { //Emissive
                ImGui::Separator();

                ImGui::PushItemWidth( 250 );
                FColour3 emissive = material->getEmissive( fromTexture, texture );
                if ( Util::colourInput3( _parent, "[Emissive]", emissive, fromTexture || readOnly, [&material]( const FColour3& col )
                                         {
                                             material->properties().emissive( col ); return true;
                                         } ) )
                {
                    ret = true;
                }
                                         ImGui::PopItemWidth();
                                         if ( fromTexture && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
                                         {
                                             ImGui::SetTooltip( "Control managed by application (e.g. is overriden by a texture)" );
                                             skipAutoTooltip( true );
                                         }
                                         ApplyAllButton( id, fromTexture || readOnly, *material, []( const Material& baseMaterial, Material* matInstance )
                                                         {
                                                             matInstance->properties().emissive( baseMaterial.properties().emissive() );
                                                         } );
                                         if ( PreviewTextureButton( id, texture, !fromTexture ) )
                                         {
                                             _previewTexture = texture;
                                         }
                                         ImGui::Separator();
            }
            { //Ambient
                ImGui::Separator();
                ImGui::PushItemWidth( 250 );
                FColour3 ambient = material->getAmbient( fromTexture, texture );
                if ( Util::colourInput3( _parent, "[Ambient]", ambient, fromTexture || readOnly, [material]( const FColour3& colour )
                                         {
                                             material->properties().ambient( colour ); return true;
                                         } ) )
                {
                    ret = true;
                }
                                         ImGui::PopItemWidth();
                                         if ( fromTexture && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
                                         {
                                             ImGui::SetTooltip( "Control managed by application (e.g. is overriden by a texture)" );
                                             skipAutoTooltip( true );
                                         }
                                         ApplyAllButton( id, fromTexture || readOnly, *material, []( const Material& baseMaterial, Material* matInstance )
                                                         {
                                                             matInstance->properties().ambient( baseMaterial.properties().ambient() );
                                                         } );
                                         if ( PreviewTextureButton( id, texture, !fromTexture ) )
                                         {
                                             _previewTexture = texture;
                                         }
                                         ImGui::Separator();
            }
            if ( material->properties().shadingMode() != ShadingMode::PBR_MR &&
                 material->properties().shadingMode() != ShadingMode::PBR_SG )
            {
                FColour3 specular = material->getSpecular( fromTexture, texture );
                F32 shininess = material->properties().shininess();

                { //Specular power
                    EditorComponentField tempField = {};
                    tempField._name = "Shininess";
                    tempField._basicType = PushConstantType::FLOAT;
                    tempField._type = EditorComponentFieldType::SLIDER_TYPE;
                    tempField._readOnly = readOnly;
                    tempField._data = &shininess;
                    tempField._range = { 0.0f, Material::MAX_SHININESS };
                    tempField._dataSetter = [&material]( const void* s, [[maybe_unused]] void* user_data)
                    {
                        material->properties().shininess( *static_cast<const F32*>(s) );
                    };

                    ImGui::PushItemWidth( 175 );
                    ret = processBasicField( tempField ) || ret;
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    ApplyAllButton( id, fromTexture || readOnly, *material, []( const Material& baseMaterial, Material* matInstance )
                                    {
                                        matInstance->properties().shininess( baseMaterial.properties().shininess() );
                                    } );
                }
                { //Specular colour
                    ImGui::Separator();
                    ImGui::PushItemWidth( 250 );
                    if ( Util::colourInput3( _parent, "[Specular]", specular, fromTexture || readOnly, [material]( const FColour3& col )
                                             {
                                                 material->properties().specular( col ); return true;
                                             } ) )
                    {
                        ret = true;
                    }
                                             ImGui::PopItemWidth();
                                             if ( fromTexture && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
                                             {
                                                 ImGui::SetTooltip( "Control managed by application (e.g. is overriden by a texture)" );
                                             }
                                             ApplyAllButton( id, fromTexture || readOnly, *material, []( const Material& baseMaterial, Material* matInstance )
                                                             {
                                                                 matInstance->properties().specular( baseMaterial.properties().specular() );
                                                             } );
                                             if ( PreviewTextureButton( id, texture, !fromTexture ) )
                                             {
                                                 _previewTexture = texture;
                                             }
                                             skipAutoTooltip( true );
                                             ImGui::Separator();
                }
            }
            else
            {
                { // Metallic
                    ImGui::Separator();
                    F32 metallic = material->getMetallic( fromTexture, texture );
                    EditorComponentField tempField = {};
                    tempField._name = "Metallic";
                    tempField._basicType = PushConstantType::FLOAT;
                    tempField._type = EditorComponentFieldType::SLIDER_TYPE;
                    tempField._readOnly = fromTexture || readOnly;
                    if ( fromTexture )
                    {
                        tempField._tooltip = "Control managed by application (e.g. is overriden by a texture)";
                    }
                    tempField._data = &metallic;
                    tempField._range = { 0.0f, 1.0f };
                    tempField._dataSetter = [material]( const void* m, [[maybe_unused]] void* user_data)
                    {
                        material->properties().metallic( *static_cast<const F32*>(m) );
                    };

                    ImGui::PushItemWidth( 175 );
                    ret = processBasicField( tempField ) || ret;
                    ImGui::PopItemWidth();

                    ImGui::SameLine();
                    ApplyAllButton( id, fromTexture || readOnly, *material, []( const Material& baseMaterial, Material* matInstance )
                                    {
                                        matInstance->properties().metallic( baseMaterial.properties().metallic() );
                                    } );
                    if ( PreviewTextureButton( id, texture, !fromTexture ) )
                    {
                        _previewTexture = texture;
                    }
                    ImGui::Separator();
                }
                { // Roughness
                    ImGui::Separator();
                    F32 roughness = material->getRoughness( fromTexture, texture );
                    EditorComponentField tempField = {};
                    tempField._name = "Roughness";
                    tempField._basicType = PushConstantType::FLOAT;
                    tempField._type = EditorComponentFieldType::SLIDER_TYPE;
                    tempField._readOnly = fromTexture || readOnly;
                    if ( fromTexture )
                    {
                        tempField._tooltip = "Control managed by application (e.g. is overriden by a texture)";
                    }
                    tempField._data = &roughness;
                    tempField._range = { 0.0f, 1.0f };
                    tempField._dataSetter = [material]( const void* r, [[maybe_unused]] void* user_data)
                    {
                        material->properties().roughness( *static_cast<const F32*>(r) );
                    };
                    ImGui::PushItemWidth( 175 );
                    ret = processBasicField( tempField ) || ret;
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    ApplyAllButton( id, fromTexture || readOnly, *material, []( const Material& baseMaterial, Material* matInstance )
                                    {
                                        matInstance->properties().roughness( baseMaterial.properties().roughness() );
                                    } );
                    if ( PreviewTextureButton( id, texture, !fromTexture ) )
                    {
                        _previewTexture = texture;
                    }
                    ImGui::Separator();
                }

                { // Occlusion
                    ImGui::Separator();
                    F32 occlusion = material->getOcclusion( fromTexture, texture );
                    EditorComponentField tempField = {};
                    tempField._name = "Occlusion";
                    tempField._basicType = PushConstantType::FLOAT;
                    tempField._type = EditorComponentFieldType::SLIDER_TYPE;
                    tempField._readOnly = fromTexture || readOnly;
                    if ( fromTexture )
                    {
                        tempField._tooltip = "Control managed by application (e.g. is overriden by a texture)";
                    }
                    tempField._data = &occlusion;
                    tempField._range = { 0.0f, 1.0f };
                    tempField._dataSetter = [material]( const void* m, [[maybe_unused]] void* user_data)
                    {
                        material->properties().occlusion( *static_cast<const F32*>(m) );
                    };

                    ImGui::PushItemWidth( 175 );
                    ret = processBasicField( tempField ) || ret;
                    ImGui::PopItemWidth();

                    ImGui::SameLine();
                    ApplyAllButton( id, fromTexture || readOnly, *material, []( const Material& baseMaterial, Material* matInstance )
                                    {
                                        matInstance->properties().occlusion( baseMaterial.properties().occlusion() );
                                    } );
                    if ( PreviewTextureButton( id, texture, !fromTexture ) )
                    {
                        _previewTexture = texture;
                    }
                    ImGui::Separator();
                }
            }
            { // Parallax
                ImGui::Separator();
                F32 parallax = material->properties().parallaxFactor();
                EditorComponentField tempField = {};
                tempField._name = "Parallax";
                tempField._basicType = PushConstantType::FLOAT;
                tempField._type = EditorComponentFieldType::SLIDER_TYPE;
                tempField._readOnly = readOnly;
                tempField._data = &parallax;
                tempField._range = { 0.0f, 1.0f };
                tempField._dataSetter = [material]( const void* p, [[maybe_unused]] void* user_data)
                {
                    material->properties().parallaxFactor( *static_cast<const F32*>(p) );
                };
                ImGui::PushItemWidth( 175 );
                ret = processBasicField( tempField ) || ret;
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ApplyAllButton( id, fromTexture || readOnly, *material, []( const Material& baseMaterial, Material* matInstance )
                                {
                                    matInstance->properties().parallaxFactor( baseMaterial.properties().parallaxFactor() );
                                } );
                ImGui::Separator();
            }
            { // Texture operations
                constexpr const char* const names[] = {
                    "Tex operation [Albedo - Tex0]",
                    "Tex operation [(Albedo*Tex0) - Tex1]",
                    "Tex operation [SpecColour - SpecMap]"
                };

                static UndoEntry<I32> opUndo = {};
                for ( U8 i = 0; i < 3; ++i )
                {
                    const TextureSlot targetTex = i == 0 ? TextureSlot::UNIT0
                        : i == 1 ? TextureSlot::UNIT1
                        : TextureSlot::SPECULAR;

                    const bool hasTexture = material->getTexture( targetTex ) != INVALID_HANDLE<Texture>;

                    ImGui::PushID( 4321234 + id++ );
                    if ( !hasTexture )
                    {
                        PushReadOnly();
                    }
                    const TextureOperation crtOp = material->getTextureInfo( targetTex )._operation;
                    ImGui::Text( names[i] );
                    if ( ImGui::BeginCombo( "", TypeUtil::TextureOperationToString( crtOp ), ImGuiComboFlags_PopupAlignLeft ) )
                    {
                        for ( U8 n = 0; n < to_U8( TextureOperation::COUNT ); ++n )
                        {
                            const TextureOperation op = static_cast<TextureOperation>(n);
                            const bool isSelected = op == crtOp;

                            if ( ImGui::Selectable( TypeUtil::TextureOperationToString( op ), isSelected ) )
                            {

                                opUndo._type = PushConstantType::INT;
                                opUndo._name = "Tex Operation " + Util::to_string( i );
                                opUndo._oldVal = to_I32( crtOp );
                                opUndo._newVal = to_I32( op );
                                opUndo._dataSetter = [material, targetTex]( const I32& data )
                                {
                                    material->setTextureOperation( targetTex, static_cast<TextureOperation>(data) );
                                };
                                _context.editor().registerUndoEntry( opUndo );
                                material->setTextureOperation( targetTex, op );
                            }
                            if ( isSelected )
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ApplyAllButton( id, fromTexture || readOnly, *material, [targetTex]( const Material& baseMaterial, Material* matInstance )
                                    {
                                        matInstance->setTextureOperation( targetTex, baseMaterial.getTextureInfo( targetTex )._operation );
                                    } );
                    if ( !hasTexture )
                    {
                        PopReadOnly();
                        if ( ImGui::IsItemHovered() )
                        {
                            ImGui::SetTooltip( "Insuficient input textures for this operation!" );
                            skipAutoTooltip( true );
                        }
                    }
                    ImGui::PopID();
                }

                ImGui::Separator();
            }
            bool ignoreTexAlpha = material->properties().overrides().ignoreTexDiffuseAlpha();
            bool doubleSided = material->properties().doubleSided();
            RefractorType refractorType = material->properties().refractorType();
            ReflectorType reflectorType = material->properties().reflectorType();

            ImGui::Text( "[Double Sided]" ); ImGui::SameLine();
            if ( ImGui::ToggleButton( "[Double Sided]", &doubleSided ) && !readOnly )
            {
                RegisterUndo<bool, false>( _parent, PushConstantType::BOOL, !doubleSided, doubleSided, "DoubleSided", [material]( const bool& oldVal )
                                           {
                                               material->properties().doubleSided( oldVal );
                                           } );
                material->properties().doubleSided( doubleSided );
                ret = true;
            }
            ApplyAllButton( id, fromTexture || readOnly, *material, []( const Material& baseMaterial, Material* matInstance )
                            {
                                matInstance->properties().doubleSided( baseMaterial.properties().doubleSided() );
                            } );
            ImGui::Text( "[Ignore texture Alpha]" ); ImGui::SameLine();
            if ( ImGui::ToggleButton( "[Ignore texture Alpha]", &ignoreTexAlpha ) && !readOnly )
            {
                RegisterUndo<bool, false>( _parent, PushConstantType::BOOL, !ignoreTexAlpha, ignoreTexAlpha, "IgnoretextureAlpha", [material]( const bool& oldVal )
                                           {
                                               material->properties().ignoreTexDiffuseAlpha( oldVal );
                                           } );
                material->properties().ignoreTexDiffuseAlpha( ignoreTexAlpha );
                ret = true;
            }
            ApplyAllButton( id, fromTexture || readOnly, *material, []( const Material& baseMaterial, Material* matInstance )
                            {
                                matInstance->properties().ignoreTexDiffuseAlpha( baseMaterial.properties().overrides().ignoreTexDiffuseAlpha() );
                            } );

            {
                static UndoEntry<I32> reflectUndo = {};
                const char* crtMode = TypeUtil::ReflectorTypeToString(reflectorType);
                if (ImGui::BeginCombo("Reflection Mode", crtMode, ImGuiComboFlags_PopupAlignLeft))
                {
                    for (U8 n = 0; n < to_U8(ReflectorType::COUNT); ++n)
                    {
                        const ReflectorType mode = static_cast<ReflectorType>(n);
                        const bool isSelected = reflectorType == mode;

                        if (ImGui::Selectable(TypeUtil::ReflectorTypeToString(mode), isSelected))
                        {
                            reflectUndo._type = PushConstantType::INT;
                            reflectUndo._name = "Reflect Type";
                            reflectUndo._oldVal = to_I32(reflectorType);
                            reflectUndo._newVal = to_I32(mode);
                            const RenderStagePass tempPass = currentStagePass;
                            reflectUndo._dataSetter = [material](const I32& data)
                            {
                                material->properties().reflectorType(static_cast<ReflectorType>(data));
                            };
                            _context.editor().registerUndoEntry(reflectUndo);

                            reflectorType = mode;
                            material->properties().reflectorType(mode);
                            ret = true;
                        }
                        if (isSelected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ApplyAllButton(id, fromTexture || readOnly, *material, [](const Material& baseMaterial, Material* matInstance)
                {
                    matInstance->properties().reflectorType(baseMaterial.properties().reflectorType());
                });
            }
            {
                static UndoEntry<I32> refractUndo = {};
                const char* crtMode = TypeUtil::RefractorTypeToString(refractorType);
                if (ImGui::BeginCombo("Refraction Mode", crtMode, ImGuiComboFlags_PopupAlignLeft))
                {
                    for (U8 n = 0; n < to_U8(RefractorType::COUNT); ++n)
                    {
                        const RefractorType mode = static_cast<RefractorType>(n);
                        const bool isSelected = refractorType == mode;

                        if (ImGui::Selectable(TypeUtil::RefractorTypeToString(mode), isSelected))
                        {
                            refractUndo._type = PushConstantType::INT;
                            refractUndo._name = "Refract Type";
                            refractUndo._oldVal = to_I32(refractorType);
                            refractUndo._newVal = to_I32(mode);
                            const RenderStagePass tempPass = currentStagePass;
                            refractUndo._dataSetter = [material](const I32& data)
                            {
                                material->properties().refractorType(static_cast<RefractorType>(data));
                            };
                            _context.editor().registerUndoEntry(refractUndo);

                            refractorType = mode;
                            material->properties().refractorType(mode);
                            ret = true;
                        }
                        if (isSelected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                ApplyAllButton( id, fromTexture || readOnly, *material, []( const Material& baseMaterial, Material* matInstance )
                {
                    matInstance->properties().refractorType( baseMaterial.properties().refractorType() );
                });
            }
        }
        ImGui::Separator();

        if ( readOnly )
        {
            PopReadOnly();
            ret = false;
        }
        return ret;
    }

    bool PropertyWindow::processBasicField( EditorComponentField& field )
    {
        const bool isSlider = field._type == EditorComponentFieldType::SLIDER_TYPE &&
                              field._basicType != PushConstantType::BOOL &&
                              !field.isMatrix();

        const ImGuiInputTextFlags flags = Util::GetDefaultFlagsForField(field);

        const char* name = field._name.c_str();

        ImGui::PushID( name );
        Util::PushTooltip( field._tooltip.c_str() );

        const F32 step = field._step;
        bool ret = false;
        switch ( field._basicType )
        {
            case PushConstantType::BOOL:
            {
                if ( field._readOnly )
                {
                    PushReadOnly();
                }

                ImFont* boldFont = ImGui::GetIO().Fonts->Fonts[1];

                static UndoEntry<bool> undo = {};
                bool val = field.get<bool>();
                if ( field._type == EditorComponentFieldType::SWITCH_TYPE )
                {
                    ret = ImGui::ToggleButton( "", &val );
                }
                else
                {
                    ret = ImGui::Checkbox( "", &val );
                }
                ImGui::SameLine();
                ImGui::PushFont( boldFont );
                ImGui::Text( name );
                ImGui::PopFont();
                if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
                {
                    if ( Util::IsPushedTooltip() )
                    {
                        ImGui::SetTooltip( Util::PushedToolTip() );
                    }
                    else
                    {
                        ImGui::SetTooltip( name );
                    }
                    skipAutoTooltip( true );
                }

                if ( ret && !field._readOnly )
                {
                    RegisterUndo<bool, false>( _parent, PushConstantType::BOOL, !val, val, name, [&field]( const bool& oldVal )
                                               {
                                                   field.set( oldVal );
                                               } );
                    field.set<bool>( val );
                }
                if ( field._readOnly )
                {
                    PopReadOnly();
                }
            }break;
            case PushConstantType::INT:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputOrSlider<I64, I64, 1>( _parent, isSlider, name, step, ImGuiDataType_S64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputOrSlider<I32, I32, 1>( _parent, isSlider, name, step, ImGuiDataType_S32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputOrSlider<I16, I16, 1>( _parent, isSlider, name, step, ImGuiDataType_S16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputOrSlider<I8,  I8,  1>( _parent, isSlider, name, step, ImGuiDataType_S8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
            }break;
            case PushConstantType::UINT:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputOrSlider<U64, U64, 1>( _parent, isSlider, name, step, ImGuiDataType_U64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputOrSlider<U32, U32, 1>( _parent, isSlider, name, step, ImGuiDataType_U32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputOrSlider<U16, U16, 1>( _parent, isSlider, name, step, ImGuiDataType_U16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputOrSlider<U8,  U8,  1>( _parent, isSlider, name, step, ImGuiDataType_U8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
            }break;
            case PushConstantType::DOUBLE:
            {
                ret = Util::inputOrSlider<D64, D64, 1>( _parent, isSlider, name, step, ImGuiDataType_Double, field, flags, field._format );
            }break;
            case PushConstantType::FLOAT:
            {
                ret = Util::inputOrSlider<F32, F32, 1>( _parent, isSlider, name, step, ImGuiDataType_Float, field, flags, field._format );
            }break;
            case PushConstantType::IVEC2:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputOrSlider<vec2<I64>, I64, 2>( _parent, isSlider, name, step, ImGuiDataType_S64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputOrSlider<int2,      I32, 2>( _parent, isSlider, name, step, ImGuiDataType_S32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputOrSlider<vec2<I16>, I16, 2>( _parent, isSlider, name, step, ImGuiDataType_S16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputOrSlider<vec2<I8>,  I8,  2>( _parent, isSlider, name, step, ImGuiDataType_S8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
            }break;
            case PushConstantType::IVEC3:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputOrSlider<vec3<I64>, I64, 3>( _parent, isSlider, name, step, ImGuiDataType_S64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputOrSlider<vec3<I32>, I32, 3>( _parent, isSlider, name, step, ImGuiDataType_S32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputOrSlider<vec3<I16>, I16, 3>( _parent, isSlider, name, step, ImGuiDataType_S16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputOrSlider<vec3<I8>,  I8,  3>( _parent, isSlider, name, step, ImGuiDataType_S8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
            }break;
            case PushConstantType::IVEC4:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputOrSlider<vec4<I64>, I64, 4>( _parent, isSlider, name, step, ImGuiDataType_S64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputOrSlider<int4,      I32, 4>( _parent, isSlider, name, step, ImGuiDataType_S32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputOrSlider<vec4<I16>, I16, 4>( _parent, isSlider, name, step, ImGuiDataType_S16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputOrSlider<vec4<I8>,  I8,  4>( _parent, isSlider, name, step, ImGuiDataType_S8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
            }break;
            case PushConstantType::UVEC2:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputOrSlider<vec2<U64>, U64, 2>( _parent, isSlider, name, step, ImGuiDataType_U64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputOrSlider<uint2,     U32, 2>( _parent, isSlider, name, step, ImGuiDataType_U32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputOrSlider<vec2<U16>, U16, 2>( _parent, isSlider, name, step, ImGuiDataType_U16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputOrSlider<vec2<U8>,  U8,  2>( _parent, isSlider, name, step, ImGuiDataType_U8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
            }break;
            case PushConstantType::UVEC3:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputOrSlider<vec3<U64>, U64, 3>( _parent, isSlider, name, step, ImGuiDataType_U64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputOrSlider<uint3,     U32, 3>( _parent, isSlider, name, step, ImGuiDataType_U32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputOrSlider<vec3<U16>, U16, 3>( _parent, isSlider, name, step, ImGuiDataType_U16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputOrSlider<vec3<U8>,  U8,  3>( _parent, isSlider, name, step, ImGuiDataType_U8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
            }break;
            case PushConstantType::UVEC4:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputOrSlider<vec4<U64>, U64, 4>( _parent, isSlider, name, step, ImGuiDataType_U64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputOrSlider<uint4,     U32, 4>( _parent, isSlider, name, step, ImGuiDataType_U32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputOrSlider<vec4<U16>, U16, 4>( _parent, isSlider, name, step, ImGuiDataType_U16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputOrSlider<vec4<U8>,  U8,  4>( _parent, isSlider, name, step, ImGuiDataType_U8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
            }break;
            case PushConstantType::VEC2:
            {
                ret = Util::inputOrSlider<float2, F32, 2>( _parent, isSlider, name, step, ImGuiDataType_Float, field, flags, field._format );
            }break;
            case PushConstantType::VEC3:
            {
                ret = Util::inputOrSlider<float3, F32, 3>( _parent, isSlider, name, step, ImGuiDataType_Float, field, flags, field._format );
            }break;
            case PushConstantType::VEC4:
            {
                ret = Util::inputOrSlider<float4, F32, 4>( _parent, isSlider, name, step, ImGuiDataType_Float, field, flags, field._format );
            }break;
            case PushConstantType::DVEC2:
            {
                ret = Util::inputOrSlider<vec2<D64>, D64, 2>( _parent, isSlider, name, step, ImGuiDataType_Double, field, flags, field._format );
            }break;
            case PushConstantType::DVEC3:
            {
                ret = Util::inputOrSlider<vec3<D64>, D64, 3>( _parent, isSlider, name, step, ImGuiDataType_Double, field, flags, field._format );
            }break;
            case PushConstantType::DVEC4:
            {
                ret = Util::inputOrSlider<vec4<D64>, D64, 4>( _parent, isSlider, name, step, ImGuiDataType_Double, field, flags, field._format );
            }break;
            case PushConstantType::IMAT2:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputMatrix<mat2<I64>, 2>( _parent, name, step, ImGuiDataType_S64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputMatrix<mat2<I32>, 2>( _parent, name, step, ImGuiDataType_S32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputMatrix<mat2<I16>, 2>( _parent, name, step, ImGuiDataType_S16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputMatrix<mat2<I8>,  2>( _parent, name, step, ImGuiDataType_S8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
            }break;
            case PushConstantType::IMAT3:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputMatrix<mat3<I64>, 3>( _parent, name, step, ImGuiDataType_S64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputMatrix<mat3<I32>, 3>( _parent, name, step, ImGuiDataType_S32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputMatrix<mat3<I16>, 3>( _parent, name, step, ImGuiDataType_S16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputMatrix<mat3<I8>,  3>( _parent, name, step, ImGuiDataType_S8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
                ImGui::Separator();
            }break;
            case PushConstantType::IMAT4:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputMatrix<mat4<I64>, 4>( _parent, name, step, ImGuiDataType_S64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputMatrix<mat4<I32>, 4>( _parent, name, step, ImGuiDataType_S32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputMatrix<mat4<I16>, 4>( _parent, name, step, ImGuiDataType_S16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputMatrix<mat4<I8>,  4>( _parent, name, step, ImGuiDataType_S8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
                ImGui::Separator();
            }break;
            case PushConstantType::UMAT2:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputMatrix<mat2<U64>, 2>( _parent, name, step, ImGuiDataType_U64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputMatrix<mat2<U32>, 2>( _parent, name, step, ImGuiDataType_U32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputMatrix<mat2<U16>, 2>( _parent, name, step, ImGuiDataType_U16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputMatrix<mat2<U8>,  2>( _parent, name, step, ImGuiDataType_U8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
            }break;
            case PushConstantType::UMAT3:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputMatrix<mat3<U64>, 3>( _parent, name, step, ImGuiDataType_U64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputMatrix<mat3<U32>, 3>( _parent, name, step, ImGuiDataType_U32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputMatrix<mat3<U16>, 3>( _parent, name, step, ImGuiDataType_U16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputMatrix<mat3<U8>,  3>( _parent, name, step, ImGuiDataType_U8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
            }break;
            case PushConstantType::UMAT4:
            {
                switch ( field._basicTypeSize )
                {
                    case PushConstantSize::QWORD: ret = Util::inputMatrix<mat4<U64>, 4>( _parent, name, step, ImGuiDataType_U64, field, flags, field._format ); break;
                    case PushConstantSize::DWORD: ret = Util::inputMatrix<mat4<U32>, 4>( _parent, name, step, ImGuiDataType_U32, field, flags, field._format ); break;
                    case PushConstantSize::WORD:  ret = Util::inputMatrix<mat4<U16>, 4>( _parent, name, step, ImGuiDataType_U16, field, flags, field._format ); break;
                    case PushConstantSize::BYTE:  ret = Util::inputMatrix<mat4<U8>,  4>( _parent, name, step, ImGuiDataType_U8,  field, flags, field._format ); break;
                    default:
                    case PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                }
            }break;
            case PushConstantType::MAT2:
            {
                ret = Util::inputMatrix<mat2<F32>, 2>( _parent, name, step, ImGuiDataType_Float, field, flags, field._format );
            }break;
            case PushConstantType::MAT3:
            {
                ret = Util::inputMatrix<mat3<F32>, 3>( _parent, name, step, ImGuiDataType_Float, field, flags, field._format );
            }break;
            case PushConstantType::MAT4:
            {
                ret = Util::inputMatrix<mat4<F32>, 4>( _parent, name, step, ImGuiDataType_Float, field, flags, field._format );
            }break;
            case PushConstantType::DMAT2:
            {
                ret = Util::inputMatrix<mat2<D64>, 2>( _parent, name, step, ImGuiDataType_Double, field, flags, field._format );
            }break;
            case PushConstantType::DMAT3:
            {
                ret = Util::inputMatrix<mat3<D64>, 3>( _parent, name, step, ImGuiDataType_Double, field, flags, field._format );
            }break;
            case PushConstantType::DMAT4:
            {
                ret = Util::inputMatrix<mat4<D64>, 4>( _parent, name, step, ImGuiDataType_Double, field, flags, field._format );
            }break;
            case PushConstantType::FCOLOUR3:
            {
                ret = Util::colourInput3( _parent, field );
            }break;
            case PushConstantType::FCOLOUR4:
            {
                ret = Util::colourInput4( _parent, field );
            }break;
            default:
            case PushConstantType::COUNT:
            {
                ImGui::Text( name );
            }break;
        }
        Util::PopTooltip();
        ImGui::PopID();

        return ret;
    }

    string PropertyWindow::name() const
    {
        const Selections& nodes = selections();
        if ( nodes._selectionCount == 0 )
        {
            return DockedWindow::name();
        }

        if ( nodes._selectionCount == 1 )
        {
            return node( nodes._selections[0] )->name().c_str();
        }

        return Util::StringFormat( "{}, {}, ...", node( nodes._selections[0] )->name().c_str(), node( nodes._selections[1] )->name().c_str() );
    }
} //namespace Divide

DISABLE_MSVC_WARNING_POP()
