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
#ifndef DVD_SHADER_DATA_UPLOADER_H_
#define DVD_SHADER_DATA_UPLOADER_H_

#include "Platform/Video/Headers/PushConstants.h"
#include "Platform/Video/Headers/DescriptorSetsFwd.h"

namespace Divide
{
    class GFXDevice;
    FWD_DECLARE_MANAGED_CLASS( ShaderBuffer );
    namespace GFX
    {
        struct MemoryBarrierCommand;
    };

    namespace Reflection
    {
        static constexpr U8 INVALID_BINDING_INDEX = U8_MAX;

        struct DataEntry
        {
            string _name;
            U16 _stageVisibility{ 0u };
            U8 _bindingSet{ 0u };
            U8 _bindingSlot{ INVALID_BINDING_INDEX };
        };

        struct ImageEntry : DataEntry
        {
            bool _combinedImageSampler{ false };
            bool _isWriteTarget{ false };
            bool _isMultiSampled{ false };
            bool _isArray{ false };
        };

        struct BufferMember
        {
            vector<BufferMember> _members;
            string _name;
            vec2<size_t> _matrixDimensions{ 0u, 0u }; //columns, rows
            size_t _offset{ 0u };
            size_t _absoluteOffset{ 0u };
            size_t _size{ 0u };
            size_t _paddedSize{ 0u };
            size_t _arrayInnerSize{ 0u }; // array[innerSize][outerSize]
            size_t _arrayOuterSize{ 0u }; // array[innerSize][outerSize]
            size_t _vectorDimensions{ 0u };
            size_t _memberCount{ 0u };
            PushConstantType _type{ PushConstantType::COUNT };
        };

        struct BufferEntry : DataEntry
        {
            vector<BufferMember> _members;

            size_t _offset{ 0u };
            size_t _absoluteOffset{ 0u };
            size_t _size{ 0u };
            size_t _paddedSize{ 0u };
            size_t _memberCount{ 0u };
            bool _uniformBuffer{ true };
            bool _dynamic{ false };
        };

        struct Data
        {
            vector<ImageEntry> _images{};
            vector<BufferEntry> _buffers{};
            std::array<bool, to_base(RTColourAttachmentSlot::COUNT)> _fragmentOutputs{};

            U8 _uniformBlockBindingSet{ to_base( DescriptorSetUsage::PER_DRAW ) };
            U8 _uniformBlockBindingIndex{ INVALID_BINDING_INDEX };
        };


        struct UniformDeclaration
        {
            Str<256> _name;
            Str<64> _type;
            U64 _typeHash = 0u;
        };

        inline bool operator!=( const UniformDeclaration& lhs, const UniformDeclaration& rhs ) noexcept
        {
            return lhs._typeHash != rhs._typeHash ||
                   lhs._name.compare(rhs._name) != 0;
        }

        inline bool operator==( const UniformDeclaration& lhs, const UniformDeclaration& rhs ) noexcept
        {
            return lhs._typeHash == rhs._typeHash &&
                   lhs._name.compare(rhs._name) == 0;
        }
        struct UniformCompare
        {
            bool operator()( const UniformDeclaration& lhs, const UniformDeclaration& rhs ) const;
        };

        using UniformsSet = eastl::set<UniformDeclaration, UniformCompare>;

        bool SaveReflectionData( const ResourcePath& path, const ResourcePath& file, const Data& reflectionDataIn, const eastl::set<U64>& atomIDsIn );
        bool LoadReflectionData( const ResourcePath& path, const ResourcePath& file, Data& reflectionDataOut, eastl::set<U64>& atomIDsOut );
        void PreProcessUniforms( string& sourceInOut, UniformsSet& foundUniforms );

        const Reflection::BufferEntry* FindUniformBlock( const Reflection::Data& data );
    }; //namespace Reflection


class UniformBlockUploader
{
public:
    constexpr static U16 RingBufferLength = 6u;

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

    explicit UniformBlockUploader( GFXDevice& context, const eastl::string& parentShaderName, const Reflection::BufferEntry& uniformBlock, const U16 shaderStageVisibilityMask );

    void uploadPushConstant( const PushConstants& constants ) noexcept;
    [[nodiscard]] bool commit( DescriptorSet& set, GFX::MemoryBarrierCommand& memCmdInOut );
    void onFrameEnd() noexcept;
    void toggleStageVisibility( U16 visibilityMask, bool state);
    void toggleStageVisibility( ShaderStageVisibility visibility, bool state);

    [[nodiscard]] size_t totalBufferSize() const noexcept;

    PROPERTY_R_IW( Reflection::BufferEntry, uniformBlock );

private:
    void resizeBlockBuffer( bool increaseSize );
    [[nodiscard]] bool prepare( DescriptorSet& set );

private:
    GFXDevice& _context;
    U16 _shaderStageVisibilityMask{ to_base( ShaderStageVisibility::COUNT ) };

    vector<Byte> _localDataCopy;
    vector<BlockMember> _blockMembers;
    eastl::string _parentShaderName;
    ShaderBuffer_uptr _buffer{ nullptr };
    size_t _uniformBlockSizeAligned{ 0u };
    bool _uniformBlockDirty{ false };
    bool _needsQueueIncrement{ false };

    bool _needsResize{ false };
    U16 _bufferWritesThisFrame{ 0u };
    U16 _bufferSizeFactor{ 0u };
};

}; // namespace Divide

#endif //DVD_SHADER_DATA_UPLOADER_H_
