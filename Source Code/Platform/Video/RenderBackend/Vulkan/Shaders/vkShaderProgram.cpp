#include "stdafx.h"

#include "Headers/vkShaderProgram.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Utility/Headers/Localization.h"

#include <SPIRV-Reflect/spirv_reflect.h>

namespace Divide {

    namespace {
        void PrintModuleInfo(std::ostream& os, const SpvReflectShaderModule& obj, const char* /*indent*/)
        {
            os << "entry point     : " << obj.entry_point_name << "\n";
            os << "source lang     : " << spvReflectSourceLanguage(obj.source_language) << "\n";
            os << "source lang ver : " << obj.source_language_version << "\n";
            if (obj.source_language == SpvSourceLanguageHLSL) {
                os << "stage           : ";
                switch (obj.shader_stage) {
                default: break;
                case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT: os << "VS"; break;
                case SPV_REFLECT_SHADER_STAGE_TESSELLATION_CONTROL_BIT: os << "HS"; break;
                case SPV_REFLECT_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: os << "DS"; break;
                case SPV_REFLECT_SHADER_STAGE_GEOMETRY_BIT: os << "GS"; break;
                case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT: os << "PS"; break;
                case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT: os << "CS"; break;
                }
            }
        }

        std::string ToStringDescriptorType(SpvReflectDescriptorType value) {
            switch (value) {
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER: return "VK_DESCRIPTOR_TYPE_SAMPLER";
            case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return "VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER";
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return "VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE";
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE: return "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE";
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: return "VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER";
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: return "VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER";
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER";
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER: return "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER";
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: return "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC";
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: return "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC";
            case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return "VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT";
            case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: return "VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR";
            }
            // unhandled SpvReflectDescriptorType enum value
            return "VK_DESCRIPTOR_TYPE_???";
        }

        void PrintDescriptorBinding(std::ostream& os, const SpvReflectDescriptorBinding& obj, bool write_set, const char* indent)
        {
            const char* t = indent;
            os << t << "binding : " << obj.binding << "\n";
            if (write_set) {
                os << t << "set     : " << obj.set << "\n";
            }
            os << t << "type    : " << ToStringDescriptorType(obj.descriptor_type) << "\n";

            // array
            if (obj.array.dims_count > 0) {
                os << t << "array   : ";
                for (uint32_t dim_index = 0; dim_index < obj.array.dims_count; ++dim_index) {
                    os << "[" << obj.array.dims[dim_index] << "]";
                }
                os << "\n";
            }

            // counter
            if (obj.uav_counter_binding != nullptr) {
                os << t << "counter : ";
                os << "(";
                os << "set=" << obj.uav_counter_binding->set << ", ";
                os << "binding=" << obj.uav_counter_binding->binding << ", ";
                os << "name=" << obj.uav_counter_binding->name;
                os << ");";
                os << "\n";
            }

            os << t << "name    : " << obj.name;
            if ((obj.type_description->type_name != nullptr) && (strlen(obj.type_description->type_name) > 0)) {
                os << " " << "(" << obj.type_description->type_name << ")";
            }
        }
        void PrintDescriptorSet(std::ostream& os, const SpvReflectDescriptorSet& obj, const char* indent)
        {
            const char* t = indent;
            std::string tt = std::string(indent) + "  ";
            std::string ttttt = std::string(indent) + "    ";

            os << t << "set           : " << obj.set << "\n";
            os << t << "binding count : " << obj.binding_count;
            os << "\n";
            for (uint32_t i = 0; i < obj.binding_count; ++i) {
                const SpvReflectDescriptorBinding& binding = *obj.bindings[i];
                os << tt << i << ":" << "\n";
                PrintDescriptorBinding(os, binding, false, ttttt.c_str());
                if (i < (obj.binding_count - 1)) {
                    os << "\n";
                }
            }
        }
        std::string ToStringSpvBuiltIn(SpvBuiltIn built_in) {
            switch (built_in) {
            case SpvBuiltInPosition: return "Position";
            case SpvBuiltInPointSize: return "PointSize";
            case SpvBuiltInClipDistance: return "ClipDistance";
            case SpvBuiltInCullDistance: return "CullDistance";
            case SpvBuiltInVertexId: return "VertexId";
            case SpvBuiltInInstanceId: return "InstanceId";
            case SpvBuiltInPrimitiveId: return "PrimitiveId";
            case SpvBuiltInInvocationId: return "InvocationId";
            case SpvBuiltInLayer: return "Layer";
            case SpvBuiltInViewportIndex: return "ViewportIndex";
            case SpvBuiltInTessLevelOuter: return "TessLevelOuter";
            case SpvBuiltInTessLevelInner: return "TessLevelInner";
            case SpvBuiltInTessCoord: return "TessCoord";
            case SpvBuiltInPatchVertices: return "PatchVertices";
            case SpvBuiltInFragCoord: return "FragCoord";
            case SpvBuiltInPointCoord: return "PointCoord";
            case SpvBuiltInFrontFacing: return "FrontFacing";
            case SpvBuiltInSampleId: return "SampleId";
            case SpvBuiltInSamplePosition: return "SamplePosition";
            case SpvBuiltInSampleMask: return "SampleMask";
            case SpvBuiltInFragDepth: return "FragDepth";
            case SpvBuiltInHelperInvocation: return "HelperInvocation";
            case SpvBuiltInNumWorkgroups: return "NumWorkgroups";
            case SpvBuiltInWorkgroupSize: return "WorkgroupSize";
            case SpvBuiltInWorkgroupId: return "WorkgroupId";
            case SpvBuiltInLocalInvocationId: return "LocalInvocationId";
            case SpvBuiltInGlobalInvocationId: return "GlobalInvocationId";
            case SpvBuiltInLocalInvocationIndex: return "LocalInvocationIndex";
            case SpvBuiltInWorkDim: return "WorkDim";
            case SpvBuiltInGlobalSize: return "GlobalSize";
            case SpvBuiltInEnqueuedWorkgroupSize: return "EnqueuedWorkgroupSize";
            case SpvBuiltInGlobalOffset: return "GlobalOffset";
            case SpvBuiltInGlobalLinearId: return "GlobalLinearId";
            case SpvBuiltInSubgroupSize: return "SubgroupSize";
            case SpvBuiltInSubgroupMaxSize: return "SubgroupMaxSize";
            case SpvBuiltInNumSubgroups: return "NumSubgroups";
            case SpvBuiltInNumEnqueuedSubgroups: return "NumEnqueuedSubgroups";
            case SpvBuiltInSubgroupId: return "SubgroupId";
            case SpvBuiltInSubgroupLocalInvocationId: return "SubgroupLocalInvocationId";
            case SpvBuiltInVertexIndex: return "VertexIndex";
            case SpvBuiltInInstanceIndex: return "InstanceIndex";
            case SpvBuiltInSubgroupEqMaskKHR: return "SubgroupEqMaskKHR";
            case SpvBuiltInSubgroupGeMaskKHR: return "SubgroupGeMaskKHR";
            case SpvBuiltInSubgroupGtMaskKHR: return "SubgroupGtMaskKHR";
            case SpvBuiltInSubgroupLeMaskKHR: return "SubgroupLeMaskKHR";
            case SpvBuiltInSubgroupLtMaskKHR: return "SubgroupLtMaskKHR";
            case SpvBuiltInBaseVertex: return "BaseVertex";
            case SpvBuiltInBaseInstance: return "BaseInstance";
            case SpvBuiltInDrawIndex: return "DrawIndex";
            case SpvBuiltInDeviceIndex: return "DeviceIndex";
            case SpvBuiltInViewIndex: return "ViewIndex";
            case SpvBuiltInBaryCoordNoPerspAMD: return "BaryCoordNoPerspAMD";
            case SpvBuiltInBaryCoordNoPerspCentroidAMD: return "BaryCoordNoPerspCentroidAMD";
            case SpvBuiltInBaryCoordNoPerspSampleAMD: return "BaryCoordNoPerspSampleAMD";
            case SpvBuiltInBaryCoordSmoothAMD: return "BaryCoordSmoothAMD";
            case SpvBuiltInBaryCoordSmoothCentroidAMD: return "BaryCoordSmoothCentroidAMD";
            case SpvBuiltInBaryCoordSmoothSampleAMD: return "BaryCoordSmoothSampleAMD";
            case SpvBuiltInBaryCoordPullModelAMD: return "BaryCoordPullModelAMD";
            case SpvBuiltInFragStencilRefEXT: return "FragStencilRefEXT";
            case SpvBuiltInViewportMaskNV: return "ViewportMaskNV";
            case SpvBuiltInSecondaryPositionNV: return "SecondaryPositionNV";
            case SpvBuiltInSecondaryViewportMaskNV: return "SecondaryViewportMaskNV";
            case SpvBuiltInPositionPerViewNV: return "PositionPerViewNV";
            case SpvBuiltInViewportMaskPerViewNV: return "ViewportMaskPerViewNV";
            case SpvBuiltInLaunchIdKHR: return "InLaunchIdKHR";
            case SpvBuiltInLaunchSizeKHR: return "InLaunchSizeKHR";
            case SpvBuiltInWorldRayOriginKHR: return "InWorldRayOriginKHR";
            case SpvBuiltInWorldRayDirectionKHR: return "InWorldRayDirectionKHR";
            case SpvBuiltInObjectRayOriginKHR: return "InObjectRayOriginKHR";
            case SpvBuiltInObjectRayDirectionKHR: return "InObjectRayDirectionKHR";
            case SpvBuiltInRayTminKHR: return "InRayTminKHR";
            case SpvBuiltInRayTmaxKHR: return "InRayTmaxKHR";
            case SpvBuiltInInstanceCustomIndexKHR: return "InInstanceCustomIndexKHR";
            case SpvBuiltInObjectToWorldKHR: return "InObjectToWorldKHR";
            case SpvBuiltInWorldToObjectKHR: return "InWorldToObjectKHR";
            case SpvBuiltInHitTNV: return "InHitTNV";
            case SpvBuiltInHitKindKHR: return "InHitKindKHR";
            case SpvBuiltInIncomingRayFlagsKHR: return "InIncomingRayFlagsKHR";
            case SpvBuiltInRayGeometryIndexKHR: return "InRayGeometryIndexKHR";

            case SpvBuiltInMax:
            default:
                break;
            }
            // unhandled SpvBuiltIn enum value
            std::stringstream ss;
            ss << "??? (" << built_in << ")";
            return ss.str();
        }
        std::string ToStringFormat(SpvReflectFormat fmt) {
            switch (fmt) {
            case SPV_REFLECT_FORMAT_UNDEFINED: return "VK_FORMAT_UNDEFINED";
            case SPV_REFLECT_FORMAT_R32_UINT: return "VK_FORMAT_R32_UINT";
            case SPV_REFLECT_FORMAT_R32_SINT: return "VK_FORMAT_R32_SINT";
            case SPV_REFLECT_FORMAT_R32_SFLOAT: return "VK_FORMAT_R32_SFLOAT";
            case SPV_REFLECT_FORMAT_R32G32_UINT: return "VK_FORMAT_R32G32_UINT";
            case SPV_REFLECT_FORMAT_R32G32_SINT: return "VK_FORMAT_R32G32_SINT";
            case SPV_REFLECT_FORMAT_R32G32_SFLOAT: return "VK_FORMAT_R32G32_SFLOAT";
            case SPV_REFLECT_FORMAT_R32G32B32_UINT: return "VK_FORMAT_R32G32B32_UINT";
            case SPV_REFLECT_FORMAT_R32G32B32_SINT: return "VK_FORMAT_R32G32B32_SINT";
            case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT: return "VK_FORMAT_R32G32B32_SFLOAT";
            case SPV_REFLECT_FORMAT_R32G32B32A32_UINT: return "VK_FORMAT_R32G32B32A32_UINT";
            case SPV_REFLECT_FORMAT_R32G32B32A32_SINT: return "VK_FORMAT_R32G32B32A32_SINT";
            case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT: return "VK_FORMAT_R32G32B32A32_SFLOAT";
            case SPV_REFLECT_FORMAT_R64_UINT: return "VK_FORMAT_R64_UINT";
            case SPV_REFLECT_FORMAT_R64_SINT: return "VK_FORMAT_R64_SINT";
            case SPV_REFLECT_FORMAT_R64_SFLOAT: return "VK_FORMAT_R64_SFLOAT";
            case SPV_REFLECT_FORMAT_R64G64_UINT: return "VK_FORMAT_R64G64_UINT";
            case SPV_REFLECT_FORMAT_R64G64_SINT: return "VK_FORMAT_R64G64_SINT";
            case SPV_REFLECT_FORMAT_R64G64_SFLOAT: return "VK_FORMAT_R64G64_SFLOAT";
            case SPV_REFLECT_FORMAT_R64G64B64_UINT: return "VK_FORMAT_R64G64B64_UINT";
            case SPV_REFLECT_FORMAT_R64G64B64_SINT: return "VK_FORMAT_R64G64B64_SINT";
            case SPV_REFLECT_FORMAT_R64G64B64_SFLOAT: return "VK_FORMAT_R64G64B64_SFLOAT";
            case SPV_REFLECT_FORMAT_R64G64B64A64_UINT: return "VK_FORMAT_R64G64B64A64_UINT";
            case SPV_REFLECT_FORMAT_R64G64B64A64_SINT: return "VK_FORMAT_R64G64B64A64_SINT";
            case SPV_REFLECT_FORMAT_R64G64B64A64_SFLOAT: return "VK_FORMAT_R64G64B64A64_SFLOAT";
            }
            // unhandled SpvReflectFormat enum value
            return "VK_FORMAT_???";
        }
        static std::string ToStringScalarType(const SpvReflectTypeDescription& type)
        {
            switch (type.op) {
            case SpvOpTypeVoid: {
                return "void";
                break;
            }
            case SpvOpTypeBool: {
                return "bool";
                break;
            }
            case SpvOpTypeInt: {
                if (type.traits.numeric.scalar.signedness)
                    return "int";
                else
                    return "uint";
            }
            case SpvOpTypeFloat: {
                switch (type.traits.numeric.scalar.width) {
                case 32:
                    return "float";
                case 64:
                    return "double";
                default:
                    break;
                }
            }
            case SpvOpTypeStruct: {
                return "struct";
            }
            default: {
                break;
            }
            }
            return "";
        }

        static std::string ToStringHlslType(const SpvReflectTypeDescription& type)
        {
            switch (type.op) {
            case SpvOpTypeVector: {
                switch (type.traits.numeric.scalar.width) {
                case 32: {
                    switch (type.traits.numeric.vector.component_count) {
                    case 2: return "float2";
                    case 3: return "float3";
                    case 4: return "float4";
                    }
                }
                       break;

                case 64: {
                    switch (type.traits.numeric.vector.component_count) {
                    case 2: return "double2";
                    case 3: return "double3";
                    case 4: return "double4";
                    }
                }
                       break;
                }
            }
                                break;

            default:
                break;
            }
            return ToStringScalarType(type);
        }
        static std::string ToStringGlslType(const SpvReflectTypeDescription& type)
        {
            switch (type.op) {
            case SpvOpTypeVector: {
                switch (type.traits.numeric.scalar.width) {
                case 32: {
                    switch (type.traits.numeric.vector.component_count) {
                    case 2: return "vec2";
                    case 3: return "vec3";
                    case 4: return "vec4";
                    }
                }
                       break;

                case 64: {
                    switch (type.traits.numeric.vector.component_count) {
                    case 2: return "dvec2";
                    case 3: return "dvec3";
                    case 4: return "dvec4";
                    }
                }
                       break;
                }
            }
                                break;
            default:
                break;
            }
            return ToStringScalarType(type);
        }
        std::string ToStringType(SpvSourceLanguage src_lang, const SpvReflectTypeDescription& type)
        {
            if (src_lang == SpvSourceLanguageHLSL) {
                return ToStringHlslType(type);
            }

            return ToStringGlslType(type);
        }
        void PrintInterfaceVariable(std::ostream& os, SpvSourceLanguage src_lang, const SpvReflectInterfaceVariable& obj, const char* indent)
        {
            const char* t = indent;
            os << t << "location  : ";
            if (obj.decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) {
                os << ToStringSpvBuiltIn(obj.built_in) << " " << "(built-in)";
            }
            else {
                os << obj.location;
            }
            os << "\n";
            if (obj.semantic != nullptr) {
                os << t << "semantic  : " << obj.semantic << "\n";
            }
            os << t << "type      : " << ToStringType(src_lang, *obj.type_description) << "\n";
            os << t << "format    : " << ToStringFormat(obj.format) << "\n";
            os << t << "qualifier : ";
            if (obj.decoration_flags & SPV_REFLECT_DECORATION_FLAT) {
                os << "flat";
            }
            else   if (obj.decoration_flags & SPV_REFLECT_DECORATION_NOPERSPECTIVE) {
                os << "noperspective";
            }
            os << "\n";

            os << t << "name      : " << obj.name;
            if ((obj.type_description->type_name != nullptr) && (strlen(obj.type_description->type_name) > 0)) {
                os << " " << "(" << obj.type_description->type_name << ")";
            }
        }
    };
    vkShader::vkShader(GFXDevice& context, const Str256& name)
        : ShaderModule(context, name)
    {
        std::atomic_init(&_refCount, 0u);
    }

    vkShader::~vkShader()
    {
        reset();
    }
    
    void vkShader::reset() {
        if (_handle != VK_NULL_HANDLE) {
            vkDestroyShaderModule(VK_API::GetStateTracker()->_device->getVKDevice(), _handle, nullptr);
            _handle = VK_NULL_HANDLE;
            _createInfo = {};
        }
    }

    /// Load a shader by name, source code and stage
    vkShader* vkShader::LoadShader(GFXDevice& context,
                                   const Str256& name,
                                   const bool overwriteExisting,
                                   ShaderProgram::LoadData& data) {
        ScopedLock<SharedMutex> w_lock(ShaderModule::s_shaderNameLock);

        // See if we have the shader already loaded
        ShaderModule* shader = GetShaderLocked(name);
        if (overwriteExisting && shader != nullptr) {
            RemoveShaderLocked(shader, true);
            shader = nullptr;
        }

        // If we do, and don't need a recompile, just return it
        if (shader == nullptr) {
            shader = MemoryManager_NEW vkShader(context, name);

            // If we loaded the source code successfully,  register it
            s_shaderNameMap.insert({ shader->nameHash(), shader });

            // At this stage, we have a valid Shader object, so load the source code
            if (!static_cast<vkShader*>(shader)->load(data)) {
                DIVIDE_UNEXPECTED_CALL();
            }
        } else {
            shader->AddRef();
            Console::d_printfn(Locale::Get(_ID("SHADER_MANAGER_GET_INC")), shader->name().c_str(), shader->GetRef());
        }

        return static_cast<vkShader*>(shader);
    }

    bool vkShader::load(const ShaderProgram::LoadData& data) {
        _loadData = data;

        _valid = false;

        reset();

        _stageMask = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
        if (data._type != ShaderType::COUNT) {

            assert(_handle == VK_NULL_HANDLE);

            assert(!data._sourceCodeSpirV.empty());

            //create a new shader module, using the buffer we loaded
            const VkShaderModuleCreateInfo createInfo = vk::shaderModuleCreateInfo(data._sourceCodeSpirV.size() * sizeof(uint32_t), data._sourceCodeSpirV.data());

            VK_CHECK(vkCreateShaderModule(VK_API::GetStateTracker()->_device->getVKDevice(), &createInfo, nullptr, &_handle));

            _createInfo = vk::pipelineShaderStageCreateInfo(vkShaderStageTable[to_base(data._type)], _handle);
            _stageMask = vkShaderStageTable[to_base(data._type)];
        }

        if (_stageMask == VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM) {
            Console::errorfn(Locale::Get(_ID("ERROR_GLSL_NOT_FOUND")), name().c_str());
            return false;
        }

        return true;
    }

    void vkShaderProgram::Idle([[maybe_unused]] PlatformContext& platformContext) {
        OPTICK_EVENT();

        assert(Runtime::isMainThread());
    }

    vkShaderProgram::vkShaderProgram(GFXDevice& context,
                                     const size_t descriptorHash,
                                     const Str256& name,
                                     const Str256& assetName,
                                     const ResourcePath& assetLocation,
                                     const ShaderProgramDescriptor& descriptor,
                                     ResourceCache& parentCache)
        : ShaderProgram(context, descriptorHash, name, assetName, assetLocation, descriptor, parentCache)
    {
    }

    vkShaderProgram::~vkShaderProgram()
    {
        unload();
    }

    bool vkShaderProgram::unload() {
        // Remove every shader attached to this program
        eastl::for_each(begin(_shaderStage),
                        end(_shaderStage),
                        [](vkShader* shader) {
                            ShaderModule::RemoveShader(shader);
                        });
        _shaderStage.clear();

        return ShaderProgram::unload();
    }

    ShaderResult vkShaderProgram::validatePreBind(const bool rebind) {
        OPTICK_EVENT();

        return ShaderResult::OK;
    }

    bool vkShaderProgram::recompile(bool& skipped) {
        OPTICK_EVENT();

        if (!ShaderProgram::recompile(skipped)) {
            return false;
        }

        if (validatePreBind(false) != ShaderResult::OK) {
            return false;
        }

        skipped = false;

        threadedLoad(true);

        return true;
    }

    bool vkShaderProgram::reloadShaders(hashMap<U64, PerFileShaderData>& fileData, bool reloadExisting) {
        OPTICK_EVENT();

        if (ShaderProgram::reloadShaders(fileData, reloadExisting)) {
            _stagesBound = false;
            _shaderStage.clear();

            for (auto& [fileHash, loadDataPerFile] : fileData) {
                assert(!loadDataPerFile._modules.empty());

                for (auto& loadData : loadDataPerFile._loadData) {
                    if (loadData._type == ShaderType::COUNT) {
                        continue;
                    }
                    
                    vkShader* shader = vkShader::LoadShader(_context, loadDataPerFile._programName, reloadExisting, loadData);
                    _shaderStage.push_back(shader);
                }
  
            }
            return !_shaderStage.empty();
        }

        return false;
    }

}; //namespace Divide
