#include "stdafx.h"

#include "Headers/glBufferedPushConstantUploader.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"

namespace Divide {
    namespace {
        [[nodiscard]] size_t GetSizeOf(const GFX::PushConstantType type) noexcept {
            switch (type) {
                case GFX::PushConstantType::INT: return sizeof(I32);
                case GFX::PushConstantType::UINT: return sizeof(U32);
                case GFX::PushConstantType::FLOAT: return sizeof(F32);
                case GFX::PushConstantType::DOUBLE: return sizeof(D64);
            };

            DIVIDE_UNEXPECTED_CALL_MSG("Unexpected push constant type");
            return 0u;
        }
    };

    glBufferedPushConstantUploader::glBufferedPushConstantUploader(GFXDevice& context, const glBufferedPushConstantUploaderDescriptor& descriptor)
        : glPushConstantUploader(descriptor._programHandle)
        , _context(context)
        , _uniformBufferName(descriptor._uniformBufferName)
        , _parentShaderName(descriptor._parentShaderName)
        , _uniformBlockBindingIndex(descriptor._bindingIndex)
        , _reflectionData(descriptor._reflectionData)
    {
    }

    void glBufferedPushConstantUploader::commit() {
        if (!_uniformBlockDirty) {
            return;
        }

        DIVIDE_ASSERT(_uniformBlockBindingIndex != GLUtil::k_invalidObjectID && _uniformBuffer != nullptr);
        if (_needsQueueIncrement) {
            _uniformBuffer->incQueue();
            _needsQueueIncrement = false;
        }
        _uniformBuffer->writeData(_uniformBlockBuffer.data());
        _needsQueueIncrement = true;
        _uniformBlockDirty = false;
    }

    void glBufferedPushConstantUploader::prepare() {
        DIVIDE_ASSERT(_uniformBlockBindingIndex != GLUtil::k_invalidObjectID && _uniformBuffer != nullptr);
        _uniformBuffer->bind(to_U8(_uniformBlockBindingIndex));
    }

    void glBufferedPushConstantUploader::cacheUniforms() {
        if (_reflectionData._blockMembers.empty()) {
            return;
        }

        const GLint activeMembers = (GLint)_reflectionData._blockMembers.size();
        _blockMembers.resize(activeMembers);
        if (_reflectionData._blockSize > _uniformBlockSizeAligned || _uniformBuffer == nullptr) {
            _uniformBlockSizeAligned = GLUtil::GLMemory::GetAlignmentCorrected(_reflectionData._blockSize, ShaderBuffer::AlignmentRequirement(ShaderBuffer::Usage::CONSTANT_BUFFER));
            _uniformBlockBuffer.resize(_uniformBlockSizeAligned);

            ShaderBufferDescriptor bufferDescriptor = {};
            bufferDescriptor._usage = ShaderBuffer::Usage::CONSTANT_BUFFER;
            bufferDescriptor._bufferParams._elementCount = 1;
            bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
            bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
            bufferDescriptor._ringBufferLength = 3;
            bufferDescriptor._name = (_uniformBufferName + "_" + _parentShaderName).c_str();
            bufferDescriptor._bufferParams._elementSize = _uniformBlockSizeAligned;
            _uniformBuffer = _context.newSB(bufferDescriptor);
        } else {
            _uniformBuffer->clearData();
        }

        std::memset(_uniformBlockBuffer.data(), 0, _uniformBlockBuffer.size());

        size_t requiredExtraMembers = 0;
        for (GLint member = 0; member < activeMembers; ++member) {
            BlockMember& bMember = _blockMembers[member];
            bMember._externalData = _reflectionData._blockMembers[member];
            bMember._nameHash = _ID(bMember._externalData._name.c_str());
            bMember._elementSize = GetSizeOf(bMember._externalData._type);
            if (bMember._externalData._matrixDimensions.x > 0 || bMember._externalData._matrixDimensions.y > 0) {
                bMember._elementSize = bMember._externalData._matrixDimensions.x * bMember._externalData._matrixDimensions.y * bMember._elementSize;
            } else {
                bMember._elementSize = bMember._externalData._vectorDimensions * bMember._elementSize;
            }

            bMember._size = bMember._elementSize;
            if (bMember._externalData._arrayInnerSize > 0) {
                bMember._size *= bMember._externalData._arrayInnerSize;
            }
            if (bMember._externalData._arrayOuterSize > 0) {
                bMember._size *= bMember._externalData._arrayOuterSize;
            }

            requiredExtraMembers += (bMember._externalData._arrayInnerSize * bMember._externalData._arrayOuterSize);
        }

        vector<BlockMember> arrayMembers;
        arrayMembers.reserve(requiredExtraMembers);

        for (GLint member = 0; member < activeMembers; ++member) {
            const BlockMember& bMember = _blockMembers[member];
            size_t offset = 0u;
            if (bMember._externalData._arrayInnerSize > 0) {
                for (size_t i = 0; i < bMember._externalData._arrayOuterSize; ++i) {
                    for (size_t j = 0; j < bMember._externalData._arrayInnerSize; ++j) {
                        BlockMember newMember = bMember;
                        newMember._externalData._name = Util::StringFormat("%s[%d][%d]", bMember._externalData._name.c_str(), i, j);
                        newMember._nameHash = _ID(newMember._externalData._name.c_str());
                        newMember._externalData._arrayOuterSize -= i;
                        newMember._externalData._arrayInnerSize -= j;
                        newMember._size -= offset;
                        newMember._externalData._offset = offset;
                        offset += bMember._elementSize;
                        arrayMembers.push_back(newMember);
                    }
                }
                for (size_t i = 0; i < bMember._externalData._arrayOuterSize; ++i) {
                    BlockMember newMember = bMember;
                    newMember._externalData._name = Util::StringFormat("%s[%d]", bMember._externalData._name.c_str(), i);
                    newMember._nameHash = _ID(newMember._externalData._name.c_str());
                    newMember._externalData._arrayOuterSize -= i;
                    newMember._size -= i * (bMember._externalData._arrayInnerSize * bMember._elementSize);
                    newMember._externalData._offset = i * (bMember._externalData._arrayInnerSize * bMember._elementSize);
                    arrayMembers.push_back(newMember);
                }
            } else if (bMember._externalData._arrayOuterSize > 0) {
                for (size_t i = 0; i < bMember._externalData._arrayOuterSize; ++i) {
                    BlockMember newMember = bMember;
                    newMember._externalData._name = Util::StringFormat("%s[%d]", bMember._externalData._name.c_str(), i);
                    newMember._nameHash = _ID(newMember._externalData._name.c_str());
                    newMember._externalData._arrayOuterSize -= i;
                    newMember._size -= offset;
                    newMember._externalData._offset = offset;
                    offset += bMember._elementSize;
                    arrayMembers.push_back(newMember);
                }
            }
        }

        if (!arrayMembers.empty()) {
            _blockMembers.insert(end(_blockMembers), begin(arrayMembers), end(arrayMembers));
        }

    }

    void glBufferedPushConstantUploader::uploadPushConstant(const GFX::PushConstant& constant, [[maybe_unused]] const bool force) noexcept {
        if (constant.type() == GFX::PushConstantType::COUNT || constant.bindingHash() == 0u) {
            return;
        }

        assert(!_uniformBlockBuffer.empty());

        for (BlockMember& member : _blockMembers) {
            if (member._nameHash == constant.bindingHash()) {
                DIVIDE_ASSERT(constant.dataSize() <= member._size);

                      Byte* dst = &_uniformBlockBuffer.data()[member._externalData._offset];
                const Byte* src = constant.data();
                const size_t numBytes = constant.dataSize();

                if (std::memcmp(dst, src, numBytes) != 0) {
                    std::memcpy(dst, src, numBytes);
                    _uniformBlockDirty = true;
                }

                return;
            }
        }
    }

} // namespace Divide
