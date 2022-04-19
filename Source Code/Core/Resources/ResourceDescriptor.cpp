#include "stdafx.h"

#include "Headers/ResourceDescriptor.h"

#include "Core/Math/Headers/MathHelper.h"
#include "Core/Headers/StringHelper.h"

namespace Divide {


size_t PropertyDescriptor::getHash() const {
    Util::Hash_combine(_hash, to_base(_type));
    return _hash;
}

ResourceDescriptor::ResourceDescriptor(const Str256& resourceName)
    : _resourceName(resourceName)
{
}

size_t ResourceDescriptor::getHash() const {
    _hash = 9999991;
    const std::string fullPath = _assetName.empty()
                                    ? resourceName().c_str()
                                    : Util::ReplaceString((_assetLocation + "/" + _assetName).str(), "//", "/", true);

    Util::Hash_combine(_hash, fullPath, _flag, _ID, _mask.i, _enumValue, _data.x, _data.y, _data.z);
    if (_propertyDescriptor) {
        Util::Hash_combine(_hash, _propertyDescriptor->getHash());
    }

    return _hash;
}

}