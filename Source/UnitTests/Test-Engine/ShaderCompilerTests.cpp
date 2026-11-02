/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#include "UnitTests/unitTestCommon.h"

#include "Platform/Video/Headers/RenderAPIEnums.h"
#include "Platform/Video/Shaders/Headers/GLSLToSPIRV.h"

namespace Divide
{
namespace
{
    struct ShaderCompilerLifetime
    {
        ShaderCompilerLifetime() : _ownsInitialization( !SpirvHelper::s_isInit )
        {
            if ( _ownsInitialization )
            {
                SpirvHelper::Init();
            }
        }

        ~ShaderCompilerLifetime()
        {
            if ( _ownsInitialization )
            {
                SpirvHelper::Finalize();
            }
        }

        bool _ownsInitialization;
    };
}

TEST_CASE( "Shader compilation uses the selected backend's SPIR-V environment", "[shader]" )
{
    const ShaderCompilerLifetime compilerLifetime{};
    constexpr char openGLShader[] = R"(
        #version 460
        layout(location = 0) in vec3 position;
        layout(std140, binding = 1) uniform DrawData { mat4 transform; };
        void main() { gl_Position = transform * vec4(position, 1.f); }
    )";
    constexpr char vulkanShader[] = R"(
        #version 460
        layout(location = 0) in vec3 position;
        layout(std140, set = 1, binding = 1) uniform DrawData { mat4 transform; };
        layout(push_constant) uniform PushData { vec4 offset; } pushData;
        void main() { gl_Position = transform * vec4(position, 1.f) + pushData.offset; }
    )";

    std::vector<unsigned int> spirv;
    REQUIRE( SpirvHelper::GLSLtoSPV( ShaderType::VERTEX, openGLShader, spirv, RenderAPI::OpenGL ) );
    REQUIRE( spirv.size() >= 5 );
    CHECK( spirv[0] == 0x07230203u );
    CHECK( spirv[1] == 0x00010000u );

    spirv.clear();
    REQUIRE( SpirvHelper::GLSLtoSPV( ShaderType::VERTEX, vulkanShader, spirv, RenderAPI::Vulkan ) );
    REQUIRE( spirv.size() >= 5 );
    CHECK( spirv[0] == 0x07230203u );
    CHECK( spirv[1] == 0x00010600u );
}
}
