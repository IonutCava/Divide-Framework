#pragma once
#ifndef _PRE_RENDER_OPERATOR_H_
#define _PRE_RENDER_OPERATOR_H_

#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Platform/Video/Headers/CommandsImpl.h"
#include "Platform/Video/Textures/Headers/Texture.h"


namespace Divide {
struct Configuration;

class Quad3D;
class Camera;
class Texture;
class ShaderProgram;

FWD_DECLARE_MANAGED_CLASS(ShaderProgram);

enum class RenderStage : U8;

enum class FilterType : U16 {
    FILTER_SS_ANTIALIASING,
    FILTER_SS_AMBIENT_OCCLUSION,
    FILTER_SS_REFLECTIONS,
    FILTER_DEPTH_OF_FIELD,
    FILTER_MOTION_BLUR,
    FILTER_BLOOM,
    FILTER_LUT_CORECTION,
    FILTER_UNDERWATER,
    FILTER_NOISE,
    FILTER_VIGNETTE,
    FILTER_COUNT
};

enum class FilterSpace : U8 {
    // HDR Space: operators that work AND MODIFY the HDR screen target (e.g. SSAO, SSR)
    FILTER_SPACE_HDR = 0,
    // HDR Space: operators that work on the HDR target (e.g. Bloom, DoF)
    FILTER_SPACE_HDR_POST_SS,
    // LDR Space: operators that work on the post-tonemap target (e.g. Post-AA)
    FILTER_SPACE_LDR,
    COUNT
};

class PreRenderBatch;
/// It's called a prerender operator because it operates on the buffer before "rendering" to the screen
/// Technically, it's a post render operation
class NOINITVTABLE PreRenderOperator {
   public:
    /// The RenderStage is used to inform the GFXDevice of what we are currently
    /// doing to set up appropriate states
    /// The target is the full screen quad to which we want to apply our
    /// operation to generate the result
    PreRenderOperator(GFXDevice& context, PreRenderBatch& parent, FilterType operatorType);
    virtual ~PreRenderOperator() = default;

    /// Return true if we rendered into "output"
    virtual bool execute(const Camera* camera, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut);

    virtual void reshape(U16 width, U16 height);

    [[nodiscard]] FilterType operatorType() const noexcept { return _operatorType; }

    virtual void onToggle(bool state);

    [[nodiscard]] virtual bool ready() const { return true; }

   protected:
    GFXDevice& _context;

    PreRenderBatch& _parent;
    GFX::DrawCommand _pointDrawCmd = {};
    GFX::DrawCommand _triangleDrawCmd = {};
    RTDrawDescriptor _screenOnlyDraw;
    FilterType  _operatorType = FilterType::FILTER_COUNT;
    bool _enabled = true;
};

};  // namespace Divide
#endif
