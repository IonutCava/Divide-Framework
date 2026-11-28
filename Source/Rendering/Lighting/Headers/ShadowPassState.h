/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#pragma once

#include "../../../../Assets/Shaders/GLSL/Common/csmShadow.cmn"

#include <algorithm>

namespace Divide
{
    // Clip-space-normalised matrix entries
    constexpr F32 SHADOW_MATRIX_TOLERANCE = 1e-5f;
    // World units
    constexpr F32 SHADOW_POSITION_TOLERANCE = 1e-3f;
    // World units
    constexpr F32 SHADOW_DISTANCE_TOLERANCE = 1e-2f;

    template<size_t N>
    [[nodiscard]] constexpr bool ShadowValuesEqual(const std::array<F32, N>& lhs, const std::array<F32, N>& rhs,
                                                   const F32 tolerance) noexcept
    {
        for (size_t i = 0u; i < N; ++i)
        {
            if (!COMPARE_TOLERANCE(lhs[i], rhs[i], tolerance))
            {
                return false;
            }
        }
        return true;
    }

    template<typename Matrix>
    [[nodiscard]] bool ShadowMatricesEqual(const Matrix& lhs, const Matrix& rhs) noexcept
    {
        if constexpr (requires { lhs.compare(rhs, SHADOW_MATRIX_TOLERANCE); })
        {
            return lhs.compare(rhs, SHADOW_MATRIX_TOLERANCE);
        }
        else
        {
            return ShadowValuesEqual(lhs, rhs, SHADOW_MATRIX_TOLERANCE);
        }
    }

    // Fraction of the general visibility distance the camera may travel before camera-dependent static shadow content
    // (terrain tessellation, vegetation visibility) is rebuilt.
    constexpr F32 SHADOW_RANGE_ANCHOR_VISIBILITY_FRACTION = 0.05f;

    [[nodiscard]] inline F32 QuantiseUpToStep(const F32 value, const F32 step) noexcept
    {
        return step > 0.f ? std::ceil(value / step) * step : value;
    }

    // Per-axis (x, y, z) anchor cell size. X and Z are multiples of the terrain snap grid so anchor cells align with
    // terrain tessellation steps.
    [[nodiscard]] inline std::array<F32, 3> ShadowRangeAnchorStep(const std::array<F32, 2>& terrainSnapXZ,
                                                                  const F32 generalVisibility) noexcept
    {
        const F32 base = std::max(0.f, generalVisibility * SHADOW_RANGE_ANCHOR_VISIBILITY_FRACTION);
        const auto alignToSnap = [base](const F32 snap) noexcept
        {
            return snap > 0.f ? std::max(1.f, std::ceil(base / snap)) * snap : base;
        };
        return { alignToSnap(terrainSnapXZ[0]), base, alignToSnap(terrainSnapXZ[1]) };
    }

    [[nodiscard]] inline std::array<F32, 3> SnapShadowRangeAnchor(const std::array<F32, 3>& eye,
                                                                  const std::array<F32, 3>& step) noexcept
    {
        std::array<F32, 3> anchor = eye;
        for (size_t i = 0u; i < anchor.size(); ++i)
        {
            if (step[i] > 0.f)
            {
                anchor[i] = std::floor(eye[i] / step[i]) * step[i];
            }
        }
        return anchor;
    }

    struct WorldAOCacheKey
    {
        I64 _sceneGUID = -1;
        U64 _revision = 0u;
        std::array<F32, 2> _center{};
        std::array<F32, 2> _height{};
        U32 _renderMask = 0u;
    };

    struct WorldAOCacheState
    {
        static constexpr F32 HORIZONTAL_EXTENT = 500.f;
        static constexpr F32 RECENTER_DISTANCE = HORIZONTAL_EXTENT * 0.5f;

        [[nodiscard]] bool needsRefresh(const WorldAOCacheKey& key, const bool pendingCasters) const noexcept
        {
            return !_valid || pendingCasters || key._sceneGUID != _key._sceneGUID ||
                   key._revision != _key._revision || !ShadowValuesEqual(key._height, _key._height, SHADOW_DISTANCE_TOLERANCE) ||
                   key._renderMask != _key._renderMask ||
                   std::abs(key._center[0] - _key._center[0]) >= RECENTER_DISTANCE ||
                   std::abs(key._center[1] - _key._center[1]) >= RECENTER_DISTANCE;
        }

        WorldAOCacheKey _key{};
        bool _valid = false;
    };

    [[nodiscard]] constexpr F32 CSMOverlapNearFraction(const F32 cameraNear, const F32 clipRange,
                                                        const F32 previousStart, const F32 previousEnd) noexcept
    {
        DIVIDE_ASSERT(clipRange > 0.f);
        return std::clamp((CSM_BLEND_START(previousStart, previousEnd) - cameraNear) / clipRange, 0.f, 1.f);
    }

    [[nodiscard]] constexpr U32 ShadowLayerMask(const U8 layerCount) noexcept
    {
        DIVIDE_ASSERT(layerCount <= 32u);
        return layerCount == 32u ? ~U32{0u} : (U32{1u} << layerCount) - 1u;
    }

    [[nodiscard]] constexpr bool ShadowLayerSelected(const U32 mask, const U8 layer) noexcept
    {
        return layer < 32u && (mask & (U32{1u} << layer)) != 0u;
    }

    [[nodiscard]] constexpr U32 ShadowLayerUpdateMask(const U8 layerCount, const U32 dirtyLayers,
                                                      const bool cacheValid, const bool fullInvalidation) noexcept
    {
        const U32 allLayers = ShadowLayerMask(layerCount);
        return !cacheValid || fullInvalidation ? allLayers : dirtyLayers & allLayers;
    }

    struct ShadowRefreshPlan
    {
        U32 _staticMask = 0u;
        U32 _dynamicMask = 0u;

        [[nodiscard]] constexpr U32 updateMask() const noexcept
        {
            return _staticMask | _dynamicMask;
        }
    };

    struct ShadowStaticCacheKey
    {
        I64 _owner = -1;
        std::array<F32, 16> _viewProjection{};
        U64 _coverageRevision = 0u;
        U32 _renderMask = 0u;
        std::array<U16, 2> _resolution{};
        std::array<F32, 2> _visibilityDistances{};
        std::array<F32, 3> _rangeCameraEye{};
        U8 _samples = 0u;
        U8 _splitCount = 0u;

        [[nodiscard]] bool operator==(const ShadowStaticCacheKey& other) const noexcept
        {
            return _owner == other._owner && _coverageRevision == other._coverageRevision &&
                   _renderMask == other._renderMask && _resolution == other._resolution &&
                   _samples == other._samples && _splitCount == other._splitCount &&
                   ShadowValuesEqual(_viewProjection, other._viewProjection, SHADOW_MATRIX_TOLERANCE) &&
                   ShadowValuesEqual(_visibilityDistances, other._visibilityDistances, SHADOW_DISTANCE_TOLERANCE) &&
                   ShadowValuesEqual(_rangeCameraEye, other._rangeCameraEye, SHADOW_POSITION_TOLERANCE);
        }
    };

    struct ShadowStaticCacheState
    {
        [[nodiscard]] bool needsRefresh(const ShadowStaticCacheKey& key, const bool staticDirty) const noexcept
        {
            return !_valid || staticDirty || _key != key;
        }

        void complete(const ShadowStaticCacheKey& key, const bool pendingCasters) noexcept
        {
            _key = key;
            _valid = !pendingCasters;
        }

        ShadowStaticCacheKey _key{};
        bool _valid = false;
    };

    [[nodiscard]] constexpr ShadowRefreshPlan BuildShadowRefreshPlan(const U8 layerCount, const U32 dirtyLayers,
                                                                       const bool cacheValid, const bool staticDirty,
                                                                       const bool dynamicDirty) noexcept
    {
        const U32 staticMask = ShadowLayerUpdateMask(layerCount, dirtyLayers, cacheValid, staticDirty);
        // A rebuilt static base must have dynamic casters composited again too.
        return {staticMask, staticMask | ShadowLayerUpdateMask(layerCount, dirtyLayers, cacheValid, dynamicDirty)};
    }

    [[nodiscard]] constexpr F32 ShadowBlurTexelScale(const U8 firstLayer) noexcept
    {
        DIVIDE_ASSERT(firstLayer < 32u);
        return 1.f / static_cast<F32>(U32{1u} << firstLayer);
    }

    template<typename Callback>
    void ForEachShadowLayerRange(const U32 mask, const U8 layerCount, Callback&& callback)
    {
        DIVIDE_ASSERT(layerCount <= 32u);
        for (U8 layer = 0u; layer < layerCount;)
        {
            if (!ShadowLayerSelected(mask, layer))
            {
                ++layer;
                continue;
            }
            const U8 first = layer++;
            while (layer < layerCount && ShadowLayerSelected(mask, layer))
            {
                ++layer;
            }
            callback(first, static_cast<U8>(layer - first));
        }
    }

    [[nodiscard]] constexpr size_t ShadowReservationCount(const size_t contentCount,
                                                           const size_t maxCascades,
                                                           const bool directional) noexcept
    {
        if (contentCount == 0u || (directional && contentCount > maxCascades))
        {
            return 0u;
        }
        return directional ? maxCascades : contentCount;
    }

    template<typename Bounds>
    [[nodiscard]] bool ShadowVolumeAffectsLight(const bool directional, const Bounds& volume,
                                                 const Bounds& lightBounds) noexcept
    {
        return directional || volume.collision(lightBounds);
    }

    template<typename Queue, typename Bounds>
    void AppendShadowVolumeChange(Queue& pending, const Bounds& previous, const bool previousStatic,
                                  const Bounds& current, const bool currentStatic)
    {
        pending.push_back({ previous, previousStatic });
        pending.push_back({ current, currentStatic });
    }

    template<typename Layers, typename Owner>
    [[nodiscard]] bool TryReuseShadowLayers(Layers& layers, const Owner owner,
                                             const size_t offset, const size_t count,
                                             const size_t contentCount) noexcept
    {
        if (count == 0u || contentCount == 0u || offset > layers.size() || count > layers.size() - offset)
        {
            return false;
        }
        for (size_t i = offset; i < offset + count; ++i)
        {
            if (layers[i]._lightGUID != owner || layers[i]._contentLayers != contentCount)
            {
                return false;
            }
        }
        for (size_t i = offset; i < offset + count; ++i)
        {
            layers[i]._lifetime = 0u;
        }
        return true;
    }

    template<typename Layers, typename Owner, typename Lifetime>
    void ReleaseShadowLayers(Layers& layers, const Owner owner, const Lifetime expired) noexcept
    {
        for (auto& layer : layers)
        {
            if (layer._lightGUID == owner)
            {
                layer._lifetime = expired;
                layer._lightGUID = -1;
                layer._contentLayers = 0u;
            }
        }
    }

    template<typename Layers, typename Lifetime>
    [[nodiscard]] size_t FindAvailableShadowLayers(const Layers& layers, const size_t count,
                                                    const Lifetime expired) noexcept
    {
        if (count == 0u || count > layers.size())
        {
            return layers.size();
        }
        for (size_t offset = 0u; offset <= layers.size() - count; ++offset)
        {
            bool available = true;
            for (size_t i = offset; i < offset + count; ++i)
            {
                if (layers[i]._lifetime < expired)
                {
                    available = false;
                    break;
                }
            }
            if (available)
            {
                return offset;
            }
        }
        return layers.size();
    }

    template<size_t TypeCount>
    class ShadowPassSlots
    {
      public:
        explicit ShadowPassSlots(const U32 totalLimit) noexcept
            : _totalLimit(totalLimit)
        {
        }

        [[nodiscard]] bool hasCapacity(const size_t type, const U32 typeLimit) const noexcept
        {
            DIVIDE_ASSERT(type < TypeCount);
            return _total < _totalLimit && _perType[type] < typeLimit;
        }

        [[nodiscard]] I32 reserve(const size_t type, const U32 typeLimit) noexcept
        {
            if (!hasCapacity(type, typeLimit))
            {
                return -1;
            }

            ++_total;
            return to_I32(_perType[type]++);
        }

      private:
        std::array<U32, TypeCount> _perType{};
        U32 _total = 0u;
        U32 _totalLimit;
    };

    class ShadowMovementBatch
    {
      public:
        struct Snapshot
        {
            size_t _count;
            bool _invalidateAll;
        };

        [[nodiscard]] Snapshot begin(const size_t pendingCount) noexcept
        {
            if (!_active)
            {
                _count = pendingCount;
                _active = true;
            }
            return { _count, _invalidateAll };
        }

        [[nodiscard]] size_t finish(const size_t pendingCount) noexcept
        {
            size_t consumed = 0u;
            if (_active)
            {
                DIVIDE_ASSERT(_count <= pendingCount);
                consumed = _count;
                _invalidateAll = false;
            }
            else if (pendingCount > 0u)
            {
                consumed = pendingCount;
                _invalidateAll = true;
            }
            _count = 0u;
            _active = false;
            return consumed;
        }

      private:
        size_t _count = 0u;
        bool _active = false;
        bool _invalidateAll = false;
    };

    template<typename Matrix>
    class ShadowCameraState
    {
      public:
        [[nodiscard]] bool update(const Matrix& view, const Matrix& projection,
                                  const F32 nearPlane, const F32 farPlane) noexcept
        {
            const bool changed = !_valid ||
                                 !COMPARE_TOLERANCE(_nearPlane, nearPlane, SHADOW_DISTANCE_TOLERANCE) ||
                                 !COMPARE_TOLERANCE(_farPlane, farPlane, SHADOW_DISTANCE_TOLERANCE) ||
                                 !ShadowMatricesEqual(_view, view) || !ShadowMatricesEqual(_projection, projection);
            if (changed)
            {
                _view = view;
                _projection = projection;
                _nearPlane = nearPlane;
                _farPlane = farPlane;
                _valid = true;
            }
            return changed;
        }

      private:
        Matrix _view{};
        Matrix _projection{};
        F32 _nearPlane = 0.f;
        F32 _farPlane = 0.f;
        bool _valid = false;
    };

    class ShadowRangeState
    {
      public:
        [[nodiscard]] bool update(const F32 range) noexcept
        {
            if (!_valid || !COMPARE_TOLERANCE(_range, range, SHADOW_DISTANCE_TOLERANCE))
            {
                _range = range;
                _valid = true;
                return true;
            }
            return false;
        }

      private:
        F32 _range = 0.f;
        bool _valid = false;
    };

    template<std::size_t SplitCount>
    class ShadowDirectionalSettingsState
    {
      public:
        [[nodiscard]] bool update(const F32 nearClipOffset, const std::array<bool, SplitCount>& sceneFit) noexcept
        {
            const bool changed = !_valid || !COMPARE_TOLERANCE(_nearClipOffset, nearClipOffset, SHADOW_DISTANCE_TOLERANCE) ||
                                 _sceneFit != sceneFit;
            if (changed)
            {
                _nearClipOffset = nearClipOffset;
                _sceneFit = sceneFit;
                _valid = true;
            }
            return changed;
        }

      private:
        std::array<bool, SplitCount> _sceneFit{};
        F32 _nearClipOffset = 0.f;
        bool _valid = false;
    };
}
