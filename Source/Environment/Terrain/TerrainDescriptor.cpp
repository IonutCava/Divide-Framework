

#include "Headers/TerrainDescriptor.h"

#include "Core/Headers/StringHelper.h"
#include "Platform/File/Headers/FileManagement.h"

namespace Divide {
    namespace {
        constexpr U8 g_minTerrainSideLength = 8u;
    }

    TerrainDescriptor::TerrainDescriptor( std::string_view name ) noexcept
        : PropertyDescriptor(DescriptorType::DESCRIPTOR_TERRAIN_INFO)
        , _name(name)
        , _altitudeRange{ 0.f, 1.f }
    {
        for (LayerDataEntry& entry : _layerDataEntries) {
            entry.fill(VECTOR2_UNIT);
        }
    }

    TerrainDescriptor::~TerrainDescriptor()
    {
    }

    bool TerrainDescriptor::loadFromXML(const boost::property_tree::ptree& pt, const string& name) {
        const ResourcePath terrainDescriptorPath = ResourcePath{ pt.get<string>("descriptor", "") };

        if ( terrainDescriptorPath.empty())
        {
            return false;
        }

        addVariable("terrainName", name);
        addVariable("descriptor", terrainDescriptorPath.string());
        addVariable("waterCaustics", pt.get<string>("waterCaustics"));
        addVariable("underwaterAlbedoTexture", pt.get<string>("underwaterAlbedoTexture", "sandfloor009a.jpg"));
        addVariable("underwaterDetailTexture", pt.get<string>("underwaterDetailTexture", "terrain_detail_NM.png"));
        addVariable("tileNoiseTexture", pt.get<string>("tileNoiseTexture", "bruit_gaussien_2.jpg"));
        addVariable("underwaterTileScale", pt.get<F32>("underwaterTileScale", 1.0f));
        ResourcePath alphaMapDescriptor;
        {
            boost::property_tree::ptree descTree = {};
            XML::readXML(Paths::g_heightmapLocation / terrainDescriptorPath / "descriptor.xml", descTree);

            addVariable("heightfield", descTree.get<string>("heightfield", ""));
            addVariable("heightfieldTex", descTree.get<string>("heightfieldTex", ""));
            dimensions(vec2<U16>(descTree.get<U16>("heightfieldResolution.<xmlattr>.x", 0), descTree.get<U16>("heightfieldResolution.<xmlattr>.y", 0)));

            ringCount(to_U8(std::max(descTree.get<U8>("tileSettings.<xmlattr>.ringCount", 4u) + 1u, 2u)));
            startWidth(descTree.get<F32>("tileSettings.<xmlattr>.startWidth", 0.25f));
            _ringTileCount[0] = 0u;

            U8 prevSize = 8u;
            for (U8 i = 1; i < ringCount(); ++i) {
                _ringTileCount[i] = (descTree.get<U8>(Util::StringFormat("tileSettings.<xmlattr>.ring{}", i).c_str(), prevSize));
                prevSize = _ringTileCount[i];
            }
            if (dimensions().minComponent() < g_minTerrainSideLength) {
                return false;
            }
            altitudeRange(vec2<F32>(descTree.get<F32>("altitudeRange.<xmlattr>.min", 0.0f), descTree.get<F32>("altitudeRange.<xmlattr>.max", 255.0f)));
            addVariable("grassMap", descTree.get<string>("vegetation.grassMap"));
            addVariable("treeMap", descTree.get<string>("vegetation.treeMap"));

            for (I32 j = 1; j < 5; ++j) {
                addVariable(Util::StringFormat("grassBillboard{}", j), descTree.get<string>(Util::StringFormat("vegetation.grassBillboard{}", j), ""));
                addVariable(Util::StringFormat("grassScale{}", j), descTree.get<F32>(Util::StringFormat("vegetation.grassBillboard{}.<xmlattr>.scale", j), 1.0f));

                addVariable(Util::StringFormat("treeMesh{}", j), descTree.get<string>(Util::StringFormat("vegetation.treeMesh{}", j), ""));
                addVariable(Util::StringFormat("treeScale{}", j), descTree.get<F32>(Util::StringFormat("vegetation.treeMesh{}.<xmlattr>.scale", j), 1.0f));
                addVariable(Util::StringFormat("treeRotationX{}", j), descTree.get<F32>(Util::StringFormat("vegetation.treeMesh{}.<xmlattr>.rotate_x", j), 0.0f));
                addVariable(Util::StringFormat("treeRotationY{}", j), descTree.get<F32>(Util::StringFormat("vegetation.treeMesh{}.<xmlattr>.rotate_y", j), 0.0f));
                addVariable(Util::StringFormat("treeRotationZ{}", j), descTree.get<F32>(Util::StringFormat("vegetation.treeMesh{}.<xmlattr>.rotate_z", j), 0.0f));
            }
            alphaMapDescriptor = ResourcePath{ descTree.get<string>("alphaMaps.<xmlattr>.file", "") };
            for (boost::property_tree::ptree::iterator itLayerData= std::begin(descTree.get_child("alphaMaps"));
                itLayerData != std::end(descTree.get_child("alphaMaps"));
                ++itLayerData)
            {
                const string format(itLayerData->first);
                if (format.find("<xmlcomment>") != string::npos || format.find("<xmlattr>") != string::npos) {
                    continue;
                }
                
                const U8 matIndex = itLayerData->second.get<U8>("<xmlattr>.material", 0u);
                if (matIndex < layerDataEntries().size()) {
                    LayerDataEntry& entry = _layerDataEntries[matIndex];
                    const U8 layerIndex = itLayerData->second.get<U8>("<xmlattr>.channel", 0u);
                    if (layerIndex < 4) {
                        vec2<F32>& tileFactors = entry[layerIndex];
                        tileFactors.s = CLAMPED(itLayerData->second.get("<xmlattr>.s", 1.f), 1.f, 255.f);
                        tileFactors.t = CLAMPED(itLayerData->second.get("<xmlattr>.t", 1.f), 1.f, 255.f);
                    }
                }
            }
        }

        if (alphaMapDescriptor.empty())
        {
            return false;
        }

        {
            boost::property_tree::ptree alphaTree = {};
            XML::readXML(Paths::g_heightmapLocation / terrainDescriptorPath / alphaMapDescriptor, alphaTree);

            const U8 numLayers = alphaTree.get<U8>("AlphaData.nImages");
            const U8 numImages = alphaTree.get<U8>("AlphaData.nLayers");
            if (numLayers == 0 || numImages == 0) {
                return false;
            }
            textureLayers(numLayers);

            const string imageListNode = "AlphaData.ImageList";
            I32 i = 0;
            string blendMap;
            std::array<string, 4> arrayMaterials;
            string layerOffsetStr;
            for (boost::property_tree::ptree::iterator itImage = std::begin(alphaTree.get_child(imageListNode));
                itImage != std::end(alphaTree.get_child(imageListNode));
                ++itImage, ++i)
            {
                string layerName(itImage->second.data());
                string format(itImage->first);

                if (format.find("<xmlcomment>") != string::npos || format.find("<xmlattr>") != string::npos) {
                    i--;
                    continue;
                }

                layerOffsetStr = Util::to_string(i);
                addVariable("blendMap" + layerOffsetStr, stripQuotes(itImage->second.get<std::string>("FileName", "").c_str()));

                for (boost::property_tree::ptree::iterator itLayer = std::begin(itImage->second.get_child("LayerList"));
                    itLayer != std::end(itImage->second.get_child("LayerList"));
                    ++itLayer)
                {
                    if (string(itLayer->first).find("<xmlcomment>") != string::npos) {
                        continue;
                    }

                    string layerColour = itLayer->second.get<string>("LayerColour", "");
                    string materialName;
                    for (boost::property_tree::ptree::iterator itMaterial = std::begin(itLayer->second.get_child("MtlList"));
                        itMaterial != std::end(itLayer->second.get_child("MtlList"));
                        ++itMaterial)
                    {
                        if (string(itMaterial->first).find("<xmlcomment>") != string::npos) {
                            continue;
                        }

                        materialName = itMaterial->second.data();
                        // Only one material per channel!
                        break;
                    }

                    addVariable(layerColour + layerOffsetStr + "_mat", materialName);
                }
            }
        }

        return true;
    }

    void TerrainDescriptor::addVariable(const string& name, const string& value) {
        insert(_variables, hashAlg::make_pair(_ID(name.c_str()), value));
    }

    void TerrainDescriptor::addVariable(const string& name, F32 value) {
        insert(_variablesf, hashAlg::make_pair(_ID(name.c_str()), value));
    }
} //namespace Divide