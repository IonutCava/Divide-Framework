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
#ifndef _RENDER_STATE_BLOCK_H
#define _RENDER_STATE_BLOCK_H

#include "RenderAPIEnums.h"
#include "Core/Headers/Hashable.h"

namespace Divide {

namespace TypeUtil {
    const char* ComparisonFunctionToString(ComparisonFunction func) noexcept;
    const char* StencilOperationToString(StencilOperation op) noexcept;
    const char* FillModeToString(FillMode mode) noexcept;
    const char* CullModeToString(CullMode mode) noexcept;

    ComparisonFunction StringToComparisonFunction(const char* name) noexcept;
    StencilOperation StringToStencilOperation(const char* name) noexcept;
    FillMode StringToFillMode(const char* name) noexcept;
    CullMode StringToCullMode(const char* name) noexcept;
};

class RenderStateBlock final : public GUIDWrapper, public Hashable {
    public:
       static void clear();
       /// Retrieve a state block by hash value.
       /// If the hash value does not exist in the state block map, return the default state block
       static const RenderStateBlock& get(size_t renderStateBlockHash);
       /// Returns false if the specified hash is not found in the map
       static const RenderStateBlock& get(size_t renderStateBlockHash, bool& blockFound);
       static void saveToXML(const RenderStateBlock& block, const string& entryName, boost::property_tree::ptree& pt);

       static RenderStateBlock loadFromXML(const string& entryName, const boost::property_tree::ptree& pt);

       static size_t defaultHash() noexcept;

    protected:
        using RenderStateMap = hashMap<size_t, RenderStateBlock, NoHash<size_t>>;
        static RenderStateMap s_stateBlockMap;
        static SharedMutex s_stateBlockMapMutex;
        static size_t s_defaultHashValue;

    public:
        RenderStateBlock() noexcept;
        RenderStateBlock(const RenderStateBlock& other) noexcept = default;

        /// Can't assign due to the GUID restrictions
        RenderStateBlock& operator=(const RenderStateBlock& other) noexcept = delete;
        /// Use "from" instead of "operator=" to bypass the GUID restrictions
        void from(const RenderStateBlock& other);

       bool operator==(const RenderStateBlock& rhs) const {
            return getHash() == rhs.getHash();
        }

       bool operator!=(const RenderStateBlock& rhs) const {
            return getHash() != rhs.getHash();
        }

        void setFillMode(FillMode mode) noexcept;
        void setTessControlPoints(U32 count) noexcept;
        void setZBias(F32 zBias, F32 zUnits) noexcept;
        void setZFunc(ComparisonFunction zFunc = ComparisonFunction::LEQUAL) noexcept;
        void flipCullMode() noexcept;
        void flipFrontFace() noexcept;
        void setCullMode(CullMode mode) noexcept;
        void setFrontFaceCCW(bool state) noexcept;
        void depthTestEnabled(bool enable) noexcept;
        void setScissorTest(bool enable) noexcept;

        void setStencil(bool enable,
                        U32 stencilRef = 0u,
                        StencilOperation stencilFailOp  = StencilOperation::KEEP,
                        StencilOperation stencilPassOp = StencilOperation::KEEP,
                        StencilOperation stencilZFailOp = StencilOperation::KEEP,
                        ComparisonFunction stencilFunc = ComparisonFunction::NEVER) noexcept;

        void setStencilReadWriteMask(U32 read, U32 write) noexcept;

        void setColourWrites(bool red, bool green, bool blue, bool alpha) noexcept;

        bool cullEnabled() const noexcept;

        size_t getHash() const override;

    public:
        PROPERTY_R(P32, colourWrite, P32_FLAGS_TRUE);
        PROPERTY_R(F32, zBias, 0.0f);
        PROPERTY_R(F32, zUnits, 0.0f);
        PROPERTY_R(U32, tessControlPoints, 3);
        PROPERTY_R(U32, stencilRef, 0u);
        PROPERTY_R(U32, stencilMask, 0xFFFFFFFF);
        PROPERTY_R(U32, stencilWriteMask, 0xFFFFFFFF);

        PROPERTY_R(ComparisonFunction, zFunc, ComparisonFunction::LEQUAL);
        PROPERTY_R(StencilOperation, stencilFailOp, StencilOperation::KEEP);
        PROPERTY_R(StencilOperation, stencilPassOp, StencilOperation::KEEP);
        PROPERTY_R(StencilOperation, stencilZFailOp, StencilOperation::KEEP);
        PROPERTY_R(ComparisonFunction, stencilFunc, ComparisonFunction::NEVER);

        PROPERTY_R(CullMode, cullMode, CullMode::BACK);
        PROPERTY_R(FillMode, fillMode, FillMode::SOLID);

        PROPERTY_R(bool, frontFaceCCW, true);
        PROPERTY_R(bool, scissorTestEnabled, false);
        PROPERTY_R(bool, depthTestEnabled, true);
        PROPERTY_R(bool, stencilEnable, false);

        mutable bool _dirty = true;
};

};  // namespace Divide
#endif
