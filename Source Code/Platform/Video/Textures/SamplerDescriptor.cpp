#include "stdafx.h"

#include "Headers/SamplerDescriptor.h"
#include "Platform/Video/Headers/RenderStateBlock.h"

namespace Divide {
    namespace TypeUtil {
        const char* TextureBorderColourToString(const TextureBorderColour colour) noexcept {
            return Names::textureBorderColour[to_base(colour)];
        }

        TextureBorderColour StringToTextureBorderColour(const string& colour) {
            for (U8 i = 0; i < to_U8(TextureBorderColour::COUNT); ++i) {
                if (strcmp(colour.c_str(), Names::textureBorderColour[i]) == 0) {
                    return static_cast<TextureBorderColour>(i);
                }
            }

            return TextureBorderColour::COUNT;
        }

        const char* WrapModeToString(const TextureWrap wrapMode) noexcept {
            return Names::textureWrap[to_base(wrapMode)];
        }

        TextureWrap StringToWrapMode(const string& wrapMode) {
            for (U8 i = 0; i < to_U8(TextureWrap::COUNT); ++i) {
                if (strcmp(wrapMode.c_str(), Names::textureWrap[i]) == 0) {
                    return static_cast<TextureWrap>(i);
                }
            }

            return TextureWrap::COUNT;
        }

        const char* TextureFilterToString(const TextureFilter filter) noexcept {
            return Names::textureFilter[to_base(filter)];
        }

        TextureFilter StringToTextureFilter(const string& filter) {
            for (U8 i = 0; i < to_U8(TextureFilter::COUNT); ++i) {
                if (strcmp(filter.c_str(), Names::textureFilter[i]) == 0) {
                    return static_cast<TextureFilter>(i);
                }
            }

            return TextureFilter::COUNT;
        }

        const char* TextureMipSamplingToString(TextureMipSampling sampling) noexcept {
            return Names::textureMipSampling[to_base(sampling)];
        }

        TextureMipSampling StringToTextureMipSampling(const string& sampling) {
            for (U8 i = 0; i < to_U8(TextureMipSampling::COUNT); ++i) {
                if (strcmp(sampling.c_str(), Names::textureMipSampling[i]) == 0) {
                    return static_cast<TextureMipSampling>(i);
                }
            }

            return TextureMipSampling::COUNT;
        }
    } //namespace TypeUtil

    SamplerDescriptor::SamplerDescriptorMap SamplerDescriptor::s_samplerDescriptorMap;
    SharedMutex SamplerDescriptor::s_samplerDescriptorMapMutex;
    size_t SamplerDescriptor::s_defaultHashValue = 0;

    SamplerDescriptor::SamplerDescriptor()
    {
        if (s_defaultHashValue == 0) {
            s_defaultHashValue = getHash();
            _hash = s_defaultHashValue;
        }
    }

    size_t SamplerDescriptor::getHash() const {
        size_t tempHash = 59;
        Util::Hash_combine(tempHash, to_U32(_cmpFunc),
                                     _useRefCompare,
                                     to_U32(_wrapU),
                                     to_U32(_wrapV),
                                     to_U32(_wrapW),
                                     to_U32(_minFilter),
                                     to_U32(_magFilter),
                                     to_U32(_mipSampling),
                                     _minLOD,
                                     _maxLOD,
                                     _biasLOD,
                                     _anisotropyLevel,
                                     _borderColour,
                                     _customBorderColour.r,
                                     _customBorderColour.g,
                                     _customBorderColour.b,
                                     _customBorderColour.a);
        if (tempHash != _hash) {
            ScopedLock<SharedMutex> w_lock(s_samplerDescriptorMapMutex);
            insert(s_samplerDescriptorMap, tempHash, *this);
            _hash = tempHash;
        }
        return _hash;
    }

    void SamplerDescriptor::Clear() {
        ScopedLock<SharedMutex> w_lock(s_samplerDescriptorMapMutex);
        s_samplerDescriptorMap.clear();
    }

    const SamplerDescriptor& SamplerDescriptor::Get(const size_t samplerDescriptorHash) {
        bool descriptorFound = false;
        const SamplerDescriptor& desc = Get(samplerDescriptorHash, descriptorFound);
        // Assert if it doesn't exist. Avoids programming errors.
        DIVIDE_ASSERT(descriptorFound, "SamplerDescriptor error: Invalid sampler descriptor hash specified for getSamplerDescriptor!");
        return desc;
    }
   
    const SamplerDescriptor& SamplerDescriptor::Get(const size_t samplerDescriptorHash, bool& descriptorFound) {
        descriptorFound = false;

        SharedLock<SharedMutex> r_lock(s_samplerDescriptorMapMutex);
        // Find the render state block associated with the received hash value
        const SamplerDescriptorMap::const_iterator it = s_samplerDescriptorMap.find(samplerDescriptorHash);
        if (it != std::cend(s_samplerDescriptorMap)) {
            descriptorFound = true;
            return it->second;
        }

        return s_samplerDescriptorMap.find(s_defaultHashValue)->second;
    }

    namespace XMLParser {
        void saveToXML(const SamplerDescriptor& sampler, const string& entryName, boost::property_tree::ptree& pt) {
            pt.put(entryName + ".Sampler.Filter.<xmlattr>.min", TypeUtil::TextureFilterToString(sampler.minFilter()));
            pt.put(entryName + ".Sampler.Filter.<xmlattr>.mag", TypeUtil::TextureFilterToString(sampler.magFilter()));
            pt.put(entryName + ".Sampler.Filter.<xmlattr>.sampling", TypeUtil::TextureMipSamplingToString(sampler.mipSampling()));
            pt.put(entryName + ".Sampler.Map.<xmlattr>.U", TypeUtil::WrapModeToString(sampler.wrapU()));
            pt.put(entryName + ".Sampler.Map.<xmlattr>.V", TypeUtil::WrapModeToString(sampler.wrapV()));
            pt.put(entryName + ".Sampler.Map.<xmlattr>.W", TypeUtil::WrapModeToString(sampler.wrapW()));
            pt.put(entryName + ".Sampler.useRefCompare", sampler.useRefCompare());
            pt.put(entryName + ".Sampler.comparisonFunction", TypeUtil::ComparisonFunctionToString(sampler.cmpFunc()));
            pt.put(entryName + ".Sampler.anisotropy", to_U32(sampler.anisotropyLevel()));
            pt.put(entryName + ".Sampler.minLOD", sampler.minLOD());
            pt.put(entryName + ".Sampler.maxLOD", sampler.maxLOD());
            pt.put(entryName + ".Sampler.biasLOD", sampler.biasLOD());
            pt.put(entryName + ".borderColour", TypeUtil::TextureBorderColourToString(sampler.borderColour()));
            pt.put(entryName + ".Sampler.customBorderColour.<xmlattr>.r", sampler.customBorderColour().r);
            pt.put(entryName + ".Sampler.customBorderColour.<xmlattr>.g", sampler.customBorderColour().g);
            pt.put(entryName + ".Sampler.customBorderColour.<xmlattr>.b", sampler.customBorderColour().b);
            pt.put(entryName + ".Sampler.customBorderColour.<xmlattr>.a", sampler.customBorderColour().a);
        }

        size_t loadFromXML(const string& entryName, const boost::property_tree::ptree& pt) {
            SamplerDescriptor sampler = {};
            sampler.minFilter(TypeUtil::StringToTextureFilter(pt.get<string>(entryName + ".Sampler.Filter.<xmlattr>.min", TypeUtil::TextureFilterToString(TextureFilter::LINEAR))));
            sampler.magFilter(TypeUtil::StringToTextureFilter(pt.get<string>(entryName + ".Sampler.Filter.<xmlattr>.mag", TypeUtil::TextureFilterToString(TextureFilter::LINEAR))));
            sampler.mipSampling(TypeUtil::StringToTextureMipSampling(pt.get<string>(entryName + ".Sampler.Filter.<xmlattr>.sampling", TypeUtil::TextureMipSamplingToString(TextureMipSampling::LINEAR))));
            sampler.wrapU(TypeUtil::StringToWrapMode(pt.get<string>(entryName + ".Sampler.Map.<xmlattr>.U", TypeUtil::WrapModeToString(TextureWrap::REPEAT))));
            sampler.wrapV(TypeUtil::StringToWrapMode(pt.get<string>(entryName + ".Sampler.Map.<xmlattr>.V", TypeUtil::WrapModeToString(TextureWrap::REPEAT))));
            sampler.wrapW(TypeUtil::StringToWrapMode(pt.get<string>(entryName + ".Sampler.Map.<xmlattr>.W", TypeUtil::WrapModeToString(TextureWrap::REPEAT))));
            sampler.useRefCompare(pt.get(entryName + ".Sampler.useRefCompare", false));
            sampler.cmpFunc(TypeUtil::StringToComparisonFunction(pt.get(entryName + ".Sampler.comparisonFunction", "LEQUAL").c_str()));
            sampler.anisotropyLevel(to_U8(pt.get(entryName + ".Sampler.anisotropy", 255u)));
            sampler.minLOD(pt.get(entryName + ".Sampler.minLOD", -1000.f));
            sampler.maxLOD(pt.get(entryName + ".Sampler.maxLOD", 1000.f));
            sampler.biasLOD(pt.get(entryName + ".Sampler.biasLOD", 0.f));
            sampler.borderColour(TypeUtil::StringToTextureBorderColour(pt.get<string>(entryName + ".Sampler.borderColour", TypeUtil::TextureBorderColourToString(TextureBorderColour::OPAQUE_BLACK_F32))));
            sampler.customBorderColour(UColour4
                {
                    pt.get(entryName + ".Sampler.customBorderColour.<xmlattr>.r", to_U8(0u)),
                    pt.get(entryName + ".Sampler.customBorderColour.<xmlattr>.g", to_U8(0u)),
                    pt.get(entryName + ".Sampler.customBorderColour.<xmlattr>.b", to_U8(0u)),
                    pt.get(entryName + ".Sampler.customBorderColour.<xmlattr>.a", to_U8(1u))
                }
        );

            return sampler.getHash();
        }
    } //namespace XMLPareset
} //namespace Divide
