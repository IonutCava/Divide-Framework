#include "stdafx.h"

#include "Core/Headers/StringHelper.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Shapes/Headers/Mesh.h"
#include "Geometry/Importer/Headers/MeshImporter.h"

namespace Divide {

struct MeshLoadData {
    explicit MeshLoadData(Mesh_ptr mesh,
                          ResourceCache* cache,
                          PlatformContext* context,
                          const ResourceDescriptor& descriptor)
        : _mesh(MOV(mesh)),
          _cache(cache),
          _context(context),
          _descriptor(descriptor)
    {
    }

    Mesh_ptr _mesh;
    ResourceCache* _cache;
    PlatformContext* _context;
    ResourceDescriptor _descriptor;

};

void threadedMeshLoad(MeshLoadData loadData, Import::ImportData tempMeshData) {
    OPTICK_EVENT();

    if (MeshImporter::loadMeshDataFromFile(*loadData._context, tempMeshData)) {
        if (!MeshImporter::loadMesh(tempMeshData.loadedFromFile(), loadData._mesh, *loadData._context, loadData._cache, tempMeshData)) {
            loadData._mesh.reset();
        }
    } else {
        loadData._mesh.reset();
        //handle error
        const string msg = Util::StringFormat("Failed to import mesh [ %s ]!", loadData._descriptor.assetName().c_str());
        DIVIDE_UNEXPECTED_CALL_MSG(msg.c_str());
    }

    if (!loadData._mesh->load()) {
        loadData._mesh.reset();
    }
}

template<>
CachedResource_ptr ImplResourceLoader<Mesh>::operator()() {
    Import::ImportData tempMeshData(_descriptor.assetLocation(), _descriptor.assetName());
    Mesh_ptr ptr(MemoryManager_NEW Mesh(_context.gfx(),
                                        _cache,
                                        _loadingDescriptorHash,
                                        _descriptor.resourceName(),
                                        tempMeshData.modelName(),
                                        tempMeshData.modelPath()),
                                DeleteResource(_cache));

    if (ptr) {
        ptr->setState(ResourceState::RES_LOADING);
    }

    MeshLoadData loadingData(ptr, _cache, &_context, _descriptor);
    if (_descriptor.threaded()) {
        Task* task = CreateTask([this, tempMeshData, loadingData](const Task &) {
                                    threadedMeshLoad(loadingData, tempMeshData);
                                });

        Start(*task, _context.taskPool(TaskPoolType::HIGH_PRIORITY));
    } else {
        threadedMeshLoad(loadingData, tempMeshData);
    }
    return ptr;
}

}
