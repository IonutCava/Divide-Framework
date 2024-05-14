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

#ifndef DVD_MATERIAL_INL_
#define DVD_MATERIAL_INL_

namespace Divide {

inline void Material::lockInstancesForRead() const noexcept
{
    _instanceLock.lock_shared();
}

inline void Material::unlockInstancesForRead() const noexcept
{
    _instanceLock.unlock_shared();
}

inline void Material::lockInstancesForWrite() const noexcept
{
    _instanceLock.lock();
}

inline void Material::unlockInstancesForWrite() const noexcept
{
    _instanceLock.unlock();
}

inline const vector<Handle<Material>>& Material::getInstancesLocked() const noexcept
{
    return _instances;
}

inline const vector<Handle<Material>>& Material::getInstances() const
{
    SharedLock<SharedMutex> r_lock(_instanceLock);
    return getInstancesLocked();
}

inline Handle<Texture> Material::getTexture(const TextureSlot textureUsage) const
{
    SharedLock<SharedMutex> r_lock(_textureLock);
    return _textures[to_U32(textureUsage)]._ptr;
}

inline bool Material::hasTransparency() const noexcept
{
    return properties().translucencySource() != TranslucencySource::COUNT &&
           properties().overrides().transparencyEnabled();
}

inline bool Material::isReflective() const noexcept
{
    return properties().metallic() > 0.05f &&
           properties().roughness() < 0.99f;
}

inline bool Material::isRefractive() const noexcept
{
    return hasTransparency() &&
           properties().isRefractive();
}

inline ShaderProgramInfo& Material::shaderInfo(const RenderStagePass renderStagePass)
{
    return _shaderInfo[to_base(renderStagePass._stage)][to_base(renderStagePass._passType)][to_base(renderStagePass._variant)];
}

inline const ShaderProgramInfo& Material::shaderInfo(const RenderStagePass renderStagePass) const
{
    return _shaderInfo[to_base(renderStagePass._stage)][to_base(renderStagePass._passType)][to_base(renderStagePass._variant)];
}

inline void Material::addShaderDefine(const ShaderType type, const string& define, const bool addPrefix = true)
{
    if (type != ShaderType::COUNT)
    {
        addShaderDefineInternal(type, define, addPrefix);
    }
    else
    {
        for (U8 i = 0u; i < to_U8(ShaderType::COUNT); ++i)
        {
            addShaderDefine(static_cast<ShaderType>(i), define, addPrefix);
        }
    }
}

inline const Material::TextureInfo& Material::getTextureInfo(const TextureSlot usage) const
{
    return _textures[to_base(usage)];
}

inline void Material::addShaderDefineInternal(const ShaderType type, const string& define, bool addPrefix)
{
    ModuleDefines& defines = _extraShaderDefines[to_base(type)];

    if (!eastl::any_of(eastl::cbegin(defines),
                       eastl::cend(defines),
                       [&define, addPrefix](const ModuleDefine& it)
                       {
                            return it._addPrefix == addPrefix &&
                                   it._define.compare(define.c_str()) == 0;
                        }))
    {
        defines.emplace_back(define, addPrefix);
    }
}

inline const ModuleDefines& Material::shaderDefines(const ShaderType type) const
{
    assert(type != ShaderType::COUNT);
    return _extraShaderDefines[to_base(type)];
}

}; //namespace Divide

#endif //DVD_MATERIAL_INL_
