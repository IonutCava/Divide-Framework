#include "stdafx.h"

#include "Headers/TextureDescriptor.h"
#include "Platform/Video/Headers/RenderStateBlock.h"

namespace Divide {
    namespace TypeUtil {
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
    };

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
        Util::Hash_combine(tempHash, to_U32(_cmpFunc));
        Util::Hash_combine(tempHash, _useRefCompare);
        Util::Hash_combine(tempHash, to_U32(_wrapU));
        Util::Hash_combine(tempHash, to_U32(_wrapV));
        Util::Hash_combine(tempHash, to_U32(_wrapW));
        Util::Hash_combine(tempHash, to_U32(_minFilter));
        Util::Hash_combine(tempHash, to_U32(_magFilter));
        Util::Hash_combine(tempHash, _minLOD);
        Util::Hash_combine(tempHash, _maxLOD);
        Util::Hash_combine(tempHash, _biasLOD);
        Util::Hash_combine(tempHash, _anisotropyLevel);
        Util::Hash_combine(tempHash, _borderColour.r);
        Util::Hash_combine(tempHash, _borderColour.g);
        Util::Hash_combine(tempHash, _borderColour.b);
        Util::Hash_combine(tempHash, _borderColour.a);
        if (tempHash != _hash) {
            ScopedLock<SharedMutex> w_lock(s_samplerDescriptorMapMutex);
            insert(s_samplerDescriptorMap, tempHash, *this);
            _hash = tempHash;
        }
        return _hash;
    }

    void SamplerDescriptor::clear() {
        ScopedLock<SharedMutex> w_lock(s_samplerDescriptorMapMutex);
        s_samplerDescriptorMap.clear();
    }

    const SamplerDescriptor& SamplerDescriptor::get(const size_t samplerDescriptorHash) {
        bool descriptorFound = false;
        const SamplerDescriptor& desc = get(samplerDescriptorHash, descriptorFound);
        // Assert if it doesn't exist. Avoids programming errors.
        DIVIDE_ASSERT(descriptorFound, "SamplerDescriptor error: Invalid sampler descriptor hash specified for getSamplerDescriptor!");
        return desc;
    }
   
    const SamplerDescriptor& SamplerDescriptor::get(const size_t samplerDescriptorHash, bool& descriptorFound) {
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

    size_t TextureDescriptor::getHash() const {
        _hash = PropertyDescriptor::getHash();

        Util::Hash_combine(_hash, _layerCount);
        Util::Hash_combine(_hash, _mipBaseLevel);
        Util::Hash_combine(_hash, to_base(_mipMappingState));
        Util::Hash_combine(_hash, _msaaSamples);
        Util::Hash_combine(_hash, to_U32(_dataType));
        Util::Hash_combine(_hash, to_U32(_baseFormat));
        Util::Hash_combine(_hash, to_U32(_texType));
        Util::Hash_combine(_hash, _srgb);
        Util::Hash_combine(_hash, _normalized);
        Util::Hash_combine(_hash, _textureOptions._alphaChannelTransparency);
        Util::Hash_combine(_hash, _textureOptions._fastCompression);
        Util::Hash_combine(_hash, _textureOptions._isNormalMap);
        Util::Hash_combine(_hash, _textureOptions._mipFilter);
        Util::Hash_combine(_hash, _textureOptions._skipMipMaps);
        Util::Hash_combine(_hash, _textureOptions._outputFormat);
        Util::Hash_combine(_hash, _textureOptions._outputSRGB);
        Util::Hash_combine(_hash, _textureOptions._useDDSCache);

        return _hash;
    }
    
    [[nodiscard]] bool IsCompressed(const GFXImageFormat format) noexcept {
        return format == GFXImageFormat::BC1            ||
               format == GFXImageFormat::BC1a           ||
               format == GFXImageFormat::BC2            ||
               format == GFXImageFormat::BC3            ||
               format == GFXImageFormat::BC3n           ||
               format == GFXImageFormat::BC4s           ||
               format == GFXImageFormat::BC4u           ||
               format == GFXImageFormat::BC5s           ||
               format == GFXImageFormat::BC5u           ||
               format == GFXImageFormat::BC6s           ||
               format == GFXImageFormat::BC6u           ||
               format == GFXImageFormat::BC7            ||
               format == GFXImageFormat::BC7_SRGB       ||
               format == GFXImageFormat::DXT1_RGB_SRGB  ||
               format == GFXImageFormat::DXT1_RGBA_SRGB ||
               format == GFXImageFormat::DXT5_RGBA_SRGB ||
               format == GFXImageFormat::DXT3_RGBA_SRGB;
    }
    [[nodiscard]] bool HasAlphaChannel(const GFXImageFormat format) noexcept {
        return format == GFXImageFormat::BC1a           ||
               format == GFXImageFormat::BC2            ||
               format == GFXImageFormat::BC3            ||
               format == GFXImageFormat::BC3n           ||
               format == GFXImageFormat::BC7            ||
               format == GFXImageFormat::BC7_SRGB       ||
               format == GFXImageFormat::DXT1_RGBA_SRGB ||
               format == GFXImageFormat::DXT3_RGBA_SRGB ||
               format == GFXImageFormat::DXT5_RGBA_SRGB ||
               format == GFXImageFormat::BGRA           ||
               format == GFXImageFormat::RGBA;
    }
    namespace XMLParser {
        void saveToXML(const SamplerDescriptor& sampler, const string& entryName, boost::property_tree::ptree& pt) {
            pt.put(entryName + ".Sampler.Filter.<xmlattr>.min", TypeUtil::TextureFilterToString(sampler.minFilter()));
            pt.put(entryName + ".Sampler.Filter.<xmlattr>.mag", TypeUtil::TextureFilterToString(sampler.magFilter()));
            pt.put(entryName + ".Sampler.Map.<xmlattr>.U", TypeUtil::WrapModeToString(sampler.wrapU()));
            pt.put(entryName + ".Sampler.Map.<xmlattr>.V", TypeUtil::WrapModeToString(sampler.wrapV()));
            pt.put(entryName + ".Sampler.Map.<xmlattr>.W", TypeUtil::WrapModeToString(sampler.wrapW()));
            pt.put(entryName + ".Sampler.useRefCompare", sampler.useRefCompare());
            pt.put(entryName + ".Sampler.comparisonFunction", TypeUtil::ComparisonFunctionToString(sampler.cmpFunc()));
            pt.put(entryName + ".Sampler.anisotropy", to_U32(sampler.anisotropyLevel()));
            pt.put(entryName + ".Sampler.minLOD", sampler.minLOD());
            pt.put(entryName + ".Sampler.maxLOD", sampler.maxLOD());
            pt.put(entryName + ".Sampler.biasLOD", sampler.biasLOD());
            pt.put(entryName + ".Sampler.borderColour.<xmlattr>.r", sampler.borderColour().r);
            pt.put(entryName + ".Sampler.borderColour.<xmlattr>.g", sampler.borderColour().g);
            pt.put(entryName + ".Sampler.borderColour.<xmlattr>.b", sampler.borderColour().b);
            pt.put(entryName + ".Sampler.borderColour.<xmlattr>.a", sampler.borderColour().a);
        }

        size_t loadFromXML(const string& entryName, const boost::property_tree::ptree& pt) {
            SamplerDescriptor sampler = {};
            sampler.minFilter(TypeUtil::StringToTextureFilter(pt.get<string>(entryName + ".Sampler.Filter.<xmlattr>.min", TypeUtil::TextureFilterToString(TextureFilter::LINEAR))));
            sampler.magFilter(TypeUtil::StringToTextureFilter(pt.get<string>(entryName + ".Sampler.Filter.<xmlattr>.mag", TypeUtil::TextureFilterToString(TextureFilter::LINEAR))));
            sampler.wrapU(TypeUtil::StringToWrapMode(pt.get<string>(entryName + ".Sampler.Map.<xmlattr>.U", TypeUtil::WrapModeToString(TextureWrap::REPEAT))));
            sampler.wrapV(TypeUtil::StringToWrapMode(pt.get<string>(entryName + ".Sampler.Map.<xmlattr>.V", TypeUtil::WrapModeToString(TextureWrap::REPEAT))));
            sampler.wrapW(TypeUtil::StringToWrapMode(pt.get<string>(entryName + ".Sampler.Map.<xmlattr>.W", TypeUtil::WrapModeToString(TextureWrap::REPEAT))));
            sampler.useRefCompare(pt.get(entryName + ".Sampler.useRefCompare", false));
            sampler.cmpFunc(TypeUtil::StringToComparisonFunction(pt.get(entryName + ".Sampler.comparisonFunction", "LEQUAL").c_str()));
            sampler.anisotropyLevel(to_U8(pt.get(entryName + ".Sampler.anisotropy", 255u)));
            sampler.minLOD(pt.get(entryName + ".Sampler.minLOD", -1000));
            sampler.maxLOD(pt.get(entryName + ".Sampler.maxLOD", 1000));
            sampler.biasLOD(pt.get(entryName + ".Sampler.biasLOD", 0));
            sampler.borderColour(FColour4
                {
                    pt.get(entryName + ".Sampler.borderColour.<xmlattr>.r", 0.0f),
                    pt.get(entryName + ".Sampler.borderColour.<xmlattr>.g", 0.0f),
                    pt.get(entryName + ".Sampler.borderColour.<xmlattr>.b", 0.0f),
                    pt.get(entryName + ".Sampler.borderColour.<xmlattr>.a", 1.0f) 
                }
            );

            return sampler.getHash();
        }
    };
}; //namespace Divide