/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _SCENE_POOL_H_
#define _SCENE_POOL_H_

namespace Divide {

class Scene;
class SceneManager;
class ResourceCache;
class PlatformContext;
class ScenePool {
protected:
    SET_DELETE_FRIEND
    friend class SceneManager;
    ScenePool(SceneManager& parentMgr);
    ~ScenePool();

    Scene* getOrCreateScene(PlatformContext& context, ResourceCache* cache, SceneManager& parent, const Str256& name, bool& foundInCache);
    bool   deleteScene(I64 targetGUID);

    bool   defaultSceneActive() const noexcept;

    Scene&       defaultScene() noexcept;
    const Scene& defaultScene() const noexcept;
    Scene&       activeScene() noexcept;
    const Scene& activeScene() const noexcept;
    void         activeScene(Scene& scene) noexcept;

    vector<Str256> sceneNameList(bool sorted) const;

private:
    /// Pointer to the currently active scene
    Scene* _activeScene = nullptr;
    Scene* _loadedScene = nullptr;
    Scene* _defaultScene = nullptr;
    vector<std::shared_ptr<Scene>> _createdScenes;

    SceneManager& _parentMgr;

    mutable SharedMutex _sceneLock;
};
} //namespace Divide

#endif //_SCENE_POOL_H_
