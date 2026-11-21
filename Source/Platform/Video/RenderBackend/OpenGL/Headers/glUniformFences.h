// Copyright (c) 2026 DIVIDE-Studio

#pragma once

namespace Divide::GLUtil
{
    template<typename Fence, size_t Extent, typename Wait, typename Finish, typename Destroy>
    [[nodiscard]] bool FinishUniformContextFences( const std::span<vector<Fence>, Extent> groups, Wait&& wait,
                                                    Finish&& finish, Destroy&& destroy )
    {
        for ( auto& fences : groups )
        {
            for ( const Fence fence : fences )
            {
                if ( fence != nullptr && !wait( fence ) )
                {
                    return false;
                }
            }
        }

        if ( !finish() )
        {
            return false;
        }
        for ( auto& fences : groups )
        {
            for ( Fence& fence : fences )
            {
                if ( fence != nullptr )
                {
                    destroy( fence );
                }
            }
            fences.clear();
        }
        return true;
    }
}
