#include "stdafx.h"

#include "Headers/DockedWindow.h"
#include "Geometry/Shapes/Headers/Object3D.h"

#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/SpotLightComponent.h"
#include "ECS/Components/Headers/PointLightComponent.h"
#include "ECS/Components/Headers/DirectionalLightComponent.h"
#include "ECS/Components/Headers/EnvironmentProbeComponent.h"
#include "ECS/Components/Headers/ScriptComponent.h"
#include "ECS/Components/Headers/UnitComponent.h"
#include "Dynamics/Entities/Units/Headers/Unit.h"
#include "Dynamics/Entities/Units/Headers/Character.h"
#include "Dynamics/Entities/Units/Headers/NPC.h"
#include "Dynamics/Entities/Units/Headers/Player.h"

#include <IconFontCppHeaders/IconsForkAwesome.h>

namespace Divide {

    DockedWindow::DockedWindow(Editor& parent, Descriptor descriptor) noexcept
        : _parent(parent),
          _focused(false),
          _isHovered(false),
          _descriptor(MOV(descriptor))
    {
    }

    void DockedWindow::backgroundUpdate() {
        backgroundUpdateInternal();
    }

    void DockedWindow::draw() {
        OPTICK_EVENT();

        //window_flags |= ImGuiWindowFlags_NoTitleBar;
        //window_flags |= ImGuiWindowFlags_NoScrollbar;
        //window_flags |= ImGuiWindowFlags_MenuBar;
        //window_flags |= ImGuiWindowFlags_NoMove;
        //window_flags |= ImGuiWindowFlags_NoResize;
        //window_flags |= ImGuiWindowFlags_NoCollapse;
        //window_flags |= ImGuiWindowFlags_NoNav;

        ImGui::SetNextWindowPos(_descriptor.position, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(_descriptor.size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(_descriptor.minSize, _descriptor.maxSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, _descriptor.minSize);
        ImGui::GetStyle().WindowMenuButtonPosition = _descriptor.showCornerButton ? ImGuiDir_Left : ImGuiDir_None;
        if (ImGui::Begin(_descriptor.name.c_str(), nullptr, windowFlags() | _descriptor.flags)) {
            _focused = ImGui::IsWindowFocused();
            _isHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
            drawInternal();
        }
        ImGui::PopStyleVar();
        ImGui::End();
    }

    
    const char* DockedWindow::getIconForNode(const SceneGraphNode* sgn) noexcept {
        switch (sgn->getNode().type()) {
            case SceneNodeType::TYPE_OBJECT3D: {
                switch (sgn->getNode<Object3D>().geometryType()) {
                    case ObjectType::SPHERE_3D: return ICON_FK_CIRCLE;
                    case ObjectType::BOX_3D: return ICON_FK_CUBE;
                    case ObjectType::QUAD_3D: return ICON_FK_SQUARE;
                    case ObjectType::PATCH_3D: return ICON_FK_PLUS_SQUARE;
                    case ObjectType::MESH: return ICON_FK_BUILDING;
                    case ObjectType::SUBMESH: return ICON_FK_PUZZLE_PIECE;
                    case ObjectType::TERRAIN: return ICON_FK_TREE;
                    case ObjectType::DECAL: return ICON_FK_STICKY_NOTE;
                };
                case SceneNodeType::TYPE_TRANSFORM:{
                    if (sgn->HasComponents(ComponentType::DIRECTIONAL_LIGHT)) {
                        return ICON_FK_SUN;
                    } else if (sgn->HasComponents(ComponentType::POINT_LIGHT)) {
                        return ICON_FK_LIGHTBULB_O;
                    } else if (sgn->HasComponents(ComponentType::SPOT_LIGHT)) {
                        return ICON_FK_DOT_CIRCLE_O;
                    } else if (sgn->HasComponents(ComponentType::SCRIPT)) {
                        return ICON_FK_FILE_TEXT;
                    } else if (sgn->HasComponents(ComponentType::ENVIRONMENT_PROBE)) {
                        return ICON_FK_GLOBE;
                    } else if (sgn->HasComponents(ComponentType::UNIT)) {
                        const UnitComponent* comp = sgn->GetComponent<UnitComponent>();
                        if (comp->getUnit() != nullptr) {
                            switch(comp->getUnit()->type()) {
                                case UnitType::UNIT_TYPE_CHARACTER: {
                                    switch (comp->getUnit<Character>()->characterType()) {
                                        case Character::CharacterType::CHARACTER_TYPE_NPC: return ICON_FK_FEMALE;
                                        case Character::CharacterType::CHARACTER_TYPE_PLAYER: return ICON_FK_GAMEPAD;
                                    }
                                }
                                case UnitType::UNIT_TYPE_VEHICLE: {
                                    return ICON_FK_CAR;
                                }
                            }
                        }
                    }
                        return ICON_FK_ARROWS;
                }
                case SceneNodeType::TYPE_WATER: return ICON_FK_SHIP;
                case SceneNodeType::TYPE_TRIGGER: return ICON_FK_COGS;
                case SceneNodeType::TYPE_PARTICLE_EMITTER: return ICON_FK_FIRE;
                case SceneNodeType::TYPE_SKY: return ICON_FK_CLOUD;
                case SceneNodeType::TYPE_INFINITEPLANE: return ICON_FK_ARROWS;
                case SceneNodeType::TYPE_VEGETATION: return ICON_FK_TREE;
            }break;
        }

        return ICON_FK_QUESTION;
    }
} //namespace Divide