/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef _MATERIAL_H_
#define _MATERIAL_H_

#include "MaterialEnums.h"

#include "Utility/Headers/XMLParser.h"

#include "Core/Resources/Headers/Resource.h"
#include "Core/Resources/Headers/ResourceDescriptor.h"

#include "Platform/Video/Headers/RenderAPIEnums.h"
#include "Platform/Video/Headers/RenderStagePass.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include "Geometry/Material/Headers/ShaderProgramInfo.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Rendering/RenderPass/Headers/NodeBufferedData.h"

namespace Divide {

class RenderingComponent;
struct NodeMaterialData;

class RenderStateBlock;
class ResourceDescriptor;

enum class BlendProperty : U8;
enum class ReflectorType : U8;

constexpr F32 Specular_Glass = 0.5f;
constexpr F32 Specular_Plastic = 0.5f;
constexpr F32 Specular_Quarts = 0.57f;
constexpr F32 Specular_Ice = 0.224f;
constexpr F32 Specular_Water = 0.255f;
constexpr F32 Specular_Milk = 0.277f;
constexpr F32 Specular_Skin = 0.35f;


constexpr U8 g_TransparentSlots[] = {
   to_base(TextureUsage::UNIT0),
   to_base(TextureUsage::OPACITY)
};

constexpr U8 g_ExtraSlots[] = {
    to_base(TextureUsage::NORMALMAP),
    to_base(TextureUsage::HEIGHTMAP),
    to_base(TextureUsage::SPECULAR),
    to_base(TextureUsage::METALNESS),
    to_base(TextureUsage::ROUGHNESS),
    to_base(TextureUsage::OCCLUSION),
    to_base(TextureUsage::EMISSIVE),
    to_base(TextureUsage::UNIT1),
    to_base(TextureUsage::PROJECTION)
    // Reflection and Refraction are handled by the RenderingComponent
};

namespace TypeUtil {
    const char* MaterialDebugFlagToString(const MaterialDebugFlag unitType) noexcept;
    MaterialDebugFlag StringToMaterialDebugFlag(const string& name);

    const char* ShadingModeToString(ShadingMode shadingMode) noexcept;
    ShadingMode StringToShadingMode(const string& name);

    const char* TextureUsageToString(TextureUsage texUsage) noexcept;
    TextureUsage StringToTextureUsage(const string& name);

    const char* TextureOperationToString(TextureOperation textureOp) noexcept;
    TextureOperation StringToTextureOperation(const string& operation);

    const char* BumpMethodToString(BumpMethod bumpMethod) noexcept;
    BumpMethod StringToBumpMethod(const string& name);
};

class Material final : public CachedResource {
   public:
    /// Since most variants come from different light sources, this seems like a good idea (famous last words ...)
    static constexpr size_t g_maxVariantsPerPass = 3;
    static constexpr size_t INVALID_MAT_HASH = std::numeric_limits<size_t>::max();

    struct ShaderData {
        Str64 _depthShaderVertSource = "baseVertexShaders";
        Str32 _depthShaderVertVariant = "BasicLightData";
        Str32 _shadowShaderVertVariant = "BasicData";

        Str64 _colourShaderVertSource = "baseVertexShaders";
        Str32 _colourShaderVertVariant = "BasicLightData";

        Str64 _depthShaderFragSource = "depthPass";
        Str32 _depthShaderFragVariant = "";

        Str64 _colourShaderFragSource = "material";
        Str32 _colourShaderFragVariant = "";
    };

    using CustomShaderUpdateCBK = DELEGATE_STD<bool, Material&, RenderStagePass>;

   public:
    explicit Material(GFXDevice& context, ResourceCache* parentCache, size_t descriptorHash, const Str256& name);

    static void ApplyDefaultStateBlocks(Material& target);

    /// Return a new instance of this material with the name composed of the
    /// base material's name and the give name suffix.
    /// clone calls CreateResource internally!)
    [[nodiscard]] Material_ptr clone(const Str256& nameSuffix);
    [[nodiscard]] bool unload() override;
    /// Returns true if the material changed between update calls
    [[nodiscard]] bool update(U64 deltaTimeUS);
    bool setSampler(TextureUsage textureUsageSlot,
                    size_t samplerHash,
                    bool applyToInstances = false);

    bool setTexture(TextureUsage textureUsageSlot,
                    const Texture_ptr& texture,
                    size_t samplerHash,
                    TextureOperation op = TextureOperation::REPLACE,
                    bool applyToInstances = false);
    void setTextureOperation(TextureUsage textureUsageSlot,
                             TextureOperation op,
                             bool applyToAllInstances = false);
    void textureUseForDepth(TextureUsage slot, bool state, bool applyToInstances = false);
    void hardwareSkinning(bool state, bool applyToInstances = false);
    void shadingMode(ShadingMode mode, bool applyToInstances = false);
    void doubleSided(bool state, bool applyToInstances = false);
    void receivesShadows(bool state, bool applyToInstances = false);
    void refractive(bool state, bool applyToInstances = false);
    void isStatic(bool state, bool applyToInstances = false);
    void ignoreTexDiffuseAlpha(bool state, bool applyToInstances = false);
    void usePlanarReflections(bool state, bool applyToInstances = false);
    void usePlanarRefractions(bool state, bool applyToInstances = false);
    void parallaxFactor(F32 factor, bool applyToInstances = false);
    void bumpMethod(BumpMethod newBumpMethod, bool applyToInstances = false);
    void toggleTransparency(bool state, bool applyToInstances = false);
    void baseColour(const FColour4& colour, bool applyToInstances = false);
    void emissiveColour(const FColour3& colour, bool applyToInstances = false);
    void ambientColour(const FColour3& colour, bool applyToInstances = false);
    void specularColour(const FColour3& colour, bool applyToInstances = false);
    void shininess(F32 value, bool applyToInstances = false);
    void metallic(F32 value, bool applyToInstances = false);
    void occlusion(F32 value, bool applyToInstances = false);
    void roughness(F32 value, bool applyToInstances = false);

    FColour4 getBaseColour(bool& hasTextureOverride, Texture*& textureOut) const noexcept;
    FColour3 getEmissive(bool& hasTextureOverride, Texture*& textureOut) const noexcept;
    FColour3 getAmbient(bool& hasTextureOverride, Texture*& textureOut) const noexcept;
    FColour4 getSpecular(bool& hasTextureOverride, Texture*& textureOut) const noexcept;
    F32 getMetallic(bool& hasTextureOverride, Texture*& textureOut) const noexcept;
    F32 getRoughness(bool& hasTextureOverride, Texture*& textureOut) const noexcept;
    F32 getOcclusion(bool& hasTextureOverride, Texture*& textureOut) const noexcept;
    

    /// Add the specified shader to specific RenderStagePass parameters. Use "COUNT" and/or g_AllVariantsID for global options
    /// e.g. a RenderPassType::COUNT will use the shader in the specified stage+variant combo but for all of the passes
    void setShaderProgram(const ShaderProgram_ptr& shader, RenderStage stage, RenderPassType pass, U8 variant = g_AllVariantsID);
    /// Add the specified renderStateBlockHash to specific RenderStagePass parameters. Use "COUNT" and/or "g_AllVariantsID" for global options
    /// e.g. a RenderPassType::COUNT will use the block in the specified stage+variant combo but for all of the passes
    void setRenderStateBlock(size_t renderStateBlockHash, RenderStage stage, RenderPassType pass, U8 variant = g_AllVariantsID);

    void getSortKeys(const RenderStagePass& renderStagePass, I64& shaderKey, I32& textureKey) const;

    // Returns the material's hash value (just for the uploadable data)
    void getData(const RenderingComponent& parentComp, U32 bestProbeID, NodeMaterialData& dataOut, NodeMaterialTextures& texturesOut);

    size_t getRenderStateBlock(const RenderStagePass& renderStagePass) const;
    Texture_wptr getTexture(TextureUsage textureUsage) const;
    size_t getSampler(const TextureUsage textureUsage) const noexcept { return _samplers[to_base(textureUsage)]; }

    bool getTextureData(const RenderStagePass& renderStagePass, TextureDataContainer& textureData);
    I64 getProgramGUID(const RenderStagePass& renderStagePass) const;
    I64 computeAndGetProgramGUID(const RenderStagePass& renderStagePass);

    void rebuild();
    bool hasTransparency() const;

    bool isPBRMaterial() const;
    bool reflective() const;
    bool refractive() const;

    bool canDraw(const RenderStagePass& renderStagePass, bool& shaderJustFinishedLoading);

    void saveToXML(const string& entryName, boost::property_tree::ptree& pt) const;
    void loadFromXML(const string& entryName, const boost::property_tree::ptree& pt);

    // type == ShaderType::Count = add to all stages
    void addShaderDefine(ShaderType type, const Str128& define, bool addPrefix);
    const ModuleDefines& shaderDefines(ShaderType type) const;

   private:
    /// Constructs a shader for the specified renderStatePass
    void computeShader(const RenderStagePass& renderStagePass);

    void addShaderDefineInternal(ShaderType type, const Str128& define, bool addPrefix);

    void updateTransparency();

    void recomputeShaders();
    void setShaderProgramInternal(const ResourceDescriptor& shaderDescriptor,
                                  const RenderStagePass& stagePass,
                                  bool computeOnAdd);

    void setShaderProgramInternal(const ShaderProgram_ptr& shader,
                                  ShaderProgramInfo& shaderInfo,
                                  RenderStage stage,
                                  RenderPassType pass,
                                  U8 variant) const;

    ShaderProgramInfo& shaderInfo(const RenderStagePass& renderStagePass);

    const ShaderProgramInfo& shaderInfo(const RenderStagePass& renderStagePass) const;

    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "Material"; }

    void saveRenderStatesToXML(const string& entryName, boost::property_tree::ptree& pt) const;
    void loadRenderStatesFromXML(const string& entryName, const boost::property_tree::ptree& pt);

    void saveTextureDataToXML(const string& entryName, boost::property_tree::ptree& pt) const;
    void loadTextureDataFromXML(const string& entryName, const boost::property_tree::ptree& pt);

    PROPERTY_RW(bool, ignoreXMLData, false);
    PROPERTY_RW(ShaderData, baseShaderData);
    POINTER_R_IW(Material, baseMaterial, nullptr);

    PROPERTY_R(FColour4, baseColour, DefaultColours::WHITE);
    PROPERTY_R(FColour3, emissive, DefaultColours::BLACK);
    PROPERTY_R(FColour3, ambient, DefaultColours::BLACK);
    PROPERTY_R(FColour4, specular, DefaultColours::BLACK); //< Only used by WorkFlow::PHONG_SPECULAR. A = shininess
    PROPERTY_R(F32, metallic, 0.0f);
    PROPERTY_R(F32, roughness, 0.5f);
    PROPERTY_R(F32, occlusion, 1.0f);

    /// parallax/relief factor (higher value > more pronounced effect)
    PROPERTY_R(F32, parallaxFactor, 1.0f);
    PROPERTY_R(ShadingMode, shadingMode, ShadingMode::COUNT);
    PROPERTY_R(TranslucencySource, translucencySource, TranslucencySource::COUNT);
    PROPERTY_R(BumpMethod, bumpMethod, BumpMethod::NONE);
    PROPERTY_R(bool, doubleSided, false);
    PROPERTY_R(bool, transparencyEnabled, true);
    PROPERTY_R(bool, receivesShadows, true);
    PROPERTY_R(bool, isStatic, false);
    PROPERTY_R(bool, specTextureHasAlpha, false);
    PROPERTY_R(bool, ignoreTexDiffuseAlpha, false);
    /// Use shaders that have bone transforms implemented
    PROPERTY_R(bool, hardwareSkinning, false);
    /// If the metalness textures has 3 (or 4) channels, those channels are interpreted automatically as R: Occlusion, G: Metalness, B: Roughness
    PROPERTY_R(bool, usePackedOMR, false);
    PROPERTY_RW(CustomShaderUpdateCBK, customShaderCBK);
    /// These settings don't affect Environment and SSR reflections! Just custom reflection/refraction textures 
    /// E.g. water and it's custom reflection and refraction render-to-texture passes
    
    /// Mirrors might use planar reflections, but complex objects might need cube maps
    PROPERTY_R(bool, usePlanarReflections, true);
    PROPERTY_R(bool, usePlanarRefractions, true);

    PROPERTY_R(bool, useBindlessTextures, false);

    using TexOpArray = std::array<TextureOperation, to_base(TextureUsage::COUNT)>;
    PROPERTY_R(TexOpArray, textureOperations);

   private:
    bool _isRefractive = false;

    GFXDevice& _context;
    ResourceCache* _parentCache = nullptr;

    bool _needsNewShader = true;

    using ShaderPerVariant = std::array<ShaderProgramInfo, g_maxVariantsPerPass>;
    using ShaderVariantsPerPass = eastl::array<ShaderPerVariant, to_base(RenderPassType::COUNT)>;
    using ShaderPassesPerStage = eastl::array<ShaderVariantsPerPass, to_base(RenderStage::COUNT)>;
    ShaderPassesPerStage _shaderInfo{};

    using StatesPerVariant = std::array<size_t, g_maxVariantsPerPass>;
    using StateVariantsPerPass = eastl::array<StatesPerVariant, to_base(RenderPassType::COUNT)>;
    using StatePassesPerStage = eastl::array<StateVariantsPerPass, to_base(RenderStage::COUNT)>;
    StatePassesPerStage _defaultRenderStates{};

    /// use this map to add textures to the material
    mutable SharedMutex _textureLock{};
    std::array<Texture_ptr, to_base(TextureUsage::COUNT)> _textures = {};
    std::array<size_t, to_base(TextureUsage::COUNT)> _samplers = {};
    std::array<bool, to_base(TextureUsage::COUNT)> _textureUseForDepth = {};

    std::array<SamplerAddress, to_base(TextureUsage::COUNT)> _textureAddresses = {};

    I32 _textureKeyCache = -1;
    std::array<ModuleDefines, to_base(ShaderType::COUNT)> _extraShaderDefines{};

    vector<Material*> _instances{};
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(Material);

};  // namespace Divide

#endif //_MATERIAL_H_

#include "Material.inl"
