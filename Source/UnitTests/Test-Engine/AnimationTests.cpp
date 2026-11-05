// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"
#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "ECS/Systems/Headers/AnimationSystem.h"
#include "Geometry/Animations/Headers/SceneAnimator.h"

namespace Divide
{
    struct AnimationSystemTestAccess
    {
        static void SetComponents(AnimationSystem& system, const vector<AnimationComponent*>& components)
        {
            system._componentCache = components;
        }

        [[nodiscard]] static D64 CurrentTimeStamp(const AnimationComponent& component) noexcept
        {
            return component._currentTimeStamp;
        }

        static void SetCurrentTimeStamp(AnimationComponent& component, const D64 value) noexcept
        {
            component._currentTimeStamp = value;
        }

        static void SetFrame(AnimationComponent& component, const AnimEvaluator::FrameIndex frame) noexcept
        {
            component._frameIndex = frame;
        }
    };

    TEST_CASE("An inactive animation does not prevent later components from updating", "[animation][render_smoke]")
    {
        platformInitRunListener::PlatformInit();

        Application app;
        PlatformContext context{ app };
        ECS::ECSEngine engine;
        SceneAnimator animator{ false };
        AnimationComponent first{ nullptr, context };
        AnimationComponent second{ nullptr, context };
        AnimationSystem system{ engine, context };

        second.setAnimator(&animator);
        second.playAnimations(false);
        second.resetTimers(100.0);
        AnimationSystemTestAccess::SetComponents(system, { &first, &second });

        SECTION("A component without an animator is skipped")
        {
            system.Update(0.f);
            CHECK(AnimationSystemTestAccess::CurrentTimeStamp(second) == 100.0);
        }

        SECTION("A component with an unchanged timestamp is skipped")
        {
            first.setAnimator(&animator);
            first.playAnimations(false);
            first.resetTimers(0.0);
            system.Update(0.f);
            REQUIRE(AnimationSystemTestAccess::CurrentTimeStamp(first) == 0.0);
            REQUIRE(AnimationSystemTestAccess::CurrentTimeStamp(second) == 100.0);

            second.resetTimers(200.0);
            system.Update(0.f);
            CHECK(AnimationSystemTestAccess::CurrentTimeStamp(second) == 200.0);
        }
    }

    TEST_CASE("Animation updates report changes to the rendered frame", "[animation][render_smoke]")
    {
        platformInitRunListener::PlatformInit();

        Application app;
        PlatformContext context{ app };
        ECS::ECSEngine engine;
        SceneAnimator animator{ false };
        AnimationComponent component{ nullptr, context };
        AnimationSystem system{ engine, context };
        component.setAnimator(&animator);
        component.resetTimers(100.0);
        AnimationSystemTestAccess::SetCurrentTimeStamp(component, 100.0);
        AnimationSystemTestAccess::SetComponents(system, { &component });

        const bool previousGlobalState = system.getAnimationState();
        SCOPE_EXIT
        {
            system.toggleAnimationState(previousGlobalState);
        };
        system.toggleAnimationState(true);

        AnimationSystemTestAccess::SetFrame(component, { ._curr = 0, ._prev = 9, ._next = 1 });
        system.Update(0.f);
        CHECK_FALSE(component.frameTicked());

        AnimationSystemTestAccess::SetFrame(component, { ._curr = 3, ._prev = 2, ._next = 4 });
        system.Update(0.f);
        CHECK(component.frameTicked());
        system.Update(0.f);
        CHECK_FALSE(component.frameTicked());

        SECTION("Wrapping in either direction is a change")
        {
            AnimationSystemTestAccess::SetFrame(component, { ._curr = 0, ._prev = 9, ._next = 1 });
            system.Update(0.f);
            CHECK(component.frameTicked());
            AnimationSystemTestAccess::SetFrame(component, { ._curr = 9, ._prev = 0, ._next = 8 });
            system.Update(0.f);
            CHECK(component.frameTicked());
        }

        SECTION("Component pause and resume track the frame actually rendered")
        {
            component.playAnimations(false);
            system.Update(0.f);
            CHECK(component.frameTicked());
            system.Update(0.f);
            CHECK_FALSE(component.frameTicked());
            component.playAnimations(true);
            system.Update(0.f);
            CHECK(component.frameTicked());
            system.Update(0.f);
            CHECK_FALSE(component.frameTicked());
        }

        SECTION("Global pause and resume track the frame actually rendered")
        {
            system.toggleAnimationState(false);
            system.Update(0.f);
            CHECK(component.frameTicked());
            system.Update(0.f);
            CHECK_FALSE(component.frameTicked());
            system.toggleAnimationState(true);
            system.Update(0.f);
            CHECK(component.frameTicked());
        }

        SECTION("Timer reset preserves the previous observed frame")
        {
            component.resetTimers(100.0);
            AnimationSystemTestAccess::SetCurrentTimeStamp(component, 100.0);
            system.Update(0.f);
            CHECK(component.frameTicked());
        }
    }
}
