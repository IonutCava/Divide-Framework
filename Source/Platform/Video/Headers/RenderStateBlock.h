/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef DVD_RENDER_STATE_BLOCK_H_
#define DVD_RENDER_STATE_BLOCK_H_

#include "Core/Headers/Hashable.h"

namespace Divide {

namespace TypeUtil {
    [[nodiscard]] const char* ComparisonFunctionToString(ComparisonFunction func) noexcept;
    [[nodiscard]] const char* StencilOperationToString(StencilOperation op) noexcept;
    [[nodiscard]] const char* FillModeToString(FillMode mode) noexcept;
    [[nodiscard]] const char* CullModeToString(CullMode mode) noexcept;

    [[nodiscard]] ComparisonFunction StringToComparisonFunction(const char* name) noexcept;
    [[nodiscard]] StencilOperation StringToStencilOperation(const char* name) noexcept;
    [[nodiscard]] FillMode StringToFillMode(const char* name) noexcept;
    [[nodiscard]] CullMode StringToCullMode(const char* name) noexcept;
};

struct RenderStateBlock
{
    P32 _colourWrite{P32_FLAGS_TRUE};
    F32 _zBias{0.f};
    F32 _zUnits{0.f};
    U32 _tessControlPoints{4u};
    U32 _stencilRef{0u};
    U32 _stencilMask{0xFFFFFFFF};
    U32 _stencilWriteMask{0xFFFFFFFF};

    ComparisonFunction _zFunc{ComparisonFunction::LEQUAL};
    StencilOperation   _stencilFailOp{StencilOperation::KEEP};
    StencilOperation   _stencilPassOp{StencilOperation::KEEP};
    StencilOperation   _stencilZFailOp{StencilOperation::KEEP};
    ComparisonFunction _stencilFunc{ComparisonFunction::NEVER};

    CullMode _cullMode{CullMode::BACK};
    FillMode _fillMode{FillMode::SOLID};

    bool _frontFaceCCW{true};
    bool _scissorTestEnabled{false};
    bool _depthTestEnabled{true};
    bool _depthWriteEnabled{true};
    bool _stencilEnabled{false};
    bool _primitiveRestartEnabled{ false };
    bool _rasterizationEnabled{ true };

    bool operator==(const RenderStateBlock&) const = default;
};


size_t GetHash( const RenderStateBlock& block );
void SaveToXML( const RenderStateBlock& block, const std::string& entryName, boost::property_tree::ptree& pt );
void LoadFromXML( const std::string& entryName, const boost::property_tree::ptree& pt, RenderStateBlock& blockInOut );

};  // namespace Divide
#endif //DVD_RENDER_STATE_BLOCK_H_
