# Copyright (c) 2026 DIVIDE-Studio
# Copyright (c) 2009-2026 Ionut Cava
# SPDX-License-Identifier: MIT

"""Regression tests for the Hi-Z sampler and its backend mappings."""

import re
import unittest

if __package__:
    from .ExposureBufferTests import ROOT, run_cpp
else:
    from ExposureBufferTests import ROOT, run_cpp


VIDEO = ROOT / "Source" / "Platform" / "Video"


class HiZSamplerTests(unittest.TestCase):
    def test_sampler_uses_nearest_texels_and_nearest_mips_on_both_backends(self):
        device = (VIDEO / "GFXDevice.cpp").read_text(encoding="utf-8")
        begin = device.index("        SamplerDescriptor hiZSampler = {};")
        end = device.index("        RenderTargetDescriptor hizRTDesc = {};", begin)
        sampler_setup = device[begin:end]

        sampler = (VIDEO / "Textures" / "Headers" / "SamplerDescriptor.h").read_text(encoding="utf-8")
        lod = re.search(r"struct SamplerLOD\s*\{(.*?)\s*bool operator==", sampler, re.DOTALL)
        self.assertIsNotNone(lod)
        gl = (VIDEO / "RenderBackend" / "OpenGL" / "Textures" / "glSamplerOject.cpp").read_text(encoding="utf-8")
        gl_begin = gl.index("    [[nodiscard]] inline gl46core::GLenum getMagFilterMode")
        gl_end = gl.index("\n};", gl_begin)
        vk = (VIDEO / "RenderBackend" / "Vulkan" / "Textures" / "vkSamplerObject.cpp").read_text(encoding="utf-8")
        vk_begin = vk.index("        const F32 maxAnisotropy =")
        vk_end = vk.index("\n        samplerInfo.addressModeU =", vk_begin)

        source = r"""
#include <algorithm>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
using U8 = std::uint8_t;
using F32 = float;
template<typename T> F32 to_F32(T value) { return static_cast<F32>(value); }
template<typename T> U8 to_U8(T value) { return static_cast<U8>(value); }
enum class TextureFilter { LINEAR, NEAREST, COUNT };
enum class TextureMipSampling { LINEAR, NEAREST, NONE, COUNT };
enum class TextureWrap { REPEAT, CLAMP_TO_EDGE };
struct SamplerLOD { @LOD_FIELDS@ };
struct SamplerDescriptor {
    SamplerLOD _lod{};
    TextureFilter _minFilter = TextureFilter::LINEAR;
    TextureFilter _magFilter = TextureFilter::LINEAR;
    TextureMipSampling _mipSampling = TextureMipSampling::LINEAR;
    TextureWrap _wrapU = TextureWrap::REPEAT;
    TextureWrap _wrapV = TextureWrap::REPEAT;
    TextureWrap _wrapW = TextureWrap::REPEAT;
    U8 _anisotropyLevel = 255u;
};
namespace gl46core {
    enum GLenum { GL_LINEAR, GL_NEAREST, GL_NEAREST_MIPMAP_LINEAR,
                  GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_NEAREST };
}
@GL_FILTERS@
enum VkFilter { VK_FILTER_LINEAR, VK_FILTER_NEAREST };
enum class VkSamplerMipmapMode { VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST };
constexpr bool VK_FALSE = false;
struct VkSamplerCreateInfo {
    unsigned flags = 0;
    VkFilter minFilter{}, magFilter{};
    VkSamplerMipmapMode mipmapMode{};
    float minLod = 0, maxLod = 0, mipLodBias = 0, maxAnisotropy = 0;
    bool anisotropyEnable = false;
};
namespace vk {
    VkSamplerCreateInfo samplerCreateInfo() { return {}; }
}
struct GFXDevice {
    struct DeviceInformation { U8 _maxAnisotropy = 16u; };
    static DeviceInformation GetDeviceInformation() { return {}; }
};
VkSamplerCreateInfo MakeVulkanSampler(const SamplerDescriptor& descriptor) {
    @VK_SETUP@
    return samplerInfo;
}
int main() {
    @SAMPLER_SETUP@
    assert(hiZSampler._wrapU == TextureWrap::CLAMP_TO_EDGE);
    assert(hiZSampler._wrapV == TextureWrap::CLAMP_TO_EDGE);
    assert(hiZSampler._wrapW == TextureWrap::CLAMP_TO_EDGE);
    assert(hiZSampler._minFilter == TextureFilter::NEAREST);
    assert(hiZSampler._magFilter == TextureFilter::NEAREST);
    assert(hiZSampler._mipSampling == TextureMipSampling::NEAREST);
    assert(hiZSampler._anisotropyLevel == 0u);
    assert(getMinFilterMode(hiZSampler._minFilter, hiZSampler._mipSampling) ==
           gl46core::GL_NEAREST_MIPMAP_NEAREST);
    assert(getMagFilterMode(hiZSampler._magFilter) == gl46core::GL_NEAREST);
    assert(hiZSampler._lod._min == 0.f && hiZSampler._lod._max >= 4.f && hiZSampler._lod._bias == 0.f);
    const VkSamplerCreateInfo actual = MakeVulkanSampler(hiZSampler);
    assert(actual.minFilter == VK_FILTER_NEAREST && actual.magFilter == VK_FILTER_NEAREST);
    assert(actual.mipmapMode == VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST);
    assert(actual.minLod == 0.f && actual.maxLod >= 4.f && actual.mipLodBias == 0.f);
    assert(!actual.anisotropyEnable);
    hiZSampler._mipSampling = TextureMipSampling::NONE;
    assert(getMinFilterMode(hiZSampler._minFilter, hiZSampler._mipSampling) == gl46core::GL_NEAREST);
    assert(MakeVulkanSampler(hiZSampler).maxLod < 0.5f);
}
"""
        run_cpp(self, source.replace("@LOD_FIELDS@", lod.group(1))
                           .replace("@GL_FILTERS@", gl[gl_begin:gl_end])
                           .replace("@VK_SETUP@", vk[vk_begin:vk_end])
                           .replace("@SAMPLER_SETUP@", sampler_setup))

    def test_consumers_use_the_full_mip_view_and_reduction_uses_single_mip_views(self):
        device = (VIDEO / "GFXDevice.cpp").read_text(encoding="utf-8")
        texture = (VIDEO / "Textures" / "Texture.cpp").read_text(encoding="utf-8")
        vegetation = (ROOT / "Source" / "Environment" / "Vegetation" / "Vegetation.cpp").read_text(encoding="utf-8")
        reduction = (ROOT / "Assets" / "Shaders" / "GLSL" / "HiZConstruct.glsl").read_text(encoding="utf-8")

        self.assertIn("hiZDescriptor._mipMappingState = MipMappingState::MANUAL;", device)
        self.assertIn("InternalRTAttachmentDescriptor{ hiZDescriptor, hiZSampler", device)
        self.assertIn("return { HiZAtt->texture(), HiZAtt->_descriptor._sampler };", device)
        self.assertIn("Set( binding._data, hizTex->getView(), sampler );", device)
        self.assertIn("Set( binding._data, hizTexture->getView(), hizAttachment->_descriptor._sampler );", vegetation)
        self.assertRegex(texture, r"ImageView Texture::getView\(\) const noexcept[\s\S]*?"
                                  r"\._mipLevels\s*=\s*\{\s*\._count\s*=\s*_mipCount")
        self.assertIn("HiZTex->getView( { to_U16(i - 1u), 1u }, { 0u, 1u })", device)
        self.assertRegex(reduction, r"texelFetch\(\s*inImage\s*,\s*ivec2\(x,\s*y\)\s*,\s*0\s*\)")


if __name__ == "__main__":
    unittest.main()
