#pragma once

#include <vector>

#include <GLES3/gl32.h>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Box.hpp>

class CShader;

namespace Render {
    class IFramebuffer;
    class ITexture;
}

// One surface (main, subsurface or popup) of a matched window, in
// monitor-local logical px, plus the rounding its own draw used.
struct STintBox {
    CBox  box;
    float rounding      = 0.F; // scaled px
    float roundingPower = 2.F;
    // set only for sampleable RGBA surfaces; keeps the buffer alive until drawn
    SP<Render::ITexture> alphaTex;
};

// Tints the pixels of a matched window in place: at RENDER_POST_WINDOW the
// surface elements that window just queued are collected, and one custom
// pass element is queued right after them. When the pass draws it, the
// element copies those boxes out of the framebuffer being rendered and draws
// them back through the tint shader, scissored to the frame's damage (so no
// pixel is tinted twice across frames). Anything drawn later -- other
// windows, layers -- stays on top and untinted.
class CTintManager {
  public:
    void onRenderStage(eRenderStage stage);
    void onConfigReloaded();
    void reset();
    void drawBoxes(const std::vector<STintBox>& boxes);

  private:
    bool        matches(const PHLWINDOW& w) const;
    bool        ensureShader();
    bool        ensureCopy(const Vector2D& size);

    SP<CShader>              m_shader;
    bool                     m_shaderFailed = false;
    GLuint                   m_vao          = 0;
    GLuint                   m_vbo          = 0;
    SP<Render::IFramebuffer> m_copy;

    struct {
        GLint tex, surfTex, useSurfAlpha, surfM, surfO, uvScale, uvOffset, resolution, radius, roundingPower, tintHS, strength, satLo, satHi, minLuma, lightLo, lightHi;
    } m_uni = {};

    // tint hue/saturation, recomputed and re-uploaded only when col.tint
    // changes (e.g. a matugen reload) or the shader is rebuilt
    int64_t m_tintKey      = -1;
    bool    m_tintUploaded = false;
    float   m_tintH = 0.F, m_tintS = 0.F;

    // pass element count at RENDER_PRE_WINDOW for a matched window, -1 = none
    long m_startIndex = -1;
};

class CTintPassElement : public IPassElement {
  public:
    explicit CTintPassElement(std::vector<STintBox>&& boxes) : m_boxes(std::move(boxes)) {}
    virtual ~CTintPassElement() = default;

    virtual std::vector<UP<IPassElement>> draw();
    virtual bool                          needsLiveBlur() {
        return false;
    }
    virtual bool needsPrecomputeBlur() {
        return false;
    }
    virtual const char* passName() {
        return "CTintPassElement";
    }
    virtual ePassElementType type() {
        return EK_CUSTOM;
    }
    // no bounding box + undiscardable: simplify() must never cull the tint,
    // drawBoxes() clips to damage itself
    virtual std::optional<CBox> boundingBox() {
        return std::nullopt;
    }
    virtual bool undiscardable() {
        return true;
    }
    virtual CRegion opaqueRegion() {
        return {};
    }

  private:
    std::vector<STintBox> m_boxes;
};

inline CTintManager g_tint;
