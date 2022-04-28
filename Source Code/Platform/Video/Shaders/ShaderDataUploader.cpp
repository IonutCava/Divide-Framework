#include "stdafx.h"

#include "Headers/ShaderDataUploader.h"
#include "Platform/Video/Headers/GFXDevice.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4458)
#pragma warning(disable:4706)
#endif
#include <boost/regex.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace Divide {

namespace Reflection {
    constexpr U16 BYTE_BUFFER_VERSION = 1u;

    bool UniformCompare::operator()(const UniformDeclaration& lhs, const UniformDeclaration& rhs) const {
        //Note: this doesn't care about arrays so those won't sort properly to reduce wastage
        const auto g_TypePriority = [](const U64 typeHash) -> I32 {
            switch (typeHash) {
                case _ID("dmat4"):              //128 bytes
                case _ID("dmat4x3"): return 0;  // 96 bytes
                case _ID("dmat3")  : return 1;  // 72 bytes
                case _ID("dmat4x2"):            // 64 bytes
                case _ID("mat4")   : return 2;  // 64 bytes
                case _ID("dmat3x2"):            // 48 bytes
                case _ID("mat4x3") : return 3;  // 48 bytes
                case _ID("mat3")   : return 4;  // 36 bytes
                case _ID("dmat2"):              // 32 bytes
                case _ID("dvec4"):              // 32 bytes
                case _ID("mat4x2") : return 5;  // 32 bytes
                case _ID("dvec3"):              // 24 bytes
                case _ID("mat3x2") : return 6;  // 24 bytes
                case _ID("mat2"):               // 16 bytes
                case _ID("dvec2"):              // 16 bytes
                case _ID("bvec4"):              // 16 bytes
                case _ID("ivec4"):              // 16 bytes
                case _ID("uvec4"):              // 16 bytes
                case _ID("vec4")   : return 7;  // 16 bytes
                case _ID("bvec3"):              // 12 bytes
                case _ID("ivec3"):              // 12 bytes
                case _ID("uvec3"):              // 12 bytes
                case _ID("vec3")   : return 8;  // 12 bytes
                case _ID("double"):             //  8 bytes
                case _ID("bvec2"):              //  8 bytes
                case _ID("ivec2"):              //  8 bytes
                case _ID("uvec2"):              //  8 bytes
                case _ID("vec2")   : return 9;  //  8 bytes
                case _ID("int"):                //  4 bytes
                case _ID("uint"):               //  4 bytes
                case _ID("float")  : return 10; //  4 bytes
                // No real reason for this, but generated shader code looks cleaner
                case _ID("bool")   : return 11; //  4 bytes
                default: DIVIDE_UNEXPECTED_CALL(); break;
            }

            return 999;
        };

        const I32 lhsPriority = g_TypePriority(lhs._typeHash);
        const I32 rhsPriority = g_TypePriority(rhs._typeHash);
        if (lhsPriority != rhsPriority) {
            return lhsPriority < rhsPriority;
        }

        return lhs._name < rhs._name;
    };

bool SaveReflectionData(const ResourcePath& path, const ResourcePath& file, const Reflection::Data& reflectionDataIn, const eastl::set<U64>& atomIDsIn) {
    ByteBuffer buffer;
    buffer << BYTE_BUFFER_VERSION;
    buffer << reflectionDataIn._targetBlockBindingIndex;
    buffer << reflectionDataIn._targetBlockName;
    buffer << reflectionDataIn._blockSize;
    buffer << reflectionDataIn._blockMembers.size();
    for (const auto& member : reflectionDataIn._blockMembers) {
        buffer << std::string(member._name.c_str());
        buffer << to_base(member._type);
        buffer << member._offset;
        buffer << member._arrayInnerSize;
        buffer << member._arrayOuterSize;
        buffer << member._vectorDimensions;
        buffer << member._matrixDimensions.x;
        buffer << member._matrixDimensions.y;
    }
    
    buffer << atomIDsIn.size();
    for (const U64 id : atomIDsIn) {
        buffer << id;
    }

    return buffer.dumpToFile(path.c_str(), file.c_str());
}

bool LoadReflectionData(const ResourcePath& path, const ResourcePath& file, Reflection::Data& reflectionDataOut, eastl::set<U64>& atomIDsOut) {
    ByteBuffer buffer;
    if (buffer.loadFromFile(path.c_str(), file.c_str())) {
        auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
        buffer >> tempVer;
        if (tempVer == BYTE_BUFFER_VERSION) {
            buffer >> reflectionDataOut._targetBlockBindingIndex;
            buffer >> reflectionDataOut._targetBlockName;
            buffer >> reflectionDataOut._blockSize;

            size_t sizeTemp = 0u;
            buffer >> sizeTemp;
            reflectionDataOut._blockMembers.reserve(sizeTemp);

            std::string tempStr;
            std::underlying_type_t<GFX::PushConstantType> tempType{0u};
            
            for (size_t i = 0u; i < sizeTemp; ++i) {
                Reflection::BlockMember& tempMember = reflectionDataOut._blockMembers.emplace_back();
                buffer >> tempStr;
                tempMember._name = tempStr.c_str();
                buffer >> tempType;
                tempMember._type = static_cast<GFX::PushConstantType>(tempType);
                buffer >> tempMember._offset;
                buffer >> tempMember._arrayInnerSize;
                buffer >> tempMember._arrayOuterSize;
                buffer >> tempMember._vectorDimensions;
                buffer >> tempMember._matrixDimensions.x;
                buffer >> tempMember._matrixDimensions.y;
            }

            buffer >> sizeTemp;
            U64 tempID = 0u;
            for (size_t i = 0u; i < sizeTemp; ++i) {
                buffer >> tempID;
                atomIDsOut.insert(tempID);
            }

            return true;
        }
    }

    return false;
}

eastl::string GatherUniformDeclarations(const eastl::string & source, Reflection::UniformsSet& foundUniforms) {
    static const boost::regex uniformPattern { R"(^\s*uniform\s+\s*([^),^;^\s]*)\s+([^),^;^\s]*\[*\s*\]*)\s*(?:=*)\s*(?:\d*.*)\s*(?:;+))" };

    eastl::string ret;
    ret.reserve(source.size());

    string line;
    boost::smatch matches;
    istringstream input(source.c_str());
    while (std::getline(input, line)) {
        if (Util::BeginsWith(line, "uniform", true) &&
            boost::regex_search(line, matches, uniformPattern))
        {
            const auto type = Util::Trim(matches[1].str());

            foundUniforms.insert(Reflection::UniformDeclaration{
                type, //type
                _ID(type.c_str()), //type hash
                Util::Trim(matches[2].str())  //name
            });
        } else {
            ret.append(line.c_str());
            ret.append("\n");
        }
    }

    return ret;
}

};

UniformBlockUploader::UniformBlockUploader(GFXDevice& context, const UniformBlockUploaderDescriptor& descriptor) 
     : _descriptor(descriptor)
{
    const auto GetSizeOf = [](const GFX::PushConstantType type) noexcept -> size_t {
        switch (type) {
            case GFX::PushConstantType::INT: return sizeof(I32);
            case GFX::PushConstantType::UINT: return sizeof(U32);
            case GFX::PushConstantType::FLOAT: return sizeof(F32);
            case GFX::PushConstantType::DOUBLE: return sizeof(D64);
        };

        DIVIDE_UNEXPECTED_CALL_MSG("Unexpected push constant type");
        return 0u;
    };

    if (_descriptor._reflectionData._blockMembers.empty()) {
        return;
    }

    const size_t activeMembers = _descriptor._reflectionData._blockMembers.size();
    _blockMembers.resize(activeMembers);
    if (_descriptor._reflectionData._blockSize > _uniformBlockSizeAligned || _buffer == nullptr) {
        _uniformBlockSizeAligned = Util::GetAlignmentCorrected(_descriptor._reflectionData._blockSize, ShaderBuffer::AlignmentRequirement(ShaderBuffer::Usage::CONSTANT_BUFFER));
        _localDataCopy.resize(_uniformBlockSizeAligned);

        ShaderBufferDescriptor bufferDescriptor{};
        bufferDescriptor._name = _descriptor._reflectionData._targetBlockName.c_str();
        bufferDescriptor._name.append("_");
        bufferDescriptor._name.append(_descriptor._parentShaderName.c_str());
        bufferDescriptor._usage = ShaderBuffer::Usage::CONSTANT_BUFFER;
        bufferDescriptor._bufferParams._elementCount = 1;
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
        bufferDescriptor._ringBufferLength = 3;
        bufferDescriptor._bufferParams._elementSize = _uniformBlockSizeAligned;
        _buffer = context.newSB(bufferDescriptor);
    } else {
        _buffer->clearData();
    }

    std::memset(_localDataCopy.data(), 0, _localDataCopy.size());

    size_t requiredExtraMembers = 0;
    for (size_t member = 0; member < activeMembers; ++member) {
        BlockMember& bMember = _blockMembers[member];
        bMember._externalData = _descriptor._reflectionData._blockMembers[member];
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

    for (size_t member = 0; member < activeMembers; ++member) {
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

void UniformBlockUploader::uploadPushConstant(const GFX::PushConstant& constant, bool force) noexcept {
    if (constant.type() == GFX::PushConstantType::COUNT || constant.bindingHash() == 0u) {
        return;
    }

    if (_descriptor._reflectionData._targetBlockBindingIndex != Reflection::INVALID_BINDING_INDEX && _buffer != nullptr) {
        for (BlockMember& member : _blockMembers) {
            if (member._nameHash == constant.bindingHash()) {
                DIVIDE_ASSERT(constant.dataSize() <= member._size);

                      Byte* dst = &_localDataCopy.data()[member._externalData._offset];
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
}

void UniformBlockUploader::commit() {
    if (!_uniformBlockDirty) {
        return;
    }

    DIVIDE_ASSERT(_descriptor._reflectionData._targetBlockBindingIndex != Reflection::INVALID_BINDING_INDEX && _buffer != nullptr);
    const bool rebind = _needsQueueIncrement;
    if (_needsQueueIncrement) {
        _buffer->incQueue();
        _needsQueueIncrement = false;
    }
    _buffer->writeData(_localDataCopy.data());
    _needsQueueIncrement = true;
    _uniformBlockDirty = false;
    if (rebind) {
        prepare();
    }
}

void UniformBlockUploader::prepare() {
    if (_descriptor._reflectionData._targetBlockBindingIndex != Reflection::INVALID_BINDING_INDEX && _buffer != nullptr) {
        Attorney::ShaderBufferBind::bind(*_buffer, to_U8(_descriptor._reflectionData._targetBlockBindingIndex));
    }
}

}; //namespace Divide