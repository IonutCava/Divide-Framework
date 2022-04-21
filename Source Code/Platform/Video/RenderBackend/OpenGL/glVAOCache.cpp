#include "stdafx.h"

#include "Headers/glVAOCache.h"
#include "Utility/Headers/Localization.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {
namespace GLUtil {

glVAOCache::~glVAOCache()
{
    DIVIDE_ASSERT(_cache.empty());
}

void glVAOCache::clear() {
    for (VAOMap::value_type& value : _cache) {
        if (value.second != GLUtil::k_invalidObjectID) {
            GL_API::DeleteVAOs(1, &value.second);
        }
    }    _cache.clear();
}


bool glVAOCache::getVAO(const size_t attributeHash, GLuint& vaoOut) {
    static U32 s_VAOidx = 0u;

    DIVIDE_ASSERT(Runtime::isMainThread());

    vaoOut = GLUtil::k_invalidObjectID;

    // See if we already have a matching VAO
    const VAOMap::iterator it = _cache.find(attributeHash);
    if (it != std::cend(_cache)) {
        // Remember it if we do
        vaoOut = it->second;
        // Return true on a cache hit;
        return true;
    }

    // Otherwise allocate a new VAO and save it in the cache
    glCreateVertexArrays(1, &vaoOut);
    DIVIDE_ASSERT(vaoOut != GLUtil::k_invalidObjectID, Locale::Get(_ID("ERROR_VAO_INIT")));
    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glObjectLabel(GL_VERTEX_ARRAY, vaoOut, -1, Util::StringFormat("GENERIC_VAO_%d", s_VAOidx++).c_str());
    }
    insert(_cache, attributeHash, vaoOut);
    return false;
}

}; //namespace GLUtil
}; //namespace Divide