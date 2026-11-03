# Copyright (c) 2026 DIVIDE-Studio
# Copyright (c) 2009-2026 Ionut Cava
# SPDX-License-Identifier: MIT

"""Regression tests for the engine's shader preprocessing callbacks."""

from pathlib import Path
import unittest

if __package__:
    from .ExposureBufferTests import run_cpp
else:
    from ExposureBufferTests import run_cpp


ROOT = Path(__file__).resolve().parents[2]
SHADER_PROGRAM = ROOT / "Source" / "Platform" / "Video" / "Shaders" / "ShaderProgram.cpp"


class ShaderPreprocessorTests(unittest.TestCase):
    def test_input_returns_partial_lines_and_never_reads_past_end(self):
        source = SHADER_PROGRAM.read_text(encoding="utf-8")
        begin = source.index("            static char* Input(")
        end = source.index("            FORCE_INLINE void Output(", begin)
        callback = source[begin:end]
        run_cpp(self, f"""
#include <cassert>
#include <string>
struct WorkData {{
    std::string _input;
    unsigned _fGetsPos = 0;
}};
{callback}
int main() {{
    WorkData work{{"abcd\\nxyz"}};
    char buffer[4] = {{}};
    assert(Input(buffer, sizeof(buffer), &work) && std::string(buffer) == "abc");
    assert(Input(buffer, sizeof(buffer), &work) && std::string(buffer) == "d\\n");
    assert(Input(buffer, sizeof(buffer), &work) && std::string(buffer) == "xyz");
    assert(Input(buffer, sizeof(buffer), &work) == nullptr);
    assert(Input(buffer, 1, &work) == nullptr);
    WorkData empty;
    assert(Input(buffer, sizeof(buffer), &empty) == nullptr);
}}
""")

    def test_error_callback_formats_all_arguments(self):
        source = SHADER_PROGRAM.read_text(encoding="utf-8")
        begin = source.index("            static void Error(")
        end = source.index("\n        }\n\n        static void OnThreadCreated()", begin)
        callback = source[begin:end]
        run_cpp(self, f"""
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
using string = std::string;
std::size_t to_size(int value) {{ return static_cast<std::size_t>(value); }}
#define LOCALE_STR(key) key
struct WorkData {{ bool _firstError = true; string _fileName = "effect.glsl"; }};
struct Console {{
    static inline std::vector<string> messages;
    static void errorfn(const char* value) {{ messages.emplace_back(value); }}
    static void errorfn(const char* label, const string& value) {{
        messages.emplace_back(string(label) + ":" + value);
    }}
}};
{callback}
void report(WorkData& work, const char* format, ...) {{
    va_list args;
    va_start(args, format);
    Error(&work, format, args);
    va_end(args);
}}
int main() {{
    WorkData work;
    report(work, "line %d: %s (%g)", 42, "bad macro", 3.5);
    assert(Console::messages.size() == 3);
    assert(Console::messages[1].find("effect.glsl") != string::npos);
    assert(Console::messages[2].find("line 42: bad macro (3.5)") != string::npos);
    report(work, "second %d", 7);
    assert(Console::messages.size() == 4);
    assert(Console::messages.back().find("second 7") != string::npos);
}}
""")

    def test_failed_preprocessing_never_reaches_compilation_or_cache(self):
        source = SHADER_PROGRAM.read_text(encoding="utf-8")
        begin = source.index("    bool ShaderProgram::loadSourceCode(")
        end = source.index("    void ShaderProgram::EraseAtom(", begin)
        functions = source[begin:end]
        run_cpp(self, f"""
#include <cassert>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
using U8 = std::uint8_t;
using U64 = std::uint64_t;
using string = std::string;
namespace eastl {{ template<typename T> using set = std::set<T>; }}
using ModuleDefines = std::vector<std::pair<string, bool>>;
#define DIVIDE_EXPECTED_CALL(value) assert(value)
#define DIVIDE_GPU_ASSERT(value, ...) assert(value)
#define LOCALE_STR(value) value
#define NOP() ((void)0)
enum class RenderAPI {{ Vulkan, OpenGL }};
enum class DescriptorSetUsage {{ PER_DRAW = 3 }};
template<typename T> U8 to_base(T value) {{ return static_cast<U8>(value); }}
constexpr U8 s_uniformsStartOffset = 14;
constexpr int GL_PUSH_CONSTANTS_LOCATION = 18;
constexpr const char* UNIFORM_BLOCK_NAME = "block";
RenderAPI s_renderAPI = RenderAPI::Vulkan;
int parses = 0, uniformExtractions = 0, conversions = 0, reflections = 0, saves = 0;
bool preprocessSucceeds = false, spirvCacheHit = false;
const char* glswSource = nullptr;
const char* glswGetShader(const char*) {{ return glswSource; }}
struct Console {{ template<typename... T> static void errorfn(T&&...) {{}} }};
namespace Reflection {{
    constexpr U8 INVALID_BINDING_INDEX = 255;
    struct UniformDeclaration {{
        string _type, _name;
        bool operator==(const UniformDeclaration&) const = default;
    }};
    using UniformsSet = std::vector<UniformDeclaration>;
    enum class PushConstantsState {{ NOT_USED }};
    struct Data {{
        U8 _uniformBlockBindingSet = 0;
        U8 _uniformBlockBindingIndex = INVALID_BINDING_INDEX;
        PushConstantsState _pushConstantsState = PushConstantsState::NOT_USED;
    }};
    void PreProcessUniforms(string&, UniformsSet&) {{ ++uniformExtractions; }}
}}
namespace Util {{
    template<typename... T> string StringFormat(const char* value, T&&...) {{ return value; }}
    template<typename... T> void StringFormatTo(string&, const char*, T&&...) {{}}
    void ReplaceStringInPlace(string& value, const string& key, const string& replacement) {{
        std::size_t pos = 0;
        while ((pos = value.find(key, pos)) != string::npos) {{
            value.replace(pos, key.size(), replacement);
            pos += replacement.size();
        }}
    }}
}}
namespace Preprocessor {{
    bool PreProcessMacros(std::string_view, string&) {{ ++parses; return preprocessSucceeds; }}
}}
struct LoadData {{
    enum class ShaderCacheType {{ COUNT, SPIRV, GLSL, REFLECTION }};
    string _sourceCodeGLSL, _shaderName = "name", _sourceName = "section", _uniformBlock;
    std::vector<unsigned> _sourceCodeSpirV;
    int _type = 0;
    Reflection::UniformsSet _uniforms;
    Reflection::Data _reflectionData;
}};
namespace SpirvHelper {{
    bool GLSLtoSPV(int, const char*, std::vector<unsigned>& output, RenderAPI) {{
        ++conversions; output.push_back(1); return true;
    }}
    bool BuildReflectionData(int, const std::vector<unsigned>&, RenderAPI, Reflection::Data&) {{
        ++reflections; return true;
    }}
}}
Reflection::PushConstantsState DetectPushConstantsUsage(const string&) {{
    return Reflection::PushConstantsState::NOT_USED;
}}
struct ShaderProgram {{
    eastl::set<U64> _usedAtomIDs;
    bool cache = false;
    bool useShaderCache() const {{ return cache; }}
    string resourceName() const {{ return "resource"; }}
    static U8 GetGLBindingForDescriptorSlot(DescriptorSetUsage, U8 slot) {{ return slot; }}
    static bool DeleteCache(LoadData::ShaderCacheType, const string&) {{ return true; }}
    static bool SaveToCache(LoadData::ShaderCacheType, const LoadData&, const eastl::set<U64>&) {{
        ++saves; return true;
    }}
    static bool LoadFromCache(LoadData::ShaderCacheType type, LoadData& data, eastl::set<U64>&) {{
        if (type == LoadData::ShaderCacheType::SPIRV && spirvCacheHit) {{
            data._sourceCodeSpirV.push_back(2); return true;
        }}
        return false;
    }}
    static void PreprocessIncludes(const string&, string&, int, eastl::set<U64>&, bool) {{}}
    bool loadSourceCode(const ModuleDefines&, bool, LoadData&, Reflection::UniformsSet&, U8&);
    void loadAndParseGLSL(const ModuleDefines&, LoadData&, Reflection::UniformsSet&, U8&, eastl::set<U64>&);
}};
{functions}
void reset() {{ parses = uniformExtractions = conversions = reflections = saves = 0; }}
int main() {{
    ShaderProgram program;
    Reflection::UniformsSet uniforms;
    U8 block = 0;
    for (RenderAPI api : {{RenderAPI::Vulkan, RenderAPI::OpenGL}}) {{
        s_renderAPI = api;
        for (const char* input : {{static_cast<const char*>(nullptr), "", "unprocessed source"}}) {{
            reset(); glswSource = input; preprocessSucceeds = false;
            LoadData data;
            assert(!program.loadSourceCode({{}}, false, data, uniforms, block));
            assert(data._sourceCodeGLSL.empty());
            assert(uniformExtractions == 0 && conversions == 0 && reflections == 0 && saves == 0);
            assert(parses == (input && *input ? 1 : 0));
        }}
        reset(); glswSource = "valid source"; preprocessSucceeds = true;
        LoadData valid;
        assert(program.loadSourceCode({{}}, false, valid, uniforms, block));
        assert(parses == 1 && uniformExtractions == 1 && conversions == 1 && reflections == 1 && saves == 3);
    }}
    reset(); s_renderAPI = RenderAPI::Vulkan; spirvCacheHit = true; program.cache = true;
    glswSource = nullptr; preprocessSucceeds = false;
    LoadData cached;
    assert(program.loadSourceCode({{}}, false, cached, uniforms, block));
    assert(parses == 0 && conversions == 0 && reflections == 1 && saves == 1);
}}
""")


if __name__ == "__main__":
    unittest.main()
