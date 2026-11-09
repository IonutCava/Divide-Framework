/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#include "UnitTests/unitTestCommon.h"
#include "EngineConfig.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/Headers/ClusteredLighting.h"
#include "Rendering/Lighting/Headers/LightUploadState.h"
#include "Rendering/Lighting/Headers/ShadowCasterBounds.h"
#include "Rendering/Lighting/Headers/ShadowPassState.h"
#include "Rendering/Lighting/Headers/SpotLightGeometry.h"
#include "Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h"
#include "Rendering/RenderPass/Headers/RenderPassCuller.h"
#include "Graphs/Headers/SceneNodeRenderState.h"
#include "../../../Assets/Shaders/GLSL/Common/shadowStrength.cmn"
#include "../../../Assets/Shaders/GLSL/Common/spotShadowProjection.cmn"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace Divide
{
    TEST_CASE("World AO reuses coverage until content or its retained footprint changes", "[lighting][shadows][worldAO]")
    {
        WorldAOCacheKey key
        {
            ._sceneGUID = 42,
            ._revision = 7u,
            ._center = {100.f, -200.f},
            ._height = {-10.f, 80.f},
            ._renderMask = 7u
        };
        WorldAOCacheState cache{};
        CHECK(cache.needsRefresh(key, false));
        cache._key = key;
        cache._valid = true;
        CHECK_FALSE(cache.needsRefresh(key, false));
        CHECK(cache.needsRefresh(key, true));

        SECTION("Small camera movements preserve the original footprint and matrix")
        {
            key._center[0] += WorldAOCacheState::RECENTER_DISTANCE - 1.f;
            key._center[1] -= WorldAOCacheState::RECENTER_DISTANCE - 1.f;
            CHECK_FALSE(cache.needsRefresh(key, false));
            CHECK(cache._key._center == std::array<F32, 2>{{100.f, -200.f}});
        }
        SECTION("Each horizontal boundary forces a new footprint")
        {
            for (size_t axis = 0u; axis < 2u; ++axis)
            {
                for (const F32 sign : {-1.f, 1.f})
                {
                    auto moved = key;
                    moved._center[axis] += sign * WorldAOCacheState::RECENTER_DISTANCE;
                    CHECK(cache.needsRefresh(moved, false));
                }
            }
        }
        SECTION("Scene changes cannot reuse another scene's shared layer")
        {
            ++key._sceneGUID;
            CHECK(cache.needsRefresh(key, false));
        }
        SECTION("Geometry and material revisions invalidate coverage")
        {
            ++key._revision;
            CHECK(cache.needsRefresh(key, false));
        }
        SECTION("Height and debug visibility changes invalidate coverage")
        {
            key._height[1] += 1.f;
            CHECK(cache.needsRefresh(key, false));
            key._height = cache._key._height;
            key._renderMask ^= 1u;
            CHECK(cache.needsRefresh(key, false));
        }
        SECTION("Target recreation invalidates an otherwise matching cache")
        {
            cache._valid = false;
            CHECK(cache.needsRefresh(key, false));
        }
    }

    TEST_CASE("Coverage passes exclude vegetation, skinned geometry and terrain independently", "[lighting][shadows][worldAO]")
    {
        constexpr U16 vegetation = to_base(CullOptions::CULL_VEGETATION_NODES);
        constexpr U16 skinned = to_base(CullOptions::CULL_SKINNED_NODES);
        constexpr U16 terrain = to_base(CullOptions::CULL_TERRAIN_NODES);
        for (const bool isVegetation : {false, true})
        {
            for (const bool isSkinned : {false, true})
            {
                for (const bool isTerrain : {false, true})
                {
                    CHECK_FALSE(RenderPassCuller::CullGeometryCategory(0u, isVegetation, isSkinned, isTerrain));
                    CHECK(RenderPassCuller::CullGeometryCategory(vegetation, isVegetation, isSkinned, isTerrain) == isVegetation);
                    CHECK(RenderPassCuller::CullGeometryCategory(skinned, isVegetation, isSkinned, isTerrain) == isSkinned);
                    CHECK(RenderPassCuller::CullGeometryCategory(terrain, isVegetation, isSkinned, isTerrain) == isTerrain);
                    CHECK(RenderPassCuller::CullGeometryCategory(vegetation | skinned | terrain, isVegetation, isSkinned, isTerrain) ==
                          (isVegetation || isSkinned || isTerrain));
                }
            }
        }
    }

    TEST_CASE("Generic draw masks resolve defaults and per-instance overrides without changing shared resources", "[rendering][worldAO]")
    {
        SceneNodeRenderState source{};
        SceneNodeRenderState first{};
        SceneNodeRenderState second{};
        const RenderStagePass coverage = ShadowMap::WorldAOStagePass();
        RenderStagePass mainPass = coverage;
        mainPass._passType = RenderPassType::MAIN_PASS;
        source.addToDrawExclusionMask(RenderStage::SHADOW);
        CHECK_FALSE(first.drawState(coverage, true, &source));
        CHECK_FALSE(first.drawState(mainPass, true, &source));
        first.setDrawStateOverride(coverage, true);
        CHECK(first.drawState(coverage, false, &source));
        CHECK(first.drawState(mainPass, false, &source));
        CHECK_FALSE(second.drawState(coverage, false, &source));
        CHECK_FALSE(source.drawState(coverage));
        CHECK(first.drawState({RenderStage::DISPLAY, RenderPassType::MAIN_PASS}, true, &source));
        CHECK(first.drawState({RenderStage::REFLECTION, RenderPassType::MAIN_PASS}, true, &source));
        CHECK_FALSE(first.drawState({RenderStage::SHADOW, RenderPassType::MAIN_PASS, 0u, coverage._variant}, true, &source));

        source.drawState(false);
        CHECK_FALSE(first.drawState(coverage, true, &source));
        source.drawState(true);
        first.setDrawStateOverride(coverage, false);
        CHECK_FALSE(first.drawState(coverage, true, &source));
        first.clearDrawStateOverride(coverage);
        CHECK_FALSE(first.drawStateOverride(coverage).has_value());
        CHECK_FALSE(first.drawState(coverage, true, &source));
    }

    TEST_CASE("Draw mask specificity takes precedence over broad rules and revisions track real edits", "[rendering][worldAO]")
    {
        SceneNodeRenderState state{};
        const RenderStagePass coverage = ShadowMap::WorldAOStagePass();
        const RenderStagePass shadows{RenderStage::SHADOW, RenderPassType::COUNT, g_AllIndicesID,
                                     RenderStagePass::VariantType::COUNT, RenderStagePass::PassIndex::COUNT};
        state.setDrawStateOverride(coverage, true);
        state.setDrawStateOverride(shadows, false);
        CHECK(state.drawState(coverage, false));
        const U64 revision = state.coverageRevision();
        state.setDrawStateOverride(coverage, true);
        CHECK(state.coverageRevision() == revision);
        state.setDrawStateOverride(coverage, false);
        CHECK(state.coverageRevision() == revision + 1u);
        CHECK_FALSE(state.drawState(coverage, true));
        state.setDrawStateOverride(coverage, true);
        CHECK(state.drawState(coverage, false));
        state.clearDrawStateOverride(coverage);
        CHECK_FALSE(state.drawState(coverage, true));
        const U64 afterClear = state.coverageRevision();
        state.clearDrawStateOverride(coverage);
        CHECK(state.coverageRevision() == afterClear);
        state.clearDrawStateOverride(shadows);
        CHECK(state.drawState(coverage, true));
        CHECK_FALSE(state.drawState(coverage, false));
    }

    TEST_CASE("World AO category exclusions are defaults rather than irreversible bans", "[rendering][worldAO]")
    {
        const U16 flags = ShadowMap::WorldAOCullFlags();
        const RenderStagePass coverage = ShadowMap::WorldAOStagePass();
        for (U8 categories = 0u; categories < 16u; ++categories)
        {
            SceneNodeRenderState state{};
            const bool defaultState = !RenderPassCuller::CullGeometryCategory(flags,
                (categories & 1u) != 0u, (categories & 2u) != 0u, (categories & 4u) != 0u, (categories & 8u) != 0u);
            CHECK(defaultState == (categories == 0u));
            CHECK(state.drawState(coverage, defaultState) == defaultState);
            state.setDrawStateOverride(coverage, true);
            CHECK(state.drawState(coverage, defaultState));
            state.setDrawStateOverride(coverage, false);
            CHECK_FALSE(state.drawState(coverage, defaultState));
            state.clearDrawStateOverride(coverage);
            CHECK(state.drawState(coverage, defaultState) == defaultState);
        }
    }

    TEST_CASE("Node coverage revisions track visibility and new pass exclusions", "[lighting][shadows][worldAO]")
    {
        SceneNodeRenderState state{};
        const U64 initial = state.coverageRevision();
        state.drawState(true);
        CHECK(state.coverageRevision() == initial);
        state.drawState(false);
        CHECK(state.coverageRevision() == initial + 1u);
        state.drawState(false);
        CHECK(state.coverageRevision() == initial + 1u);
        state.drawState(true);
        CHECK(state.coverageRevision() == initial + 2u);
        state.addToDrawExclusionMask(RenderStage::SHADOW);
        CHECK(state.coverageRevision() == initial + 3u);
        state.addToDrawExclusionMask(RenderStage::SHADOW);
        CHECK(state.coverageRevision() == initial + 3u);
    }

    TEST_CASE("Imported foliage can exclude world AO without changing directional shadows", "[lighting][shadows][worldAO]")
    {
        SceneNodeRenderState state{};
        const auto variant = static_cast<RenderStagePass::VariantType>(ShadowType::CSM);
        state.addToDrawExclusionMask(RenderStage::SHADOW, RenderPassType::COUNT, variant, ShadowMap::WORLD_AO_LAYER_INDEX);
        CHECK_FALSE(state.drawState({RenderStage::SHADOW, RenderPassType::COUNT, ShadowMap::WORLD_AO_LAYER_INDEX, variant}));
        CHECK_FALSE(state.drawState({RenderStage::SHADOW, RenderPassType::MAIN_PASS, ShadowMap::WORLD_AO_LAYER_INDEX, variant}));
        CHECK(state.drawState({RenderStage::SHADOW, RenderPassType::MAIN_PASS, 0u, variant}));
        CHECK(state.drawState({RenderStage::DISPLAY, RenderPassType::MAIN_PASS}));
        CHECK(state.drawState({RenderStage::REFLECTION, RenderPassType::MAIN_PASS}));
    }

    TEST_CASE( "Shadow matrices apply the XY bias after projection", "[lighting][shadows]" )
    {
        const mat4<F32> view = Camera::LookAt( float3{ 4.f, 3.f, 6.f }, float3{ 0.f, 0.f, 0.f }, float3{ 0.f, 1.f, 0.f } );
        const mat4<F32> projection = Camera::Perspective( 90.f, 1.f, 0.1f, 100.f );
        const mat4<F32> viewProjection = mat4<F32>::Multiply( projection, view );
        const mat4<F32> biased = ShadowMap::BiasedViewProjection( viewProjection );
        const mat4<F32> wrongOrder = mat4<F32>::Multiply( viewProjection, MAT4_BIAS_ZERO_ONE_Z );

        for ( const float4& world : std::array<float4, 3>{ float4{ 0.f, 0.f, 0.f, 1.f },
                                                          float4{ 1.f, 0.f, 0.f, 1.f },
                                                          float4{ 0.f, 1.f, 1.f, 1.f } } )
        {
            const float4 clip = viewProjection * world;
            const float4 result = biased * world;
            REQUIRE( clip.w > 0.f );
            CHECK( result.x == Catch::Approx( 0.5f * ( clip.x + clip.w ) ).margin( 0.0001f ) );
            CHECK( result.y == Catch::Approx( 0.5f * ( clip.y + clip.w ) ).margin( 0.0001f ) );
            CHECK( result.z == Catch::Approx( clip.z ).margin( 0.0001f ) );
            CHECK( result.w == Catch::Approx( clip.w ).margin( 0.0001f ) );
        }

        const float4 reference{ 0.f, 0.f, 0.f, 1.f };
        CHECK( ( wrongOrder * reference ).x != Catch::Approx( ( biased * reference ).x ) );
    }

    TEST_CASE("Shadow strength attenuates occlusion without dimming lit pixels", "[lighting][shadows]")
    {
        const auto visibility = [](const F32 shadow, const F32 strength)
        {
            return std::clamp(SHADOW_VISIBILITY_WITH_STRENGTH(shadow, strength), 0.f, 1.f);
        };

        for (const F32 strength : { 0.f, 0.25f, 1.f, 2.f, 10.f })
        {
            CHECK(visibility(1.f, strength) == 1.f);
        }
        for (const F32 shadow : { 0.f, 0.25f, 0.5f, 1.f })
        {
            CHECK(visibility(shadow, 0.f) == 1.f);
            CHECK(visibility(shadow, 1.f) == Catch::Approx(shadow));
        }
        CHECK(visibility(0.f, 0.25f) == Catch::Approx(0.75f));
        CHECK(visibility(0.5f, 0.5f) == Catch::Approx(0.75f));
        CHECK(visibility(0.75f, 2.f) == Catch::Approx(0.5f));
        CHECK(visibility(0.f, 10.f) == 0.f);
    }

    TEST_CASE("Maximum light bleed bias has a finite limiting visibility", "[lighting][shadows]")
    {
        const auto reduce = [](const F32 bound, const F32 bias)
        {
            using std::clamp;
            return SHADOW_REDUCE_LIGHT_BLEED(bound, bias);
        };
        const F32 almostOne = std::nextafter(1.f, 0.f);
        for (const F32 bound : { 0.f, 0.25f, 0.5f, 0.75f, almostOne, 1.f })
        {
            for (const F32 bias : { 0.f, 0.02f, 0.5f, almostOne, 1.f })
            {
                const F32 visibility = reduce(bound, bias);
                CHECK(std::isfinite(visibility));
                CHECK(visibility >= 0.f);
                CHECK(visibility <= 1.f);
                if (bias < 1.f)
                {
                    CHECK(visibility == Catch::Approx(std::clamp((bound - bias) / (1.f - bias), 0.f, 1.f)));
                }
            }
            CHECK(reduce(bound, 0.f) == Catch::Approx(bound));
            CHECK(reduce(bound, 1.f) == (bound == 1.f ? 1.f : 0.f));
        }
        CHECK(reduce(0.5f, 0.2f) == Catch::Approx(0.375f));
        CHECK(reduce(1.f, almostOne) == 1.f);
        CHECK(reduce(almostOne, almostOne) == 0.f);
    }

    TEST_CASE("Cascade blend ranges match their overlapping shadow frusta", "[lighting][shadows][csm]")
    {
        constexpr F32 cameraNear = 0.1f;
        constexpr F32 cameraFar = 1000.f;
        constexpr F32 clipRange = cameraFar - cameraNear;
        constexpr std::array<F32, 4u> splitEnds{ { 20.f, 70.f, 200.f, 600.f } };
        const auto blendWeight = [](const F32 depth, const F32 start, const F32 end)
        {
            using std::clamp;
            const F32 t = CSM_BLEND_LINEAR_WEIGHT(depth, start, end);
            return CSM_BLEND_SMOOTH_WEIGHT(t);
        };

        for (size_t layer = 1u; layer < splitEnds.size(); ++layer)
        {
            const F32 previousStart = layer > 1u ? splitEnds[layer - 2u] : 0.f;
            const F32 previousEnd = splitEnds[layer - 1u];
            const F32 blendStart = CSM_BLEND_START(previousStart, previousEnd);
            const F32 nearFraction = CSMOverlapNearFraction(cameraNear, clipRange, previousStart, previousEnd);
            const F32 actualNear = cameraNear + nearFraction * clipRange;
            CHECK(actualNear == Catch::Approx(blendStart).margin(0.0001f));

            CHECK(blendWeight(blendStart - 1.f, blendStart, previousEnd) == 0.f);
            CHECK(blendWeight(blendStart, blendStart, previousEnd) == 0.f);
            CHECK(blendWeight((blendStart + previousEnd) * 0.5f, blendStart, previousEnd) == Catch::Approx(0.5f));
            CHECK(blendWeight(previousEnd, blendStart, previousEnd) == 1.f);
            CHECK(blendWeight(previousEnd + 1.f, blendStart, previousEnd) == 1.f);
        }

        CHECK(CSMOverlapNearFraction(cameraNear, clipRange, 0.f, 0.05f) == 0.f);
        CHECK(blendWeight(1.f, 1.f, 1.f) == 0.f);
    }

    TEST_CASE("Directional cascade variance and bias retain their world-space scale", "[lighting][shadows][csm]")
    {
        constexpr F32 referenceGradient = 0.01f;
        constexpr F32 referenceVariance = 0.00001f;
        constexpr F32 referenceBias = 0.00001f;
        constexpr F32 worldSeparation = 0.5f;
        const F32 referenceDelta = worldSeparation * referenceGradient - referenceBias;
        const F32 expectedBound = referenceVariance / (referenceVariance + referenceDelta * referenceDelta);

        for (const F32 gradient : { 0.02f, 0.01f, 0.002f, 0.00025f })
        {
            const F32 scaleSq = CSM_DEPTH_SCALE_SQUARED(gradient * gradient, referenceGradient * referenceGradient);
            const F32 variance = CSM_MIN_VARIANCE_FOR_SCALE(referenceVariance, scaleSq);
            const F32 bias = CSM_DEPTH_BIAS_FOR_SCALE(referenceBias, std::sqrt(scaleSq));
            const F32 delta = worldSeparation * gradient - bias;
            const F32 bound = variance / (variance + delta * delta);
            CHECK(variance / (gradient * gradient) == Catch::Approx(referenceVariance / (referenceGradient * referenceGradient)));
            CHECK(bias / gradient == Catch::Approx(referenceBias / referenceGradient));
            CHECK(bound == Catch::Approx(expectedBound));
        }

        const F32 distantDelta = worldSeparation * 0.00025f;
        const F32 unscaledBound = referenceVariance / (referenceVariance + distantDelta * distantDelta);
        CHECK(unscaledBound > expectedBound + 0.5f);

        const mat4<F32> lightView = Camera::LookAt(float3{ 4.f, 8.f, 3.f }, float3{ 0.f }, float3{ 0.f, 1.f, 0.f });
        const auto depthGradientSquared = [&](const F32 farPlane)
        {
            const mat4<F32> projection = Camera::Ortho(-10.f, 10.f, -10.f, 10.f, 0.1f, farPlane);
            const mat4<F32> matrix = ShadowMap::BiasedViewProjection(mat4<F32>::Multiply(projection, lightView));
            const float3 gradient{ matrix.m[0][2], matrix.m[1][2], matrix.m[2][2] };
            return gradient.lengthSquared();
        };
        const F32 referenceMatrixGradientSq = depthGradientSquared(100.f);
        for (const F32 farPlane : { 50.f, 100.f, 500.f, 1000.f })
        {
            const F32 scaleSq = CSM_DEPTH_SCALE_SQUARED(depthGradientSquared(farPlane), referenceMatrixGradientSq);
            const F32 rangeRatio = 99.9f / (farPlane - 0.1f);
            CHECK(scaleSq == Catch::Approx(rangeRatio * rangeRatio).epsilon(0.0001f));
        }
    }

    TEST_CASE("Spot shadow projection rejects fragments outside the biased clip volume", "[lighting][shadows]")
    {
        const auto inside = [](const F32 x, const F32 y, const F32 z, const F32 w)
        {
            return SPOT_SHADOW_INSIDE_FRUSTUM(x, y, z, w);
        };
        constexpr F32 epsilon = 0.001f;
        constexpr F32 nan = std::numeric_limits<F32>::quiet_NaN();

        CHECK(inside(0.f, 0.f, 0.f, 1.f));
        CHECK(inside(1.f, 1.f, 1.f, 1.f));
        CHECK(inside(1.f, 0.5f, 1.5f, 2.f));
        CHECK_FALSE(inside(-epsilon, 0.5f, 0.5f, 1.f));
        CHECK_FALSE(inside(1.f + epsilon, 0.5f, 0.5f, 1.f));
        CHECK_FALSE(inside(0.5f, -epsilon, 0.5f, 1.f));
        CHECK_FALSE(inside(0.5f, 1.f + epsilon, 0.5f, 1.f));
        CHECK_FALSE(inside(0.5f, 0.5f, -epsilon, 1.f));
        CHECK_FALSE(inside(0.5f, 0.5f, 1.f + epsilon, 1.f));
        CHECK_FALSE(inside(0.f, 0.f, 0.f, 0.f));
        CHECK_FALSE(inside(0.f, 0.f, 0.f, -1.f));
        CHECK_FALSE(inside(nan, 0.5f, 0.5f, 1.f));
        CHECK_FALSE(inside(0.5f, 0.5f, 0.5f, nan));
    }

    TEST_CASE("Cluster light budgets always resolve to a valid limit", "[lighting][clusters]")
    {
        using namespace ClusteredLighting;
        using namespace Config::Lighting::ClusteredForward;
        constexpr I32 maxLimit = to_I32( MAX_LIGHTS_PER_CLUSTER );

        STATIC_CHECK( ResolveLightsPerCluster( -1 ) == maxLimit );
        STATIC_CHECK( ResolveLightsPerCluster( 0 ) == 1 );
        STATIC_CHECK( ResolveLightsPerCluster( 1 ) == 1 );
        STATIC_CHECK( ResolveLightsPerCluster( maxLimit ) == maxLimit );
        CHECK( ResolveLightsPerCluster( std::numeric_limits<I32>::min() ) == maxLimit );
        CHECK( ResolveLightsPerCluster( -2 ) == maxLimit );
        if constexpr ( maxLimit > 1 )
        {
            CHECK( ResolveLightsPerCluster( maxLimit / 2 ) == maxLimit / 2 );
        }
        for ( I32 limit = 1; limit <= maxLimit; ++limit )
        {
            CHECK( ResolveLightsPerCluster( limit ) == limit );
        }
        CHECK( ResolveLightsPerCluster( maxLimit + 1 ) == maxLimit );
        CHECK( ResolveLightsPerCluster( std::numeric_limits<I32>::max() ) == maxLimit );
    }

    TEST_CASE("Clustered lighting only accepts positive, ordered, finite depth planes", "[lighting][clusters]")
    {
        using namespace ClusteredLighting;
        using namespace Config::Lighting::ClusteredForward;
        constexpr F32 inf = std::numeric_limits<F32>::infinity();
        constexpr F32 nan = std::numeric_limits<F32>::quiet_NaN();

        STATIC_CHECK( IsValidClusterDepthRange( 0.1f, 1000.f ) );
        STATIC_CHECK( IsValidClusterDepthRange( std::numeric_limits<F32>::min(), F32_MAX ) );

        STATIC_CHECK_FALSE( IsValidClusterDepthRange( 0.f, 1000.f ) );
        STATIC_CHECK_FALSE( IsValidClusterDepthRange( -0.f, 1000.f ) );
        STATIC_CHECK_FALSE( IsValidClusterDepthRange( -100.f, 100.f ) );
        STATIC_CHECK_FALSE( IsValidClusterDepthRange( -10.f, -1.f ) );
        STATIC_CHECK_FALSE( IsValidClusterDepthRange( 10.f, 10.f ) );
        STATIC_CHECK_FALSE( IsValidClusterDepthRange( 10.f, 1.f ) );
        CHECK_FALSE( IsValidClusterDepthRange( 0.1f, inf ) );
        CHECK_FALSE( IsValidClusterDepthRange( nan, 1000.f ) );
        CHECK_FALSE( IsValidClusterDepthRange( 0.1f, nan ) );
        CHECK_FALSE( IsValidClusterDepthRange( -inf, 1000.f ) );

        // Every accepted range produces finite, monotonically increasing exponential slice boundaries.
        for ( const auto [zNear, zFar] : std::array<std::pair<F32, F32>, 4>{ { { 0.01f, 10.f }, { 0.1f, 1000.f }, { 1.f, 1.0001f }, { 0.5f, 50000.f } } } )
        {
            REQUIRE( IsValidClusterDepthRange( zNear, zFar ) );
            F32 previous = zNear;
            for ( U8 slice = 1u; slice <= CLUSTERS_Z; ++slice )
            {
                const F32 boundary = zNear * std::pow( zFar / zNear, to_F32( slice ) / CLUSTERS_Z );
                CHECK( std::isfinite( boundary ) );
                CHECK( boundary >= previous );
                previous = boundary;
            }
            CHECK( previous == Catch::Approx( zFar ).epsilon( 1e-4 ) );
        }
    }

    TEST_CASE("Light uploads never reserve past the configured capacity", "[lighting][lights]")
    {
        std::uint32_t count = 4095u;
        CHECK(TryReserveLightUploadSlot(count, std::uint16_t{ 4096u }));
        CHECK(count == 4096u);
        CHECK_FALSE(TryReserveLightUploadSlot(count, std::uint16_t{ 4096u }));
        CHECK(count == 4096u);

        count = 0u;
        CHECK_FALSE(TryReserveLightUploadSlot(count, std::uint16_t{ 0u }));
        CHECK(count == 0u);
        CHECK_FALSE(ShouldUploadLightData(count));
        CHECK(ShouldUploadLightData(std::uint32_t{ 1u }));
    }

    TEST_CASE("Shadow movement batches retain late changes and cover skipped passes", "[lighting][shadows]")
    {
        ShadowMovementBatch batch;
        std::vector<int> pending{ 1, 2 };
        const auto finishFrame = [&]()
        {
            const size_t consumed = batch.finish(pending.size());
            pending.erase(pending.begin(), pending.begin() + consumed);
        };

        const auto first = batch.begin(pending.size());
        CHECK(first._count == 2u);
        CHECK_FALSE(first._invalidateAll);
        pending.push_back(3);

        const auto secondPass = batch.begin(pending.size());
        CHECK(secondPass._count == first._count);
        CHECK_FALSE(secondPass._invalidateAll);
        finishFrame();
        REQUIRE(pending == std::vector<int>{ 3 });

        const auto nextFrame = batch.begin(pending.size());
        CHECK(nextFrame._count == 1u);
        CHECK_FALSE(nextFrame._invalidateAll);
        finishFrame();
        CHECK(pending.empty());

        pending.push_back(4);
        finishFrame();
        CHECK(pending.empty());
        finishFrame();
        pending.push_back(5);
        const auto afterSkippedPass = batch.begin(pending.size());
        CHECK(afterSkippedPass._count == 1u);
        CHECK(afterSkippedPass._invalidateAll);
        pending.push_back(6);
        finishFrame();
        REQUIRE(pending == std::vector<int>{ 6 });

        const auto retained = batch.begin(pending.size());
        CHECK(retained._count == 1u);
        CHECK_FALSE(retained._invalidateAll);
        finishFrame();
        CHECK(pending.empty());
    }

    TEST_CASE("Directional shadow invalidation is not limited by a local-light sphere", "[lighting][shadows]")
    {
        const BoundingSphere light{ float3{ 0.f, 0.f, 0.f }, 10.f };
        const BoundingSphere nearby{ float3{ 5.f, 0.f, 0.f }, 1.f };
        const BoundingSphere distant{ float3{ 100.f, 0.f, 0.f }, 1.f };

        CHECK(ShadowVolumeAffectsLight(true, nearby, light));
        CHECK(ShadowVolumeAffectsLight(true, distant, light));
        CHECK(ShadowVolumeAffectsLight(false, nearby, light));
        CHECK_FALSE(ShadowVolumeAffectsLight(false, distant, light));
    }

    TEST_CASE("Shadow movement records old and new volumes together", "[lighting][shadows]")
    {
        struct MovingVolume
        {
            BoundingSphere _volume;
            bool _staticSource;
        };
        const BoundingSphere oldVolume{ float3{ 0.f, 0.f, 0.f }, 1.f };
        const BoundingSphere newVolume{ float3{ 100.f, 0.f, 0.f }, 1.f };
        const BoundingSphere oldLight{ float3{ 0.f, 0.f, 0.f }, 5.f };
        const BoundingSphere newLight{ float3{ 100.f, 0.f, 0.f }, 5.f };
        std::vector<MovingVolume> pending;
        ShadowMovementBatch batch;

        const auto active = batch.begin(pending.size());
        AppendShadowVolumeChange(pending, oldVolume, true, newVolume, false);
        CHECK(active._count == 0u);
        CHECK(batch.finish(pending.size()) == 0u);
        REQUIRE(pending.size() == 2u);
        CHECK(pending[0]._staticSource);
        CHECK_FALSE(pending[1]._staticSource);
        CHECK(ShadowVolumeAffectsLight(false, pending[0]._volume, oldLight));
        CHECK_FALSE(ShadowVolumeAffectsLight(false, pending[1]._volume, oldLight));
        CHECK_FALSE(ShadowVolumeAffectsLight(false, pending[0]._volume, newLight));
        CHECK(ShadowVolumeAffectsLight(false, pending[1]._volume, newLight));
        CHECK(batch.begin(pending.size())._count == 2u);
    }

    TEST_CASE("Cascade feedback stores independent caster bounds", "[lighting][shadows]")
    {
        ShadowCasterBounds feedback[3]{};
        BoundingBox first{ 10.f, 20.f, 30.f, 12.f, 22.f, 32.f };
        const BoundingBox second{ 14.f, 24.f, 34.f, 16.f, 26.f, 36.f };
        ShadowCasterBounds* selected = SelectCascadeBoundsFeedback(feedback[2], 2u, true);
        REQUIRE(selected == &feedback[2]);
        CHECK_FALSE(selected->_valid);
        selected->add(first);
        selected->add(second);
        first.set(100.f, 100.f, 100.f, 200.f, 200.f, 200.f);
        CHECK(selected->_valid);
        CHECK(selected->_bounds._min.x == Catch::Approx(10.f));
        CHECK(selected->_bounds._min.y == Catch::Approx(20.f));
        CHECK(selected->_bounds._max.z == Catch::Approx(36.f));

        feedback[1].add(second);
        selected = SelectCascadeBoundsFeedback(feedback[1], 1u, false);
        CHECK(selected == nullptr);
        CHECK_FALSE(feedback[1]._valid);
        selected = SelectCascadeBoundsFeedback(feedback[0], 0u, true);
        CHECK(selected == nullptr);
        CHECK(feedback[2]._valid);
        selected = SelectCascadeBoundsFeedback(feedback[2], 2u, true);
        REQUIRE(selected == &feedback[2]);
        CHECK_FALSE(selected->_valid);
    }

    TEST_CASE("Shadow scratch blits preserve cascade and world AO destinations", "[lighting][shadows]")
    {
        constexpr U16 splits = Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT;
        constexpr U16 lightCount = Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS;
        for (U16 light = 0u; light < lightCount; ++light)
        {
            for (U16 cascade = 0u; cascade < splits; ++cascade)
            {
                const U16 destination = to_U16(light * splits + cascade);
                const RTBlitEntry blit = ShadowMap::ScratchBlit(destination);
                CHECK(blit._input._layerOffset == 0u);
                CHECK(blit._input._mipOffset == 0u);
                CHECK(blit._input._index == 0u);
                CHECK(blit._output._layerOffset == destination);
                CHECK(blit._output._mipOffset == 0u);
                CHECK(blit._output._index == 0u);
                CHECK(blit._layerCount == 1u);
                CHECK(blit._mipCount == 1u);
                CHECK(destination < ShadowMap::WORLD_AO_LAYER_INDEX);
            }
        }

        const RTBlitEntry worldAO = ShadowMap::ScratchBlit(ShadowMap::WORLD_AO_LAYER_INDEX);
        CHECK(worldAO._input._layerOffset == 0u);
        CHECK(worldAO._output._layerOffset == ShadowMap::WORLD_AO_LAYER_INDEX);
        CHECK(worldAO._layerCount == 1u);
    }

    TEST_CASE("Shadow layers retain their owner and can reclaim unused ranges", "[lighting][shadows]")
    {
        struct Layer
        {
            std::int64_t _lightGUID = -1;
            unsigned _lifetime = 32u;
            unsigned _contentLayers = 0u;
        };
        std::array<Layer, 6> layers{};
        layers[0] = { 10, 4u, 1u };
        layers[1] = { 20, 3u, 1u };
        layers[2] = { 10, 2u, 1u };
        layers[3] = { 30, 0u, 1u };

        CHECK_FALSE(TryReuseShadowLayers(layers, 10, 0u, 2u, 1u));
        CHECK(layers[0]._lifetime == 4u);
        CHECK_FALSE(TryReuseShadowLayers(layers, 10, 0u, 1u, 2u));
        CHECK(layers[0]._lifetime == 4u);
        CHECK(TryReuseShadowLayers(layers, 10, 0u, 1u, 1u));
        CHECK(layers[0]._lifetime == 0u);
        CHECK_FALSE(TryReuseShadowLayers(layers, 10, 5u, 2u, 1u));
        CHECK_FALSE(TryReuseShadowLayers(layers, 10, 0u, 0u, 1u));

        ReleaseShadowLayers(layers, 10, 32u);
        CHECK(layers[0]._lightGUID == -1);
        CHECK(layers[2]._lightGUID == -1);
        CHECK(layers[1]._lightGUID == 20);
        CHECK(layers[3]._lightGUID == 30);
        CHECK(FindAvailableShadowLayers(layers, 2u, 32u) == 4u);
        CHECK(FindAvailableShadowLayers(layers, 3u, 32u) == layers.size());
        CHECK(FindAvailableShadowLayers(layers, 3u, 1u) == 0u);
        CHECK(FindAvailableShadowLayers(layers, 7u, 1u) == layers.size());
    }

    TEST_CASE("Directional shadows reserve full blocks without fragmentation", "[lighting][shadows]")
    {
        struct Layer
        {
            std::int64_t _lightGUID = -1;
            unsigned _lifetime = 32u;
            unsigned _contentLayers = 0u;
        };
        std::array<Layer, 8> layers{};
        const auto allocate = [&layers](const std::int64_t owner, const std::size_t contentCount)
        {
            const std::size_t count = ShadowReservationCount(contentCount, 4u, true);
            const std::size_t offset = FindAvailableShadowLayers(layers, count, 32u);
            if (offset != layers.size())
            {
                for (std::size_t i = offset; i < offset + count; ++i)
                {
                    layers[i] = { owner, 0u, to_U32(contentCount) };
                }
            }
            return offset;
        };
        CHECK(ShadowReservationCount(0u, 4u, true) == 0u);
        CHECK(ShadowReservationCount(5u, 4u, true) == 0u);
        CHECK(ShadowReservationCount(1u, 4u, false) == 1u);
        CHECK(allocate(10, 1u) == 0u);
        CHECK(allocate(20, 4u) == 4u);
        ReleaseShadowLayers(layers, 10, 32u);
        CHECK(allocate(30, 4u) == 0u);
        CHECK(TryReuseShadowLayers(layers, 20, 4u, 4u, 4u));
        CHECK_FALSE(TryReuseShadowLayers(layers, 20, 4u, 4u, 3u));
    }

    TEST_CASE("Shadow slots cover the budget and repack cached lights safely", "[lighting][shadows]")
    {
        ShadowPassSlots<3> full(12u);
        constexpr std::array<std::uint32_t, 3> limits{{ 2u, 4u, 6u }};
        for (std::size_t type = 0u; type < 3u; ++type)
        {
            for (std::int32_t index = 0; index < to_I32(limits[type]); ++index)
            {
                REQUIRE(full.hasCapacity(type, limits[type]));
                CHECK(full.reserve(type, limits[type]) == index);
            }
            CHECK_FALSE(full.hasCapacity(type, limits[type]));
        }
        CHECK_FALSE(full.hasCapacity(0u, limits[0]));
        CHECK(full.reserve(0u, limits[0]) == -1);

        struct Candidate
        {
            bool _eligible;
            bool _cached;
            bool _generated;
            int _properties;
            std::int32_t _slot = -1;
        };
        std::array<Candidate, 4> lights{{
            { true, true, false, 101 },
            { true, false, true, 202 },
            { false, false, true, 303 },
            { true, false, false, 404 }
        }};
        auto runPass = [&lights](const std::array<std::size_t, 4>& order)
        {
            ShadowPassSlots<1> slots(2u);
            std::array<int, 2> properties{};
            for (Candidate& light : lights)
            {
                light._slot = -1;
            }
            for (const std::size_t index : order)
            {
                Candidate& light = lights[index];
                if (!light._eligible || !slots.hasCapacity(0u, 2u) || (!light._cached && !light._generated))
                {
                    continue;
                }
                light._slot = slots.reserve(0u, 2u);
                properties[light._slot] = light._properties;
            }
            return properties;
        };

        const auto first = runPass({{ 2u, 3u, 0u, 1u }});
        CHECK(lights[0]._slot == 0);
        CHECK(lights[1]._slot == 1);
        CHECK(lights[2]._slot == -1);
        CHECK(lights[3]._slot == -1);
        CHECK(first[0] == 101);
        CHECK(first[1] == 202);

        lights[1]._cached = true;
        const auto reordered = runPass({{ 1u, 0u, 2u, 3u }});
        CHECK(lights[1]._slot == 0);
        CHECK(lights[0]._slot == 1);
        CHECK(reordered[0] == 202);
        CHECK(reordered[1] == 101);
    }

    TEST_CASE("Directional shadow camera changes include view projection and clip planes", "[lighting][shadows]")
    {
        using Matrix = std::array<float, 16>;
        ShadowCameraState<Matrix> state;
        Matrix view{};
        Matrix projection{};
        CHECK(state.update(view, projection, 0.1f, 100.f));
        CHECK_FALSE(state.update(view, projection, 0.1f, 100.f));

        view[12] = 1.f;
        CHECK(state.update(view, projection, 0.1f, 100.f));
        view[0] = 0.5f;
        CHECK(state.update(view, projection, 0.1f, 100.f));
        projection[5] = 2.f;
        CHECK(state.update(view, projection, 0.1f, 100.f));
        CHECK(state.update(view, projection, 0.2f, 100.f));
        CHECK(state.update(view, projection, 0.2f, 200.f));
        CHECK_FALSE(state.update(view, projection, 0.2f, 200.f));
    }

    TEST_CASE("Directional shadow camera comparisons short circuit by cost", "[lighting][shadows]")
    {
        struct CountingMatrix
        {
            int _value = 0;
            unsigned* _comparisons = nullptr;

            bool operator!=(const CountingMatrix& other) const noexcept
            {
                ++*_comparisons;
                return _value != other._value;
            }
        };
        unsigned viewComparisons = 0u;
        unsigned projectionComparisons = 0u;
        CountingMatrix view{ 0, &viewComparisons };
        CountingMatrix projection{ 0, &projectionComparisons };
        ShadowCameraState<CountingMatrix> state;

        CHECK(state.update(view, projection, 0.1f, 100.f));
        CHECK(state.update(view, projection, 0.2f, 100.f));
        CHECK(state.update(view, projection, 0.2f, 200.f));
        CHECK(viewComparisons == 0u);
        CHECK(projectionComparisons == 0u);
        view._value = 1;
        CHECK(state.update(view, projection, 0.2f, 200.f));
        CHECK(viewComparisons == 1u);
        CHECK(projectionComparisons == 0u);
        projection._value = 1;
        CHECK(state.update(view, projection, 0.2f, 200.f));
        CHECK(viewComparisons == 2u);
        CHECK(projectionComparisons == 1u);
    }

    TEST_CASE("Shadow range changes invalidate cached maps", "[lighting][shadows]")
    {
        ShadowRangeState state;
        CHECK(state.update(10.f));
        CHECK_FALSE(state.update(10.f));
        CHECK(state.update(20.f));
        CHECK_FALSE(state.update(20.f));
        CHECK(state.update(10.f));
    }

    TEST_CASE("Directional shadow settings invalidate cached maps", "[lighting][shadows]")
    {
        ShadowDirectionalSettingsState<4u> state;
        std::array<bool, 4> sceneFit{ true, true, true, true };
        CHECK(state.update(0.f, sceneFit));
        CHECK_FALSE(state.update(0.f, sceneFit));
        CHECK(state.update(5.f, sceneFit));
        CHECK_FALSE(state.update(5.f, sceneFit));
        sceneFit[2] = false;
        CHECK(state.update(5.f, sceneFit));
        CHECK_FALSE(state.update(5.f, sceneFit));
        sceneFit[2] = true;
        CHECK(state.update(5.f, sceneFit));
    }

    TEST_CASE("Spotlight angles remain ordered through edits and loading", "[lighting][spotlights]")
    {
        SpotConeAngles angles;
        CHECK(angles._inner == 15.f);
        CHECK(angles._outer == 35.f);

        angles.setInnerClamped(60.f);
        CHECK(angles._inner == 35.f);
        CHECK(angles._outer == 35.f);
        angles.setInnerClamped(15.f);
        angles.setOuterClamped(10.f);
        CHECK(angles._inner == 15.f);
        CHECK(angles._outer == 15.f);
        angles.setOuterClamped(35.f);

        angles.setInner(60.f);
        CHECK(angles._inner == 60.f);
        CHECK(angles._outer == 60.f);
        angles.setOuter(20.f);
        CHECK(angles._inner == 20.f);
        CHECK(angles._outer == 20.f);
        angles._inner = 35.f;
        angles._outer = 15.f;
        angles.normalize();
        CHECK(angles._inner == 15.f);
        CHECK(angles._outer == 35.f);
        angles.setOuter(180.f);
        angles.setInner(180.f);
        CHECK(angles._inner == 180.f);
        CHECK(angles._outer == 180.f);
        angles.setOuter(-5.f);
        CHECK(angles._inner == 0.f);
        CHECK(angles._outer == 0.f);
    }

    TEST_CASE("Spotlight bounds enclose the GPU attenuation sector", "[lighting][spotlights]")
    {
        constexpr float pi = 3.14159265358979323846f;
        constexpr float slant = 2.f;
        for (const float angle : { 0.f, 30.f, 45.f, 60.f, 89.f, 90.f, 120.f, 180.f })
        {
            const float radians = angle * pi / 180.f;
            const SpotBoundingSphere sphere = SpotConeBoundingSphere(slant, std::cos(radians));
            CHECK(std::isfinite(sphere._offset));
            CHECK(std::isfinite(sphere._radius));
            CHECK(sphere._radius >= 0.f);
            for (unsigned sample = 0u; sample <= 20u; ++sample)
            {
                const float theta = radians * to_F32(sample) / 20.f;
                for (const float distance : { 0.f, 1.f, slant })
                {
                    const float x = distance * std::sin(theta);
                    const float z = distance * std::cos(theta) - sphere._offset;
                    CHECK(std::sqrt(x * x + z * z) <= sphere._radius + 1e-5f * slant);
                }
            }
        }

        const SpotBoundingSphere wide = SpotConeBoundingSphere(2.f, std::cos(60.f * pi / 180.f));
        const float theta = 59.55f * pi / 180.f;
        const float x = 1.85f * std::sin(theta);
        const float z = 1.85f * std::cos(theta) - wide._offset;
        CHECK(std::sqrt(x * x + z * z) <= wide._radius);
    }
}
