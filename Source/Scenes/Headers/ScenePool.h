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
#ifndef DVD_SCENE_POOL_H_
#define DVD_SCENE_POOL_H_

namespace Divide {

class Project;
class PlatformContext;

struct SceneEntry;

FWD_DECLARE_MANAGED_CLASS( Scene );

class ScenePool
{
  protected:
    friend class Project;
    ScenePool(Project& parentProject);
    ~ScenePool();

    Scene* getOrCreateScene(PlatformContext& context, Project& parent, const SceneEntry& sceneEntry, bool& foundInCache);
    bool   deleteScene(I64 targetGUID);

  public:
    bool   defaultSceneActive() const noexcept;

    Scene* defaultScene() const noexcept;

    Scene* activeScene() const noexcept;
    void   activeScene(Scene& scene) noexcept;

    vector<Str<256>> customCodeScenes(bool sorted) const;

  private:
    mutable SharedMutex _sceneLock;

    Project& _parentProject;

    Scene* _activeScene = nullptr;
    Scene* _loadedScene = nullptr;
    Scene* _defaultScene = nullptr;
    
    vector<Scene_ptr> _createdScenes;

};

FWD_DECLARE_MANAGED_CLASS(ScenePool);

} //namespace Divide

#endif //DVD_SCENE_POOL_H_
