// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include "Core/Headers/Configuration.h"
#include "Platform/Video/Headers/PushConstants.h"

namespace Divide::SSAO
{
    [[nodiscard]] inline std::array<I32, 2> ConfiguredBlurKernelSizes(
        const Configuration::SSAOSettings& fullRes, const Configuration::SSAOSettings& halfRes ) noexcept
    {
        return { { fullRes.BlurKernelSize, halfRes.BlurKernelSize } };
    }

    [[nodiscard]] inline vec2<U16> HalfResolution( const vec2<U16>& resolution ) noexcept
    {
        return { std::max<U16>( 1u, to_U16( ( resolution.width + 1u ) / 2u ) ),
                 std::max<U16>( 1u, to_U16( ( resolution.height + 1u ) / 2u ) ) };
    }

    inline void UploadSampleKernel( UniformData& uniforms, const vector<float4>& samples )
    {
        if ( !DIVIDE_ASSERT( !samples.empty(), "SSAO sample kernel must not be empty" ) )
        {
            return;
        }
        uniforms.set( _ID( "sampleKernel" ), PushConstantType::VEC4, samples.data(), samples.size() );
    }

    template<typename Target>
    void ResizeTargets( const vec2<U16>& resolution, Target& output, Target& blur,
                        Target& halfOutput, Target& halfDepthAndNormals )
    {
        const vec2<U16> halfResolution = HalfResolution( resolution );
        output.resize( resolution.width, resolution.height );
        blur.resize( resolution.width, resolution.height );
        halfOutput.resize( halfResolution.width, halfResolution.height );
        halfDepthAndNormals.resize( halfResolution.width, halfResolution.height );
    }
}
