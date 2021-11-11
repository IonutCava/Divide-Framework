#include "stdafx.h"

#include "Headers/glHardwareQueryPool.h"

namespace Divide {

glHardwareQueryPool::glHardwareQueryPool(GFXDevice& context)
    : _context(context)
{
}

glHardwareQueryPool::~glHardwareQueryPool()
{
    destroy();
}

void glHardwareQueryPool::init(const hashMap<GLenum, U32>& sizes) {
    destroy();
    for (auto [type, size] : sizes) {
        const U32 j = std::max(size, 1u);
        _index[type] = 0;
        auto& pool = _queryPool[type];
        for (U32 i = 0; i < j; ++i) {
            pool.emplace_back(MemoryManager_NEW glHardwareQueryRing(_context, type, 1, i));
        }
    }
}

void glHardwareQueryPool::destroy() {
    for (auto& [type, container] : _queryPool) {
        MemoryManager::DELETE_CONTAINER(container);
    }
    _queryPool.clear();
}

glHardwareQueryRing& glHardwareQueryPool::allocate(const GLenum queryType) {
    return *_queryPool[queryType][++_index[queryType]];
}

void glHardwareQueryPool::deallocate(const glHardwareQueryRing& query) {
    const GLenum type = query.type();
    U32& index = _index[type];
    auto& pool = _queryPool[type];
    for (U32 i = 0; i < index; ++i) {
        if (pool[i]->id() == query.id()) {
            std::swap(pool[i], pool[index - 1]);
            --index;
            return;
        }
    }

    DIVIDE_UNEXPECTED_CALL();
}

}; //namespace Divide