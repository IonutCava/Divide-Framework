// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"
#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/TaskPool.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "GUI/Headers/GUIButton.h"
#include "Platform/Audio/Headers/AudioDescriptor.h"
#include "Platform/Headers/ConditionalWait.h"

namespace Divide
{
    struct GUIButtonTestAccess
    {
        [[nodiscard]] static GUIButton* Create()
        {
            return new GUIButton("sound-test", "", "", RelativePosition2D{}, RelativeScale2D{}, nullptr);
        }

        static void Destroy(GUIButton* button)
        {
            delete button;
        }

        [[nodiscard]] static bool SoundsAreUnset(const GUIButton& button) noexcept
        {
            for (const Handle<AudioDescriptor> sound : button._eventSound)
            {
                if (sound != INVALID_HANDLE<AudioDescriptor>)
                {
                    return false;
                }
            }
            return true;
        }
    };

    TEST_CASE("GUI button sound references survive replacement and release at destruction", "[gui][resources][render_smoke]")
    {
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        Application app;
        PlatformContext context{ app };
        TaskPool& loader = context.taskPool(TaskPoolType::ASSET_LOADER);
        REQUIRE(loader.init(1u));
        SCOPE_EXIT
        {
            loader.shutdown();
        };
        InitConditionalWait(context);
        ResourceCache::Init(RenderAPI::None, context);
        SCOPE_EXIT
        {
            ResourceCache::Stop();
        };

        ResourceDescriptor<AudioDescriptor> firstDescriptor{ "button-sound-first" };
        ResourceDescriptor<AudioDescriptor> secondDescriptor{ "button-sound-second" };
        firstDescriptor.waitForReady(true);
        secondDescriptor.waitForReady(true);
        Handle<AudioDescriptor> first = CreateResource(firstDescriptor);
        Handle<AudioDescriptor> second = CreateResource(secondDescriptor);
        GUIButton* button = GUIButtonTestAccess::Create();
        SCOPE_EXIT
        {
            if (button != nullptr)
            {
                GUIButtonTestAccess::Destroy(button);
            }
            DestroyResource(first);
            DestroyResource(second);
            ResourceCache::OnFrameStart();
        };

        REQUIRE(first != INVALID_HANDLE<AudioDescriptor>);
        REQUIRE(second != INVALID_HANDLE<AudioDescriptor>);
        const Handle<AudioDescriptor> firstHandle = first;
        const Handle<AudioDescriptor> secondHandle = second;
        CHECK(GUIButtonTestAccess::SoundsAreUnset(*button));

        button->setEventSound(GUIButton::Event::MouseClick, first);
        button->setEventSound(GUIButton::Event::MouseClick, first);
        button->setEventSound(GUIButton::Event::MouseDown, first);
        DestroyResource(first);
        ResourceCache::OnFrameStart();
        REQUIRE(Get(firstHandle) != nullptr);

        button->setEventSound(GUIButton::Event::MouseClick, second);
        DestroyResource(second);
        ResourceCache::OnFrameStart();
        REQUIRE(Get(firstHandle) != nullptr);
        REQUIRE(Get(secondHandle) != nullptr);

        button->setEventSound(GUIButton::Event::MouseDown, INVALID_HANDLE<AudioDescriptor>);
        ResourceCache::OnFrameStart();
        CHECK(Get(firstHandle) == nullptr);
        REQUIRE(Get(secondHandle) != nullptr);

        GUIButtonTestAccess::Destroy(button);
        button = nullptr;
        ResourceCache::OnFrameStart();
        CHECK(Get(secondHandle) == nullptr);
    }
}
