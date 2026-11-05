// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"
#include "ECS/Components/Headers/BoundsUpdateState.h"
#include "ECS/Systems/Headers/TransformHierarchy.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"

namespace Divide
{
    TEST_CASE( "Shadow bounds detect even small sphere changes", "[bounds][lighting][shadows]" )
    {
        const BoundingSphere previous{ float3{ 10.f, 20.f, 30.f }, 4.f };
        BoundingSphere current = previous;
        CHECK_FALSE(BoundingSphereChanged(previous, current));
        current._sphere.x = std::nextafter(previous._sphere.x, F32_MAX);
        CHECK(BoundingSphereChanged(previous, current));
        current = previous;
        current._sphere.y += 1.f;
        CHECK(BoundingSphereChanged(previous, current));
        current = previous;
        current._sphere.z += 1.f;
        CHECK(BoundingSphereChanged(previous, current));
        current = previous;
        current._sphere.w += 1.f;
        CHECK(BoundingSphereChanged(previous, current));
    }

    TEST_CASE( "Oriented bounds are independent snapshots of the published shadow sphere", "[bounds][threading][lighting]" )
    {
        const BoundingBox reference{ -1.f, -2.f, -3.f, 1.f, 2.f, 3.f };
        mat4<F32> transform{};
        transform.setTranslation(float3{ 10.f, 20.f, 30.f });
        BoundingBox world{};
        world.transform(reference, transform);
        BoundingSphere sphere{};
        sphere.fromBoundingBox(world);
        const BoundingSphere published = sphere;

        const OBB localBox = CalculateBoundsOBB(world, reference, false, transform);
        CHECK(localBox.position().x == Catch::Approx(10.f));
        CHECK(localBox.position().y == Catch::Approx(20.f));
        CHECK(localBox.position().z == Catch::Approx(30.f));
        CHECK(localBox.halfExtents().x == Catch::Approx(1.f));
        CHECK(localBox.halfExtents().y == Catch::Approx(2.f));
        CHECK(localBox.halfExtents().z == Catch::Approx(3.f));

        const BoundingBox aggregate{ -10.f, -8.f, -6.f, 4.f, 2.f, 12.f };
        const OBB worldBox = CalculateBoundsOBB(aggregate, reference, true, transform);
        CHECK(worldBox.position().x == Catch::Approx(-3.f));
        CHECK(worldBox.position().y == Catch::Approx(-3.f));
        CHECK(worldBox.position().z == Catch::Approx(3.f));
        CHECK(worldBox.halfExtents().x == Catch::Approx(7.f));
        CHECK(worldBox.halfExtents().y == Catch::Approx(5.f));
        CHECK(worldBox.halfExtents().z == Catch::Approx(9.f));
        CHECK_FALSE(BoundingSphereChanged(published, sphere));

        std::atomic_bool mismatch{ false };
        std::array<std::thread, 4u> readers;
        for (std::thread& reader : readers)
        {
            reader = std::thread([&]
            {
                for (U32 i = 0u; i < 1000u; ++i)
                {
                    const OBB box = CalculateBoundsOBB(world, reference, false, transform);
                    if (box.position().x != localBox.position().x ||
                        box.position().y != localBox.position().y ||
                        box.position().z != localBox.position().z ||
                        box.halfExtents().x != localBox.halfExtents().x ||
                        box.halfExtents().y != localBox.halfExtents().y ||
                        box.halfExtents().z != localBox.halfExtents().z)
                    {
                        mismatch.store(true);
                    }
                }
            });
        }
        for (std::thread& reader : readers)
        {
            reader.join();
        }
        CHECK_FALSE(mismatch.load());
        CHECK_FALSE(BoundingSphereChanged(published, sphere));
    }

    TEST_CASE( "Dirty bounds use the current world transform when recomputed", "[bounds][scene_graph][render_smoke]" )
    {
        const BoundingBox reference{-1.f, -2.f, -3.f, 1.f, 2.f, 3.f};
        BoundingBox bounds{};
        BoundsUpdateState state;
        CHECK_FALSE(state.dirty());
        CHECK_FALSE(state.consumeDirty());

        state.markDirty(1u);
        mat4<F32> transform{};
        transform.setTranslation(float3{2.f, 3.f, 4.f});
        state.markDirty(2u);
        transform.setTranslation(float3{20.f, 30.f, 40.f});
        REQUIRE(state.consumeDirty());
        state.update(bounds, reference, false, transform);
        CHECK(bounds._min.x == Catch::Approx(19.f));
        CHECK(bounds._min.y == Catch::Approx(28.f));
        CHECK(bounds._min.z == Catch::Approx(37.f));
        CHECK(bounds._max.x == Catch::Approx(21.f));
        CHECK_FALSE(state.dirty());

        transform.setTranslation(float3{50.f, 60.f, 70.f});
        CHECK_FALSE(state.consumeDirty());
        CHECK(bounds._min.x == Catch::Approx(19.f));

        state.markDirty(1u);
        REQUIRE(state.consumeDirty());
        state.update(bounds, reference, false, transform);
        CHECK(bounds._min.x == Catch::Approx(49.f));
        CHECK(bounds._min.y == Catch::Approx(58.f));
        CHECK(bounds._min.z == Catch::Approx(67.f));

        state.markDirty(1u);
        REQUIRE(state.consumeDirty());
        state.update(bounds, reference, true, transform);
        CHECK(bounds._min.x == Catch::Approx(reference._min.x));
        CHECK(bounds._max.z == Catch::Approx(reference._max.z));
        CHECK_FALSE(state.dirty());
    }

    TEST_CASE( "Bounds update events follow recomputation without dropping new dirtiness", "[bounds][scene_graph][threading]" )
    {
        BoundsUpdateState state;
        CHECK_FALSE(state.consumeDirty());
        CHECK_FALSE(state.recomputed());

        state.markDirty(1u);
        REQUIRE(state.consumeDirty());
        CHECK(state.recomputed());

        state.markDirty(2u);
        CHECK(state.recomputed());
        REQUIRE(state.consumeDirty());
        CHECK(state.recomputed());
        CHECK_FALSE(state.consumeDirty());
        CHECK_FALSE(state.recomputed());
    }

    TEST_CASE( "Bounds changes during recomputation remain pending for every instance", "[bounds][scene_graph][threading][render_smoke]" )
    {
        BoundsChangeCounter changes;
        BoundsChangeTracker first;
        BoundsChangeTracker second;
        first.capture(changes.revision());
        second.capture(changes.revision());
        first.complete();
        second.complete();

        changes.markChanged();
        REQUIRE(first.changed(changes.revision()));
        REQUIRE(second.changed(changes.revision()));
        first.capture(changes.revision());
        changes.markChanged();
        first.complete();
        CHECK(first.changed(changes.revision()));
        CHECK(second.changed(changes.revision()));

        second.capture(changes.revision());
        second.complete();
        CHECK_FALSE(second.changed(changes.revision()));
        CHECK(first.changed(changes.revision()));
        first.capture(changes.revision());
        first.complete();
        CHECK_FALSE(first.changed(changes.revision()));

        BoundsUpdateState dirty;
        dirty.markDirty(1u);
        REQUIRE(dirty.consumeDirty());
        dirty.markDirty(2u);
        CHECK(dirty.dirty());
        CHECK(dirty.consumeDirty());
        CHECK_FALSE(dirty.dirty());
    }

    TEST_CASE( "Parent transform changes reach descendant bounds in the same update", "[bounds][scene_graph][render_smoke]" )
    {
        struct TestTransform
        {
            TransformComponent::Values _local{};
            TransformComponent::Values _world{};
            TestTransform* _parent = nullptr;
            U32 _broadcastMask = 0u;
            U64 _resolvedEpoch = 0u;
        };

        const auto parentOf = [](TestTransform& transform) { return transform._parent; };
        const auto entryOf = [](TestTransform& transform) -> TransformHierarchyEntry
        {
            return { transform._local, transform._world, transform._broadcastMask, transform._resolvedEpoch, false };
        };
        const auto setPosition = [](TestTransform& transform, const F32 x)
        {
            transform._local._values._translation = float3{ x, 0.f, 0.f };
            transform._local._matrix.setTranslation(transform._local._values._translation);
            transform._world._computed = false;
            transform._broadcastMask |= to_base(TransformType::TRANSLATION);
        };

        TestTransform root;
        TestTransform child;
        TestTransform grandchild;
        child._parent = &root;
        grandchild._parent = &child;
        setPosition(root, 10.f);
        setPosition(child, 2.f);
        setPosition(grandchild, 3.f);

        ResolveWorldTransform(grandchild, 1u, parentOf, entryOf);
        ResolveWorldTransform(child, 1u, parentOf, entryOf);
        CHECK(root._world._values._translation.x == Catch::Approx(10.f));
        CHECK(child._world._values._translation.x == Catch::Approx(12.f));
        CHECK(grandchild._world._values._translation.x == Catch::Approx(15.f));

        root._broadcastMask = child._broadcastMask = grandchild._broadcastMask = 0u;
        setPosition(root, 20.f);
        ResolveWorldTransform(grandchild, 2u, parentOf, entryOf);
        CHECK(child._world._values._translation.x == Catch::Approx(22.f));
        CHECK(grandchild._world._values._translation.x == Catch::Approx(25.f));
        CHECK(grandchild._world._previousValues._translation.x == Catch::Approx(15.f));
        CHECK(child._broadcastMask == to_base(TransformType::TRANSLATION));
        CHECK(grandchild._broadcastMask == to_base(TransformType::TRANSLATION));

        const BoundingBox reference{ -1.f, -1.f, -1.f, 1.f, 1.f, 1.f };
        BoundsUpdateState rootBoundsState;
        BoundsUpdateState childBoundsState;
        BoundsUpdateState grandchildBoundsState;
        BoundingBox rootBounds;
        BoundingBox childBounds;
        BoundingBox grandchildBounds;
        rootBoundsState.markDirty(root._broadcastMask);
        childBoundsState.markDirty(child._broadcastMask);
        grandchildBoundsState.markDirty(grandchild._broadcastMask);
        REQUIRE(rootBoundsState.consumeDirty());
        REQUIRE(childBoundsState.consumeDirty());
        REQUIRE(grandchildBoundsState.consumeDirty());
        rootBoundsState.update(rootBounds, reference, false, root._world._matrix);
        childBoundsState.update(childBounds, reference, false, child._world._matrix);
        grandchildBoundsState.update(grandchildBounds, reference, false, grandchild._world._matrix);
        childBounds.add(grandchildBounds);
        rootBounds.add(childBounds);
        CHECK(rootBounds._min.x == Catch::Approx(19.f));
        CHECK(rootBounds._max.x == Catch::Approx(26.f));
        CHECK_FALSE(rootBoundsState.dirty());
        CHECK_FALSE(childBoundsState.dirty());
        CHECK_FALSE(grandchildBoundsState.dirty());
    }

    TEST_CASE( "Topology changes invalidate the old and new ancestor bounds", "[bounds][scene_graph][render_smoke]" )
    {
        struct Node
        {
            Node* _parent = nullptr;
            bool _dirty = false;
        };
        Node root;
        Node oldParent{ &root };
        Node intermediary{ &root };
        Node newParent{ &intermediary };

        const auto parentOf = [](Node& node) { return node._parent; };
        const auto mark = [](Node& node) { node._dirty = true; };
        MarkAncestorBoundsChanged(&oldParent, parentOf, mark);
        MarkAncestorBoundsChanged(&newParent, parentOf, mark);
        CHECK(root._dirty);
        CHECK(oldParent._dirty);
        CHECK(intermediary._dirty);
        CHECK(newParent._dirty);
    }

    TEST_CASE( "Reparenting preserves composed transforms and pending local edits", "[bounds][scene_graph][render_smoke]" )
    {
        TransformValues oldParent;
        oldParent._translation = { 12.f, -8.f, 3.f };
        oldParent._scale = { 2.f, 3.f, -4.f };
        oldParent._orientation = quatf(WORLD_Y_AXIS, Angle::to_RADIANS(Angle::DEGREES_F{ 35.f }));

        TransformValues newParent;
        newParent._translation = { -5.f, 4.f, 10.f };
        newParent._scale = { -3.f, 2.f, 0.5f };
        newParent._orientation = quatf(WORLD_X_AXIS, Angle::to_RADIANS(Angle::DEGREES_F{ -25.f }));

        TransformValues pendingLocal;
        pendingLocal._translation = { 7.f, 2.f, -9.f };
        pendingLocal._scale = { 0.5f, 4.f, 2.f };
        pendingLocal._orientation = quatf(WORLD_Z_AXIS, Angle::to_RADIANS(Angle::DEGREES_F{ 20.f }));

        const auto checkEquivalent = [](const TransformValues& actual, const TransformValues& expected)
        {
            CHECK(actual._translation.x == Catch::Approx(expected._translation.x).margin(0.001f));
            CHECK(actual._translation.y == Catch::Approx(expected._translation.y).margin(0.001f));
            CHECK(actual._translation.z == Catch::Approx(expected._translation.z).margin(0.001f));
            CHECK(actual._scale.x == Catch::Approx(expected._scale.x).margin(0.001f));
            CHECK(actual._scale.y == Catch::Approx(expected._scale.y).margin(0.001f));
            CHECK(actual._scale.z == Catch::Approx(expected._scale.z).margin(0.001f));
            CHECK(actual._orientation.X() == Catch::Approx(expected._orientation.X()).margin(0.001f));
            CHECK(actual._orientation.Y() == Catch::Approx(expected._orientation.Y()).margin(0.001f));
            CHECK(actual._orientation.Z() == Catch::Approx(expected._orientation.Z()).margin(0.001f));
            CHECK(actual._orientation.W() == Catch::Approx(expected._orientation.W()).margin(0.001f));
        };

        for (const bool localRotation : { false, true })
        {
            CAPTURE(localRotation);
            const TransformValues expected = ComposeWorldTransform(pendingLocal, &oldParent, localRotation);
            TransformValues reparented;
            REQUIRE(TryReparentTransform(pendingLocal, &oldParent, &newParent, localRotation, reparented));
            checkEquivalent(ComposeWorldTransform(reparented, &newParent, localRotation), expected);

            TransformValues detached;
            REQUIRE(TryReparentTransform(pendingLocal, &oldParent, nullptr, localRotation, detached));
            checkEquivalent(detached, expected);

            TransformValues attached;
            REQUIRE(TryReparentTransform(pendingLocal, nullptr, &newParent, localRotation, attached));
            checkEquivalent(ComposeWorldTransform(attached, &newParent, localRotation), pendingLocal);
        }

        TransformValues unchanged = pendingLocal;
        newParent._scale.y = 0.f;
        CHECK_FALSE(TryReparentTransform(pendingLocal, &oldParent, &newParent, false, unchanged));
        checkEquivalent(unchanged, pendingLocal);
        newParent._scale.y = 2.f;
        newParent._orientation = quatf(0.f, 0.f, 0.f, 0.f);
        CHECK_FALSE(TryReparentTransform(pendingLocal, &oldParent, &newParent, false, unchanged));
        checkEquivalent(unchanged, pendingLocal);
        newParent._orientation = quatf{};
        newParent._translation.x = std::numeric_limits<F32>::quiet_NaN();
        CHECK_FALSE(TryReparentTransform(pendingLocal, &oldParent, &newParent, true, unchanged));
        checkEquivalent(unchanged, pendingLocal);
    }
}
