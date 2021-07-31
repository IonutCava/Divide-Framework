#include "stdafx.h"

#include "Headers/Defines.h"
#include "Core/Time/Headers/ProfileTimer.h"

#include <atomic>

namespace Divide {

bool TaskPool::USE_OPTICK_PROFILER = false;

namespace {
    Mutex printLock{};

    void printLine(const char* line) {
        UniqueLock<Mutex> lock(printLock);
        std::cout << line << std::endl;
    };

    void printLine(const std::string& string) {
        printLine(string.c_str());
    }
};

TEST(TaskPoolContructionTest)
{
    Console::toggleErrorStream(false);
    TaskPool test;

    // Not enough workers
    bool init = test.init(0, TaskPool::TaskPoolType::TYPE_BLOCKING);
    CHECK_FALSE(init);

    // Valid
    init = test.init(1, TaskPool::TaskPoolType::TYPE_BLOCKING);
    CHECK_TRUE(init);

    // Double init
    init = test.init(HardwareThreadCount(), TaskPool::TaskPoolType::TYPE_BLOCKING);
    CHECK_FALSE(init);
}

TEST(ParallelForTest)
{
    Console::toggleErrorStream(false);

    TaskPool test;
    const bool init = test.init(HardwareThreadCount(), TaskPool::TaskPoolType::TYPE_BLOCKING);
    CHECK_TRUE(init);

    const U32 partitionSize = 4;
    const U32 loopCount = partitionSize * 4 + 2;

    std::atomic_uint loopCounter = 0;
    std::atomic_uint totalCounter = 0;

    ParallelForDescriptor descriptor = {};
    descriptor._iterCount = loopCount;
    descriptor._partitionSize = partitionSize;
    descriptor._cbk = [&totalCounter, &loopCounter](const Task* parentTask, const U32 start, const U32 end) {
        ACKNOWLEDGE_UNUSED(parentTask);

        ++loopCounter;
        for (U32 i = start; i < end; ++i) {
            ++totalCounter;
        }
    };

    parallel_for(test, descriptor);

    CHECK_EQUAL(loopCounter, 5u);
    CHECK_EQUAL(totalCounter, 18u);
}


TEST(TaskCallbackTest)
{
    TaskPool test;
    const bool init = test.init(to_U8(HardwareThreadCount()), TaskPool::TaskPoolType::TYPE_BLOCKING);
    CHECK_TRUE(init);

    bool testValue = false;

    Task* job = CreateTask([](const Task& parentTask) {
        ACKNOWLEDGE_UNUSED(parentTask);

        Time::ProfileTimer timer;
        timer.start();
        printLine("TaskCallbackTest: Thread sleeping for 500ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        timer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>(timer.get() - Time::ProfileTimer::overhead());
        printLine("TaskCallbackTest: Thread waking up (" + std::to_string(durationMS) + "ms )");
    });

    Start(*job, test, TaskPriority::DONT_CARE, [&testValue]() {
        printLine("TaskCallbackTest: Callback called!");
        testValue = true;
        printLine("TaskCallbackTest: Value changed to: [ " + std::string(testValue ? "true" : "false") + " ]!");
    });

    CHECK_FALSE(testValue);
    printLine("TaskCallbackTest: waiting for task!");
    Wait(*job, test);
    CHECK_TRUE(Finished(*job));
    CHECK_FALSE(testValue);

    printLine("TaskCallbackTest: flushing queue!");
    const size_t callbackCount = test.flushCallbackQueue();
    CHECK_EQUAL(callbackCount, 1u);
    printLine("TaskCallbackTest: flushing test! Value: " + std::string(testValue ? "true" : "false"));
    CHECK_TRUE(testValue);
}

namespace {
    class ThreadedTest {
      public:
        ThreadedTest() noexcept
        {
            std::atomic_init(&_testValue, false);
        }

        void setTestValue(const bool state) noexcept {
            printLine("ThreadedTest: Setting value to [ " + std::string(state ? "true" : "false") + " ]!");
            _testValue.store(state, std::memory_order_release);
        }

        [[nodiscard]] bool getTestValue() const noexcept{
            return _testValue.load(std::memory_order_acquire);
        }

        void threadedFunction(const Task& parentTask) {
            ACKNOWLEDGE_UNUSED(parentTask);
            Time::ProfileTimer timer;
            timer.start();
            printLine("ThreadedTest: Thread sleeping for 300ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            timer.stop();
            const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>(timer.get() - Time::ProfileTimer::overhead());
            printLine("ThreadedTest: Thread waking up (" + std::to_string(durationMS) + "ms )");
            setTestValue(true);
        }

      private:
        std::atomic_bool _testValue;
    };
}

TEST(TaskClassMemberCallbackTest)
{
    TaskPool test;
    const bool init = test.init(to_U8(HardwareThreadCount()), TaskPool::TaskPoolType::TYPE_BLOCKING);
    CHECK_TRUE(init);

    ThreadedTest testObj;

    Task* job = CreateTask([&testObj](const Task& parentTask) {
        printLine("TaskClassMemberCallbackTest: Threaded called!");
        testObj.threadedFunction(parentTask);
    });

    CHECK_FALSE(testObj.getTestValue());

    Start(*job, test, TaskPriority::DONT_CARE, [&testObj]() {
        printLine("TaskClassMemberCallbackTest: Callback called!");
        testObj.setTestValue(false);
    });

    CHECK_FALSE(testObj.getTestValue());

    printLine("TaskClassMemberCallbackTest: Waiting for task!");
    Wait(*job, test);

    CHECK_TRUE(testObj.getTestValue());

    printLine("TaskClassMemberCallbackTest: Flushing callback queue!");
    const size_t callbackCount = test.flushCallbackQueue();
    CHECK_EQUAL(callbackCount, 1u);

    const bool finalValue = testObj.getTestValue();

    printLine("TaskClassMemberCallbackTest: final value is [ " + std::string(finalValue ? "true" : "false") + " ] !");

    CHECK_FALSE(finalValue);
}

TEST(TaskSpeedTest)
{
    const U64 timerOverhead = Time::ProfileTimer::overhead();
    {
        TaskPool test;
        bool init = test.init(to_U8(HardwareThreadCount()), TaskPool::TaskPoolType::TYPE_BLOCKING);
        CHECK_TRUE(init);

        Time::ProfileTimer timer;

        timer.start();
        Task* job = CreateTask([](const Task&) { NOP();});

        for (std::size_t i = 0; i < 60 * 1000; ++i) {
            Start(*CreateTask(job,
                [](const Task& parentTask) {
                    ACKNOWLEDGE_UNUSED(parentTask);
                    NOP();
                }
            ), test);
        }

        StartAndWait(*job, test);

        timer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>(timer.get() - timerOverhead);
        printLine("Threading speed test (blocking): 60K tasks completed in: " + std::to_string(durationMS) + " ms.");
    }
    {
        TaskPool test;
        bool init = test.init(to_U8(HardwareThreadCount()), TaskPool::TaskPoolType::TYPE_LOCKFREE);
        CHECK_TRUE(init);

        Time::ProfileTimer timer;

        timer.start();
        Task* job = CreateTask(
            [](const Task& parentTask) {
                ACKNOWLEDGE_UNUSED(parentTask);
                NOP();
            }
        );

        for (std::size_t i = 0; i < 60 * 1000; ++i) {
            Start(*CreateTask(job,
                [](const Task& parentTask) {
                    ACKNOWLEDGE_UNUSED(parentTask);
                    NOP();
                }
            ), test);
        }

        StartAndWait(*job, test);
        timer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>(timer.get() - timerOverhead);
        printLine("Threading speed test (lockfree): 60K tasks completed in: " + std::to_string(durationMS) + " ms.");
    }
    {
        TaskPool test;
        bool init = test.init(to_U8(HardwareThreadCount()), TaskPool::TaskPoolType::TYPE_BLOCKING);
        CHECK_TRUE(init);

        constexpr U32 partitionSize = 256;
        constexpr U32 loopCount = partitionSize * 8192 + 2;

        Time::ProfileTimer timer;
        timer.start();

        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = loopCount;
        descriptor._partitionSize = partitionSize;
        descriptor._useCurrentThread = false;
        descriptor._cbk = [](const Task* parentTask, const U32 start, const U32 end) {
            ACKNOWLEDGE_UNUSED(parentTask);
            ACKNOWLEDGE_UNUSED(start);
            ACKNOWLEDGE_UNUSED(end);
            NOP();
        };

        parallel_for(test, descriptor);
        timer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>(timer.get() - timerOverhead);
        printLine("Threading speed test (parallel_for - blocking): 8192 + 1 partitions tasks completed in: " + std::to_string(durationMS) + " ms.");
    }
    {
        TaskPool test;
        bool init = test.init(to_U8(HardwareThreadCount()), TaskPool::TaskPoolType::TYPE_BLOCKING);
        CHECK_TRUE(init);

        constexpr U32 partitionSize = 256;
        constexpr U32 loopCount = partitionSize * 8192 + 2;

        Time::ProfileTimer timer;
        timer.start();

        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = loopCount;
        descriptor._partitionSize = partitionSize;
        descriptor._useCurrentThread = true;
        descriptor._cbk = [](const Task* parentTask, const U32 start, const U32 end) {
            ACKNOWLEDGE_UNUSED(parentTask);
            ACKNOWLEDGE_UNUSED(start);
            ACKNOWLEDGE_UNUSED(end);
            NOP();
        };

        parallel_for(test, descriptor);

        timer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>(timer.get() - timerOverhead);
        printLine("Threading speed test (parallel_for - blocking - use current thread): 8192 + 1 partitions tasks completed in: " + std::to_string(durationMS) + " ms.");
    }
    {
        TaskPool test;
        bool init = test.init(to_U8(HardwareThreadCount()), TaskPool::TaskPoolType::TYPE_LOCKFREE);
        CHECK_TRUE(init);

        constexpr U32 partitionSize = 256;
        constexpr U32 loopCount = partitionSize * 8192 + 2;

        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = loopCount;
        descriptor._partitionSize = partitionSize;
        descriptor._useCurrentThread = false;
        descriptor._cbk = [](const Task* parentTask, const U32 start, const U32 end) {
            ACKNOWLEDGE_UNUSED(parentTask);
            ACKNOWLEDGE_UNUSED(start);
            ACKNOWLEDGE_UNUSED(end);
            NOP();
        };

        Time::ProfileTimer timer;
        timer.start();
        parallel_for(test, descriptor);
        timer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>(timer.get() - timerOverhead);
        printLine("Threading speed test (parallel_for - lockfree): 8192 + 1 partitions tasks completed in: " + std::to_string(durationMS) + " ms.");
    }
    {
        TaskPool test;
        bool init = test.init(to_U8(HardwareThreadCount()), TaskPool::TaskPoolType::TYPE_LOCKFREE);
        CHECK_TRUE(init);

        constexpr U32 partitionSize = 256;
        constexpr U32 loopCount = partitionSize * 8192 + 2;

        Time::ProfileTimer timer;
        timer.start();

        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = loopCount;
        descriptor._partitionSize = partitionSize;
        descriptor._useCurrentThread = true;
        descriptor._cbk = [](const Task* parentTask, const U32 start, const U32 end) {
            ACKNOWLEDGE_UNUSED(parentTask);
            ACKNOWLEDGE_UNUSED(start);
            ACKNOWLEDGE_UNUSED(end);

            NOP();
        };

        parallel_for(test, descriptor);

        timer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>(timer.get() - timerOverhead);
        printLine("Threading speed test (parallel_for - lockfree - use current thread): 8192 + 1 partitions tasks completed in: " + std::to_string(durationMS) + " ms.");
    }
}

TEST(TaskPriorityTest)
{
    TaskPool test;
    const bool init = test.init(to_U8(HardwareThreadCount()), TaskPool::TaskPoolType::TYPE_BLOCKING);
    CHECK_TRUE(init);

    U32 callbackValue = 0u;

    Task* job = CreateTask([&callbackValue](const Task& /*parentTask*/ ) {
        ++callbackValue;
    });

    Start(*job, test, TaskPriority::DONT_CARE, [&callbackValue]() {
        ++callbackValue;
    });

    //StartAndWait calls Wait after Start which MAY call flushCallbackQueue on its own while waiting for the task to finish
    //Manually waiting fot the task to finish guarantees that we won't call the main thread callback before the CHECK_EQUAL
    WAIT_FOR_CONDITION(Finished(*job));
    CHECK_EQUAL(callbackValue, 1u);

    size_t callbackCount = test.flushCallbackQueue();
    CHECK_EQUAL(callbackCount, 1u);
    CHECK_EQUAL(callbackValue, 2u);

    job = CreateTask([&callbackValue](const Task& /*parentTask*/ ) {
        ++callbackValue;
    });

    // Since we don't have a main thread callback, it's OK to call StartAndWait here
    StartAndWait(*job, test);
    CHECK_EQUAL(callbackValue, 3u);

    callbackCount = test.flushCallbackQueue();
    CHECK_EQUAL(callbackCount, 0u);
    CHECK_EQUAL(callbackValue, 3u);

    job = CreateTask([&callbackValue](const Task& /*parentTask*/ ) {
        ++callbackValue;
    });
    StartAndWait(*job, test, TaskPriority::REALTIME, [&callbackValue]() {
        ++callbackValue;
    });
    CHECK_EQUAL(callbackValue, 5u);
}

} //namespace Divide
