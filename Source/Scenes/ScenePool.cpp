

#include "Headers/ScenePool.h"

#include "Managers/Headers/ProjectManager.h"
#include "Scenes/DefaultScene/Headers/DefaultScene.h"

#include "DefaultScene/Headers/DefaultScene.h"
#include "WarScene/Headers/WarScene.h"

namespace Divide {

namespace SceneList
{
    [[nodiscard]] static SceneFactoryMap& sceneFactoryMap()
    {
        NO_DESTROY static SceneFactoryMap sceneFactory{};
        return sceneFactory;
    }

    [[nodiscard]] static SceneNameMap& sceneNameMap()
    {
        NO_DESTROY static SceneNameMap sceneNameMap{};
        return sceneNameMap;
    }

    void registerSceneFactory(const char* name, const ScenePtrFactory& factoryFunc)
    {
        sceneNameMap()[_ID(name)] = name;
        sceneFactoryMap()[_ID(name)] = factoryFunc;
    }
}

ScenePool::ScenePool(Project& parentMgr)
  : _parentProject(parentMgr)
{
    assert(!SceneList::sceneFactoryMap().empty());
}

ScenePool::~ScenePool()
{
    vector<std::shared_ptr<Scene>> tempScenes;
    {   
        SharedLock<SharedMutex> r_lock(_sceneLock);
        tempScenes.insert(eastl::cend(tempScenes),
                          eastl::cbegin(_createdScenes),
                          eastl::cend(_createdScenes));
    }

    for (const std::shared_ptr<Scene>& scene : tempScenes)
    {
        Attorney::ProjectScenePool::unloadScene( _parentProject, scene.get());
        deleteScene(scene->getGUID());
    }

    {
        LockGuard<SharedMutex> w_lock(_sceneLock);
        _createdScenes.clear();
    }
}

bool ScenePool::defaultSceneActive() const noexcept
{
    return !_defaultScene || !_activeScene || _activeScene->getGUID() == _defaultScene->getGUID();
}

Scene* ScenePool::activeScene() const noexcept
{
    return _activeScene;
}

void ScenePool::activeScene(Scene& scene) noexcept
{
    _activeScene = &scene;
}

Scene* ScenePool::defaultScene() const noexcept
{
    return _defaultScene;
}

Scene* ScenePool::getOrCreateScene(PlatformContext& context, Project& parent, const SceneEntry& sceneEntry, bool& foundInCache)
{
    DIVIDE_ASSERT(!sceneEntry._name.empty());

    foundInCache = false;
    Scene_ptr ret = nullptr;

    LockGuard<SharedMutex> lock(_sceneLock);
    for (const Scene_ptr& scene : _createdScenes)
    {
        if (scene->resourceName().compare(sceneEntry._name) == 0)
        {
            ret = scene;
            foundInCache = true;
            break;
        }
    }

    if (ret == nullptr)
    {
        const auto creationFunc = SceneList::sceneFactoryMap()[_ID(sceneEntry._name.c_str())];
        if (creationFunc)
        {
            ret = creationFunc(context, parent, sceneEntry );
        }
        else
        {
            ret = std::make_shared<Scene>(context, parent, sceneEntry );
        }

        // Default scene is the first scene we load
        if (!_defaultScene)
        {
            _defaultScene = ret.get();
        }

        if (ret != nullptr)
        {
            _createdScenes.push_back(ret);
        }
    }
    
    return ret.get();
}

bool ScenePool::deleteScene(const I64 targetGUID)
{
    if (targetGUID != -1)
    {
        const I64 defaultGUID = _defaultScene ? _defaultScene->getGUID() : 0;
        const I64 activeGUID = _activeScene ? _activeScene->getGUID() : 0;

        if (targetGUID != defaultGUID)
        {
            if (targetGUID == activeGUID && defaultGUID != 0)
            {
                _parentProject.setActiveScene(_defaultScene);
            }
        }
        else
        {
            _defaultScene = nullptr;
        }

        LockGuard<SharedMutex> w_lock(_sceneLock);
        erase_if(_createdScenes, [&targetGUID](const auto& s) noexcept { return s->getGUID() == targetGUID; });

        return true;
    }

    return false;
}

vector<Str<256>> ScenePool::customCodeScenes(const bool sorted) const
{
    vector<Str<256>> scenes;
    for (const SceneList::SceneNameMap::value_type& it : SceneList::sceneNameMap())
    {
        scenes.push_back(it.second);
    }

    if (sorted)
    {
        eastl::sort(begin(scenes),
                    end(scenes),
                    [](const Str<256>& a, const Str<256>& b)
                    {
                        return a < b;
                    });
    }

    return scenes;
}

} //namespace Divide
