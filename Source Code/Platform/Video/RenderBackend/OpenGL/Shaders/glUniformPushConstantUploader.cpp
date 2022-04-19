#include "stdafx.h"

#include "Headers/glUniformPushConstantUploader.h"

namespace Divide {
    template<typename T_out, size_t T_out_count, typename T_in>
    std::pair<GLsizei, const T_out*> convertData(const GLsizei byteCount, const Byte* const values) noexcept {
        static_assert(sizeof(T_out) * T_out_count == sizeof(T_in), "Invalid cast data");

        const GLsizei size = byteCount / sizeof(T_in);
        return { size, (const T_out*)values };
    }

    glUniformPushConstantUploader::glUniformPushConstantUploader(const GLuint programHandle)
        : glPushConstantUploader(programHandle)
    {
    }

    void glUniformPushConstantUploader::cacheUniforms() {
        _shaderVarLocation.clear();

        GLint numActiveUniforms = 0;
        glGetProgramInterfaceiv(_programHandle, GL_UNIFORM, GL_ACTIVE_RESOURCES, &numActiveUniforms);

        vector<GLchar> nameData(256);
        vector<GLenum> properties;
        properties.push_back(GL_NAME_LENGTH);
        properties.push_back(GL_TYPE);
        properties.push_back(GL_ARRAY_SIZE);
        properties.push_back(GL_BLOCK_INDEX);
        properties.push_back(GL_LOCATION);

        vector<GLint> values(properties.size());

        for (GLint attrib = 0; attrib < numActiveUniforms; ++attrib) {
            glGetProgramResourceiv(_programHandle, GL_UNIFORM, attrib, static_cast<GLsizei>(properties.size()), properties.data(), static_cast<GLsizei>(values.size()), nullptr, &values[0]);
            if (values[3] != -1 || values[4] == -1) {
                continue;
            }
            nameData.resize(values[0]); //The length of the name.
            glGetProgramResourceName(_programHandle, GL_UNIFORM, attrib, static_cast<GLsizei>(nameData.size()), nullptr, &nameData[0]);
            std::string name((char*)&nameData[0], nameData.size() - 1);

            if (name.length() > 3 && Util::GetTrailingCharacters(name, 3) == "[0]") {
                const GLint arraySize = values[2];
                // Array uniform. Use non-indexed version as an equally-valid alias
                name = name.substr(0, name.length() - 3);
                const GLint startLocation = values.back();
                for (GLint i = 0; i < arraySize; ++i) {
                    //Arrays and structs will be assigned sequentially increasing locations, starting with the given location
                    insert(_shaderVarLocation, _ID(Util::StringFormat("%s[%d]", name.c_str(), i).c_str()), startLocation + i);
                }
            }

            insert(_shaderVarLocation, _ID(name.c_str()), values.back());
        }

        // Reupload any uniforms we may have cached so far
        for (const UniformsByNameHash::ShaderVarMap::value_type& it : _uniformsByNameHash._shaderVars) {
            uploadPushConstant(it.second, true);
        }
    }


    void glUniformPushConstantUploader::uploadPushConstant(const GFX::PushConstant& constant, const bool force) {
        const I32 binding = cachedValueUpdate(constant, force);
        uniform(binding, constant.type(), constant.data(), static_cast<GLsizei>(constant.dataSize()));
    }

    I32 glUniformPushConstantUploader::cachedValueUpdate(const GFX::PushConstant& constant, const bool force) {
        assert(_programHandle != 0u);
        assert(constant.type() != GFX::PushConstantType::COUNT && constant.bindingHash() > 0u);
        // Check the cache for the location
        const auto& locationIter = _shaderVarLocation.find(constant.bindingHash());
        if (locationIter != std::cend(_shaderVarLocation)) {
            UniformsByNameHash::ShaderVarMap& map = _uniformsByNameHash._shaderVars;
            const auto& constantIter = map.find(constant.bindingHash());
            if (constantIter != std::cend(map)) {
                if (force || constantIter->second != constant) {
                    constantIter->second = constant;
                } else {
                    return -1;
                }
            } else {
                emplace(map, constant.bindingHash(), constant);
            }

            return locationIter->second;
        }

        return -1;
    }

    void glUniformPushConstantUploader::uniform(const I32 binding, const GFX::PushConstantType type, const Byte* const values, const GLsizei byteCount) const {
        if (binding == -1) {
            return;
        }

        switch (type) {
        case GFX::PushConstantType::BOOL:
        {
            const auto&[size, data] = convertData<GLuint, 1, U32>(byteCount, values);
            glProgramUniform1uiv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::INT:
        {
            const auto&[size, data] = convertData<GLint, 1, I32>(byteCount, values);
            glProgramUniform1iv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::UINT:
        {
            const auto&[size, data] = convertData<GLuint, 1, U32>(byteCount, values);
            glProgramUniform1uiv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::DOUBLE:
        {
            const auto&[size, data] = convertData<GLdouble, 1, D64>(byteCount, values);
            glProgramUniform1dv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::FLOAT:
        {
            const auto&[size, data] = convertData<GLfloat, 1, F32>(byteCount, values);
            glProgramUniform1fv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::IVEC2:
        {
            const auto&[size, data] = convertData<GLint, 2, vec2<I32>>(byteCount, values);
            glProgramUniform2iv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::IVEC3:
        {
            const auto&[size, data] = convertData<GLint, 3, vec3<I32>>(byteCount, values);
            glProgramUniform3iv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::IVEC4:
        {
            const auto&[size, data] = convertData<GLint, 4, vec4<I32>>(byteCount, values);
            glProgramUniform4iv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::UVEC2:
        {
            const auto&[size, data] = convertData<GLuint, 2, vec2<U32>>(byteCount, values);
            glProgramUniform2uiv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::UVEC3:
        {
            const auto&[size, data] = convertData<GLuint, 3, vec3<U32>>(byteCount, values);
            glProgramUniform3uiv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::UVEC4:
        {
            const auto&[size, data] = convertData<GLuint, 4, vec4<U32>>(byteCount, values);
            glProgramUniform4uiv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::DVEC2:
        {
            const auto&[size, data] = convertData<GLdouble, 2, vec2<D64>>(byteCount, values);
            glProgramUniform2dv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::DVEC3:
        {
            const auto&[size, data] = convertData<GLdouble, 3, vec3<D64>>(byteCount, values);
            glProgramUniform3dv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::DVEC4:
        {
            const auto&[size, data] = convertData<GLdouble, 4, vec4<D64>>(byteCount, values);
            glProgramUniform4dv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::VEC2:
        {
            const auto&[size, data] = convertData<GLfloat, 2, vec2<F32>>(byteCount, values);
            glProgramUniform2fv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::VEC3:
        {
            const auto&[size, data] = convertData<GLfloat, 3, vec3<F32>>(byteCount, values);
            glProgramUniform3fv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::FCOLOUR3:
        {
            const auto&[size, data] = convertData<GLfloat, 3, FColour3>(byteCount, values);
            glProgramUniform3fv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::VEC4:
        {
            const auto&[size, data] = convertData<GLfloat, 4, vec4<F32>>(byteCount, values);
            glProgramUniform4fv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::FCOLOUR4:
        {
            const auto&[size, data] = convertData<GLfloat, 4, FColour4>(byteCount, values);
            glProgramUniform3fv(_programHandle, binding, size, data);
        } break;
        case GFX::PushConstantType::IMAT2:
        {
            const auto&[size, data] = convertData<GLfloat, 4, mat2<I32>>(byteCount, values);
            glProgramUniformMatrix2fv(_programHandle, binding, size, GL_FALSE, data);
        } break;
        case GFX::PushConstantType::IMAT3:
        {
            const auto&[size, data] = convertData<GLfloat, 9, mat3<I32>>(byteCount, values);
            glProgramUniformMatrix3fv(_programHandle, binding, size, GL_FALSE, data);
        } break;
        case GFX::PushConstantType::IMAT4:
        {
            const auto&[size, data] = convertData<GLfloat, 16, mat4<I32>>(byteCount, values);
            glProgramUniformMatrix4fv(_programHandle, binding, size, GL_FALSE, data);
        } break;
        case GFX::PushConstantType::UMAT2:
        {
            const auto&[size, data] = convertData<GLfloat, 4, mat2<U32>>(byteCount, values);
            glProgramUniformMatrix2fv(_programHandle, binding, size, GL_FALSE, data);
        } break;
        case GFX::PushConstantType::UMAT3:
        {
            const auto&[size, data] = convertData<GLfloat, 9, mat3<U32>>(byteCount, values);
            glProgramUniformMatrix3fv(_programHandle, binding, size, GL_FALSE, data);
        } break;
        case GFX::PushConstantType::UMAT4:
        {
            const auto&[size, data] = convertData<GLfloat, 16, mat4<U32>>(byteCount, values);
            glProgramUniformMatrix4fv(_programHandle, binding, size, GL_FALSE, data);
        } break;
        case GFX::PushConstantType::MAT2:
        {
            const auto&[size, data] = convertData<GLfloat, 4, mat2<F32>>(byteCount, values);
            glProgramUniformMatrix2fv(_programHandle, binding, size, GL_FALSE, data);
        } break;
        case GFX::PushConstantType::MAT3:
        {
            const auto&[size, data] = convertData<GLfloat, 9, mat3<F32>>(byteCount, values);
            glProgramUniformMatrix3fv(_programHandle, binding, size, GL_FALSE, data);
        } break;
        case GFX::PushConstantType::MAT4:
        {
            const auto&[size, data] = convertData<GLfloat, 16, mat4<F32>>(byteCount, values);
            glProgramUniformMatrix4fv(_programHandle, binding, size, GL_FALSE, data);
        } break;
        case GFX::PushConstantType::DMAT2:
        {
            const auto&[size, data] = convertData<GLdouble, 4, mat2<D64>>(byteCount, values);
            glProgramUniformMatrix2dv(_programHandle, binding, size, GL_FALSE, data);
        } break;
        case GFX::PushConstantType::DMAT3:
        {
            const auto&[size, data] = convertData<GLdouble, 9, mat3<D64>>(byteCount, values);
            glProgramUniformMatrix3dv(_programHandle, binding, size, GL_FALSE, data);
        } break;
        case GFX::PushConstantType::DMAT4:
        {
            const auto&[size, data] = convertData<GLdouble, 16, mat4<D64>>(byteCount, values);
            glProgramUniformMatrix4dv(_programHandle, binding, size, GL_FALSE, data);
        } break;
        default:
            DIVIDE_ASSERT(false, "glShaderProgram::Uniform error: Unhandled data type!");
        }
    }

    void glUniformPushConstantUploader::commit() noexcept {
        NOP();
    }
    
    void glUniformPushConstantUploader::prepare() noexcept {
        NOP();
    }
} // namespace Divide
