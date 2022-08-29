#include "stdafx.h"

#include "Headers/Hashable.h"

namespace Divide {
    size_t Hashable::getHash() const {
        return _hash;
    }

    Hashable& Hashable::operator=(Hashable const& old) noexcept
    {
        if (&old != this) {
            _hash = old._hash;
        }

        return *this;
    }
} //namespace Divide
