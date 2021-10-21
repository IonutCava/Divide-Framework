#include "stdafx.h"

#include "Headers/glVAOPool.h"
#include "Core/Headers/StringHelper.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"


namespace Divide {
namespace GLUtil {

namespace {
    constexpr U32 g_numWarmupVAOs = 25u;
};

glVAOPool::~glVAOPool()
{
    destroy();
}

void glVAOPool::init(const U32 capacity) {
    destroy();

    GLuint warmupVAOs[g_numWarmupVAOs];
    glCreateVertexArrays(g_numWarmupVAOs, warmupVAOs);

    _pool.resize(capacity, std::make_pair(0, false));
    for (auto& [vaoID, isUsed] : _pool) {
        glCreateVertexArrays(1, &vaoID);
        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            glObjectLabel(GL_VERTEX_ARRAY,
                vaoID,
                -1,
                Util::StringFormat("DVD_VAO_%d", vaoID).c_str());
        }
    }


    GL_API::DeleteVAOs(g_numWarmupVAOs, warmupVAOs);
}

void glVAOPool::destroy() {
    for (auto& [vaoID, isUsed] : _pool) {
        GL_API::DeleteVAOs(1, &vaoID);
    }
    _pool.clear();
}

GLuint glVAOPool::allocate() {
    for (auto& [vaoID, isUsed] : _pool) {
        if (isUsed == false) {
            isUsed = true;
            return vaoID;
        }
    }

    DIVIDE_UNEXPECTED_CALL();
    return 0;
}

void glVAOPool::allocate(const U32 count, GLuint* vaosOUT) {
    for (U32 i = 0; i < count; ++i) {
        vaosOUT[i] = allocate();
    }
}

void glVAOPool::deallocate(GLuint& vao) {
    assert(Runtime::isMainThread());

    vector<std::pair<GLuint, bool>>::iterator it = eastl::find_if(eastl::begin(_pool),
                                                                  eastl::end(_pool),
                                                                  [vao](std::pair<GLuint, bool>& entry) noexcept
                                                                  {
                                                                      return entry.first == vao;
                                                                  });

    assert(it != eastl::cend(_pool));
    // We don't know what kind of state we may have in the current VAO so delete it and create a new one.
    GL_API::DeleteVAOs(1, &it->first);
    glCreateVertexArrays(1, &it->first);

    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glObjectLabel(GL_VERTEX_ARRAY,
            it->first,
            -1,
            Util::StringFormat("DVD_VAO_%d", it->first).c_str());
    }
    it->second = false;
    vao = 0;
}


}; //namespace GLUtil
}; //namespace Divide