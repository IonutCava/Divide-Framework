

#include "Headers/SelectionSystem.h"

namespace Divide
{

    SelectionSystem::SelectionSystem( ECS::ECSEngine& parentEngine, PlatformContext& context )
        : PlatformContextComponent( context )
        , ECSSystem( parentEngine )
    {
    }

    SelectionSystem::~SelectionSystem()
    {
    }

    void SelectionSystem::Update( const F32 dt )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::Update( dt );

        for ( SelectionComponent* comp : _componentCache )
        {
            SceneGraphNode* sgn = comp->parentSGN();

            comp->_selectionType = SelectionComponent::SelectionType::NONE;

            if ( sgn->hasFlag( SceneGraphNode::Flags::SELECTED ) )
            {
                if ( sgn->parent() && sgn->parent()->hasFlag( SceneGraphNode::Flags::SELECTED ) )
                {
                    comp->_selectionType = SelectionComponent::SelectionType::PARENT_SELECTED;
                }
                else
                {
                    comp->_selectionType = SelectionComponent::SelectionType::SELECTED;
                }
            }
            else if ( sgn->hasFlag( SceneGraphNode::Flags::HOVERED ) )
            {
                if ( sgn->parent() && sgn->parent()->hasFlag( SceneGraphNode::Flags::HOVERED ) )
                {
                    comp->_selectionType = SelectionComponent::SelectionType::PARENT_HOVERED;
                }
                else
                {
                    comp->_selectionType = SelectionComponent::SelectionType::HOVERED;
                }
            }
        }
    }


    bool SelectionSystem::saveCache( const SceneGraphNode* sgn, ByteBuffer& outputBuffer )
    {
        if ( Parent::saveCache( sgn, outputBuffer ) )
        {
            const SelectionComponent* sComp = sgn->GetComponent<SelectionComponent>();
            if ( sComp != nullptr && !sComp->saveCache( outputBuffer ) )
            {
                return false;
            }

            return true;
        }

        return false;
    }

    bool SelectionSystem::loadCache( SceneGraphNode* sgn, ByteBuffer& inputBuffer )
    {
        if ( Parent::loadCache( sgn, inputBuffer ) )
        {
            SelectionComponent* sComp = sgn->GetComponent<SelectionComponent>();
            if ( sComp != nullptr && !sComp->loadCache( inputBuffer ) )
            {
                return false;
            }

            return true;
        }

        return false;
    }
} //namespace Divide
