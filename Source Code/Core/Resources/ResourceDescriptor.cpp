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

    Util::Hash_combine(_hash, fullPath);
    Util::Hash_combine(_hash, _flag);
    Util::Hash_combine(_hash, _ID);
    Util::Hash_combine(_hash, _mask.i);
    Util::Hash_combine(_hash, _enumValue);
    Util::Hash_combine(_hash, _data.x);
    Util::Hash_combine(_hash, _data.y);
    Util::Hash_combine(_hash, _data.z);
    if (_propertyDescriptor) {
        Util::Hash_combine(_hash, _propertyDescriptor->getHash());
    }

    return _hash;
}

}