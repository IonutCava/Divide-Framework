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
#include "Scenes/Headers/SceneComponent.h"

/// This class contains all the variables that define each scene's
/// "unique"-ness:
/// background music, wind information, visibility settings, camera movement,
/// BB and Skeleton visibility, fog info, etc

/// Fog information (fog is so game specific, that it belongs in SceneState not
/// SceneRenderState

namespace Divide {

FWD_DECLARE_MANAGED_CLASS(AudioDescriptor);

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

bool operator==(const WaterBodyData& lhs, const WaterBodyData& rhs) noexcept;
bool operator!=(const WaterBodyData& lhs, const WaterBodyData& rhs) noexcept;

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

bool operator==(const FogDetails& lhs, const FogDetails& rhs) noexcept;
bool operator!=(const FogDetails& lhs, const FogDetails& rhs) noexcept;

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
        SELECTION_GIZMO = toBit(8),
        ALL_GIZMOS = toBit(9),
        COUNT = 9
    };

    explicit SceneRenderState(Scene& parentScene) noexcept;

    void renderMask(U16 mask);
    void enableOption(RenderOptions option) noexcept;
    void disableOption(RenderOptions option) noexcept;
    void toggleOption(RenderOptions option) noexcept;
    void toggleOption(RenderOptions option, bool state) noexcept;
    [[nodiscard]] bool isEnabledOption(RenderOptions option) const noexcept;

    PROPERTY_RW(F32, generalVisibility, 1000.0f);
    PROPERTY_RW(F32, grassVisibility, 1000.0f);
    PROPERTY_RW(F32, treeVisibility, 1000.0f);
    PROPERTY_RW(U8, renderPass, 0u);
    PROPERTY_RW(FogDetails, fogDetails);
    PROPERTY_RW(I64, singleNodeRenderGUID, -1);
    [[nodiscard]] vec4<U16>& lodThresholds() noexcept { return _lodThresholds; }
    [[nodiscard]] vec4<U16> lodThresholds(RenderStage stage = RenderStage::DISPLAY) const noexcept;

  protected:
    vec4<U16> _lodThresholds;
    U16 _stateMask = 0u;
};

class Camera;

enum class MoveDirection : I8
{
    NONE = 0,
    NEGATIVE = -1,
    POSITIVE = 1
};

constexpr F32 DEFAULT_PLAYER_HEIGHT = 1.82f;

struct MovementStack
{
    std::array<MoveDirection, 2> _directions = { MoveDirection::NONE, MoveDirection::NONE };

    [[nodiscard]] MoveDirection top() const noexcept
    {
        return _directions[0];
    }

    void push( const MoveDirection direction ) noexcept
    {
        _directions[1] = _directions[0];
        _directions[0] = direction;
    }

    void reset() noexcept
    {
        _directions.fill(MoveDirection::NONE);
    }
};

struct SceneStatePerPlayer {
    void resetMoveDirections() noexcept;
    void resetAll() noexcept;

    PROPERTY_RW(bool, cameraUnderwater, false);
    PROPERTY_RW(bool, cameraUpdated, false);
    PROPERTY_RW(bool, cameraLockedToMouse, false);

    MovementStack _moveFB;
    MovementStack _moveLR;
    MovementStack _moveUD;
    MovementStack _angleUD;
    MovementStack _angleLR;
    MovementStack _roll;
    MovementStack _zoom;

    POINTER_RW(Camera, overrideCamera, nullptr);

    const F32 _headHeight = DEFAULT_PLAYER_HEIGHT;
};

using WaterBodyDataContainer = eastl::fixed_vector<WaterBodyData, 5, true, eastl::dvd_allocator>;

class SceneState : public SceneComponent {
   public:
    /// Background music map : trackName - track
    using MusicPlaylist = hashMap<U64, AudioDescriptor_ptr>;

    explicit SceneState(Scene& parentScene);

    void onPlayerAdd(const U8 index) noexcept { onPlayerRemove(index); }

    inline void onPlayerRemove(const U8 index) noexcept { _playerState[index].resetAll(); }

    [[nodiscard]] inline SceneStatePerPlayer& playerState() noexcept { return _playerState[playerPass()]; }
    [[nodiscard]] inline const SceneStatePerPlayer& playerState() const noexcept { return _playerState[playerPass()]; }

    [[nodiscard]] inline SceneStatePerPlayer& playerState(const U8 index) noexcept { return _playerState[index]; }
    [[nodiscard]] inline const SceneStatePerPlayer& playerState(const U8 index) const noexcept { return _playerState[index]; }

    [[nodiscard]] inline SceneRenderState& renderState() noexcept { return _renderState; }
    [[nodiscard]] inline const SceneRenderState& renderState() const noexcept { return _renderState; }

    [[nodiscard]] inline MusicPlaylist& music(const MusicType type) noexcept { return _music[to_U32(type)]; }
    [[nodiscard]] inline const MusicPlaylist& music(const MusicType type) const noexcept { return _music[to_U32(type)]; }

    [[nodiscard]] inline WaterBodyDataContainer& waterBodies() noexcept { return _waterBodies; }
    [[nodiscard]] inline const WaterBodyDataContainer& waterBodies() const noexcept { return _waterBodies; }

    PROPERTY_RW(U8, playerPass, 0u);
    PROPERTY_RW(bool, saveLoadDisabled, false);
    PROPERTY_RW(F32, windSpeed, 1.0f);
    PROPERTY_RW(F32, windDirX, 0.1f);
    PROPERTY_RW(F32, windDirZ, 0.7f);
    PROPERTY_RW(F32, lightBleedBias, 0.2f);
    PROPERTY_RW(F32, minShadowVariance, 0.00001f);
    PROPERTY_RW(bool, screenshotRequestQueued, false);

protected:
    SceneRenderState _renderState;
    std::array<MusicPlaylist, to_base(MusicType::COUNT)> _music;
    std::array<SceneStatePerPlayer, Config::MAX_LOCAL_PLAYER_COUNT> _playerState;
    WaterBodyDataContainer _waterBodies;
};

FWD_DECLARE_MANAGED_CLASS(SceneState);

}  // namespace Divide
#endif