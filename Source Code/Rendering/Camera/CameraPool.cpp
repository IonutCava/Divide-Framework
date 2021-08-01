#include "stdafx.h"

#include "Headers/Camera.h"

#include "Utility/Headers/Localization.h"

#include "Rendering/Camera/Headers/FirstPersonCamera.h"
#include "Rendering/Camera/Headers/FreeFlyCamera.h"
#include "Rendering/Camera/Headers/OrbitCamera.h"
#include "Rendering/Camera/Headers/ScriptedCamera.h"
#include "Rendering/Camera/Headers/ThirdPersonCamera.h"

namespace Divide {

std::array<Camera*, to_base(Camera::UtilityCamera::COUNT)> Camera::_utilityCameras;

U32 Camera::s_changeCameraId = 0;
Camera::ListenerMap Camera::s_changeCameraListeners;

SharedMutex Camera::s_cameraPoolLock;
Camera::CameraPool Camera::s_cameraPool;

void Camera::update(const U64 deltaTimeUS) {
    OPTICK_EVENT();

    const F32 deltaTimeMS = Time::MicrosecondsToMilliseconds<F32>(deltaTimeUS);

    SharedLock<SharedMutex> r_lock(s_cameraPoolLock);
    for (Camera* cam : s_cameraPool) {
        cam->update(deltaTimeMS);
    }
}

Camera* Camera::utilityCameraInternal(const UtilityCamera type) {
    if (type != UtilityCamera::COUNT) {
        return _utilityCameras[to_base(type)];
    }

    return nullptr;
}

void Camera::initPool() {
    _utilityCameras[to_base(UtilityCamera::DEFAULT)] = createCamera<FreeFlyCamera>("DefaultCamera");
    _utilityCameras[to_base(UtilityCamera::_2D)] = createCamera<StaticCamera>("2DRenderCamera");
    _utilityCameras[to_base(UtilityCamera::_2D_FLIP_Y)] = createCamera<StaticCamera>("2DRenderCameraFlipY");
    _utilityCameras[to_base(UtilityCamera::CUBE)] = createCamera<StaticCamera>("CubeCamera");
    _utilityCameras[to_base(UtilityCamera::DUAL_PARABOLOID)] = createCamera<StaticCamera>("DualParaboloidCamera");
}

void Camera::destroyPool() {
    Console::printfn(Locale::Get(_ID("CAMERA_MANAGER_DELETE")));
    Console::printfn(Locale::Get(_ID("CAMERA_MANAGER_REMOVE_CAMERAS")));

    _utilityCameras.fill(nullptr);

    ScopedLock<SharedMutex> w_lock(s_cameraPoolLock);
    for (Camera* cam : s_cameraPool) {
        cam->unload();
        MemoryManager::DELETE(cam);
    }
    s_cameraPool.clear();
}

Camera* Camera::createCameraInternal(const Str256& cameraName, const CameraType type) {
    Camera* camera = nullptr;
    switch (type) {
        case CameraType::FIRST_PERSON:
            camera = MemoryManager_NEW FirstPersonCamera(cameraName);
            break;
        case CameraType::STATIC:
            camera = MemoryManager_NEW StaticCamera(cameraName);
            break;
        case CameraType::FREE_FLY:
            camera = MemoryManager_NEW FreeFlyCamera(cameraName);
            break;
        case CameraType::ORBIT:
            camera = MemoryManager_NEW OrbitCamera(cameraName);
            break;
        case CameraType::SCRIPTED:
            camera = MemoryManager_NEW ScriptedCamera(cameraName);
            break;
        case CameraType::THIRD_PERSON:
            camera = MemoryManager_NEW ThirdPersonCamera(cameraName);
            break;
        case CameraType::COUNT: break;
    }

    if (camera != nullptr) {
        ScopedLock<SharedMutex> w_lock(s_cameraPoolLock);
        s_cameraPool.push_back(camera);
    }

    return camera;
}

bool Camera::destroyCameraInternal(Camera* camera) {
    if (camera != nullptr) {
        const U64 targetHash = _ID(camera->resourceName().c_str());
        if (camera->unload()) {
            ScopedLock<SharedMutex> w_lock(s_cameraPoolLock);
            erase_if(s_cameraPool, [targetHash](Camera* cam) { return _ID(cam->resourceName().c_str()) == targetHash; });
            MemoryManager::DELETE(camera);
            return true;
        }
    }

    return false;
}

Camera* Camera::findCameraInternal(U64 nameHash) {
    SharedLock<SharedMutex> r_lock(s_cameraPoolLock);
    const auto* const it = eastl::find_if(cbegin(s_cameraPool), cend(s_cameraPool), [nameHash](Camera* cam) { return _ID(cam->resourceName().c_str()) == nameHash; });
    if (it != std::end(s_cameraPool)) {
        return *it;
    }

    return nullptr;
}

bool Camera::removeChangeListener(const U32 id) {
    const ListenerMap::const_iterator it = s_changeCameraListeners.find(id);
    if (it != std::cend(s_changeCameraListeners)) {
        s_changeCameraListeners.erase(it);
        return true;
    }

    return false;
}

U32 Camera::addChangeListener(const DELEGATE<void, const Camera&>& f) {
    insert(s_changeCameraListeners, ++s_changeCameraId, f);
    return s_changeCameraId;
}

}; //namespace Divide