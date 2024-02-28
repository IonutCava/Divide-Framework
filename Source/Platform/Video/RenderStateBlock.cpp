

#include "Headers/RenderStateBlock.h"

namespace Divide {

namespace TypeUtil
{
    const char* ComparisonFunctionToString(const ComparisonFunction func) noexcept
    {
        return Names::compFunctionNames[to_base(func)];
    }

    const char* StencilOperationToString(const StencilOperation op) noexcept
    {
        return Names::stencilOpNames[to_base(op)];
    }

    const char* FillModeToString(const FillMode mode) noexcept
    {
        return Names::fillMode[to_base(mode)];
    }

    const char* CullModeToString(const CullMode mode) noexcept
    {
        return Names::cullModes[to_base(mode)];
    }

    ComparisonFunction StringToComparisonFunction(const char* name) noexcept
    {
        for (U8 i = 0; i < to_U8(ComparisonFunction::COUNT); ++i)
        {
            if (strcmp(name, Names::compFunctionNames[i]) == 0)
            {
                return static_cast<ComparisonFunction>(i);
            }
        }

        return ComparisonFunction::COUNT;
    }

    StencilOperation StringToStencilOperation(const char* name) noexcept
    {
        for (U8 i = 0; i < to_U8(StencilOperation::COUNT); ++i)
        {
            if (strcmp(name, Names::stencilOpNames[i]) == 0)
            {
                return static_cast<StencilOperation>(i);
            }
        }

        return StencilOperation::COUNT;
    }

    FillMode StringToFillMode(const char* name) noexcept
    {
        for (U8 i = 0; i < to_U8(FillMode::COUNT); ++i)
        {
            if (strcmp(name, Names::fillMode[i]) == 0)
            {
                return static_cast<FillMode>(i);
            }
        }

        return FillMode::COUNT;
    }

    CullMode StringToCullMode(const char* name) noexcept
    {
        for (U8 i = 0; i < to_U8(CullMode::COUNT); ++i)
        {
            if (strcmp(name, Names::cullModes[i]) == 0)
            {
                return static_cast<CullMode>(i);
            }
        }
        
        return CullMode::COUNT;
    }
};

size_t GetHash( const RenderStateBlock& block )
{
    // Avoid small float rounding errors offsetting the general hash value
    const U32 zBias = to_U32(std::floor( block._zBias * 1000.0f + 0.5f));
    const U32 zUnits = to_U32(std::floor( block._zUnits * 1000.0f + 0.5f));

    size_t hash = 59;
    Util::Hash_combine(hash, block._colourWrite.i,
                             to_U32( block._cullMode),
                             block._depthTestEnabled,
                             block._depthWriteEnabled,
                             to_U32(block._zFunc),
                             zBias,
                             zUnits,
                             block._scissorTestEnabled,
                             block._stencilEnabled,
                             block._stencilRef,
                             block._stencilMask,
                             block._stencilWriteMask,
                             block._frontFaceCCW,
                             to_U32(block._stencilFailOp),
                             to_U32(block._stencilZFailOp),
                             to_U32(block._stencilPassOp),
                             to_U32(block._stencilFunc),
                             to_U32(block._fillMode),
                             block._tessControlPoints,
                             block._primitiveRestartEnabled,
                             block._rasterizationEnabled );

    return hash;
}

bool operator==( const RenderStateBlock& lhs, const RenderStateBlock& rhs )
{
    return lhs._colourWrite == rhs._colourWrite &&
           lhs._zBias == rhs._zBias &&
           lhs._zUnits == rhs._zUnits &&
           lhs._tessControlPoints == rhs._tessControlPoints &&
           lhs._stencilRef == rhs._stencilRef &&
           lhs._stencilMask == rhs._stencilMask &&
           lhs._stencilWriteMask == rhs._stencilWriteMask &&
           lhs._zFunc == rhs._zFunc &&
           lhs._stencilFailOp == rhs._stencilFailOp &&
           lhs._stencilPassOp == rhs._stencilPassOp &&
           lhs._stencilZFailOp == rhs._stencilZFailOp &&
           lhs._stencilFunc == rhs._stencilFunc &&
           lhs._cullMode == rhs._cullMode &&
           lhs._fillMode == rhs._fillMode &&
           lhs._frontFaceCCW == rhs._frontFaceCCW &&
           lhs._scissorTestEnabled == rhs._scissorTestEnabled &&
           lhs._depthTestEnabled == rhs._depthTestEnabled &&
           lhs._depthWriteEnabled == rhs._depthWriteEnabled &&
           lhs._stencilEnabled == rhs._stencilEnabled &&
           lhs._primitiveRestartEnabled == rhs._primitiveRestartEnabled &&
           lhs._rasterizationEnabled == rhs._rasterizationEnabled;
}

bool operator!=( const RenderStateBlock& lhs, const RenderStateBlock& rhs )
{
    return lhs._colourWrite != rhs._colourWrite ||
           lhs._zBias != rhs._zBias ||
           lhs._zUnits != rhs._zUnits ||
           lhs._tessControlPoints != rhs._tessControlPoints ||
           lhs._stencilRef != rhs._stencilRef ||
           lhs._stencilMask != rhs._stencilMask ||
           lhs._stencilWriteMask != rhs._stencilWriteMask ||
           lhs._zFunc != rhs._zFunc ||
           lhs._stencilFailOp != rhs._stencilFailOp ||
           lhs._stencilPassOp != rhs._stencilPassOp ||
           lhs._stencilZFailOp != rhs._stencilZFailOp ||
           lhs._stencilFunc != rhs._stencilFunc ||
           lhs._cullMode != rhs._cullMode ||
           lhs._fillMode != rhs._fillMode ||
           lhs._frontFaceCCW != rhs._frontFaceCCW ||
           lhs._scissorTestEnabled != rhs._scissorTestEnabled ||
           lhs._depthTestEnabled != rhs._depthTestEnabled ||
           lhs._depthWriteEnabled != rhs._depthWriteEnabled ||
           lhs._stencilEnabled != rhs._stencilEnabled ||
           lhs._primitiveRestartEnabled != rhs._primitiveRestartEnabled ||
           lhs._rasterizationEnabled != rhs._rasterizationEnabled;
}

void SaveToXML(const RenderStateBlock& block, const string& entryName, boost::property_tree::ptree& pt)
{

    pt.put(entryName + ".colourWrite.<xmlattr>.r", block._colourWrite.b[0] == 1);
    pt.put(entryName + ".colourWrite.<xmlattr>.g", block._colourWrite.b[1] == 1);
    pt.put(entryName + ".colourWrite.<xmlattr>.b", block._colourWrite.b[2] == 1);
    pt.put(entryName + ".colourWrite.<xmlattr>.a", block._colourWrite.b[3] == 1);

    pt.put(entryName + ".zBias", block._zBias);
    pt.put(entryName + ".zUnits", block._zUnits);

    pt.put(entryName + ".zFunc", TypeUtil::ComparisonFunctionToString(block._zFunc));
    pt.put(entryName + ".tessControlPoints", block._tessControlPoints);
    pt.put(entryName + ".cullMode", TypeUtil::CullModeToString(block._cullMode));
    pt.put(entryName + ".fillMode", TypeUtil::FillModeToString(block._fillMode));

    pt.put(entryName + ".frontFaceCCW", block._frontFaceCCW);
    pt.put(entryName + ".scissorTestEnabled", block._scissorTestEnabled);
    pt.put(entryName + ".depthTestEnabled", block._depthTestEnabled);
    pt.put(entryName + ".depthWriteEnabled", block._depthWriteEnabled);

    pt.put(entryName + ".stencilEnable", block._stencilEnabled);
    pt.put(entryName + ".stencilFailOp", TypeUtil::StencilOperationToString(block._stencilFailOp));
    pt.put(entryName + ".stencilPassOp", TypeUtil::StencilOperationToString(block._stencilPassOp));
    pt.put(entryName + ".stencilZFailOp", TypeUtil::StencilOperationToString(block._stencilZFailOp));
    pt.put(entryName + ".stencilFunc", TypeUtil::ComparisonFunctionToString(block._stencilFunc));
    pt.put(entryName + ".stencilRef", block._stencilRef);
    pt.put(entryName + ".stencilMask", block._stencilMask);
    pt.put(entryName + ".stencilWriteMask", block._stencilWriteMask);
}

void LoadFromXML(const string& entryName, const boost::property_tree::ptree& pt, RenderStateBlock& blockInOut)
{
    blockInOut._colourWrite.b[0] = pt.get(entryName + ".colourWrite.<xmlattr>.r", blockInOut._colourWrite.b[0]);
    blockInOut._colourWrite.b[1] = pt.get(entryName + ".colourWrite.<xmlattr>.g", blockInOut._colourWrite.b[1]);
    blockInOut._colourWrite.b[2] = pt.get(entryName + ".colourWrite.<xmlattr>.b", blockInOut._colourWrite.b[2]);
    blockInOut._colourWrite.b[3] = pt.get(entryName + ".colourWrite.<xmlattr>.a", blockInOut._colourWrite.b[3]);
    blockInOut._zBias = pt.get(entryName + ".zBias", blockInOut._zBias);
    blockInOut._zUnits = pt.get(entryName + ".zUnits", blockInOut._zUnits);
    blockInOut._zFunc = TypeUtil::StringToComparisonFunction(pt.get(entryName + ".zFunc", TypeUtil::ComparisonFunctionToString( blockInOut._zFunc)).c_str());
    blockInOut._tessControlPoints = pt.get(entryName + ".tessControlPoints", blockInOut._tessControlPoints);
    blockInOut._cullMode = TypeUtil::StringToCullMode(pt.get(entryName + ".cullMode", TypeUtil::CullModeToString(blockInOut._cullMode)).c_str());
    blockInOut._fillMode = TypeUtil::StringToFillMode(pt.get(entryName + ".fillMode", TypeUtil::FillModeToString(blockInOut._fillMode)).c_str());
    blockInOut._frontFaceCCW = pt.get(entryName + ".frontFaceCCW", blockInOut._frontFaceCCW);
    blockInOut._scissorTestEnabled = pt.get(entryName + ".scissorTestEnabled", blockInOut._scissorTestEnabled);
    blockInOut._depthTestEnabled = pt.get(entryName + ".depthTestEnabled", blockInOut._depthTestEnabled);
    blockInOut._depthWriteEnabled = pt.get(entryName + ".depthWriteEnabled", blockInOut._depthWriteEnabled);
    blockInOut._stencilEnabled = pt.get(entryName + ".stencilEnable", blockInOut._stencilEnabled);
    blockInOut._stencilRef = pt.get(entryName + ".stencilRef", blockInOut._stencilRef),
    blockInOut._stencilFailOp = TypeUtil::StringToStencilOperation(pt.get(entryName + ".stencilFailOp", TypeUtil::StencilOperationToString(blockInOut._stencilFailOp)).c_str()),
    blockInOut._stencilZFailOp = TypeUtil::StringToStencilOperation(pt.get(entryName + ".stencilPassOp", TypeUtil::StencilOperationToString(blockInOut._stencilPassOp)).c_str()),
    blockInOut._stencilPassOp = TypeUtil::StringToStencilOperation(pt.get(entryName + ".stencilZFailOp",TypeUtil::StencilOperationToString(blockInOut._stencilZFailOp)).c_str()),
    blockInOut._stencilFunc = TypeUtil::StringToComparisonFunction(pt.get(entryName + ".stencilFunc", TypeUtil::ComparisonFunctionToString(blockInOut._stencilFunc)).c_str());
    blockInOut._stencilMask = pt.get(entryName + ".stencilMask", blockInOut._stencilMask);
    blockInOut._stencilWriteMask = pt.get(entryName + ".stencilWriteMask", blockInOut._stencilWriteMask);
}

};
