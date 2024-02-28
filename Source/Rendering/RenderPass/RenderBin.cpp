

#include "Headers/RenderBin.h"

#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "Geometry/Material/Headers/Material.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    void RenderBin::sort( const RenderBinType type, const RenderingOrder renderOrder )
    {
        constexpr U16 k_parallelSortThreshold = 16u;

        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        if ( renderOrder == RenderingOrder::NONE || renderOrder == RenderingOrder::COUNT )
        {
            if ( renderOrder == RenderingOrder::COUNT )
            {
                Console::errorfn( Locale::Get( _ID( "ERROR_INVALID_RENDER_BIN_SORT_ORDER" ) ), Names::renderBinType[to_base( type )] );
            }

            return;
        }
   
        const auto sortFunc = [renderOrder]( const RenderBinItem& a, const RenderBinItem& b ) {
            switch ( renderOrder )
            {
                case RenderingOrder::BACK_TO_FRONT: return a._distanceToCameraSq > b._distanceToCameraSq;
                case RenderingOrder::FRONT_TO_BACK: return a._distanceToCameraSq < b._distanceToCameraSq;
                case RenderingOrder::FRONT_TO_BACK_ALPHA_LAST:
                {
                    if ( a._hasTransparency == b._hasTransparency )
                    {
                        return a._distanceToCameraSq < b._distanceToCameraSq;
                    }

                    return b._hasTransparency;
                }
                case RenderingOrder::BY_STATE:
                {
                    // Sorting opaque items is a 3 step process:
                    // 1: sort by shaders
                    // 2: if the shader is identical, sort by state hash
                    // 3: if shader is identical and state hash is identical, sort by albedo ID
                    // 4: finally, sort by distance to camera (front to back)

                    // Sort by shader in all states The sort key is the shader id (for now)
                    if ( a._shaderKey != b._shaderKey )
                    {
                        return a._shaderKey < b._shaderKey;
                    }
                    // If the shader values are the same, we use the state hash for sorting
                    // The _stateHash is a CRC value created based on the RenderState.
                    if ( a._stateHash != b._stateHash )
                    {
                        return a._stateHash < b._stateHash;
                    }
                    // If both the shader are the same and the state hashes match,
                    // we sort by the secondary key (usually the texture id)
                    if ( a._textureKey != b._textureKey )
                    {
                        return a._textureKey < b._textureKey;
                    }
                    // ... and then finally fallback to front to back
                    return a._distanceToCameraSq < b._distanceToCameraSq;
                }

                default: break;
            }
      
            return false;
        };

        const U16 binSize = _renderBinIndex.load();
        if ( binSize > k_parallelSortThreshold )
        {
            std::sort( std::execution::par_unseq, begin( _renderBinStack ), begin( _renderBinStack ) + binSize, sortFunc );
        }
        else
        {
            eastl::sort( begin( _renderBinStack ), begin( _renderBinStack ) + binSize, sortFunc );
        }
    }

    void RenderBin::clear() noexcept
    {
        _renderBinIndex.store(0u);
    }

    const RenderBinItem& RenderBin::getItem( const U16 index ) const
    {
        assert( index < _renderBinIndex.load() );
        return _renderBinStack[index];
    }

    U16 RenderBin::getBinSize() const noexcept
    {
        return _renderBinIndex.load();
    }

    U16 RenderBin::getSortedNodes( SortedQueue& nodes ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        const U16 binSize = getBinSize();

        nodes.resize( binSize );
        for ( U16 i = 0u; i < binSize; ++i )
        {
            nodes[i] = _renderBinStack[i]._renderable;
        }

        return binSize;
    }

    void RenderBin::addNodeToBin( const SceneGraphNode* sgn, const RenderStagePass renderStagePass, const F32 minDistToCameraSq )
    {
        RenderBinItem& item = _renderBinStack[_renderBinIndex.fetch_add(1u)];
        item._distanceToCameraSq = minDistToCameraSq;
        item._renderable = sgn->get<RenderingComponent>();

        // Sort by state hash depending on the current rendering stage
        // Save the render state hash value for sorting
        item._stateHash = Attorney::RenderingCompRenderBin::getStateHash( item._renderable, renderStagePass );

        const Material_ptr& nodeMaterial = item._renderable->getMaterialInstance();
        if ( nodeMaterial )
        {
            Attorney::MaterialRenderBin::getSortKeys( *nodeMaterial, renderStagePass, item._shaderKey, item._textureKey, item._hasTransparency );
        }
    }

    void RenderBin::populateRenderQueue( const RenderStagePass stagePass, RenderQueuePackages& queueInOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        const U16 binSize = getBinSize();
        for ( U16 i = 0u; i < binSize; ++i )
        {
            RenderingComponent* rComp = _renderBinStack[i]._renderable;
            queueInOut.push_back( {
                rComp,
                &Attorney::RenderingCompRenderBin::getDrawPackage( rComp, stagePass )
            } );
        }
    }

    void RenderBin::postRender( const SceneRenderState& renderState, const RenderStagePass stagePass, GFX::CommandBuffer& bufferInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        const U16 binSize = getBinSize();
        for ( U16 i = 0u; i < binSize; ++i )
        {
            Attorney::RenderingCompRenderBin::postRender( _renderBinStack[i]._renderable,
                                                          renderState,
                                                          stagePass,
                                                          bufferInOut );
        }
    }

} // namespace Divide