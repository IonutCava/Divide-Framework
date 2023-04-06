#include "stdafx.h"

#include "Headers/FileManagement.h"

#include <filesystem>

namespace Divide {

ResourcePath::ResourcePath(const std::string_view& path)
    : _str( std::filesystem::path{path}.generic_string() )
{
}

size_t ResourcePath::length() const noexcept {
    return str().length();
}

bool ResourcePath::empty() const noexcept {
    return length() == 0;
}

const char* ResourcePath::c_str() const noexcept {
    return str().c_str();
}

ResourcePath& ResourcePath::pop_back() noexcept {
    if (!empty()) {
        _str.pop_back();
    }

    return *this;
}

ResourcePath& ResourcePath::append(const std::string_view& str) {
    _str.append(str);
    return *this;
}

ResourcePath operator+(const ResourcePath& lhs, const ResourcePath& rhs) {
    if (lhs.empty()) {
        return rhs;
    }

    if (rhs.empty()) {
        return lhs;
    }

    const bool hasSeparator = *lhs.str().rbegin() == '/' || *rhs.str().begin() == '/';
    return ResourcePath(lhs.str() + (hasSeparator ? "" : "/") + rhs.str());
}

ResourcePath operator+(const ResourcePath& lhs, const std::string_view& rhs) {
    ResourcePath ret(lhs);
    ret.append(rhs);
    return ret;
}

ResourcePath& operator+=(ResourcePath& lhs, const ResourcePath& rhs) {
    lhs.append(rhs.str());
    return lhs;
}

ResourcePath& operator+=(ResourcePath& lhs, const std::string_view& rhs) {
    lhs.append(rhs);
    return lhs;
}

bool operator== (const ResourcePath& lhs, const std::string_view& rhs) {
    return lhs == ResourcePath(rhs);
}

bool operator!= (const ResourcePath& lhs, const std::string_view& rhs) {
    return lhs != ResourcePath(rhs);
}

bool operator== (const ResourcePath& lhs, const ResourcePath& rhs) {
    return pathCompare(lhs.c_str(), rhs.c_str());
}

bool operator!= (const ResourcePath& lhs, const ResourcePath& rhs) {
    return !pathCompare(lhs.c_str(), rhs.c_str());
}

}; //namespace Divide