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

#ifndef _MATERIAL_INL_
#define _MATERIAL_INL_

namespace Divide {

inline Texture_wptr Material::getTexture(const TextureUsage textureUsage) const {
    SharedLock<SharedMutex> r_lock(_textureLock);
    return _textures[to_U32(textureUsage)];
}


inline bool Material::hasTransparency() const {
    return _translucencySource != TranslucencySource::COUNT && _transparencyEnabled;
}

inline bool Material::isPBRMaterial() const {
    return shadingMode() == ShadingMode::OREN_NAYAR || shadingMode() == ShadingMode::COOK_TORRANCE;
}

inline bool Material::reflective() const {
    return metallic() > 0.05f && roughness() < 0.99f;
}

inline bool Material::refractive() const {
    return hasTransparency() && _isRefractive;
}


inline ShaderProgramInfo& Material::shaderInfo(const RenderStagePass& renderStagePass) {
    ShaderPerVariant& variantMap = _shaderInfo[to_base(renderStagePass._stage)][to_base(renderStagePass._passType)];
    assert(renderStagePass._variant < g_maxVariantsPerPass);

    return variantMap[renderStagePass._variant];
}

inline const ShaderProgramInfo& Material::shaderInfo(const RenderStagePass& renderStagePass) const {
    const ShaderPerVariant& variantMap = _shaderInfo[to_base(renderStagePass._stage)][to_base(renderStagePass._passType)];
    assert(renderStagePass._variant < g_maxVariantsPerPass);

    return variantMap[renderStagePass._variant];
}

inline void Material::addShaderDefine(const ShaderType type, const Str128& define, const bool addPrefix) {
    if (type != ShaderType::COUNT) {
        addShaderDefineInternal(type, define, addPrefix);
    } else {
        for (U8 i = 0; i < to_U8(ShaderType::COUNT); ++i) {
            addShaderDefine(static_cast<ShaderType>(i), define, addPrefix);
        }
    }
}

inline void Material::addShaderDefineInternal(const ShaderType type, const Str128& define, bool addPrefix) {
    ModuleDefines& defines = _extraShaderDefines[to_base(type)];

    if (!eastl::any_of(eastl::cbegin(defines),
                       eastl::cend(defines),
                       [&define, addPrefix](const auto& it) {
                            return it.second == addPrefix &&
                                   it.first.compare(define.c_str()) == 0;
                        }))
    {
        defines.emplace_back(define.c_str(), addPrefix);
    }
}

inline const ModuleDefines& Material::shaderDefines(const ShaderType type) const {
    assert(type != ShaderType::COUNT);

    return _extraShaderDefines[to_base(type)];
}

}; //namespace Divide

#endif //_MATERIAL_INL_
