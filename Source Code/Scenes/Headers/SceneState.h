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
#ifndef _SCENE_STATE_H_
#define _SCENE_STATE_H_

#include "Platform/Video/Headers/RenderAPIEnums.h"
#include "Platform/Audio/Headers/AudioDescriptor.h"
#include "Scenes/Headers/SceneComponent.h"
#include "Utility/Headers/Localization.h"

/// This class contains all the variables that define each scene's
/// "unique"-ness:
/// background music, wind information, visibility settings, camera movement,
/// BB and Skeleton visibility, fog info, etc

/// Fog information (fog is so game specific, that it belongs in SceneState not
/// SceneRenderState

namespace Divide {

enum class MusicType : U8 {
    TYPE_BACKGROUND = 0,
    TYPE_COMBAT,
    COUNT
};

struct WaterBodyData
{
    vec4<F32> _positionW = VECTOR4_ZERO;
    ///length, depth, width
    vec4<F32> _extents = VECTOR4_ZERO;
};

struct ProbeData
{
    // (w == 1) - enabled
    vec4<F32> _positionW = VECTOR4_ZERO;
    vec4<F32> _halfExtents = VECTOR4_ZERO;
};

struct FogDetails
{
    vec4<F32> _colourAndDensity = VECTOR4_ZERO;
    vec4<F32> _colourSunScatter = VECTOR4_ZERO;
};

class Scene;
class LightPool;
class RenderPass;

/// Contains all the information needed to render the scene: camera position,
/// render state, etc
class SceneRenderState : public SceneComponent {
   public:
    enum class RenderOptions : U16 {
        /// Show/hide axis aligned bounding boxes
        RENDER_AABB = toBit(1),
        /// Show/hide oriented bounding boxes
        RENDER_OBB = toBit(2),
        /// Show/hide bounding spheres
        RENDER_BSPHERES = toBit(3),
        /// Show/hide custom IMPrimitive elements
        RENDER_CUSTOM_PRIMITIVES = toBit(4),
        /// Show/hide geometry
        RENDER_GEOMETRY = toBit(5),
        /// Render skeletons for animated geometry
        RENDER_SKELETONS = toBit(6),
        /// Render wireframe for all scene geometry
        RENDER_WIREFRAME = toBit(7),
        RENDER_OCTREE_REGIONS = toBit(8),
        SCENE_GIZMO = toBit(9),
        SELECTION_GIZMO = toBit(10),
        ALL_GIZMOS = toBit(11),
        COUNT = 11
    };

    explicit SceneRenderState(Scene& parentScene);

    void renderMask(U16 mask);
    [[nodiscard]] bool isEnabledOption(RenderOptions option) const;
    void enableOption(RenderOptions option);
    void disableOption(RenderOptions option);
    void toggleOption(RenderOptions option);
    void toggleOption(RenderOptions option, bool state);
    
    PROPERTY_RW(F32, generalVisibility, 1000.0f);
    PROPERTY_RW(F32, grassVisibility, 1000.0f);
    PROPERTY_RW(F32, treeVisibility, 1000.0f);
    PROPERTY_RW(U8, renderPass, 0u);
    PROPERTY_RW(FogDetails, fogDetails);

    [[nodiscard]] vec4<U16>& lodThresholds() noexcept { return _lodThresholds; }
    [[nodiscard]] vec4<U16> lodThresholds(RenderStage stage = RenderStage::DISPLAY) const noexcept;

  protected:
    vec4<U16> _lodThresholds;
    U16 _stateMask;
};

class Camera;

enum class MoveDirection : I8 {
    NONE = 0,
    NEGATIVE = -1,
    POSITIVE = 1
};

constexpr F32 DEFAULT_PLAYER_HEIGHT = 1.82f;

struct SceneStatePerPlayer {
    void resetMovement() noexcept {
        _moveFB = _moveLR = _moveUD = _angleUD = _angleLR = _roll = _zoom = MoveDirection::NONE;
    }

    void resetAll() noexcept {
        resetMovement();
        _cameraUnderwater = false;
        _cameraUpdated = false;
        _overrideCamera = nullptr;
        _cameraLockedToMouse = false;
    }

    PROPERTY_RW(bool, cameraUnderwater, false);
    PROPERTY_RW(bool, cameraUpdated, false);
    PROPERTY_RW(bool, cameraLockedToMouse, false);
    PROPERTY_RW(MoveDirection, moveFB, MoveDirection::NONE);   ///< forward-back move change detected
    PROPERTY_RW(MoveDirection, moveLR, MoveDirection::NONE);   ///< left-right move change detected
    PROPERTY_RW(MoveDirection, moveUD, MoveDirection::NONE);   ///< up-down move change detected
    PROPERTY_RW(MoveDirection, angleUD, MoveDirection::NONE);  ///< up-down angle change detected
    PROPERTY_RW(MoveDirection, angleLR, MoveDirection::NONE);  ///< left-right angle change detected
    PROPERTY_RW(MoveDirection, roll, MoveDirection::NONE);     ///< roll left or right change detected
    PROPERTY_RW(MoveDirection, zoom, MoveDirection::NONE);     ///< zoom in or out detected
    PROPERTY_RW(mat4<F32>, previousViewMatrix);
    PROPERTY_RW(mat4<F32>, previousProjectionMatrix);
    POINTER_RW(Camera, overrideCamera, nullptr);

    const F32 _headHeight = DEFAULT_PLAYER_HEIGHT;
};

class SceneState : public SceneComponent {
   public:
    /// Background music map : trackName - track
    using MusicPlaylist = hashMap<U64, AudioDescriptor_ptr>;

    explicit SceneState(Scene& parentScene)
        : SceneComponent(parentScene),
          _renderState(parentScene)
     {
     }

    void onPlayerAdd(const U8 index) {
        // Just reset everything
        onPlayerRemove(index);
    }

    inline void onPlayerRemove(const U8 index) {
        _playerState[index].resetAll();
    }

    inline [[nodiscard]] SceneStatePerPlayer& playerState() {
        return _playerState[playerPass()];
    }

    inline [[nodiscard]] const SceneStatePerPlayer& playerState() const {
        return _playerState[playerPass()];
    }

    inline [[nodiscard]] SceneStatePerPlayer& playerState(const U8 index) {
        return _playerState[index];
    }

    inline [[nodiscard]] const SceneStatePerPlayer& playerState(const U8 index) const {
        return _playerState[index];
    }

    inline [[nodiscard]] SceneRenderState& renderState() noexcept { return _renderState; }
    inline [[nodiscard]] MusicPlaylist& music(const MusicType type) noexcept { return _music[to_U32(type)]; }

    inline [[nodiscard]] const SceneRenderState& renderState() const noexcept { return _renderState; }
    inline [[nodiscard]] const MusicPlaylist& music(const MusicType type) const noexcept { return _music[to_U32(type)]; }

    inline [[nodiscard]] vector<WaterBodyData>& waterBodies() noexcept { return _waterBodies; }
    inline [[nodiscard]] const vector<WaterBodyData>& waterBodies() const noexcept { return _waterBodies; }

    PROPERTY_RW(U8, playerPass, 0u);
    PROPERTY_RW(bool, saveLoadDisabled, false);
    PROPERTY_RW(F32, windSpeed, 1.0f);
    PROPERTY_RW(F32, windDirX, 0.1f);
    PROPERTY_RW(F32, windDirZ, 0.7f);
    PROPERTY_RW(F32, lightBleedBias, 0.2f);
    PROPERTY_RW(F32, minShadowVariance, 0.00001f);

protected:
    SceneRenderState _renderState;
    std::array<MusicPlaylist, to_base(MusicType::COUNT)> _music;
    std::array<SceneStatePerPlayer, Config::MAX_LOCAL_PLAYER_COUNT> _playerState;
    vector<WaterBodyData> _waterBodies;
};

}  // namespace Divide
#endif