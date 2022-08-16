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
#ifndef _SHADER_DATA_UPLOADER_H_
#define _SHADER_DATA_UPLOADER_H_

#include "Platform/Video/Headers/PushConstant.h"

namespace Divide {

    class GFXDevice;
    FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);
    namespace GFX {
        struct MemoryBarrierCommand;
    };

    namespace Reflection {
        static constexpr U8 INVALID_BINDING_INDEX = std::numeric_limits<U8>::max();

        struct BlockMember {
            GFX::PushConstantType _type{ GFX::PushConstantType::COUNT };
            Str64 _name{};
            size_t _offset{ 0u };
            size_t _arrayInnerSize{ 0u }; // array[innerSize][outerSize]
            size_t _arrayOuterSize{ 0u }; // array[innerSize][outerSize]
            size_t _vectorDimensions{ 0u };
            vec2<size_t> _matrixDimensions{ 0u, 0u }; //columns, rows
        };

        struct ImageEntry {
            bool _combinedImageSampler{ false };
            bool _isWriteTarget{ false };

            string _imageName;
            U8 _bindingSet{ 0u };
            U8 _targetImageBindingIndex{ INVALID_BINDING_INDEX };
        };

        struct Data {
            U8 _bindingSet{ 0u };
            U8 _targetBlockBindingIndex{ INVALID_BINDING_INDEX };
            string _targetBlockName;
            size_t _blockSize{ 0u };
            vector<BlockMember> _blockMembers{};
            std::array<bool, 16> _enabledAttributes;

            vector<ImageEntry> _images{};
        };


        struct UniformDeclaration {
            Str64 _type;
            U64 _typeHash = 0u;
            Str256 _name;
        };

        inline bool operator!=(const UniformDeclaration& lhs, const UniformDeclaration& rhs) noexcept {
            return lhs._typeHash != rhs._typeHash ||
                   lhs._name != rhs._name;
        }

        inline bool operator==(const UniformDeclaration& lhs, const UniformDeclaration& rhs) noexcept {
            return lhs._typeHash == rhs._typeHash &&
                   lhs._name == rhs._name;
        }
        struct UniformCompare {
            bool operator()(const UniformDeclaration& lhs, const UniformDeclaration& rhs) const;
        };

        using UniformsSet = eastl::set<UniformDeclaration, UniformCompare>;

        bool SaveReflectionData(const ResourcePath& path, const ResourcePath& file, const Data& reflectionDataIn, const eastl::set<U64>& atomIDsIn);
        bool LoadReflectionData(const ResourcePath& path, const ResourcePath& file, Data& reflectionDataOut, eastl::set<U64>& atomIDsOut);
        eastl::string GatherUniformDeclarations(const eastl::string& source, UniformsSet& foundUniforms);
    }; //namespace Reflection

    struct UniformBlockUploaderDescriptor {
        eastl::string _parentShaderName = "";
        Reflection::Data _reflectionData{};
    };

    class UniformBlockUploader {
    public:
        struct BlockMember
        {
            Reflection::BlockMember _externalData;
            U64    _nameHash{ 0u };
            size_t _size{ 0 };
            size_t _elementSize{ 0 };
        };


        explicit UniformBlockUploader(GFXDevice& context, const UniformBlockUploaderDescriptor& descriptor);

        void uploadPushConstant(const GFX::PushConstant& constant, bool force = false) noexcept;
        void commit(GFX::MemoryBarrierCommand& memCmdInOut);
        void prepare();

        PROPERTY_R_IW(UniformBlockUploaderDescriptor, descriptor);
    private:
        vector<Byte> _localDataCopy;
        vector<BlockMember> _blockMembers;
        ShaderBuffer_uptr _buffer = nullptr;
        size_t _uniformBlockSizeAligned = 0u;
        bool _uniformBlockDirty = false;
        bool _needsQueueIncrement = false;

    };

}; // namespace Divide

#endif //_SHADER_DATA_UPLOADER_H_