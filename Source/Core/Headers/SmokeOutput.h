#pragma once
// Copyright (c) 2026 DIVIDE-Studio

#include <cstdio>

namespace Divide::RenderSmokeTest
{
    [[nodiscard]] std::FILE* OpenSharedOutput( const std::filesystem::path& path ) noexcept;
}
