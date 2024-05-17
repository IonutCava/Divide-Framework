

#include "Headers/DoFPreRenderOperator.h"

#include "Core/Headers/StringHelper.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

#include "Rendering/PostFX/Headers/PreRenderBatch.h"

namespace Divide {

namespace {
    constexpr U8 g_samplesOnFirstRing = 3;
    constexpr U8 g_ringCount = 4;
}

DoFPreRenderOperator::DoFPreRenderOperator(GFXDevice& context, PreRenderBatch& parent)
    : PreRenderOperator(context, parent, FilterType::FILTER_DEPTH_OF_FIELD)
{
    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "baseVertexShaders.glsl";
    vertModule._variant = "FullScreenQuad";

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "DepthOfField.glsl";
    //blur the depth buffer?
    fragModule._defines.emplace_back("USE_DEPTH_BLUR");
    //use noise instead of pattern for sample dithering
    fragModule._defines.emplace_back("USER_NOISE");
    //use pentagon as bokeh shape?
    //fragModule._defines.emplace_back("USE_PENTAGON");
    fragModule._defines.emplace_back(Util::StringFormat("RING_COUNT {}", g_ringCount));
    fragModule._defines.emplace_back(Util::StringFormat("FIRST_RING_SAMPLES {}", g_samplesOnFirstRing));
    fragModule._defines.emplace_back( Util::StringFormat( "FIRST_RING_SAMPLES {}", g_samplesOnFirstRing ) );

    fragModule._defines.emplace_back("size PushData0[0].xy");
    // autofocus point on screen (0.0,0.0 - left lower corner, 1.0,1.0 - upper right)
    fragModule._defines.emplace_back("focus PushData0[0].zw");
    fragModule._defines.emplace_back("_zPlanes PushData0[1].xy");
    //focal distance value in meters, but you may use autofocus option below
    fragModule._defines.emplace_back("focalDepth PushData0[1].z");
    //focal length in mm
    fragModule._defines.emplace_back("focalLength PushData0[1].w");
    fragModule._defines.emplace_back("fstop PushData0[2].x");
    //near dof blur start
    fragModule._defines.emplace_back("ndofstart PushData0[2].y");
    //near dof blur falloff distance
    fragModule._defines.emplace_back("ndofdist PushData0[2].z");
    //far dof blur start
    fragModule._defines.emplace_back("fdofstart PushData0[2].w");
    //far dof blur falloff distance
    fragModule._defines.emplace_back("fdofdist PushData0[3].x");
    //vignetting outer border
    fragModule._defines.emplace_back("vignout PushData0[3].y");
    //vignetting inner border
    fragModule._defines.emplace_back("vignin PushData0[3].z");
    //show debug focus point and focal range (red = focal point, green = focal range)
    fragModule._defines.emplace_back("showFocus uint(PushData0[3].w)");
    //manual dof calculation
    fragModule._defines.emplace_back("manualdof uint(PushData1[0].x)");
    //use optical lens vignetting?
    fragModule._defines.emplace_back("vignetting uint(PushData1[0].y)");
    //use autofocus in shader? disable if you use external focalDepth value
    fragModule._defines.emplace_back("autofocus uint(PushData1[0].z)");

    ShaderProgramDescriptor shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    ResourceDescriptor<ShaderProgram> dof("DepthOfField", shaderDescriptor );
    dof.waitForReady(false);

    _dofShader = CreateResource(dof);
    PipelineDescriptor pipelineDescriptor = {};
    pipelineDescriptor._stateBlock = _context.get2DStateBlock();
    pipelineDescriptor._shaderProgramHandle = _dofShader;
    pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

    _pipeline = _context.newPipeline( pipelineDescriptor );

    const vec2<U16> resolution = _parent.screenRT()._rt->getResolution();
    _constants.data[0]._vec[0].xy.set(resolution.width, resolution.height );

    parametersChanged();
}

DoFPreRenderOperator::~DoFPreRenderOperator()
{
    DestroyResource(_dofShader);
}

bool DoFPreRenderOperator::ready() const noexcept
{
    return Get(_dofShader)->getState() == ResourceState::RES_LOADED && PreRenderOperator::ready();
}

void DoFPreRenderOperator::parametersChanged() noexcept
{
    const auto& params = _context.context().config().rendering.postFX.dof;

    _constants.data[0]._vec[0].zw = params.focalPoint;
    _constants.data[0]._vec[1].z = params.focalDepth;
    _constants.data[0]._vec[1].w = params.focalLength;
    _constants.data[0]._vec[2].x = g_FStopValues[to_base( TypeUtil::StringToFStops( params.fStop ) )];
    _constants.data[0]._vec[2].y = params.ndofstart;
    _constants.data[0]._vec[2].z = params.ndofdist;
    _constants.data[0]._vec[2].w = params.fdofstart;
    _constants.data[0]._vec[3].x = params.fdofdist;
    _constants.data[0]._vec[3].y = params.vignout;
    _constants.data[0]._vec[3].z = params.vignin;
    _constants.data[0]._vec[3].w = params.debugFocus ? 1.f : 0.f;
    _constants.data[1]._vec[0].x = params.manualdof ? 1.f : 0.f;
    _constants.data[1]._vec[0].y = params.vignetting ? 1.f : 0.f;
    _constants.data[1]._vec[0].z = params.autoFocus ? 1.f : 0.f;
}

void DoFPreRenderOperator::reshape(const U16 width, const U16 height)
{
    PreRenderOperator::reshape(width, height);
    _constants.data[0]._vec[0].xy.set(width, height);
}

bool DoFPreRenderOperator::execute([[maybe_unused]] const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut)
{
    _constants.data[0]._vec[1].xy = cameraSnapshot._zPlanes;

    const auto& screenAtt = input._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);
    const auto& extraAtt = _parent.getLinearDepthRT()._rt->getAttachment(RTAttachmentType::COLOUR);
    const auto& screenTex = Get(screenAtt->texture())->getView();
    const auto& extraTex  = Get(extraAtt->texture())->getView();

    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
    cmd->_usage = DescriptorSetUsage::PER_DRAW;
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, screenTex, screenAtt->_descriptor._sampler );
    }
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, extraTex, extraAtt->_descriptor._sampler );
    }

    GFX::BeginRenderPassCommand beginRenderPassCmd{};
    beginRenderPassCmd._target = output._targetID;
    beginRenderPassCmd._descriptor = _screenOnlyDraw;
    beginRenderPassCmd._name = "DO_DOF_PASS";
    beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = DEFAULT_CLEAR_ENTRY;
    GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

    GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = _pipeline;
    GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_fastData = _constants;
    

    GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.emplace_back();
    GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);

    return true;
}
}
