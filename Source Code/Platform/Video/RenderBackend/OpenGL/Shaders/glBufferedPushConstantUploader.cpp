#include "stdafx.h"

#include "Headers/glBufferedPushConstantUploader.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

    glBufferedPushConstantUploader::glBufferedPushConstantUploader(const glBufferedPushConstantUploaderDescriptor& descriptor)
        : glPushConstantUploader(descriptor._programHandle)
        , _uniformBufferName(descriptor._uniformBufferName)
        , _parentShaderName(descriptor._parentShaderName)
        , _uniformBlockIndex(descriptor._blockIndex)
    {
    }

    glBufferedPushConstantUploader::~glBufferedPushConstantUploader() {
        if (_uniformBlockBuffer != nullptr) {
            MemoryManager::SAFE_DELETE_ARRAY(_uniformBlockBuffer);
        }
        if (_uniformBlockBufferHandle != GLUtil::k_invalidObjectID) {
            glDeleteBuffers(1, &_uniformBlockBufferHandle);
            _uniformBlockBufferHandle = 0u;
        }
    }

    void glBufferedPushConstantUploader::commit() {
        if (_uniformBlockDirty) {
            glInvalidateBufferData(_uniformBlockBufferHandle);
            glNamedBufferData(_uniformBlockBufferHandle, _uniformBlockSize, _uniformBlockBuffer, GL_STATIC_DRAW);
            _uniformBlockDirty = false;

            assert(GL_API::getStateTracker().getBoundBuffer(GL_UNIFORM_BUFFER, _uniformBlockIndex) == _uniformBlockBufferHandle);
        }
    }

    void glBufferedPushConstantUploader::prepare() {
        if (_uniformBlockBufferHandle != GLUtil::k_invalidObjectID) {
            assert(_uniformBlockIndex != GLUtil::k_invalidObjectID);
            GL_API::getStateTracker().setActiveBufferIndex(GL_UNIFORM_BUFFER, _uniformBlockBufferHandle, _uniformBlockIndex);
        }
    }

    void glBufferedPushConstantUploader::cacheUniforms() {
        if (_uniformBlockIndex == GLUtil::k_invalidObjectID) {
            return;
        }
        const GLuint blockIndex = glGetUniformBlockIndex(_programHandle, _uniformBufferName.c_str());
        if (blockIndex == GLUtil::k_invalidObjectID) {
            return;
        }

        GLint blockSize = 0u;
        glGetActiveUniformBlockiv(_programHandle, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
        assert(blockSize > 0);

        if (blockSize != _uniformBlockSize || _uniformBlockSize == 0) {
            _uniformBlockSize = blockSize;
            // Changed uniforms. Can't really hot reload shader so just thrash everything. I guess ...
            if (_uniformBlockBuffer != nullptr) {
                MemoryManager::SAFE_DELETE_ARRAY(_uniformBlockBuffer);
            }
            _uniformBlockBuffer = MemoryManager_NEW Byte[_uniformBlockSize]();
        } else {
            std::memset(_uniformBlockBuffer, 0, _uniformBlockSize);
        }

        if (_uniformBlockBufferHandle == GLUtil::k_invalidObjectID) {
            glCreateBuffers(1, &_uniformBlockBufferHandle);
            glNamedBufferData(_uniformBlockBufferHandle, _uniformBlockSize, _uniformBlockBuffer, GL_STATIC_DRAW);
            glObjectLabel(GL_BUFFER, _uniformBlockBufferHandle, -1, (_uniformBufferName + "_"  + _parentShaderName).c_str());
        } else {
            glInvalidateBufferData(_uniformBlockBufferHandle);
        }

        GLint activeMembers = 0;
        glGetActiveUniformBlockiv(_programHandle, blockIndex, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &activeMembers);

        vector<GLuint> blockIndices(activeMembers);
        glGetActiveUniformBlockiv(_programHandle, blockIndex, GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES, reinterpret_cast<GLint*>(&blockIndices[0]));

        vector<GLint> nameLengthOut(activeMembers);
        vector<GLint> offsetsOut(activeMembers);
        vector<GLint> uniArrayStride(activeMembers);
        vector<GLint> uniMatStride(activeMembers);
        glGetActiveUniformsiv(_programHandle, activeMembers, blockIndices.data(), GL_UNIFORM_NAME_LENGTH, &nameLengthOut[0]);
        glGetActiveUniformsiv(_programHandle, activeMembers, blockIndices.data(), GL_UNIFORM_OFFSET, &offsetsOut[0]);
        glGetActiveUniformsiv(_programHandle, activeMembers, blockIndices.data(), GL_UNIFORM_ARRAY_STRIDE, &uniArrayStride[0]);
        glGetActiveUniformsiv(_programHandle, activeMembers, blockIndices.data(), GL_UNIFORM_MATRIX_STRIDE, &uniMatStride[0]);

        _blockMembers.resize(activeMembers);
        for (GLint member = 0; member < activeMembers; ++member) {
            BlockMember& bMember = _blockMembers[member];
            bMember._name.resize(nameLengthOut[member]);
            glGetActiveUniform(_programHandle, blockIndices[member], static_cast<GLsizei>(bMember._name.size()), nullptr, &bMember._arraySize, &bMember._type, &bMember._name[0]);
            bMember._name.pop_back();
            bMember._nameHash = _ID(bMember._name.c_str());

            bMember._index = blockIndices[member];
            bMember._offset = offsetsOut[member];
            if (uniArrayStride[member] > 0) {
                bMember._size = to_size(bMember._arraySize) * uniArrayStride[member];
            } else if (uniMatStride[member] > 0) {
                switch (bMember._type) {
                    case GL_FLOAT_MAT2:
                    case GL_FLOAT_MAT2x3:
                    case GL_FLOAT_MAT2x4:
                    case GL_DOUBLE_MAT2:
                    case GL_DOUBLE_MAT2x3:
                    case GL_DOUBLE_MAT2x4:
                        bMember._size = to_size(uniMatStride[member]) * 2;
                        break;
                    case GL_FLOAT_MAT3:
                    case GL_FLOAT_MAT3x2:
                    case GL_FLOAT_MAT3x4:
                    case GL_DOUBLE_MAT3:
                    case GL_DOUBLE_MAT3x2:
                    case GL_DOUBLE_MAT3x4:
                        bMember._size = to_size(uniMatStride[member]) * 3;
                        break;
                    case GL_FLOAT_MAT4:
                    case GL_FLOAT_MAT4x2:
                    case GL_FLOAT_MAT4x3:
                    case GL_DOUBLE_MAT4:
                    case GL_DOUBLE_MAT4x2:
                    case GL_DOUBLE_MAT4x3:
                        bMember._size = to_size(uniMatStride[member]) * 4;
                        break;
                    default: break;
                }
            } else {
                bMember._size = GLUtil::glTypeSizeInBytes[bMember._type];
            }
        }

        vector<BlockMember> arrayMembers;
        for (GLint idx = 0; idx < activeMembers; ++idx) {
            BlockMember& member = _blockMembers[idx];
            if (member._name.length() > 3) {
                if (Util::BeginsWith(member._name, "UBM", true)) { //UBM: UNIFORM BLOCK MARKER
                    const string newName = member._name.c_str();
                    member._name = newName.substr(3, newName.length() - 3);
                    member._nameHash = _ID(member._name.c_str());
                }
                if (member._arraySize >= 1 && Util::GetTrailingCharacters(member._name, 3) == "[0]") {
                    // Array uniform. Use non-indexed version as an equally-valid alias
                    const string newName = member._name.c_str();
                    member._name = newName.substr(0, newName.length() - 3);
                    member._nameHash = _ID(member._name.c_str());
                    const size_t elementSize = member._size / member._arraySize;

                    for (GLint i = 0; i < member._arraySize; ++i) {
                        BlockMember newMember = member;
                        newMember._name.append("[" + Util::to_string(i) + "]");
                        newMember._nameHash = _ID(newMember._name.c_str());
                        newMember._index += i;
                        newMember._arraySize -= i;
                        newMember._size -= elementSize * i;
                        newMember._offset += elementSize * i;
                        arrayMembers.push_back(newMember);
                    }
                }
            }
        }
        if (!arrayMembers.empty()) {
            _blockMembers.insert(end(_blockMembers), begin(arrayMembers), end(arrayMembers));
        }

        eastl::sort(begin(_blockMembers), end(_blockMembers), [](const BlockMember& bA, const BlockMember& bB) -> bool {return bA._index < bB._index; });
    }


    void glBufferedPushConstantUploader::uploadPushConstant(const GFX::PushConstant& constant, bool force) noexcept {
        if (_uniformBlockBufferHandle == GLUtil::k_invalidObjectID ||
            constant._type == GFX::PushConstantType::COUNT ||
            constant._bindingHash == 0u)         
        {
            return;
        }

        assert(_uniformBlockBuffer != nullptr);

        for (BlockMember& member : _blockMembers) {
            if (member._nameHash == constant._bindingHash) {
                DIVIDE_ASSERT(constant._buffer.size() <= member._size);

                      Byte*  dst      = _uniformBlockBuffer + member._offset;
                const Byte*  src      = constant._buffer.data();
                const size_t numBytes = constant._buffer.size();

                if (std::memcmp(dst, src, numBytes) != 0) {
                    std::memcpy(dst, src, numBytes);
                    _uniformBlockDirty = true;
                }
                return;
            }
        }
    }

} // namespace Divide
