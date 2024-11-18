#include "Headers/TerrainDescriptor.h"

#include "Platform/File/Headers/FileManagement.h"
namespace Divide
{
    namespace
    {
        constexpr U8 g_minTerrainSideLength = 8u;
    }

void Init( TerrainDescriptor& descriptor, const std::string_view name)
{
    descriptor._name = name;
    for ( TerrainDescriptor::LayerDataEntry& entry : descriptor._layerDataEntries )
    {
        entry.fill( VECTOR2_UNIT );
    }
}

bool LoadFromXML( TerrainDescriptor& descriptor, const boost::property_tree::ptree& pt, const std::string_view name )
{
    const ResourcePath terrainDescriptorPath = ResourcePath{ pt.get<string>( "descriptor", "" ) };

    if ( terrainDescriptorPath.empty() )
    {
        return false;
    }

    AddVariable( descriptor, "terrainName", name );
    AddVariable( descriptor, "descriptor", terrainDescriptorPath.string() );
    AddVariable( descriptor, "waterCaustics", pt.get<string>( "waterCaustics" ) );
    AddVariable( descriptor, "underwaterAlbedoTexture", pt.get<string>( "underwaterAlbedoTexture", "sandfloor009a.jpg" ) );
    AddVariable( descriptor, "underwaterDetailTexture", pt.get<string>( "underwaterDetailTexture", "terrain_detail_NM.png" ) );
    AddVariable( descriptor, "tileNoiseTexture", pt.get<string>( "tileNoiseTexture", "bruit_gaussien_2.jpg" ) );
    AddVariableF( descriptor, "underwaterTileScale", pt.get<F32>( "underwaterTileScale", 1.0f ) );
    ResourcePath alphaMapDescriptor;
    {
        boost::property_tree::ptree descTree = {};
        XML::readXML( Paths::g_heightmapLocation / terrainDescriptorPath / "descriptor.xml", descTree );

        AddVariable( descriptor, "heightfield", descTree.get<string>( "heightfield", "" ) );
        AddVariable( descriptor, "heightfieldTex", descTree.get<string>( "heightfieldTex", "" ) );

        descriptor._dimensions.set( descTree.get<U16>( "heightfieldResolution.<xmlattr>.x", 0 ),
                                    descTree.get<U16>( "heightfieldResolution.<xmlattr>.y", 0 ) );

        descriptor._ringCount = to_U8( std::max( descTree.get<U8>( "tileSettings.<xmlattr>.ringCount", 4u ) + 1u, 2u ) );
        descriptor._startWidth = descTree.get<F32>( "tileSettings.<xmlattr>.startWidth", 0.25f );
        descriptor._ringTileCount[0] = 0u;

        U8 prevSize = 8u;
        for ( U8 i = 1; i < descriptor._ringCount; ++i )
        {
            descriptor._ringTileCount[i] = (descTree.get<U8>( Util::StringFormat( "tileSettings.<xmlattr>.ring{}", i ).c_str(), prevSize ));
            prevSize = descriptor._ringTileCount[i];
        }
        if ( descriptor._dimensions.minComponent() < g_minTerrainSideLength )
        {
            return false;
        }
        descriptor._altitudeRange.set( descTree.get<F32>( "altitudeRange.<xmlattr>.min", 0.0f ),
                                       descTree.get<F32>( "altitudeRange.<xmlattr>.max", 255.0f ) );

        AddVariable( descriptor, "grassMap", descTree.get<string>( "vegetation.grassMap" ) );
        AddVariable( descriptor, "treeMap", descTree.get<string>( "vegetation.treeMap" ) );

        for ( I32 j = 1; j < 5; ++j )
        {
            AddVariable( descriptor, Util::StringFormat( "grassBillboard{}", j ), descTree.get<string>( Util::StringFormat( "vegetation.grassBillboard{}", j ).c_str(), "" ) );
            AddVariable( descriptor, Util::StringFormat( "treeMesh{}", j ), descTree.get<string>( Util::StringFormat( "vegetation.treeMesh{}", j ).c_str(), "" ) );
            AddVariableF( descriptor, Util::StringFormat( "grassScale{}", j ), descTree.get<F32>( Util::StringFormat( "vegetation.grassBillboard{}.<xmlattr>.scale", j ).c_str(), 1.0f ) );
            AddVariableF( descriptor, Util::StringFormat( "treeScale{}", j ), descTree.get<F32>( Util::StringFormat( "vegetation.treeMesh{}.<xmlattr>.scale", j ).c_str(), 1.0f ) );
            AddVariableF( descriptor, Util::StringFormat( "treeRotationX{}", j ), descTree.get<F32>( Util::StringFormat( "vegetation.treeMesh{}.<xmlattr>.rotate_x", j ).c_str(), 0.0f ) );
            AddVariableF( descriptor, Util::StringFormat( "treeRotationY{}", j ), descTree.get<F32>( Util::StringFormat( "vegetation.treeMesh{}.<xmlattr>.rotate_y", j ).c_str(), 0.0f ) );
            AddVariableF( descriptor, Util::StringFormat( "treeRotationZ{}", j ), descTree.get<F32>( Util::StringFormat( "vegetation.treeMesh{}.<xmlattr>.rotate_z", j ).c_str(), 0.0f ) );
        }

        alphaMapDescriptor = ResourcePath{ descTree.get<string>( "alphaMaps.<xmlattr>.file", "" ) };
        for ( boost::property_tree::ptree::iterator itLayerData = std::begin( descTree.get_child( "alphaMaps" ) );
              itLayerData != std::end( descTree.get_child( "alphaMaps" ) );
              ++itLayerData )
        {
            const string format( itLayerData->first );
            if ( format.find( "<xmlcomment>" ) != string::npos || format.find( "<xmlattr>" ) != string::npos )
            {
                continue;
            }

            const U8 matIndex = itLayerData->second.get<U8>( "<xmlattr>.material", 0u );
            if ( matIndex < descriptor._layerDataEntries.size() )
            {
                TerrainDescriptor::LayerDataEntry& entry = descriptor._layerDataEntries[matIndex];

                const U8 layerIndex = itLayerData->second.get<U8>( "<xmlattr>.channel", 0u );
                if ( layerIndex < 4 )
                {
                    float2& tileFactors = entry[layerIndex];
                    tileFactors.s = CLAMPED( itLayerData->second.get( "<xmlattr>.s", 1.f ), 1.f, 255.f );
                    tileFactors.t = CLAMPED( itLayerData->second.get( "<xmlattr>.t", 1.f ), 1.f, 255.f );
                }
            }
        }
    }

    if ( alphaMapDescriptor.empty() )
    {
        return false;
    }

    {
        boost::property_tree::ptree alphaTree = {};
        XML::readXML( Paths::g_heightmapLocation / terrainDescriptorPath / alphaMapDescriptor, alphaTree );

        const U8 numLayers = alphaTree.get<U8>( "AlphaData.nImages" );
        const U8 numImages = alphaTree.get<U8>( "AlphaData.nLayers" );
        if ( numLayers == 0 || numImages == 0 )
        {
            return false;
        }

        descriptor._textureLayers = numLayers;

        const std::string imageListNode = "AlphaData.ImageList";
        I32 i = 0;
        string blendMap;
        std::array<string, 4> arrayMaterials;
        string layerOffsetStr;
        for ( boost::property_tree::ptree::iterator itImage = std::begin( alphaTree.get_child( imageListNode ) );
              itImage != std::end( alphaTree.get_child( imageListNode ) );
              ++itImage, ++i )
        {
            string layerName( itImage->second.data() );
            string format( itImage->first );

            if ( format.find( "<xmlcomment>" ) != string::npos || format.find( "<xmlattr>" ) != string::npos )
            {
                i--;
                continue;
            }

            layerOffsetStr = Util::to_string( i );
            AddVariable( descriptor, "blendMap" + layerOffsetStr, stripQuotes( itImage->second.get<std::string>( "FileName", "" ).c_str() ) );

            for ( boost::property_tree::ptree::iterator itLayer = std::begin( itImage->second.get_child( "LayerList" ) );
                  itLayer != std::end( itImage->second.get_child( "LayerList" ) );
                  ++itLayer )
            {
                if ( string( itLayer->first ).find( "<xmlcomment>" ) != string::npos )
                {
                    continue;
                }

                string layerColour = itLayer->second.get<string>( "LayerColour", "" );
                string materialName;
                for ( boost::property_tree::ptree::iterator itMaterial = std::begin( itLayer->second.get_child( "MtlList" ) );
                      itMaterial != std::end( itLayer->second.get_child( "MtlList" ) );
                      ++itMaterial )
                {
                    if ( string( itMaterial->first ).find( "<xmlcomment>" ) != string::npos )
                    {
                        continue;
                    }

                    materialName = itMaterial->second.data();
                    // Only one material per channel!
                    break;
                }

                AddVariable( descriptor, layerColour + layerOffsetStr + "_mat", materialName );
            }
        }
    }

    return true;
}

void SaveToXML( const TerrainDescriptor& descriptor, boost::property_tree::ptree& pt)
{
    pt.put( "descriptor", GetVariable( descriptor, "descriptor" ) );
    pt.put( "waterCaustics", GetVariable( descriptor, "waterCaustics" ) );
    pt.put( "underwaterAlbedoTexture", GetVariable( descriptor, "underwaterAlbedoTexture" ) );
    pt.put( "underwaterDetailTexture", GetVariable( descriptor, "underwaterDetailTexture" ) );
    pt.put( "tileNoiseTexture", GetVariable( descriptor, "tileNoiseTexture" ) );
    pt.put( "underwaterTileScale", GetVariableF( descriptor, "underwaterTileScale" ) );
}

void AddVariable( TerrainDescriptor& descriptor,  const std::string_view name, const std::string_view value )
{
    descriptor._variables[_ID(name)] = value;
}

void AddVariableF( TerrainDescriptor& descriptor, const std::string_view name, const F32 value )
{
    descriptor._variablesf[_ID( name )] = value;
}

string GetVariable( const TerrainDescriptor& descriptor, const std::string_view name )
{
    auto it = descriptor._variables.find( _ID( name ) );
    if ( it != std::end( descriptor. _variables ) )
    {
        return it->second;
    }

    return "";
}

 F32 GetVariableF( const TerrainDescriptor& descriptor, const std::string_view name )
{
    const auto it = descriptor._variablesf.find( _ID( name ) );
    if ( it != std::end( descriptor._variablesf ) )
    {
        return it->second;
    }
    return 0.f;
}

U32 MaxNodesPerStage( const TerrainDescriptor& descriptor ) noexcept
{
    // Quadtree, so assume worst case scenario
    return descriptor._dimensions.maxComponent() / 4;
}

[[nodiscard]] U8 TileRingCount( const TerrainDescriptor& descriptor, const U8 index ) noexcept
{
    DIVIDE_ASSERT( index < descriptor._ringCount );

    return descriptor._ringTileCount[index];
}

} //namespace Divide
