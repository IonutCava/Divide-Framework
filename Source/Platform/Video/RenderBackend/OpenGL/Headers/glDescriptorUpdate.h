// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include "Platform/Video/Headers/DescriptorSets.h"

namespace Divide
{
    enum class GLDescriptorUpdateScope : U8
    {
        NONE,
        UNIFORM_BUFFERS,
        ALL
    };

    [[nodiscard]] constexpr GLDescriptorUpdateScope SelectGLDescriptorUpdateScope(
        const bool descriptorDirty, const bool fallbackChanged, const bool perDraw, const bool uniformsChanged ) noexcept
    {
        if ( descriptorDirty || fallbackChanged )
        {
            return GLDescriptorUpdateScope::ALL;
        }
        return perDraw && uniformsChanged ? GLDescriptorUpdateScope::UNIFORM_BUFFERS : GLDescriptorUpdateScope::NONE;
    }

    [[nodiscard]] constexpr bool ShouldUpdateGLDescriptor( const GLDescriptorUpdateScope scope,
                                                            const DescriptorSetBindingType type ) noexcept
    {
        if ( scope == GLDescriptorUpdateScope::NONE )
        {
            return false;
        }
        if ( scope != GLDescriptorUpdateScope::UNIFORM_BUFFERS )
        {
            return true;
        }
        switch ( type )
        {
            case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER:
            case DescriptorSetBindingType::IMAGE:
            case DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC:
            case DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC:
            case DescriptorSetBindingType::INDIRECT_BUFFER:
                return false;
            default:
                return true;
        }
    }
}
