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


if __name__ == "__main__":
    unittest.main()
