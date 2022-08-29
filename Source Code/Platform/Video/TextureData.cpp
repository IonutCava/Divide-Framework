#include "stdafx.h"

#include "Headers/TextureData.h"

namespace Divide {
TextureEntry::TextureEntry(const TextureData& data, const size_t samplerHash, const U8 binding) noexcept
    : _data(data),
        _sampler(samplerHash),
        _binding(binding)
{
}

[[nodiscard]] size_t GetHash(const TextureData& data) {
    size_t ret = 11;
    Util::Hash_combine(ret, data._textureHandle, to_base(data._textureType));
    return ret;
}

} //namespace Divide
