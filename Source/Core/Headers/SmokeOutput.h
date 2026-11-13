#pragma once
// Copyright (c) 2026 DIVIDE-Studio

#include <cstdio>
#include <filesystem>

namespace Divide::RenderSmokeTest
{
    [[nodiscard]] std::FILE* OpenSharedOutput( const std::filesystem::path& path ) noexcept;
}
