

#include "Headers/SamplerDescriptor.h"
#include "Platform/Video/Headers/RenderStateBlock.h"

namespace Divide
{
    namespace TypeUtil
    {
        static const char* TextureBorderColourToString(const TextureBorderColour colour) noexcept
        {
            return Names::textureBorderColour[to_base(colour)];
        }

        static TextureBorderColour StringToTextureBorderColour(const string& colour)
        {
            for (U8 i = 0; i < to_U8(TextureBorderColour::COUNT); ++i)
            {
                if (strcmp(colour.c_str(), Names::textureBorderColour[i]) == 0)
                {
                    return static_cast<TextureBorderColour>(i);
                }
            }

            return TextureBorderColour::COUNT;
        }

        static const char* WrapModeToString(const TextureWrap wrapMode) noexcept
        {
            return Names::textureWrap[to_base(wrapMode)];
        }

        static TextureWrap StringToWrapMode(const string& wrapMode)
        {
            for (U8 i = 0; i < to_U8(TextureWrap::COUNT); ++i)
            {
                if (strcmp(wrapMode.c_str(), Names::textureWrap[i]) == 0)
                {
                    return static_cast<TextureWrap>(i);
                }
            }

            return TextureWrap::COUNT;
        }

        static const char* TextureFilterToString(const TextureFilter filter) noexcept
        {
            return Names::textureFilter[to_base(filter)];
        }

        static TextureFilter StringToTextureFilter(const string& filter)
        {
            for (U8 i = 0; i < to_U8(TextureFilter::COUNT); ++i)
            {
                if (strcmp(filter.c_str(), Names::textureFilter[i]) == 0)
                {
                    return static_cast<TextureFilter>(i);
                }
            }

            return TextureFilter::COUNT;
        }

        static const char* TextureMipSamplingToString(TextureMipSampling sampling) noexcept
        {
            return Names::textureMipSampling[to_base(sampling)];
        }

        static TextureMipSampling StringToTextureMipSampling(const string& sampling)
        {
            for (U8 i = 0; i < to_U8(TextureMipSampling::COUNT); ++i)
            {
                if (strcmp(sampling.c_str(), Names::textureMipSampling[i]) == 0)
                {
                    return static_cast<TextureMipSampling>(i);
                }
            }

            return TextureMipSampling::COUNT;
        }
    } //namespace TypeUtil

    namespace XMLParser
    {
        void saveToXML(const SamplerDescriptor& sampler, const std::string& entryName, boost::property_tree::ptree& pt)
        {
            pt.put(entryName + ".Sampler.Filter.<xmlattr>.min", TypeUtil::TextureFilterToString(sampler._minFilter));
            pt.put(entryName + ".Sampler.Filter.<xmlattr>.mag", TypeUtil::TextureFilterToString(sampler._magFilter));
            pt.put(entryName + ".Sampler.Filter.<xmlattr>.sampling", TypeUtil::TextureMipSamplingToString(sampler._mipSampling));
            pt.put(entryName + ".Sampler.Map.<xmlattr>.U", TypeUtil::WrapModeToString(sampler._wrapU));
            pt.put(entryName + ".Sampler.Map.<xmlattr>.V", TypeUtil::WrapModeToString(sampler._wrapV));
            pt.put(entryName + ".Sampler.Map.<xmlattr>.W", TypeUtil::WrapModeToString(sampler._wrapW));
            pt.put(entryName + ".Sampler.comparisonFunction", TypeUtil::ComparisonFunctionToString(sampler._depthCompareFunc));
            pt.put(entryName + ".Sampler.anisotropy", to_U32(sampler._anisotropyLevel));
            pt.put(entryName + ".Sampler.minLOD", sampler._lod._min);
            pt.put(entryName + ".Sampler.maxLOD", sampler._lod._max);
            pt.put(entryName + ".Sampler.biasLOD", sampler._lod._bias);
            pt.put(entryName + ".Sampler.borderColour", TypeUtil::TextureBorderColourToString(sampler._borderColour));
            pt.put(entryName + ".Sampler.customBorderColour.<xmlattr>.r", sampler._customBorderColour.r);
            pt.put(entryName + ".Sampler.customBorderColour.<xmlattr>.g", sampler._customBorderColour.g);
            pt.put(entryName + ".Sampler.customBorderColour.<xmlattr>.b", sampler._customBorderColour.b);
            pt.put(entryName + ".Sampler.customBorderColour.<xmlattr>.a", sampler._customBorderColour.a);
        }

        SamplerDescriptor loadFromXML(const std::string& entryName, const boost::property_tree::ptree& pt)
        {
            SamplerDescriptor sampler = {};
            sampler._minFilter = TypeUtil::StringToTextureFilter(pt.get<string>(entryName + ".Sampler.Filter.<xmlattr>.min", TypeUtil::TextureFilterToString(TextureFilter::LINEAR)));
            sampler._magFilter = TypeUtil::StringToTextureFilter(pt.get<string>(entryName + ".Sampler.Filter.<xmlattr>.mag", TypeUtil::TextureFilterToString(TextureFilter::LINEAR)));
            sampler._mipSampling = TypeUtil::StringToTextureMipSampling(pt.get<string>(entryName + ".Sampler.Filter.<xmlattr>.sampling", TypeUtil::TextureMipSamplingToString(TextureMipSampling::LINEAR)));
            sampler._wrapU = TypeUtil::StringToWrapMode(pt.get<string>(entryName + ".Sampler.Map.<xmlattr>.U", TypeUtil::WrapModeToString(TextureWrap::REPEAT)));
            sampler._wrapV = TypeUtil::StringToWrapMode(pt.get<string>(entryName + ".Sampler.Map.<xmlattr>.V", TypeUtil::WrapModeToString(TextureWrap::REPEAT)));
            sampler._wrapW = TypeUtil::StringToWrapMode(pt.get<string>(entryName + ".Sampler.Map.<xmlattr>.W", TypeUtil::WrapModeToString(TextureWrap::REPEAT)));
            sampler._depthCompareFunc = TypeUtil::StringToComparisonFunction(pt.get(entryName + ".Sampler.comparisonFunction", "LEQUAL").c_str());
            sampler._anisotropyLevel = to_U8(pt.get(entryName + ".Sampler.anisotropy", 255u));
            sampler._lod._min = pt.get(entryName + ".Sampler.minLOD", -1000.f);
            sampler._lod._max = pt.get(entryName + ".Sampler.maxLOD", 1000.f);
            sampler._lod._bias = pt.get(entryName + ".Sampler.biasLOD", 0.f);
            sampler._borderColour = TypeUtil::StringToTextureBorderColour(pt.get<string>(entryName + ".Sampler.borderColour", TypeUtil::TextureBorderColourToString(TextureBorderColour::OPAQUE_BLACK_F32)));
            sampler._customBorderColour = UColour4
            {
                pt.get(entryName + ".Sampler.customBorderColour.<xmlattr>.r", U8_ZERO),
                pt.get(entryName + ".Sampler.customBorderColour.<xmlattr>.g", U8_ZERO),
                pt.get(entryName + ".Sampler.customBorderColour.<xmlattr>.b", U8_ZERO),
                pt.get(entryName + ".Sampler.customBorderColour.<xmlattr>.a", U8_ONE)
            };

            return sampler;
        }
    } //namespace XMLPareset
} //namespace Divide
