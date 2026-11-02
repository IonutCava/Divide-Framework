/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#include "UnitTests/unitTestCommon.h"
#include "EngineConfig.h"
#include "Rendering/Lighting/Headers/LightUploadState.h"
#include "Rendering/Lighting/Headers/ShadowPassState.h"
#include "Rendering/Lighting/Headers/SpotLightGeometry.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace Divide
{
    TEST_CASE("Cluster light budgets always resolve to a valid limit", "[lighting][clusters]")
    {
        using namespace Config::Lighting::ClusteredForward;
        constexpr I32 maxLimit = static_cast<I32>( MAX_LIGHTS_PER_CLUSTER );

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
                    layers[i] = { owner, 0u, static_cast<unsigned>(contentCount) };
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
        constexpr std::array<std::uint32_t, 3> limits{ 2u, 4u, 6u };
        for (std::size_t type = 0u; type < 3u; ++type)
        {
            for (std::int32_t index = 0; index < static_cast<std::int32_t>(limits[type]); ++index)
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

        const auto first = runPass({ 2u, 3u, 0u, 1u });
        CHECK(lights[0]._slot == 0);
        CHECK(lights[1]._slot == 1);
        CHECK(lights[2]._slot == -1);
        CHECK(lights[3]._slot == -1);
        CHECK(first[0] == 101);
        CHECK(first[1] == 202);

        lights[1]._cached = true;
        const auto reordered = runPass({ 1u, 0u, 2u, 3u });
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
                const float theta = radians * static_cast<float>(sample) / 20.f;
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
