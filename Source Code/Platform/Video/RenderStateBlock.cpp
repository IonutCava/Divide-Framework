#include "stdafx.h"

#include "Headers/RenderStateBlock.h"

#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

RenderStateBlock::RenderStateMap RenderStateBlock::s_stateBlockMap;
SharedMutex RenderStateBlock::s_stateBlockMapMutex;
size_t RenderStateBlock::s_defaultHashValue = 0;

namespace TypeUtil {
    const char* ComparisonFunctionToString(const ComparisonFunction func) noexcept {
        return Names::compFunctionNames[to_base(func)];
    }

    const char* StencilOperationToString(const StencilOperation op) noexcept {
        return Names::stencilOpNames[to_base(op)];
    }
    const char* FillModeToString(const FillMode mode) noexcept {
        return Names::fillModes[to_base(mode)];
    }
    const char* CullModeToString(const CullMode mode) noexcept {
        return Names::cullModes[to_base(mode)];
    }

    ComparisonFunction StringToComparisonFunction(const char* name) noexcept {
        for (U8 i = 0; i < to_U8(ComparisonFunction::COUNT); ++i) {
            if (strcmp(name, Names::compFunctionNames[i]) == 0) {
                return static_cast<ComparisonFunction>(i);
            }
        }

        return ComparisonFunction::COUNT;
    }
    StencilOperation StringToStencilOperation(const char* name) noexcept {
        for (U8 i = 0; i < to_U8(StencilOperation::COUNT); ++i) {
            if (strcmp(name, Names::stencilOpNames[i]) == 0) {
                return static_cast<StencilOperation>(i);
            }
        }

        return StencilOperation::COUNT;
    }
    FillMode StringToFillMode(const char* name) noexcept {
        for (U8 i = 0; i < to_U8(FillMode::COUNT); ++i) {
            if (strcmp(name, Names::fillModes[i]) == 0) {
                return static_cast<FillMode>(i);
            }
        }

        return FillMode::COUNT;
    }

    CullMode StringToCullMode(const char* name) noexcept {
        for (U8 i = 0; i < to_U8(CullMode::COUNT); ++i) {
            if (strcmp(name, Names::cullModes[i]) == 0) {
                return static_cast<CullMode>(i);
            }
        }
        
        return CullMode::COUNT;
    }
};

RenderStateBlock::RenderStateBlock() noexcept
    : GUIDWrapper()
{
    if (defaultHash() == 0) {
        _dirty = true;
        s_defaultHashValue = RenderStateBlock::getHash();
    }
}

void RenderStateBlock::from(const RenderStateBlock& other) noexcept {
    _colourWrite = other._colourWrite;
    _zBias = other._zBias;
    _zUnits = other._zUnits;
    _tessControlPoints = other._tessControlPoints;
    _stencilRef = other._stencilRef;
    _stencilMask = other._stencilMask;
    _stencilWriteMask = other._stencilWriteMask;
    _zFunc = other._zFunc;
    _stencilFailOp = other._stencilFailOp;
    _stencilZFailOp = other._stencilZFailOp;
    _stencilPassOp = other._stencilPassOp;
    _stencilFunc = other._stencilFunc;
    _cullMode = other._cullMode;
    _fillMode = other._fillMode;
    _frontFaceCCW = other._frontFaceCCW;
    _scissorTestEnabled = other._scissorTestEnabled;
    _depthTestEnabled = other._depthTestEnabled;
    _stencilEnable = other._stencilEnable;

    {
        _hash = other.getHash();
        _dirty = false;
    }
}

void RenderStateBlock::flipCullMode()  noexcept {
    switch (cullMode()) {
        case CullMode::NONE: _cullMode = CullMode::ALL;  break;
        case CullMode::ALL:  _cullMode = CullMode::NONE; break;
        case CullMode::CW:   _cullMode = CullMode::CCW;  break;
        case CullMode::CCW:  _cullMode = CullMode::CW;   break;
        case CullMode::COUNT: DIVIDE_UNEXPECTED_CALL();  break;
    }
    _dirty = true;
}

void RenderStateBlock::setCullMode(const CullMode mode)  noexcept {
    if (cullMode() != mode) {
        _cullMode = mode;
        _dirty = true;
    }
}

void RenderStateBlock::flipFrontFace()  noexcept {
    _frontFaceCCW = !frontFaceCCW();
    _dirty = true;
}

void RenderStateBlock::setFrontFaceCCW(const bool state) noexcept {
    if (_frontFaceCCW != state) {
        _frontFaceCCW = state;
        _dirty = true;
    }
}
void RenderStateBlock::depthTestEnabled(const bool enable)  noexcept {
    if (_depthTestEnabled != enable) {
        _depthTestEnabled = enable;
        _dirty = true;
    }
}

void RenderStateBlock::setColourWrites(const bool red,
                                       const bool green,
                                       const bool blue,
                                       const bool alpha)  noexcept {
    P32 rgba = {};
    rgba.b[0] = red ? 1 : 0;
    rgba.b[1] = green ? 1 : 0;
    rgba.b[2] = blue ? 1 : 0;
    rgba.b[3] = alpha ? 1 : 0;

    if (_colourWrite != rgba) {
        _colourWrite = rgba;
        _dirty = true;
    }
}

void RenderStateBlock::setScissorTest(const bool enable)  noexcept {
    if (_scissorTestEnabled != enable) {
        _scissorTestEnabled = enable;
        _dirty = true;
    }
}

void RenderStateBlock::setZBias(const F32 zBias, const F32 zUnits)  noexcept {
    if (!COMPARE(_zBias, zBias) || !COMPARE(_zUnits, zUnits)) {
        _zBias = zBias;
        _zUnits = zUnits;
        _dirty = true;
    }
}

void RenderStateBlock::setZFunc(const ComparisonFunction zFunc)  noexcept {
    if (_zFunc != zFunc) {
        _zFunc = zFunc;
        _dirty = true;
    }
}

void RenderStateBlock::setFillMode(const FillMode mode)  noexcept {
    if (_fillMode != mode) {
        _fillMode = mode;
        _dirty = true;
    }
}

void RenderStateBlock::setTessControlPoints(const U32 count) noexcept {
    if (_tessControlPoints != count) {
        _tessControlPoints = count;
        _dirty = true;
    }
}

void RenderStateBlock::setStencilReadWriteMask(const U32 read, const U32 write)  noexcept {
    if (_stencilMask != read || _stencilWriteMask != write) {
        _stencilMask = read;
        _stencilWriteMask = write;
        _dirty = true;
    }
}

void RenderStateBlock::setStencil(const bool enable,
                                  const U32 stencilRef,
                                  const StencilOperation stencilFailOp,
                                  const StencilOperation stencilPassOp,
                                  const StencilOperation stencilZFailOp,
                                  const ComparisonFunction stencilFunc)  noexcept {
    if (_stencilEnable != enable ||
        _stencilRef != stencilRef ||
        _stencilFailOp != stencilFailOp ||
        _stencilZFailOp != stencilZFailOp ||
        _stencilPassOp != stencilPassOp ||
        _stencilFunc != stencilFunc) 
    {
        _stencilEnable = enable;
        _stencilRef = stencilRef;
        _stencilFailOp = stencilFailOp;
        _stencilZFailOp = stencilZFailOp;
        _stencilPassOp = stencilPassOp;
        _stencilFunc = stencilFunc;
        _dirty = true;
    }
}

void RenderStateBlock::clear() {
    ScopedLock<SharedMutex> w_lock(s_stateBlockMapMutex);
    s_stateBlockMap.clear();
}

size_t RenderStateBlock::defaultHash() noexcept {
    return s_defaultHashValue;
}

/// Return the render state block defined by the specified hash value.
const RenderStateBlock& RenderStateBlock::get(const size_t renderStateBlockHash) {
    bool blockFound = false;
    const RenderStateBlock& block = get(renderStateBlockHash, blockFound);
    // Assert if it doesn't exist. Avoids programming errors.
    DIVIDE_ASSERT(blockFound, "RenderStateBlock error: Invalid render state block hash specified for getRenderStateBlock!");
    // Return the state block's descriptor
    return block;
}

const RenderStateBlock& RenderStateBlock::get(const size_t renderStateBlockHash, bool& blockFound) {
    blockFound = false;

    SharedLock<SharedMutex> r_lock(s_stateBlockMapMutex);
    // Find the render state block associated with the received hash value
    const RenderStateMap::const_iterator it = s_stateBlockMap.find(renderStateBlockHash);
    if(it != std::cend(s_stateBlockMap) ) {
        blockFound = true;
        return it->second;
    }

    return s_stateBlockMap.find(defaultHash())->second;
}

bool RenderStateBlock::cullEnabled() const noexcept {
    return _cullMode != CullMode::NONE;
}

size_t RenderStateBlock::getHash() const noexcept {
    const size_t previousCache = Hashable::getHash();

    if (!_dirty) {
        return previousCache;
    }

    // Avoid small float rounding errors offsetting the general hash value
    const U32 zBias = to_U32(std::floor(_zBias * 1000.0f + 0.5f));
    const U32 zUnits = to_U32(std::floor(_zUnits * 1000.0f + 0.5f));

    _hash = 59;
    Util::Hash_combine(_hash, _colourWrite.i);
    Util::Hash_combine(_hash, to_U32(_cullMode));
    Util::Hash_combine(_hash, _depthTestEnabled);
    Util::Hash_combine(_hash, to_U32(_zFunc));
    Util::Hash_combine(_hash, zBias);
    Util::Hash_combine(_hash, zUnits);
    Util::Hash_combine(_hash, _scissorTestEnabled);
    Util::Hash_combine(_hash, _stencilEnable);
    Util::Hash_combine(_hash, _stencilRef);
    Util::Hash_combine(_hash, _stencilMask);
    Util::Hash_combine(_hash, _stencilWriteMask);
    Util::Hash_combine(_hash, _frontFaceCCW);
    Util::Hash_combine(_hash, to_U32(_stencilFailOp));
    Util::Hash_combine(_hash, to_U32(_stencilZFailOp));
    Util::Hash_combine(_hash, to_U32(_stencilPassOp));
    Util::Hash_combine(_hash, to_U32(_stencilFunc));
    Util::Hash_combine(_hash, to_U32(_fillMode));
    Util::Hash_combine(_hash, _tessControlPoints);

    if (previousCache != _hash) {
        ScopedLock<SharedMutex> w_lock(s_stateBlockMapMutex);
        insert(s_stateBlockMap, _hash, *this);
    }
    _dirty = false;
    return _hash;
}

void RenderStateBlock::saveToXML(const RenderStateBlock& block, const string& entryName, boost::property_tree::ptree& pt) {

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


    pt.put(entryName + ".stencilEnable", block._stencilEnable);
    pt.put(entryName + ".stencilFailOp", TypeUtil::StencilOperationToString(block._stencilFailOp));
    pt.put(entryName + ".stencilPassOp", TypeUtil::StencilOperationToString(block._stencilPassOp));
    pt.put(entryName + ".stencilZFailOp", TypeUtil::StencilOperationToString(block._stencilZFailOp));
    pt.put(entryName + ".stencilFunc", TypeUtil::ComparisonFunctionToString(block._stencilFunc));
    pt.put(entryName + ".stencilRef", block._stencilRef);
    pt.put(entryName + ".stencilMask", block._stencilMask);
    pt.put(entryName + ".stencilWriteMask", block._stencilWriteMask);
}

size_t RenderStateBlock::loadFromXML(const string& entryName, const boost::property_tree::ptree& pt) {
    RenderStateBlock block;
    block.setColourWrites(pt.get(entryName + ".colourWrite.<xmlattr>.r", true),
                          pt.get(entryName + ".colourWrite.<xmlattr>.g", true),
                          pt.get(entryName + ".colourWrite.<xmlattr>.b", true),
                          pt.get(entryName + ".colourWrite.<xmlattr>.a", true));

    block.setZBias(pt.get(entryName + ".zBias", 0.0f),
                   pt.get(entryName + ".zUnits", 0.0f));

    block.setZFunc(TypeUtil::StringToComparisonFunction(pt.get(entryName + ".zFunc", "LEQUAL").c_str()));
    block.setTessControlPoints(pt.get(entryName + ".tessControlPoints", 3u));
    block.setCullMode(TypeUtil::StringToCullMode(pt.get(entryName + ".cullMode", "CW/BACK").c_str()));
    block.setFillMode(TypeUtil::StringToFillMode(pt.get(entryName + ".fillMode", "SOLID").c_str()));

    block.setFrontFaceCCW(pt.get(entryName + ".frontFaceCCW", true));
    block.setScissorTest(pt.get(entryName + ".scissorTestEnabled", false));
    block.depthTestEnabled(pt.get(entryName + ".depthTestEnabled", true));

    block.setStencil(pt.get(entryName + ".stencilEnable", false),
                     pt.get(entryName + ".stencilRef", 0u),
                     TypeUtil::StringToStencilOperation(pt.get(entryName + ".stencilFailOp", "KEEP").c_str()),
                     TypeUtil::StringToStencilOperation(pt.get(entryName + ".stencilPassOp", "KEEP").c_str()),
                     TypeUtil::StringToStencilOperation(pt.get(entryName + ".stencilZFailOp", "KEEP").c_str()),
                     TypeUtil::StringToComparisonFunction(pt.get(entryName + ".stencilFunc", "NEVER").c_str()));
    
    block.setStencilReadWriteMask(pt.get(entryName + ".stencilMask", 0xFFFFFFFF),
                                  pt.get(entryName + ".stencilWriteMask", 0xFFFFFFFF));
    return block.getHash();
}

};
