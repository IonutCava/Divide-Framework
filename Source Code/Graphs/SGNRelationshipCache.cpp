#include "stdafx.h"

#include "Headers/SGNRelationshipCache.h"
#include "Headers/SceneGraphNode.h"

namespace Divide
{

    SGNRelationshipCache::SGNRelationshipCache( SceneGraphNode* parent ) noexcept
        : _parentNode( parent )
    {
        std::atomic_init( &_isValid, false );
    }

    bool SGNRelationshipCache::isValid() const noexcept
    {
        return _isValid;
    }

    void SGNRelationshipCache::invalidate() noexcept
    {
        _isValid = false;
    }

    bool SGNRelationshipCache::rebuild()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        ScopedLock<SharedMutex> w_lock( _updateMutex );
        updateChildren( 0u, _childrenRecursiveCache );
        updateParents( 0u, _parentRecursiveCache );
        updateSiblings( 0u, _siblingCache );

        _isValid = true;
        return true;
    }

    SGNRelationshipCache::RelationshipType SGNRelationshipCache::classifyNode( const I64 GUID ) const noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        assert( isValid() );

        if ( GUID != _parentNode->getGUID() )
        {
            SharedLock<SharedMutex> r_lock( _updateMutex );
            for ( const CacheEntry& it : _childrenRecursiveCache )
            {
                if ( it._guid == GUID )
                {
                    return it._level > 0u
                        ? RelationshipType::GRANDCHILD
                        : RelationshipType::CHILD;
                }
            }

            for ( const CacheEntry& it : _parentRecursiveCache )
            {
                if ( it._guid == GUID )
                {
                    return it._level > 0u
                        ? RelationshipType::GRANDPARENT
                        : RelationshipType::PARENT;
                }
            }

            for ( const CacheEntry& it : _siblingCache )
            {
                if ( it._guid == GUID )
                {
                    return RelationshipType::SIBLING;
                }
            }
        }

        return RelationshipType::COUNT;
    }
    
    bool SGNRelationshipCache::validateRelationship( const I64 GUID, const RelationshipType type ) const noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        assert(isValid());

        switch ( type )
        {
            case RelationshipType::GRANDCHILD:
            {
                for ( const CacheEntry& it : _childrenRecursiveCache )
                {
                    if ( it._guid == GUID )
                    {
                        return it._level > 0u;
                    }
                }
            } break;
            case RelationshipType::CHILD:
            {
                for ( const CacheEntry& it : _childrenRecursiveCache )
                {
                    if ( it._guid == GUID )
                    {
                        return it._level == 0u;
                    }
                }
            } break;
            case RelationshipType::GRANDPARENT:
            {
                for ( const CacheEntry& it : _parentRecursiveCache )
                {
                    if ( it._guid == GUID )
                    {
                        return it._level > 0u;
                    }
                }
            } break;
            case RelationshipType::PARENT:
            {
                for ( const CacheEntry& it : _parentRecursiveCache )
                {
                    if ( it._guid == GUID )
                    {
                        return it._level == 0u;
                    }
                }
            } break;
            case RelationshipType::SIBLING:
            {
                for ( const CacheEntry& it : _siblingCache )
                {
                    if ( it._guid == GUID  )
                    {
                        return true;
                    }
                }
            }break;
            default: break;
        };

        return false;
    }

    void SGNRelationshipCache::updateChildren( const U16 level, Cache& cache ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        const SceneGraphNode::ChildContainer& children = _parentNode->getChildren();
        SharedLock<SharedMutex> w_lock( children._lock );
        const U32 childCount = children._count;
        for ( U32 i = 0u; i < childCount; ++i )
        {
            SceneGraphNode* child = children._data[i];
            cache.emplace_back( CacheEntry{ child->getGUID(), level } );
            Attorney::SceneGraphNodeRelationshipCache::relationshipCache( child ).updateChildren( level + 1u, cache );
        }
    }

    void SGNRelationshipCache::updateParents( const U16 level, Cache& cache ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        const SceneGraphNode* parent = _parentNode->parent();
        // We ignore the root note when considering grandparent status
        if ( parent && parent->parent() )
        {
            cache.emplace_back( CacheEntry{ parent->getGUID(), level } );
            Attorney::SceneGraphNodeRelationshipCache::relationshipCache( parent ).updateParents( level + 1u, cache );
        }
    }   
    
    void SGNRelationshipCache::updateSiblings( const U16 level, Cache& cache ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        const SceneGraphNode* parent = _parentNode->parent();
        if (parent != nullptr )
        {
            const SceneGraphNode::ChildContainer& children = parent->getChildren();
            SharedLock<SharedMutex> w_lock( children._lock );
            const U32 childCount = children._count;
            for ( U32 i = 0u; i < childCount; ++i )
            {
                SceneGraphNode* child = children._data[i];
                if ( child->getGUID() != _parentNode->getGUID() )
                {
                    cache.emplace_back( CacheEntry{ child->getGUID(), 1u});
                }
            }
        }
    }

}; //namespace Divide