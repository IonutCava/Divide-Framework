# Copyright (c) 2026 DIVIDE-Studio
# Copyright (c) 2009-2026 Ionut Cava
# SPDX-License-Identifier: MIT

"""Regression tests for exposure histogram initialization and buffer visibility."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
HOST = ROOT / "Source" / "Rendering" / "PostFX" / "PreRenderBatch.cpp"
VULKAN = ROOT / "Source" / "Platform" / "Video" / "RenderBackend" / "Vulkan"
OPENGL = ROOT / "Source" / "Platform" / "Video" / "RenderBackend" / "OpenGL"


def run_cpp(test, source):
    candidates = [os.environ.get("CXX"), "c++", "clang++", "g++", "cl"]
    compiler = None
    for name in candidates:
        if name:
            compiler = shutil.which(name)
            if compiler:
                break
    if not compiler:
        test.skipTest("A C++ compiler is required for buffer initialization tests")
    with tempfile.TemporaryDirectory() as directory:
        directory = Path(directory)
        input_path = directory / "test.cpp"
        executable = directory / ("test.exe" if os.name == "nt" else "test")
        input_path.write_text(source, encoding="utf-8")
        if Path(compiler).stem.lower() == "cl":
            args = [compiler, "/nologo", "/std:c++20", "/EHsc", str(input_path),
                    f"/Fe:{executable}", f"/Fo:{directory / 'test.obj'}"]
        else:
            args = [compiler, "-std=c++20", "-o", str(executable), str(input_path)]
        compile_result = subprocess.run(args, cwd=directory, capture_output=True, text=True, encoding="utf-8")
        test.assertEqual(compile_result.returncode, 0, compile_result.stdout + compile_result.stderr)
        result = subprocess.run([str(executable)], cwd=directory, capture_output=True, text=True, encoding="utf-8")
        test.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class ExposureBufferTests(unittest.TestCase):
    def test_histogram_workgroup_synchronization(self):
        shader = (ROOT / "Assets" / "Shaders" / "GLSL" / "luminanceCalc.glsl").read_text(encoding="utf-8")
        create, average = shader.split("--Compute.Average", 1)
        self.assertRegex(create, r"histogramShared\[gl_LocalInvocationIndex\]\s*=\s*0;\s*barrier\(\);")
        self.assertRegex(
            create,
            r"atomicAdd\(histogramShared\[binIndex\],\s*1\);[\s\S]*?\}\s*"
            r"(?://[^\n]*\n\s*)*barrier\(\);[\s\S]*?"
            r"atomicAdd\(histogram\[gl_LocalInvocationIndex\],",
        )
        self.assertRegex(average, r"shared\s+float\s+histogramShared\[GROUP_SIZE\];")
        self.assertRegex(
            average,
            r"histogramShared\[gl_LocalInvocationIndex\]\s*=\s*"
            r"float\(countForThisBin\)\s*\*\s*float\(gl_LocalInvocationIndex\);\s*barrier\(\);",
        )
        self.assertRegex(
            average,
            r"histogramShared\[gl_LocalInvocationIndex\]\s*\+=\s*"
            r"histogramShared\[gl_LocalInvocationIndex\s*\+\s*binIndex\];\s*\}\s*barrier\(\);",
        )
        self.assertNotIn("groupMemoryBarrier()", create + average)

    def test_exposure_image_read_write_contract(self):
        host = HOST.read_text(encoding="utf-8")
        average = host[host.index("// Averaging pass"):host.index("if ( _adaptiveExposureValueNeedsUpdate )")]
        self.assertRegex(
            average,
            r"\._sourceLayout\s*=\s*ImageUsage::SHADER_READ,\s*"
            r"\._targetLayout\s*=\s*ImageUsage::SHADER_READ_WRITE,",
        )
        self.assertRegex(average, r"Set\(binding\._data,\s*luminanceView,\s*ImageUsage::SHADER_READ_WRITE\s*\);")
        self.assertRegex(
            average,
            r"\._sourceLayout\s*=\s*ImageUsage::SHADER_READ_WRITE,\s*"
            r"\._targetLayout\s*=\s*ImageUsage::SHADER_READ,",
        )

    def test_histogram_initialization_and_dependencies_are_wired(self):
        host = HOST.read_text(encoding="utf-8")
        shader = (ROOT / "Assets" / "Shaders" / "GLSL" / "luminanceCalc.glsl").read_text(encoding="utf-8")
        vulkan = (VULKAN / "Buffers" / "vkBufferImpl.cpp").read_text(encoding="utf-8")
        vk_wrapper = (VULKAN / "VKWrapper.cpp").read_text(encoding="utf-8")
        gl_wrapper = (OPENGL / "GLWrapper.cpp").read_text(encoding="utf-8")
        gl_buffer = (OPENGL / "Buffers" / "glBufferImpl.cpp").read_text(encoding="utf-8")

        self.assertRegex(host, r"constexpr U32 HISTOGRAM_BIN_COUNT\s*=\s*256u;")
        self.assertIn("static_assert(GROUP_X_THREADS * GROUP_Y_THREADS == HISTOGRAM_BIN_COUNT);", host)
        self.assertRegex(
            host,
            r"std::array<U32,\s*HISTOGRAM_BIN_COUNT> initialHistogram\{\};[\s\S]*?"
            r"bufferDescriptor\._elementCount\s*=\s*HISTOGRAM_BIN_COUNT;[\s\S]*?"
            r"bufferDescriptor\._initialData\s*=\s*\{\s*initialHistogram\.data\(\),\s*"
            r"initialHistogram\.size\(\)\s*\*\s*sizeof\(U32\)\s*\};[\s\S]*?"
            r"_histogramBuffer\s*=\s*_context\.newShaderBuffer\(bufferDescriptor\);",
        )
        self.assertIn("GL_MAP_COHERENT_BIT", gl_buffer)
        self.assertRegex(shader, r"histogram\[gl_LocalInvocationIndex\]\s*=\s*0;")

        execute = host[host.index("void PreRenderBatch::execute"):]
        create = execute[execute.index("// Histogram Pass"):execute.index("// Averaging pass")]
        average = execute[execute.index("// Averaging pass"):execute.index("if ( _adaptiveExposureValueNeedsUpdate )")]
        for name, dispatch in (("create", create), ("average", average)):
            with self.subTest(dispatch=name):
                self.assertRegex(
                    dispatch,
                    r"DispatchShaderTaskCommand[\s\S]*?"
                    r"\._type\s*=\s*BufferSyncUsage::GPU_WRITE_TO_GPU_WRITE,\s*"
                    r"\._buffer\s*=\s*_histogramBuffer->getBufferImpl\(\)",
                )
        self.assertRegex(
            vulkan,
            r"case BufferUsageType::UNBOUND_BUFFER:\s*\{[\s\S]*?"
            r"dstAccessMask\s*=\s*VK_ACCESS_2_SHADER_READ_BIT\s*\|\s*VK_ACCESS_2_SHADER_WRITE_BIT;",
        )
        constructor = vulkan[vulkan.index("vkBufferImpl::vkBufferImpl"):vulkan.index("BufferLock vkBufferImpl::writeBytes")]
        self.assertLess(constructor.index("vmaFlushAllocation"), constructor.index("SubmitTransferRequest"))
        self.assertIn("request.dstAccessMask = dstAccessMask;", constructor)
        self.assertIn("request.dstStageMask = dstStageMask;", constructor)
        self.assertIn("const bool useStaging = !_isMemoryMappable || !hasHostAccess;", constructor)
        self.assertIn("VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT", constructor)
        self.assertIn("VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT", constructor)
        self.assertRegex(
            vk_wrapper,
            r"case BufferSyncUsage::GPU_WRITE_TO_GPU_WRITE:\s*\{[\s\S]*?"
            r"dstAccessMask\s*=\s*VK_ACCESS_2_SHADER_WRITE_BIT\s*\|\s*VK_ACCESS_2_SHADER_READ_BIT;",
        )
        self.assertRegex(
            gl_wrapper,
            r"case BufferSyncUsage::GPU_WRITE_TO_GPU_WRITE:\s*\{"
            r"\s*mask\s*\|=\s*gl46core::GL_SHADER_STORAGE_BARRIER_BIT;",
        )
        for backend, prefix in ((OPENGL, "gl"), (VULKAN, "vk")):
            with self.subTest(backend=prefix):
                wrapper = (backend / ("GLWrapper.cpp" if prefix == "gl" else "VKWrapper.cpp")).read_text(encoding="utf-8")
                shader_buffer = (backend / "Buffers" / f"{prefix}ShaderBuffer.cpp").read_text(encoding="utf-8")
                self.assertIn(f"std::make_unique<{prefix}ShaderBuffer>( _context, descriptor )", wrapper)
                self.assertIn("descriptor._initialData", shader_buffer)

    def test_opengl_reused_allocations_start_with_zero_bins(self):
        host = HOST.read_text(encoding="utf-8")
        begin = host.index("    std::array<U32, HISTOGRAM_BIN_COUNT> initialHistogram{};")
        end = host.index("    WAIT_FOR_CONDITION(loadTasks.load() == 0);", begin)
        constructor = host[begin:end]
        constants = "\n".join(re.findall(
            r"constexpr U(?:8|32)\s+(?:GROUP_X_THREADS|GROUP_Y_THREADS|HISTOGRAM_BIN_COUNT)\s*=\s*\d+u;",
            host,
        ))
        self.assertEqual(len(constants.splitlines()), 3)
        gl_allocator = (OPENGL / "Buffers" / "glMemoryManager.cpp").read_text(encoding="utf-8")
        copy = re.search(r"memcpy\(block\._ptr, initialData\.first, initialData\.second\);", gl_allocator)
        self.assertIsNotNone(copy)

        source = r"""
#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
using U8 = std::uint8_t;
using U32 = std::uint32_t;
@CONSTANTS@
enum class BufferUsageType { UNBOUND_BUFFER };
enum class BufferUpdateFrequency { ONCE };
struct ShaderBufferDescriptor {
    std::string _name;
    unsigned _ringBufferLength = 1;
    U32 _elementCount = 0;
    std::size_t _elementSize = 0;
    BufferUsageType _usageType{};
    BufferUpdateFrequency _updateFrequency{};
    std::pair<void*, std::size_t> _initialData{};
};
struct FakeContext {
    std::array<U32, HISTOGRAM_BIN_COUNT + 2>& memory;
    int newShaderBuffer(const ShaderBufferDescriptor& descriptor) {
        assert(descriptor._name == "LUMINANCE_HISTOGRAM_BUFFER");
        assert(descriptor._ringBufferLength == 0);
        assert(descriptor._elementCount == HISTOGRAM_BIN_COUNT);
        assert(descriptor._elementSize == sizeof(U32));
        assert(descriptor._initialData.first != nullptr);
        assert(descriptor._initialData.second == HISTOGRAM_BIN_COUNT * sizeof(U32));
        struct Block { void* _ptr; } block{memory.data() + 1};
        const auto initialData = descriptor._initialData;
        @GL_COPY@
        return 1;
    }
};
struct HistogramOwner {
    FakeContext& _context;
    int _histogramBuffer = 0;
    void create() {
        @HOST_CONSTRUCTOR@
    }
};
int main() {
    std::array<U32, HISTOGRAM_BIN_COUNT + 2> memory{};
    FakeContext context{memory};
    for (int lifetime = 0; lifetime < 3; ++lifetime) {
        memory.fill(0xdeadbeefu);
        {
            HistogramOwner owner{context};
            owner.create();
            assert(owner._histogramBuffer == 1);
        }
        assert(memory.front() == 0xdeadbeefu && memory.back() == 0xdeadbeefu);
        for (std::size_t i = 1; i <= HISTOGRAM_BIN_COUNT; ++i) {
            assert(memory[i] == 0u);
        }
        ++memory[1];
        memory[HISTOGRAM_BIN_COUNT] += 3u;
        assert(memory[1] == 1u && memory[HISTOGRAM_BIN_COUNT] == 3u);
    }
}
"""
        source = source.replace("@CONSTANTS@", constants).replace("@GL_COPY@", copy.group(0))
        run_cpp(self, source.replace("@HOST_CONSTRUCTOR@", constructor))

    def test_vulkan_initial_upload_visibility_for_memory_types(self):
        vulkan = (VULKAN / "Buffers" / "vkBufferImpl.cpp").read_text(encoding="utf-8")
        constructor = vulkan.index("vkBufferImpl::vkBufferImpl")
        begin = vulkan.index("        const bool hasHostAccess =", constructor)
        end = vulkan.index(
            '        if ( useStaging )\n        {\n            const auto scopeName', begin,
        )
        initialization = vulkan[begin:end]

        source = r"""
#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>
using Byte = std::uint8_t;
using U32 = std::uint32_t;
using bufferPtr = void*;
constexpr U32 VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT = 1u;
constexpr U32 VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT = 2u;
struct VmaAllocationCreateInfo { U32 flags = 0u; };
struct Mutex {};
template<typename T> struct LockGuard { explicit LockGuard(T&) {} };
struct AllocatorInstance {
    int value = 0;
    int* _allocator = &value;
    Mutex _allocatorLock;
};
struct Tracker { AllocatorInstance _allocatorInstance; };
struct VK_API {
    static Tracker& GetStateTracker() { static Tracker tracker; return tracker; }
};
struct Allocation {
    std::vector<Byte> host;
    std::vector<Byte> visible;
    std::vector<int> events;
    bool mapped = false;
    explicit Allocation(std::size_t size) : host(size, 0xa5), visible(size, 0xa5) {}
};
int vmaMapMemory(int, Allocation* allocation, void** data) {
    assert(!allocation->mapped);
    allocation->mapped = true;
    allocation->events.push_back(1);
    *data = allocation->host.data();
    return 0;
}
int vmaFlushAllocation(int, Allocation* allocation, std::size_t offset, std::size_t size) {
    assert(allocation->mapped && offset + size <= allocation->host.size());
    allocation->events.push_back(2);
    std::copy_n(allocation->host.begin() + offset, size, allocation->visible.begin() + offset);
    return 0;
}
void vmaUnmapMemory(int, Allocation* allocation) {
    assert(allocation->mapped);
    allocation->mapped = false;
    allocation->events.push_back(3);
}
#define VK_CHECK(call) do { assert((call) == 0); } while (false)
struct AllocationInfo { void* pMappedData = nullptr; };
struct Staging {
    Allocation storage;
    Allocation* _allocation;
    AllocationInfo _allocInfo;
    explicit Staging(std::size_t size)
        : storage(size), _allocation(&storage), _allocInfo{storage.host.data()} {
        storage.mapped = true;
    }
};
namespace VKUtil {
    std::unique_ptr<Staging> createStagingBuffer(std::size_t size, const char*, bool) {
        return std::make_unique<Staging>(size);
    }
}
struct Upload {
    Allocation* _allocation;
    bool _isMemoryMappable;
    bool _isMemoryCoherent;
    bool _hasHostAccess;
    AllocationInfo _allocInfo;
    std::size_t _alignedBufferSize;
    std::size_t _totalBufferSize;
    std::unique_ptr<Staging> _stagingBuffer;
    Upload(Allocation& allocation, bool mappable, bool coherent, bool persistent,
           bool hostAccess, std::size_t size)
        : _allocation(&allocation), _isMemoryMappable(mappable), _isMemoryCoherent(coherent),
          _hasHostAccess(hostAccess), _allocInfo{persistent ? allocation.host.data() : nullptr},
          _alignedBufferSize(size), _totalBufferSize(allocation.host.size()) {
        allocation.mapped = persistent;
    }
    void initialize(std::pair<bufferPtr, std::size_t> initialData, std::size_t ringQueueLength) {
        const char* bufferName = "histogram";
        const VmaAllocationCreateInfo vmaallocInfo{
            _hasHostAccess ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT : 0u
        };
        @VULKAN_INITIALIZATION@
        if (useStaging) {
            assert(_stagingBuffer->storage.events == std::vector<int>{2});
            _allocation->visible = _stagingBuffer->storage.visible;
            if (_isMemoryMappable && _isMemoryCoherent) {
                _allocation->host = _allocation->visible;
            }
        }
    }
};
int main() {
    constexpr std::size_t size = 256 * sizeof(U32);
    constexpr std::size_t rings = 2;
    struct Case { bool mappable, coherent, persistent, hostAccess; };
    const std::array<Case, 7> cases{{
        {true, true, true, true}, {true, false, true, true}, {true, true, false, true},
        {true, false, false, true}, {false, false, false, false},
        {true, true, false, false}, {true, false, false, false}
    }};
    for (const Case c : cases) {
        for (const std::size_t stride : {size, size * 2}) {
            Allocation allocation(stride * rings);
            for (int lifetime = 0; lifetime < 4; ++lifetime) {
                std::fill(allocation.host.begin(), allocation.host.end(), 0xa5);
                std::fill(allocation.visible.begin(), allocation.visible.end(), 0xa5);
                allocation.events.clear();
                std::array<U32, 256> initial{};
                if (lifetime == 3) {
                    initial.front() = 0x12345678u;
                    initial.back() = 0x9abcdef0u;
                }
                const auto initialData = lifetime == 2
                    ? std::pair<bufferPtr, std::size_t>{nullptr, 0u}
                    : std::pair<bufferPtr, std::size_t>{initial.data(), size};
                Upload upload(allocation, c.mappable, c.coherent, c.persistent, c.hostAccess, stride);
                upload.initialize(initialData, rings);
                const auto& visible = c.mappable && c.coherent ? allocation.host : allocation.visible;
                for (std::size_t ring = 0; ring < rings; ++ring) {
                    assert(std::memcmp(visible.data() + ring * stride, initial.data(), size) == 0);
                }
                const std::vector<int> expected = (!c.mappable || !c.hostAccess) ? std::vector<int>{}
                    : c.persistent ? (c.coherent ? std::vector<int>{} : std::vector<int>{2})
                    : c.coherent ? std::vector<int>{1, 3} : std::vector<int>{1, 2, 3};
                assert(allocation.events == expected);
                assert(allocation.mapped == c.persistent);
            }
        }
    }
}
"""
        run_cpp(self, source.replace("@VULKAN_INITIALIZATION@", initialization))

    def test_vulkan_readback_allocation_requests_mapped_random_access(self):
        vulkan = (VULKAN / "Buffers" / "vkBufferImpl.cpp").read_text(encoding="utf-8")
        constructor = vulkan[vulkan.index("vkBufferImpl::vkBufferImpl"):vulkan.index("BufferLock vkBufferImpl::writeBytes")]
        self.assertRegex(
            constructor,
            r"if \( _params\._hostVisible \)\s*\{[^}]*?"
            r"vmaallocInfo\.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT \| VMA_ALLOCATION_CREATE_MAPPED_BIT;",
        )
        self.assertLess(constructor.index("if ( _params._hostVisible )"),
                        constructor.index("_params._updateFrequency != BufferUpdateFrequency::ONCE"))
        shader_buffer = (ROOT / "Source" / "Platform" / "Video" / "Buffers" / "ShaderBuffer" / "ShaderBuffer.cpp").read_text(encoding="utf-8")
        self.assertRegex(shader_buffer, r"void ShaderBuffer::readBytes[\s\S]*?_params\._hostVisible")
        vk_wrapper = (VULKAN / "VKWrapper.cpp").read_text(encoding="utf-8")
        self.assertRegex(
            vk_wrapper,
            r"case BufferSyncUsage::GPU_WRITE_TO_CPU_READ:\s*\{[^}]*?"
            r"dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;\s*"
            r"memoryBarrier\.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;",
        )

    def test_vulkan_readback_visibility_for_memory_types(self):
        vulkan = (VULKAN / "Buffers" / "vkBufferImpl.cpp").read_text(encoding="utf-8")
        begin = vulkan.index("void vkBufferImpl::readBytes")
        begin = vulkan.index("{", begin) + 1
        end = vulkan.index("\n    }\n\n    namespace VKUtil", begin)
        readback = vulkan[begin:end]

        source = r"""
#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>
using Byte = std::uint8_t;
using U32 = std::uint32_t;
using bufferPtr = void*;
struct Mutex {};
template<typename T> struct LockGuard { explicit LockGuard(T&) {} };
struct AllocatorInstance {
    int value = 0;
    int* _allocator = &value;
    Mutex _allocatorLock;
};
struct Tracker { AllocatorInstance _allocatorInstance; };
struct VK_API {
    static Tracker& GetStateTracker() { static Tracker tracker; return tracker; }
};
// "device" holds GPU writes made available to the host domain; "host" is the CPU's view of the mapping.
struct Allocation {
    std::vector<Byte> device;
    std::vector<Byte> host;
    std::vector<int> events;
    bool mapped = false;
};
int vmaMapMemory(int, Allocation* allocation, void** data) {
    assert(!allocation->mapped);
    allocation->mapped = true;
    allocation->events.push_back(1);
    *data = allocation->host.data();
    return 0;
}
int vmaInvalidateAllocation(int, Allocation* allocation, std::size_t offset, std::size_t size) {
    assert(allocation->mapped && offset + size <= allocation->host.size());
    allocation->events.push_back(2);
    std::copy_n(allocation->device.begin() + offset, size, allocation->host.begin() + offset);
    return 0;
}
void vmaUnmapMemory(int, Allocation* allocation) {
    assert(allocation->mapped);
    allocation->mapped = false;
    allocation->events.push_back(3);
}
#define VK_CHECK(call) do { assert((call) == 0); } while (false)
#define DIVIDE_ASSERT(expression, message) (expression)
struct AllocationInfo { void* pMappedData = nullptr; };
struct Params { bool _hostVisible = true; };
struct Range { std::size_t _startOffset; std::size_t _length; };
struct Readback {
    Allocation* _allocation;
    Params _params;
    bool _isMemoryMappable;
    bool _isMemoryCoherent;
    AllocationInfo _allocInfo;
    void readBytes(const Range range, std::pair<bufferPtr, std::size_t> outData) {
        @VULKAN_READBACK@
    }
};
int main() {
    constexpr std::size_t size = 64;
    struct Case { bool hostVisible, mappable, coherent, persistent; };
    const std::array<Case, 6> cases{{
        {true, true, true, true}, {true, true, false, true}, {true, true, true, false},
        {true, true, false, false}, {true, false, false, false}, {false, true, true, true}
    }};
    for (const Case c : cases) {
        for (const Range range : {Range{0u, size}, Range{16u, 32u}}) {
            Allocation allocation;
            allocation.device.resize(size);
            for (std::size_t i = 0; i < size; ++i) {
                allocation.device[i] = static_cast<Byte>(i * 3u + 1u);
            }
            allocation.host = c.coherent ? allocation.device : std::vector<Byte>(size, 0xa5);
            allocation.mapped = c.persistent;
            Readback readback{&allocation, {c.hostVisible}, c.mappable, c.coherent,
                              {c.persistent ? allocation.host.data() : nullptr}};
            std::vector<Byte> out(range._length, 0xcc);
            readback.readBytes(range, {out.data(), out.size()});

            const bool readable = c.hostVisible && c.mappable;
            if (!readable) {
                assert(std::all_of(out.begin(), out.end(), [](Byte b) { return b == 0u; }));
                assert(allocation.events.empty());
                continue;
            }
            assert(std::equal(out.begin(), out.end(), allocation.device.begin() + range._startOffset));
            std::vector<int> expected;
            if (!c.persistent) expected.push_back(1);
            if (!c.coherent) expected.push_back(2);
            if (!c.persistent) expected.push_back(3);
            assert(allocation.events == expected);
            assert(allocation.mapped == c.persistent);
        }
    }
}
"""
        run_cpp(self, source.replace("@VULKAN_READBACK@", readback))

    def test_shader_buffer_readback_is_bounded_per_ring_slot(self):
        shared = (ROOT / "Source" / "Platform" / "Video" / "Buffers" / "ShaderBuffer" / "ShaderBuffer.cpp").read_text(encoding="utf-8")
        begin = shared.index("void ShaderBuffer::readBytes(")
        end = shared.index("\nBufferLock ShaderBuffer::clearBytes(", begin)
        readback = shared[begin:end]

        def backend_body(path, name):
            source = path.read_text(encoding="utf-8")
            begin = source.index("{", source.index(f"void {name}::readBytesInternal")) + 1
            return source[begin:source.index("\n    }", begin)]

        gl = backend_body(OPENGL / "Buffers" / "glShaderBuffer.cpp", "glShaderBuffer")
        vk = backend_body(VULKAN / "Buffers" / "vkShaderBuffer.cpp", "vkShaderBuffer")
        source = r"""
#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
using U16 = std::uint16_t;
using bufferPtr = void*;
template<typename T = std::size_t> struct BufferRange { T _startOffset, _length; };
template<typename T> constexpr std::size_t to_size(T value) { return static_cast<std::size_t>(value); }
enum class BufferUsageType { UNBOUND_BUFFER, CONSTANT_BUFFER };
namespace Util {
    std::size_t GetAlignmentCorrected(std::size_t value, std::size_t alignment) {
        return (value + alignment - 1u) / alignment * alignment;
    }
}
unsigned assertionFailures = 0;
#define PROFILE_SCOPE_AUTO(...) do {} while (false)
#define DIVIDE_ASSERT(expression, ...) ((expression) ? true : (++assertionFailures, false))
#define DIVIDE_EXPECTED_CALL(expression) assert(expression)
struct Mutex {};
template<typename T> struct LockGuard { explicit LockGuard(T&) {} };
struct Params { bool _hostVisible = true; };
struct StubBuffer {
    BufferRange<> last{};
    unsigned calls = 0;
    bool waitForLockedRange(BufferRange<>) { return true; }
    void readBytes(std::size_t offset, std::size_t length, std::pair<bufferPtr, std::size_t>) {
        last = {offset, length};
        ++calls;
    }
    void readBytes(BufferRange<> range, std::pair<bufferPtr, std::size_t> out) {
        readBytes(range._startOffset, range._length, out);
    }
};
struct ShaderBuffer {
    Params _params;
    BufferUsageType usage = BufferUsageType::UNBOUND_BUFFER;
    std::size_t _alignedBufferSize = 64u;
    std::size_t _alignmentRequirement = 16u;
    U16 slot = 0;
    U16 queueIndex() const { return slot; }
    U16 queueLength() const { return 4; }
    BufferUsageType getUsage() const { return usage; }
    void readBytes(BufferRange<> range, std::pair<bufferPtr, std::size_t> outData);
    void readBytes(BufferRange<> range, std::pair<bufferPtr, std::size_t> outData, U16 queueIndex);
    virtual void readBytesInternal(BufferRange<> range, std::pair<bufferPtr, std::size_t> outData) = 0;
};
@SHARED_READBACK@
struct GLBuffer : ShaderBuffer {
    StubBuffer impl;
    StubBuffer* bufferImpl() { return &impl; }
    void readBytesInternal(BufferRange<> range, std::pair<bufferPtr, std::size_t> outData) override {
        @GL_READBACK@
    }
};
struct VKBuffer : ShaderBuffer {
    StubBuffer impl;
    StubBuffer* _bufferImpl = &impl;
    Mutex _implLock;
    void readBytesInternal(BufferRange<> range, std::pair<bufferPtr, std::size_t> outData) override {
        @VK_READBACK@
    }
};
template<typename T> void Check(T& buffer) {
    std::array<std::uint8_t, 64> output{};
    for (U16 slot = 0; slot < 4; ++slot) {
        buffer.slot = slot;
        buffer.readBytes({0u, 16u}, {output.data(), output.size()});
        assert(buffer.impl.last._startOffset == slot * 64u && buffer.impl.last._length == 16u);
        buffer.readBytes({48u, 32u}, {output.data(), output.size()});
        assert(buffer.impl.last._startOffset == slot * 64u + 48u && buffer.impl.last._length == 16u);
        buffer.readBytes({16u, 32u}, {output.data(), 8u});
        assert(buffer.impl.last._startOffset == slot * 64u + 16u && buffer.impl.last._length == 8u);
    }
    const unsigned calls = buffer.impl.calls;
    unsigned failures = assertionFailures;
    buffer.readBytes({64u, 16u}, {output.data(), output.size()});
    assert(buffer.impl.calls == calls && assertionFailures == ++failures);
    buffer.readBytes({0u, 16u}, {nullptr, output.size()});
    assert(buffer.impl.calls == calls && assertionFailures == ++failures);
    buffer.readBytes({0u, 16u}, {output.data(), 0u});
    assert(buffer.impl.calls == calls && assertionFailures == ++failures);
    buffer.readBytes({0u, 0u}, {output.data(), output.size()});
    assert(buffer.impl.calls == calls && assertionFailures == ++failures);
    buffer.readBytes({1u, 16u}, {output.data(), output.size()});
    assert(buffer.impl.calls == calls && assertionFailures == ++failures);
    buffer.readBytes({0u, 16u}, {output.data(), output.size()}, 4u);
    assert(buffer.impl.calls == calls && assertionFailures == ++failures);
    buffer._params._hostVisible = false;
    buffer.readBytes({0u, 16u}, {output.data(), output.size()});
    assert(buffer.impl.calls == calls && assertionFailures == ++failures);
    buffer._params._hostVisible = true;
    buffer.usage = BufferUsageType::CONSTANT_BUFFER;
    buffer.readBytes({0u, 16u}, {output.data(), output.size()});
    assert(buffer.impl.calls == calls && assertionFailures == ++failures);
}
int main() {
    GLBuffer gl;
    VKBuffer vk;
    Check(gl);
    Check(vk);
}
"""
        run_cpp(self, source.replace("@SHARED_READBACK@", readback)
                           .replace("@GL_READBACK@", gl)
                           .replace("@VK_READBACK@", vk))


if __name__ == "__main__":
    unittest.main()
