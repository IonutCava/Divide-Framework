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
#include "Platform/Video/Headers/DescriptorSets.h"

namespace Divide {

    class GFXDevice;
    FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);
    namespace GFX {
        struct MemoryBarrierCommand;
    };

    namespace Reflection {
        static constexpr U8 INVALID_BINDING_INDEX = std::numeric_limits<U8>::max();

        struct DataEntry {
            U8 _bindingSet{ 0u };
            U8 _bindingSlot{ INVALID_BINDING_INDEX };
            string _name;
        };

        struct ImageEntry : DataEntry {
            bool _combinedImageSampler{ false };
            bool _isWriteTarget{ false };
            bool _isMultiSampled{ false };
            bool _isArray{ false };
        };

        struct BufferMember {
            GFX::PushConstantType _type{ GFX::PushConstantType::COUNT };
            size_t _offset{ 0u };
            size_t _absoluteOffset{ 0u };
            size_t _size{ 0u };
            size_t _paddedSize{ 0u };
            size_t _arrayInnerSize{ 0u }; // array[innerSize][outerSize]
            size_t _arrayOuterSize{ 0u }; // array[innerSize][outerSize]
            size_t _vectorDimensions{ 0u };
            vec2<size_t> _matrixDimensions{ 0u, 0u }; //columns, rows
            string _name;

            size_t _memberCount{0u};
            vector<BufferMember> _members;
        };

        struct BufferEntry : DataEntry {
            size_t _offset{ 0u };
            size_t _absoluteOffset{ 0u };
            size_t _size{ 0u };
            size_t _paddedSize{ 0u };
            size_t _memberCount{ 0u };
            bool _uniformBuffer{ true };
            bool _dynamic{ false };
            vector<BufferMember> _members;
        };

        struct Data {
            U8 _uniformBlockBindingSet{ INVALID_BINDING_INDEX };
            U8 _uniformBlockBindingIndex{ INVALID_BINDING_INDEX };

            U16 _stageVisibility{ 0u };

            vector<ImageEntry> _images{};
            vector<BufferEntry> _buffers{};
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

        const Reflection::BufferEntry* FindUniformBlock(const Reflection::Data& data);
    }; //namespace Reflection


    class UniformBlockUploader {
    public:
        constexpr static U32 RingBufferLength = 6u;

        struct BlockMember
        {
            string _name;
            U64    _nameHash{ 0u };
            size_t _offset{ 0u };
            size_t _size{ 0u };
            size_t _elementSize{ 0u };
            size_t _arrayOuterSize{ 0u };
            size_t _arrayInnerSize{ 0u };
        };

        static void Idle();

        explicit UniformBlockUploader(GFXDevice& context, const eastl::string& parentShaderName, const Reflection::BufferEntry& uniformBlock, const U16 shaderStageVisibilityMask);

        void uploadPushConstant(const GFX::PushConstant& constant, bool force = false) noexcept;
        [[nodiscard]] bool commit(DescriptorSet& set, GFX::MemoryBarrierCommand& memCmdInOut);
        void onFrameEnd() noexcept;

        [[nodiscard]] size_t totalBufferSize() const noexcept;

        PROPERTY_R_IW(Reflection::BufferEntry, uniformBlock);

    private:
        void resizeBlockBuffer(bool increaseSize);
        [[nodiscard]] bool prepare(DescriptorSet& set);

    private:
        GFXDevice& _context;
        const U16 _shaderStageVisibilityMask{ to_base(ShaderStageVisibility::COUNT) };

        vector<Byte> _localDataCopy;
        vector<BlockMember> _blockMembers;
        eastl::string _parentShaderName;
        ShaderBuffer_uptr _buffer{ nullptr };
        size_t _uniformBlockSizeAligned{ 0u };
        bool _uniformBlockDirty{ false };
        bool _needsQueueIncrement{ false };

        bool _needsResize{ false };
        U32 _bufferWritesThisFrame{ 0u };
        U32 _bufferSizeFactor{ 0u };
    };

}; // namespace Divide

#endif //_SHADER_DATA_UPLOADER_H_