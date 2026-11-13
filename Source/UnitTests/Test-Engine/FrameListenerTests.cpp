// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Managers/Headers/FrameListenerManager.h"

namespace Divide
{
    namespace
    {
        class RecordingFrameListener final : public FrameListener
        {
        public:
            RecordingFrameListener(FrameListenerManager& manager, const U32 order, const U32 id, vector<U32>& calls)
                : FrameListener("frame_listener_test", manager, order)
                , _id(id)
                , _calls(calls)
            {
            }

            void setActive(const bool active) noexcept
            {
                enabled(active);
            }

            std::function<void()> _onStarted;
            bool _result{true};

        protected:
            bool frameStarted([[maybe_unused]] const FrameEvent& evt) override
            {
                _calls.push_back(_id);
                if (_onStarted)
                {
                    _onStarted();
                }
                return _result;
            }

            bool frameEnded([[maybe_unused]] const FrameEvent& evt) override
            {
                _calls.push_back(_id);
                return _result;
            }

        private:
            U32 _id;
            vector<U32>& _calls;
        };
    }

    TEST_CASE("Frame listeners use their call order and are registered once", "[frame_listener][render_smoke]")
    {
        platformInitRunListener::PlatformInit();
        vector<U32> calls;
        FrameListenerManager manager;
        std::array<RecordingFrameListener, 4> listeners
        {
            RecordingFrameListener{manager, 30u, 1u, calls},
            RecordingFrameListener{manager, 20u, 2u, calls},
            RecordingFrameListener{manager, 10u, 3u, calls},
            RecordingFrameListener{manager, 20u, 4u, calls}
        };

        FrameEvent evt{};
        evt._type = FrameEventType::FRAME_EVENT_STARTED;
        REQUIRE(manager.frameEvent(evt));
        CHECK(calls == vector<U32>{3u, 2u, 4u, 1u});

        manager.registerFrameListener(&listeners[0], 5u);
        manager.registerFrameListener(&listeners[1], 20u);
        calls.clear();
        REQUIRE(manager.frameEvent(evt));
        CHECK(calls == vector<U32>{1u, 3u, 2u, 4u});

        listeners[2].setActive(false);
        calls.clear();
        evt._type = FrameEventType::FRAME_EVENT_ENDED;
        REQUIRE(manager.frameEvent(evt));
        CHECK(calls == vector<U32>{1u, 2u, 4u});
    }

    TEST_CASE("Frame callbacks tolerate listener replacement and re-registration", "[frame_listener][render_smoke]")
    {
        platformInitRunListener::PlatformInit();
        vector<U32> calls;
        FrameListenerManager manager;
        RecordingFrameListener first{manager, 10u, 1u, calls};
        auto removed = std::make_unique<RecordingFrameListener>(manager, 20u, 2u, calls);
        std::unique_ptr<RecordingFrameListener> added;
        std::unique_ptr<RecordingFrameListener> earlier;
        bool changed = false;
        first._onStarted = [&]
        {
            if (!changed)
            {
                changed = true;
                removed.reset();
                added = std::make_unique<RecordingFrameListener>(manager, 20u, 3u, calls);
                earlier = std::make_unique<RecordingFrameListener>(manager, 5u, 4u, calls);
                manager.registerFrameListener(&first, 30u);
            }
        };

        FrameEvent evt{};
        evt._type = FrameEventType::FRAME_EVENT_STARTED;
        REQUIRE(manager.frameEvent(evt));
        CHECK(calls == vector<U32>{1u, 3u});

        calls.clear();
        REQUIRE(manager.frameEvent(evt));
        CHECK(calls == vector<U32>{4u, 3u, 1u});
    }

    TEST_CASE("Nested and aborted frame events retain independent dispatch state", "[frame_listener][render_smoke]")
    {
        platformInitRunListener::PlatformInit();
        vector<U32> calls;
        FrameListenerManager manager;
        RecordingFrameListener first{manager, 10u, 1u, calls};
        RecordingFrameListener second{manager, 20u, 2u, calls};
        FrameEvent evt{};
        evt._type = FrameEventType::FRAME_EVENT_STARTED;
        bool insideNestedEvent = false;
        bool nestedSucceeded = false;
        first._onStarted = [&]
        {
            if (!insideNestedEvent)
            {
                insideNestedEvent = true;
                nestedSucceeded = manager.frameEvent(evt);
                insideNestedEvent = false;
                manager.registerFrameListener(&first, 30u);
            }
        };

        REQUIRE(manager.frameEvent(evt));
        CHECK(nestedSucceeded);
        CHECK(calls == vector<U32>{1u, 1u, 2u, 2u});

        first._onStarted = {};
        manager.registerFrameListener(&first, 10u);
        first._result = false;
        calls.clear();
        CHECK_FALSE(manager.frameEvent(evt));
        CHECK(calls == vector<U32>{1u});

        first._result = true;
        calls.clear();
        REQUIRE(manager.frameEvent(evt));
        CHECK(calls == vector<U32>{1u, 2u});
    }
}
