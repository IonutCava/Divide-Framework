// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"
#include "ECS/Components/Headers/BoundsUpdateState.h"
#include "ECS/Systems/Headers/TransformHierarchy.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"
#include "Rendering/Camera/Headers/Camera.h"

namespace Divide
{
    TEST_CASE( "Camera lookAt preserves a world-space eye and orientation", "[camera][render_smoke]" )
    {
        Camera camera{ "camera-look-at-test", Camera::Mode::STATIC };
        const float3 eye{ 17.f, 3.f, -11.f };
        const float3 target{ 22.f, 5.f, -4.f };
        const float3 up{ 0.f, 1.f, 0.f };
        const mat4<F32> expected = Camera::LookAt( eye, target, up );
        const mat4<F32> actual = camera.lookAt( eye, target, up );

        CHECK( camera.snapshot()._eye.x == Catch::Approx( eye.x ).margin( 0.0001f ) );
        CHECK( camera.snapshot()._eye.y == Catch::Approx( eye.y ).margin( 0.0001f ) );
        CHECK( camera.snapshot()._eye.z == Catch::Approx( eye.z ).margin( 0.0001f ) );
        for ( size_t i = 0u; i < 16u; ++i )
        {
            CAPTURE( i );
            CHECK( actual.mat[i] == Catch::Approx( expected.mat[i] ).margin( 0.0001f ) );
        }

        const float4 viewEye = actual * float4{ eye.x, eye.y, eye.z, 1.f };
        const float4 viewTarget = actual * float4{ target.x, target.y, target.z, 1.f };
        CHECK( viewEye.x == Catch::Approx( 0.f ).margin( 0.0001f ) );
        CHECK( viewEye.y == Catch::Approx( 0.f ).margin( 0.0001f ) );
        CHECK( viewEye.z == Catch::Approx( 0.f ).margin( 0.0001f ) );
        CHECK( viewTarget.x == Catch::Approx( 0.f ).margin( 0.0001f ) );
        CHECK( viewTarget.y == Catch::Approx( 0.f ).margin( 0.0001f ) );
        CHECK( viewTarget.z < 0.f );

        const float3 offset{ -2.f, 4.f, 1.f };
        camera.setEye( eye + offset );
        const mat4<F32> moved = camera.viewMatrix();
        const mat4<F32> expectedMoved = Camera::LookAt( eye + offset, target + offset, up );
        for ( size_t i = 0u; i < 16u; ++i )
        {
            CAPTURE( i );
            CHECK( moved.mat[i] == Catch::Approx( expectedMoved.mat[i] ).margin( 0.0001f ) );
        }
    }

    TEST_CASE( "Mutable camera view projection refreshes before frustum updates", "[camera][render_smoke]" )
    {
        Camera camera{ "camera-view-projection-test", Camera::Mode::STATIC };
        camera.lookAt( float3{ 2.f, 3.f, 8.f }, float3{ 0.f, 0.f, 0.f }, float3{ 0.f, 1.f, 0.f } );
        camera.setProjection( 1.f, 60.f, float2{ 0.1f, 100.f } );
        REQUIRE( camera.updateLookAt() );

        const auto checkCombined = [&]()
        {
            const mat4<F32> actual = camera.viewProjectionMatrix();
            const mat4<F32> expected = mat4<F32>::Multiply( camera.projectionMatrix(), camera.viewMatrix() );
            for ( size_t i = 0u; i < 16u; ++i )
            {
                CAPTURE( i );
                CHECK( actual.mat[i] == Catch::Approx( expected.mat[i] ).margin( 0.0001f ) );
            }
            return actual;
        };

        const mat4<F32> initial = checkCombined();
        camera.setEye( float3{ 5.f, 4.f, 7.f } );
        const mat4<F32> afterView = checkCombined();
        CHECK( afterView.mat[12] != Catch::Approx( initial.mat[12] ) );

        REQUIRE( camera.updateLookAt() );
        camera.setProjection( 1.6f, 45.f, float2{ 0.2f, 250.f } );
        const mat4<F32> afterProjection = checkCombined();
        CHECK( afterProjection.mat[0] != Catch::Approx( afterView.mat[0] ) );

        REQUIRE( camera.updateLookAt() );
        camera.lookAt( float3{ -6.f, 4.f, 9.f }, float3{ 1.f, 1.f, 0.f }, float3{ 0.f, 1.f, 0.f } );
        camera.setProjection( 1.2f, 75.f, float2{ 0.5f, 500.f } );
        const mat4<F32> afterBoth = checkCombined();
        CHECK( afterBoth.mat[0] != Catch::Approx( afterProjection.mat[0] ) );
    }

    TEST_CASE( "Frustum classification checks later planes after intersections", "[camera][frustum][render_smoke]" )
    {
        Frustum frustum;
        frustum.computePlanes( MAT4_IDENTITY );

        for ( const I8 cached : std::array<I8, 6>{{ -1, to_I8( FrustumPlane::PLANE_LEFT ),
                                                    to_I8( FrustumPlane::PLANE_TOP ),
                                                    to_I8( FrustumPlane::PLANE_FAR ),
                                                    to_I8( FrustumPlane::COUNT ), -99 }} )
        {
            CAPTURE( cached );
            I8 planeCache = cached;
            CHECK( frustum.ContainsSphere( float3{ -0.9f, 0.f, 1.4f }, 0.2f, planeCache ) == FrustumCollision::FRUSTUM_OUT );
            CHECK( planeCache == to_I8( FrustumPlane::PLANE_FAR ) );

            planeCache = cached;
            const BoundingBox box{ -1.1f, -0.1f, 1.2f, -0.8f, 0.1f, 1.4f };
            CHECK( frustum.ContainsBoundingBox( box, planeCache ) == FrustumCollision::FRUSTUM_OUT );
            CHECK( planeCache == to_I8( FrustumPlane::PLANE_FAR ) );
        }

        I8 planeCache = to_I8( FrustumPlane::PLANE_FAR );
        CHECK( frustum.ContainsSphere( float3{ 0.f, 0.f, 0.5f }, 0.1f, planeCache ) == FrustumCollision::FRUSTUM_IN );
        CHECK( planeCache == -1 );
        CHECK( frustum.ContainsSphere( float3{ -0.9f, 0.f, 0.5f }, 0.2f, planeCache ) == FrustumCollision::FRUSTUM_INTERSECT );
        CHECK( planeCache == to_I8( FrustumPlane::PLANE_LEFT ) );
        CHECK( frustum.ContainsSphere( float3{ 0.f, 0.f, -0.5f }, 0.1f, planeCache ) == FrustumCollision::FRUSTUM_OUT );
        CHECK( planeCache == to_I8( FrustumPlane::PLANE_NEAR ) );
        CHECK( frustum.ContainsSphere( float3{ 0.f, 0.f, 0.05f }, 0.1f, planeCache ) == FrustumCollision::FRUSTUM_INTERSECT );
        CHECK( planeCache == to_I8( FrustumPlane::PLANE_NEAR ) );

        const BoundingBox nearOutside{ -0.1f, -0.1f, -0.3f, 0.1f, 0.1f, -0.2f };
        CHECK( frustum.ContainsBoundingBox( nearOutside, planeCache ) == FrustumCollision::FRUSTUM_OUT );
        CHECK( planeCache == to_I8( FrustumPlane::PLANE_NEAR ) );
    }

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

    TEST_CASE( "New hierarchy transforms interpolate from their initial world pose", "[transform][scene_graph][render_smoke]" )
    {
        struct TestTransform
        {
            TransformComponent::Values _local{};
            TransformComponent::Values _world{};
            TestTransform* _parent = nullptr;
            U32 _broadcastMask = 0u;
            U64 _resolvedEpoch = 0u;
        };
        const auto parentOf = []( TestTransform& transform ) { return transform._parent; };
        const auto entryOf = []( TestTransform& transform ) -> TransformHierarchyEntry
        {
            return { transform._local, transform._world, transform._broadcastMask, transform._resolvedEpoch, false };
        };

        TestTransform parent;
        parent._local._values._translation = { 785.f, 15.f, 80.f };
        parent._local._values._scale = { 0.03f, 0.03f, 0.03f };
        parent._local._values._orientation = quatf( WORLD_Y_AXIS, Angle::to_RADIANS( Angle::DEGREES_F{ 35.f } ) );
        parent._broadcastMask = to_base( TransformType::ALL );
        TestTransform child;
        child._parent = &parent;
        child._local._values._translation = { -63.f, -2.5f, -35.f };
        child._broadcastMask = to_base( TransformType::ALL );

        ResolveWorldTransform( child, 1u, parentOf, entryOf );
        for ( const F32 alpha : { 0.f, 0.5f, 1.f } )
        {
            CAPTURE( alpha );
            const TransformValues interpolated = Lerp( child._world._previousValues, child._world._values, alpha );
            CHECK( interpolated._translation.x == Catch::Approx( child._world._values._translation.x ) );
            CHECK( interpolated._translation.y == Catch::Approx( child._world._values._translation.y ) );
            CHECK( interpolated._translation.z == Catch::Approx( child._world._values._translation.z ) );
            CHECK( interpolated._scale.x == Catch::Approx( 0.03f ) );
            CHECK( interpolated._scale.y == Catch::Approx( 0.03f ) );
            CHECK( interpolated._scale.z == Catch::Approx( 0.03f ) );
            CHECK( interpolated._orientation.X() == Catch::Approx( child._world._values._orientation.X() ).margin( 0.0001f ) );
            CHECK( interpolated._orientation.Y() == Catch::Approx( child._world._values._orientation.Y() ).margin( 0.0001f ) );
            CHECK( interpolated._orientation.Z() == Catch::Approx( child._world._values._orientation.Z() ).margin( 0.0001f ) );
            CHECK( interpolated._orientation.W() == Catch::Approx( child._world._values._orientation.W() ).margin( 0.0001f ) );
        }
    }

    TEST_CASE( "World interpolation settles after motion stops", "[transform][scene_graph][render_smoke]" )
    {
        struct TestTransform
        {
            TransformComponent::Values _local{};
            TransformComponent::Values _world{};
            TestTransform* _parent = nullptr;
            U32 _broadcastMask = 0u;
            U64 _resolvedEpoch = 0u;
        };
        const auto parentOf = []( TestTransform& transform ) { return transform._parent; };
        const auto entryOf = []( TestTransform& transform ) -> TransformHierarchyEntry
        {
            return { transform._local, transform._world, transform._broadcastMask, transform._resolvedEpoch, false };
        };

        TestTransform parent;
        parent._local._values._translation = { 10.f, 0.f, 0.f };
        parent._broadcastMask = to_base( TransformType::ALL );
        TestTransform child;
        child._parent = &parent;
        child._local._values._translation = { 2.f, 0.f, 0.f };
        child._broadcastMask = to_base( TransformType::ALL );
        ResolveWorldTransform( child, 1u, parentOf, entryOf );

        AdvanceWorldTransformHistory( parent._world );
        AdvanceWorldTransformHistory( child._world );
        parent._local._values._translation.x = 20.f;
        parent._world._computed = false;
        parent._broadcastMask = to_base( TransformType::TRANSLATION );
        child._broadcastMask = 0u;
        ResolveWorldTransform( child, 2u, parentOf, entryOf );
        CHECK( child._world._previousValues._translation.x == Catch::Approx( 12.f ) );
        CHECK( child._world._values._translation.x == Catch::Approx( 22.f ) );
        CHECK( Lerp( child._world._previousValues, child._world._values, 0.25f )._translation.x == Catch::Approx( 14.5f ) );
        CHECK( Lerp( child._world._previousValues, child._world._values, 0.75f )._translation.x == Catch::Approx( 19.5f ) );

        AdvanceWorldTransformHistory( parent._world );
        AdvanceWorldTransformHistory( child._world );
        parent._broadcastMask = child._broadcastMask = 0u;
        ResolveWorldTransform( child, 3u, parentOf, entryOf );
        for ( const F32 alpha : { 0.f, 0.5f, 1.f } )
        {
            CAPTURE( alpha );
            CHECK( Lerp( child._world._previousValues, child._world._values, alpha )._translation.x == Catch::Approx( 22.f ) );
        }
        CHECK_FALSE( child._world._historyNeedsAdvance );

        child._world = {};
        ResolveWorldTransform( child, 4u, parentOf, entryOf );
        CHECK( child._world._previousValues._translation.x == Catch::Approx( 22.f ) );
        CHECK( child._world._values._translation.x == Catch::Approx( 22.f ) );
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
