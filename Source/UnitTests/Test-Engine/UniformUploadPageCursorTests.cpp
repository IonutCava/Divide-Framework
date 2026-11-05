// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/BufferLocks.h"
#include "Platform/Video/Headers/GPUCompletionGuard.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glUniformFences.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkSwapChain.h"
#include "Platform/Video/Shaders/Headers/ShaderDataUploader.h"

namespace Divide
{
    namespace
    {
        class TestRangeLockManager final : public LockManager
        {
        public:
            bool _failWait{ true };
            U32 _waitCount{ 0u };

        protected:
            bool waitForLockedRangeLocked( const SyncObject_uptr& sync, const BufferRange<>& range,
                                           const BufferLockInstance& lock ) override
            {
                ++_waitCount;
                return !_failWait && LockManager::waitForLockedRangeLocked( sync, range, lock );
            }
        };
    }

    TEST_CASE("Uniform upload cursor allocates unique slots across pages", "[renderer][uniform-upload]")
    {
        STATIC_CHECK(UniformUploadPageCursor::SlotsPerPage == 64u);

        UniformUploadPageCursor cursor;
        REQUIRE(cursor.beginFrame(17u));
        constexpr size_t reservationCount = 1301u;

        for (size_t index = 0u; index < reservationCount; ++index)
        {
            CAPTURE(index);
            const UniformUploadPageCursor::Allocation allocation = cursor.reserve();
            CHECK(allocation._page == index / UniformUploadPageCursor::SlotsPerPage);
            CHECK(allocation._slot == to_U16(index % UniformUploadPageCursor::SlotsPerPage));
            CHECK(allocation._generation == 1u);
        }
    }

    TEST_CASE("Uniform snapshots remain unchanged across frames until their data changes", "[renderer][uniform-upload]")
    {
        UniformUploadPageCursor cursor;
        UniformUploadSnapshotState snapshot;
        REQUIRE(cursor.beginFrame(0u));
        CHECK(snapshot.needsUpload());

        const auto first = cursor.reserve();
        snapshot.markUploaded();
        snapshot.markUsed(0u);
        CHECK_FALSE(snapshot.needsUpload());
        REQUIRE(cursor.beginFrame(1u));
        snapshot.markUsed(1u);
        CHECK_FALSE(snapshot.needsUpload());
        CHECK(snapshot.usage()._frames[0]._number == 0u);
        CHECK(snapshot.usage()._frames[1]._number == 1u);
        snapshot.markDirty();
        CHECK(snapshot.needsUpload());
        const auto replacement = cursor.reserve();
        REQUIRE(cursor.release(first, snapshot.usage()));
        CHECK((replacement._page != first._page || replacement._slot != first._slot));
        snapshot.markUploaded();
        snapshot.markUsed(1u);
        CHECK_FALSE(snapshot.needsUpload());
        CHECK_FALSE(snapshot.usage()._frames[0]._valid);
        CHECK(snapshot.usage()._frames[1]._valid);

        REQUIRE(cursor.beginFrame(Config::MAX_FRAMES_IN_FLIGHT));
        const auto beforeFinalConsumer = cursor.reserve();
        CHECK((beforeFinalConsumer._page != first._page || beforeFinalConsumer._slot != first._slot));
        REQUIRE(cursor.beginFrame(Config::MAX_FRAMES_IN_FLIGHT + 1u));
        const auto recycled = cursor.reserve();
        CHECK(recycled._page == first._page);
        CHECK(recycled._slot == first._slot);
        CHECK(recycled._generation != first._generation);
    }

    TEST_CASE("Identical uniform bytes do not dirty an uploaded snapshot", "[renderer][uniform-upload]")
    {
        UniformUploadSnapshotState snapshot;
        std::array<Byte, 4> stored{{ Byte{1u}, Byte{2u}, Byte{3u}, Byte{4u} }};
        const std::array<Byte, 4> equal = stored;
        const std::array<Byte, 4> changed{{ Byte{1u}, Byte{2u}, Byte{9u}, Byte{4u} }};

        snapshot.markUploaded();
        snapshot.markUsed(0u);
        snapshot.updateBytes(stored.data(), equal.data(), equal.size());
        snapshot.markUsed(1u);
        CHECK_FALSE(snapshot.needsUpload());
        CHECK(stored == equal);

        snapshot.updateBytes(stored.data(), changed.data(), changed.size());
        CHECK(snapshot.needsUpload());
        CHECK(stored == changed);
        snapshot.markUploaded();
        snapshot.updateBytes(stored.data(), changed.data(), changed.size());
        CHECK_FALSE(snapshot.needsUpload());
        snapshot.updateBytes(nullptr, nullptr, 0u);
        CHECK_FALSE(snapshot.needsUpload());
    }

    TEST_CASE("Uniform retirement waits for every used frame slot even when frames are skipped", "[renderer][uniform-upload]")
    {
        UniformUploadPageCursor cursor;
        UniformUploadPageCursor::Usage usage;
        REQUIRE(cursor.beginFrame(0u));
        const auto first = cursor.reserve();
        usage.markUsed(0u);
        REQUIRE(cursor.beginFrame(1u));
        usage.markUsed(1u);
        REQUIRE(cursor.release(first, usage));

        REQUIRE(cursor.beginFrame(Config::MAX_FRAMES_IN_FLIGHT + 1u));
        const auto stillInUse = cursor.reserve();
        CHECK((stillInUse._page != first._page || stillInUse._slot != first._slot));
        REQUIRE(cursor.beginFrame(Config::MAX_FRAMES_IN_FLIGHT * 2u));
        const auto recycled = cursor.reserve();
        CHECK(recycled._page == first._page);
        CHECK(recycled._slot == first._slot);
    }

    TEST_CASE("Uniform retirement does not recycle repeated frames or held snapshots", "[renderer][uniform-upload]")
    {
        UniformUploadPageCursor cursor;
        UniformUploadPageCursor::Usage usage;
        REQUIRE(cursor.beginFrame(0u));
        const auto held = cursor.reserve();
        const auto retired = cursor.reserve();
        usage.markUsed(0u);
        REQUIRE(cursor.release(retired, usage));
        REQUIRE(cursor.beginFrame(0u));
        const auto next = cursor.reserve();
        CHECK(next._slot != retired._slot);
        REQUIRE(cursor.beginFrame(1u));
        CHECK_FALSE(cursor.beginFrame(0u));
        REQUIRE(cursor.beginFrame(Config::MAX_FRAMES_IN_FLIGHT));
        const auto recycled = cursor.reserve();
        CHECK(recycled._slot == retired._slot);
        const auto another = cursor.reserve();
        CHECK(another._slot != held._slot);
        CHECK(another._slot != recycled._slot);
    }

    TEST_CASE("Uniform replacements do not wrap an in-flight page", "[renderer][uniform-upload]")
    {
        UniformUploadPageCursor cursor;
        UniformUploadPageCursor::Usage usage;
        REQUIRE(cursor.beginFrame(0u));
        usage.markUsed(0u);
        constexpr size_t count = UniformUploadPageCursor::SlotsPerPage * 2u + 1u;
        std::array<UniformUploadPageCursor::Allocation, count> allocations{};
        for (size_t i = 0u; i < allocations.size(); ++i)
        {
            allocations[i] = cursor.reserve();
            REQUIRE(cursor.release(allocations[i], usage));
            CHECK(allocations[i]._page == i / UniformUploadPageCursor::SlotsPerPage);
            CHECK(allocations[i]._slot == to_U16(i % UniformUploadPageCursor::SlotsPerPage));
        }

        REQUIRE(cursor.beginFrame(Config::MAX_FRAMES_IN_FLIGHT));
        for (size_t i = 0u; i < allocations.size(); ++i)
        {
            const auto reused = cursor.reserve();
            CHECK(reused._generation == 2u);
            CHECK_FALSE(cursor.release(allocations[i], {}));
        }
    }

    TEST_CASE("Uniform allocations reject stale, duplicate, and invalid releases", "[renderer][uniform-upload]")
    {
        UniformUploadPageCursor cursor;
        REQUIRE(cursor.beginFrame(5u));
        const auto first = cursor.reserve();
        REQUIRE(cursor.release(first, {}));
        CHECK_FALSE(cursor.release(first, {}));

        const auto replacement = cursor.reserve();
        CHECK(replacement._page == first._page);
        CHECK(replacement._slot == first._slot);
        CHECK(replacement._generation != first._generation);
        CHECK_FALSE(cursor.release(first, {}));

        UniformUploadPageCursor::Usage future;
        future.markUsed(6u);
        CHECK_FALSE(cursor.release(replacement, future));
        auto invalid = replacement;
        invalid._slot = UniformUploadPageCursor::SlotsPerPage;
        CHECK_FALSE(cursor.release(invalid, {}));
        invalid = replacement;
        invalid._page = SIZE_MAX;
        CHECK_FALSE(cursor.release(invalid, {}));
        REQUIRE(cursor.release(replacement, {}));
    }

    TEST_CASE("Uniform retirement handles maximum frame numbers", "[renderer][uniform-upload]")
    {
        UniformUploadPageCursor cursor;
        UniformUploadPageCursor::Usage usage;
        const U64 previousFrame = U64_MAX - to_U64(Config::MAX_FRAMES_IN_FLIGHT);
        REQUIRE(cursor.beginFrame(previousFrame));
        const auto first = cursor.reserve();
        usage.markUsed(previousFrame);
        REQUIRE(cursor.release(first, usage));
        REQUIRE(cursor.beginFrame(U64_MAX));
        const auto recycled = cursor.reserve();
        CHECK(recycled._page == first._page);
        CHECK(recycled._slot == first._slot);
        CHECK(recycled._generation != first._generation);
        CHECK_FALSE(cursor.beginFrame(previousFrame));
    }

    TEST_CASE("Uniform block uploaders transfer rather than copy ownership", "[renderer][uniform-upload]")
    {
        STATIC_CHECK(std::is_move_constructible_v<UniformBlockUploader>);
        STATIC_CHECK_FALSE(std::is_copy_constructible_v<UniformBlockUploader>);
        STATIC_CHECK_FALSE(std::is_copy_assignable_v<UniformBlockUploader>);
    }

    TEST_CASE("Uniform page strides isolate non-coherent atoms without inflating the bound range", "[renderer][uniform-upload]")
    {
        size_t stride = 0u;
        REQUIRE(UniformUploadArena::CalculateSlotStride(17u, 65536u, 256u, 512u, stride));
        CHECK(stride == 512u);
        REQUIRE(UniformUploadArena::CalculateSlotStride(257u, 65536u, 256u, 1024u, stride));
        CHECK(stride == 1024u);
        REQUIRE(UniformUploadArena::CalculateSlotStride(61u, 65536u, 12u, 20u, stride));
        CHECK(stride == 120u);
        REQUIRE(UniformUploadArena::CalculateSlotStride(65536u, 65536u, 256u, 4096u, stride));
        CHECK(stride == 65536u);
        REQUIRE(UniformUploadArena::CalculateSlotStride(1u, 65536u, 256u, 131072u, stride));
        CHECK(stride == 131072u);
        REQUIRE(UniformUploadArena::CalculateSlotStride(1u, 65536u, 256u, 67108864u, stride));
        CHECK(stride == 67108864u);

        CHECK_FALSE(UniformUploadArena::CalculateSlotStride(65537u, 65536u, 256u, 1u, stride));
        CHECK(stride == 0u);
        CHECK_FALSE(UniformUploadArena::CalculateSlotStride(0u, 65536u, 256u, 1u, stride));
        CHECK_FALSE(UniformUploadArena::CalculateSlotStride(1u, 65536u, 0u, 1u, stride));
        CHECK_FALSE(UniformUploadArena::CalculateSlotStride(1u, 65536u, 256u, 0u, stride));
        CHECK_FALSE(UniformUploadArena::CalculateSlotStride(SIZE_MAX, SIZE_MAX, 2u, 1u, stride));
        CHECK_FALSE(UniformUploadArena::CalculateSlotStride(1u, SIZE_MAX, SIZE_MAX, 2u, stride));
        CHECK_FALSE(UniformUploadArena::CalculateSlotStride(1u, 65536u, 256u, 134217728u, stride));
    }

    TEST_CASE("Buffer range wait failures retain locks for a later retry", "[renderer][uniform-upload][lock-manager]")
    {
        LockableBuffer buffer;
        auto manager = std::make_unique<TestRangeLockManager>();
        TestRangeLockManager* managerPtr = manager.get();
        buffer._lockManager = MOV(manager);
        SyncObjectHandle sync = LockManager::CreateSyncObject(RenderAPI::None);
        REQUIRE(sync._id != SyncObjectHandle::INVALID_SYNC_ID);
        REQUIRE(buffer.lockRange({0u, 128u}, sync));

        CHECK(buffer.waitForLockedRange({128u, 128u}));
        CHECK(managerPtr->_waitCount == 0u);
        CHECK_FALSE(buffer.waitForLockedRange({0u, 64u}));
        CHECK(managerPtr->_waitCount == 1u);
        managerPtr->_failWait = false;
        CHECK(buffer.waitForLockedRange({0u, 64u}));
        CHECK(managerPtr->_waitCount == 2u);
        CHECK(buffer.waitForLockedRange({0u, 64u}));
        CHECK(managerPtr->_waitCount == 2u);
    }

    TEST_CASE("GPU resources are destroyed only after required completion succeeds", "[renderer][uniform-upload]")
    {
        vector<I32> events;
        const auto teardown = [&] { events.push_back(2); };

        SECTION("No arena requires no GPU wait")
        {
            REQUIRE(TeardownAfterGPUCompletion(false, [&] { events.push_back(1); return false; }, teardown));
            CHECK(events == vector<I32>{2});
        }

        SECTION("A completed wait permits teardown")
        {
            REQUIRE(TeardownAfterGPUCompletion(true, [&] { events.push_back(1); return true; }, teardown));
            CHECK((events == vector<I32>{1, 2}));
        }

        SECTION("A failed wait prevents teardown")
        {
            CHECK_FALSE(TeardownAfterGPUCompletion(true, [&] { events.push_back(1); return false; }, teardown));
            CHECK(events == vector<I32>{1});
        }
    }

    TEST_CASE("OpenGL uniform teardown retains fences if a context wait fails", "[renderer][uniform-upload][opengl]")
    {
        I32 firstFence = 1;
        I32 secondFence = 2;
        std::array<vector<I32*>, Config::MAX_FRAMES_IN_FLIGHT> groups{};
        groups[0].push_back(&firstFence);
        groups[1].push_back(&secondFence);
        vector<I32> events;

        const bool success = GLUtil::FinishUniformContextFences(
            std::span{groups},
            [&](const I32* fence)
            {
                events.push_back(*fence);
                return fence == &firstFence;
            },
            [&] { events.push_back(20); return true; },
            [&](I32*& fence)
            {
                events.push_back(30 + *fence);
                fence = nullptr;
            });

        CHECK_FALSE(success);
        const vector<I32> expected{1, 2};
        CHECK(events == expected);
        REQUIRE(groups[0].size() == 1u);
        REQUIRE(groups[1].size() == 1u);
        CHECK(groups[0][0] == &firstFence);
        CHECK(groups[1][0] == &secondFence);
    }

    TEST_CASE("OpenGL uniform teardown retains fences if the main context fails to finish", "[renderer][uniform-upload][opengl]")
    {
        I32 fence = 1;
        std::array<vector<I32*>, Config::MAX_FRAMES_IN_FLIGHT> groups{};
        groups[0].push_back(&fence);
        vector<I32> events;

        CHECK_FALSE(GLUtil::FinishUniformContextFences(
            std::span{groups},
            [&](const I32*) { events.push_back(1); return true; },
            [&] { events.push_back(2); return false; },
            [&](I32*&) { events.push_back(3); }));
        CHECK((events == vector<I32>{1, 2}));
        REQUIRE(groups[0].size() == 1u);
        CHECK(groups[0][0] == &fence);
    }

    TEST_CASE("OpenGL uniform teardown finishes the main context before releasing fences", "[renderer][uniform-upload][opengl]")
    {
        I32 fence = 1;
        std::array<vector<I32*>, Config::MAX_FRAMES_IN_FLIGHT> groups{};
        groups[0].push_back(&fence);
        groups[1].push_back(nullptr);
        U32 waitCount = 0u;
        U32 finishCount = 0u;
        U32 destroyCount = 0u;

        REQUIRE(GLUtil::FinishUniformContextFences(
            std::span{groups},
            [&](const I32*) { ++waitCount; return true; },
            [&] { ++finishCount; return true; },
            [&](I32*& entry) { ++destroyCount; entry = nullptr; }));
        CHECK(waitCount == 1u);
        CHECK(finishCount == 1u);
        CHECK(destroyCount == 1u);
        for (const auto& fences : groups)
        {
            CHECK(fences.empty());
        }
    }

    TEST_CASE("Vulkan frame-slot completion waits for every submitted window", "[renderer][uniform-upload][vulkan]")
    {
        std::array<FrameData, VKSwapChain::FrameSlotCount()> firstWindow{};
        std::array<FrameData, VKSwapChain::FrameSlotCount()> secondWindow{};
        for (size_t i = 0u; i < firstWindow.size(); ++i)
        {
            firstWindow[i]._renderFence._handle = reinterpret_cast<VkFence>(&firstWindow[i]);
            secondWindow[i]._renderFence._handle = reinterpret_cast<VkFence>(&secondWindow[i]);
        }

        const VkDevice device = reinterpret_cast<VkDevice>(&firstWindow);
        vector<VkFence> waited;
        const auto wait = [&](const VkDevice requestedDevice, const VkFence fence)
        {
            CHECK(requestedDevice == device);
            waited.push_back(fence);
            return VK_SUCCESS;
        };

        firstWindow[0]._submissionPending = true;
        secondWindow[0]._submissionPending = true;
        firstWindow[1]._submissionPending = true;

        REQUIRE(VKSwapChain::WaitForFrameSlot(firstWindow, Config::MAX_FRAMES_IN_FLIGHT, device, wait) == VK_SUCCESS);
        REQUIRE(VKSwapChain::WaitForFrameSlot(secondWindow, Config::MAX_FRAMES_IN_FLIGHT, device, wait) == VK_SUCCESS);
        REQUIRE(waited.size() == 2u);
        CHECK(waited[0] == firstWindow[0]._renderFence._handle);
        CHECK(waited[1] == secondWindow[0]._renderFence._handle);
        CHECK_FALSE(firstWindow[0]._submissionPending);
        CHECK_FALSE(secondWindow[0]._submissionPending);
        CHECK(firstWindow[1]._submissionPending);

        REQUIRE(VKSwapChain::WaitForFrameSlot(firstWindow, Config::MAX_FRAMES_IN_FLIGHT, device, wait) == VK_SUCCESS);
        CHECK(waited.size() == 2u);
        REQUIRE(VKSwapChain::WaitForFrameSlot(firstWindow, 1u, device, wait) == VK_SUCCESS);
        REQUIRE(waited.size() == 3u);
        CHECK(waited[2] == firstWindow[1]._renderFence._handle);
    }

    TEST_CASE("Vulkan frame-slot failures prevent unsafe reuse without waiting on unsignaled fences",
              "[renderer][uniform-upload][vulkan]")
    {
        std::array<FrameData, VKSwapChain::FrameSlotCount()> frames{};
        for (FrameData& frame : frames)
        {
            frame._renderFence._handle = reinterpret_cast<VkFence>(&frame);
        }

        const VkDevice device = reinterpret_cast<VkDevice>(&frames);
        U32 waitCount = 0u;
        const auto lostWait = [&](const VkDevice, const VkFence)
        {
            ++waitCount;
            return VK_ERROR_DEVICE_LOST;
        };
        const auto unexpectedWait = [&](const VkDevice, const VkFence)
        {
            ++waitCount;
            return VK_SUCCESS;
        };

        frames[0]._submissionPending = true;
        REQUIRE(VKSwapChain::WaitForFrameSlot(frames, 0u, device, lostWait) == VK_ERROR_DEVICE_LOST);
        CHECK(frames[0]._submissionPending);
        CHECK(frames[0]._frameError == VK_ERROR_DEVICE_LOST);
        CHECK(waitCount == 1u);
        CHECK(VKSwapChain::WaitForFrameSlot(frames, 1u, device, unexpectedWait) == VK_ERROR_DEVICE_LOST);
        CHECK(waitCount == 1u);

        frames[0]._frameError = VK_SUCCESS;
        frames[0]._submissionPending = false;
        frames[1]._frameError = VK_ERROR_OUT_OF_HOST_MEMORY;
        CHECK(VKSwapChain::WaitForFrameSlot(frames, 0u, device, unexpectedWait) == VK_ERROR_OUT_OF_HOST_MEMORY);
        CHECK(waitCount == 1u);

        frames[1]._frameError = VK_SUCCESS;
        CHECK(VKSwapChain::WaitForFrameSlot(std::span<FrameData>{}, 0u, VK_NULL_HANDLE, unexpectedWait) == VK_SUCCESS);
        CHECK(VKSwapChain::WaitForFrameSlot(std::span{frames}.first(frames.size() - 1u), 0u, device, unexpectedWait) ==
              VK_ERROR_INITIALIZATION_FAILED);
        CHECK(VKSwapChain::WaitForFrameSlot(frames, 0u, VK_NULL_HANDLE, unexpectedWait) == VK_ERROR_INITIALIZATION_FAILED);
        CHECK(waitCount == 1u);
    }

    TEST_CASE("Uniform slots are not recycled when a window completion fails", "[renderer][uniform-upload][vulkan]")
    {
        UniformUploadPageCursor cursor;
        UniformUploadPageCursor::Usage usage;
        REQUIRE(cursor.beginFrame(0u));
        const auto allocation = cursor.reserve();
        usage.markUsed(0u);
        REQUIRE(cursor.release(allocation, usage));

        std::array<FrameData, VKSwapChain::FrameSlotCount()> firstWindow{};
        std::array<FrameData, VKSwapChain::FrameSlotCount()> secondWindow{};
        firstWindow[0]._renderFence._handle = reinterpret_cast<VkFence>(&firstWindow[0]);
        secondWindow[0]._renderFence._handle = reinterpret_cast<VkFence>(&secondWindow[0]);
        firstWindow[0]._submissionPending = true;
        secondWindow[0]._submissionPending = true;
        const VkDevice device = reinterpret_cast<VkDevice>(&firstWindow);
        const auto wait = [&](const VkDevice, const VkFence fence)
        {
            return fence == firstWindow[0]._renderFence._handle ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
        };
        const U64 nextFrame = Config::MAX_FRAMES_IN_FLIGHT;
        const bool allCompleted =
            VKSwapChain::WaitForFrameSlot(firstWindow, nextFrame, device, wait) == VK_SUCCESS &&
            VKSwapChain::WaitForFrameSlot(secondWindow, nextFrame, device, wait) == VK_SUCCESS;
        CHECK_FALSE(allCompleted);
        if (allCompleted)
        {
            REQUIRE(cursor.beginFrame(nextFrame));
        }
        const auto next = cursor.reserve();
        CHECK((next._page != allocation._page || next._slot != allocation._slot));
        CHECK(secondWindow[0]._frameError == VK_ERROR_DEVICE_LOST);
    }
}
