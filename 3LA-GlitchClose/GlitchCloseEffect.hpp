#pragma once

#include <chrono>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <GLES3/gl32.h>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprutils/math/Box.hpp>

class CShader;
class CEventLoopTimer;

namespace Render {
    class ITexture;
    class IFramebuffer;
}

// Plays a shader-driven glitch collapse over a closing window: snapshot the
// window, hold its tile slot for the whole effect, send the real close only once
// the overlay is done. The entire visual is one GLSL fragment shader rather than
// a stack of rect/texture draws.
//
// Each frame the shader renders into a per-effect framebuffer (identity NDC
// quad, so no projection math is needed), and that framebuffer is handed to a
// normal CTexPassElement, which leaves Hyprland owning positioning, clipping,
// damage and colour management. Two entry points:
//  - dispatchClose(): play over the still-alive window, close it at close_at.
//  - onWindowClose(): post-hoc effect for closes we did not initiate.
class CGlitchCloseManager {
  public:
    SDispatchResult dispatchClose();
    void            onWindowClose(const PHLWINDOW& w);
    void            onRenderStage(eRenderStage stage);
    void            onConfigReloaded();
    void            reset();

  private:
    struct SEffect {
        PHLMONITORREF                         monitor;
        PHLWORKSPACEREF                       workspace;
        CBox                                  localBox;  // monitor-local, logical px
        CBox                                  globalBox; // layout coords, for damage
        std::chrono::steady_clock::time_point start;
        // the window this effect covers, kept only to reject a second effect
        // for the same window: a stale pending entry, or an app-driven close
        // landing while the dispatcher's effect is still playing.
        PHLWINDOWREF                          window;
        float                                 seed = 0.F;
        // window content captured at effect start. null (capture failed /
        // post-hoc close) drives the shader's hasTex = 0 static-only path.
        SP<Render::IFramebuffer>              snapshot;
        // what the shader renders into, then composited as a normal texture
        SP<Render::IFramebuffer>              target;
        // local uv -> snapshot uv, see buildUV(). invariant for the effect's
        // life, so it is built once and only rebuilt if the monitor is rescaled
        // or rotated (or the snapshot resized) under a live effect.
        Vector2D                              uvOffset{0.0, 0.0};
        float                                 uvXf[4] = {1.F, 0.F, 0.F, 1.F}; // column-major mat2
        bool                                  uvValid = false;
        double                                uvScale = 0.0;
        uint32_t                              uvTransform = 0;
        Vector2D                              uvSnapSize;
        // when the fade tail started. unset while the burst is still running or
        // while the overlay is HOLDING at full collapse waiting for the window
        // to actually vanish -- the fade may never dissolve back onto a window
        // that is still on screen, or the close ends on the real window.
        std::optional<std::chrono::steady_clock::time_point> fadeStart;
        // we suppressed the window's own close animation; put it back if the
        // window outlives the effect (an app that refused to close)
        bool                                  noAnimSet = false;
    };

    // a managed close: effect already playing over the live window, real close
    // scheduled. suppresses the duplicate effect when the close event lands, and
    // lives exactly as long as the effect it belongs to.
    struct SPending {
        PHLWINDOWREF        window;
        SP<CEventLoopTimer> timer;
    };

    // the clamped timing knobs, derived in one place so the close schedule, the
    // staleness window and the render loop can never drift apart
    struct STiming {
        double duration; // burst, ms
        double fade;     // fade-out tail, ms
        double hold;     // max wait for the window to actually vanish, ms
        double closeAt;  // fraction of the burst, 0..1
    };
    static STiming timing();

    bool        beginEffect(const PHLWINDOW& w);
    static void setNoAnim(const PHLWINDOWREF& w, bool on);
    bool        ensureShader();
    bool        ensureTarget(SEffect& e, const PHLMONITOR& mon);
    static void buildUV(SEffect& e, const PHLMONITOR& mon, const SP<Render::ITexture>& snap);
    void        renderToTarget(SEffect& e, const PHLMONITOR& mon, double elapsedMs, double durationMs, float env);
    void                 drawEffect(const SEffect& e, const PHLMONITOR& mon, double elapsedMs, double durationMs, float env);
    void                 drawCaption(const CBox& box, const PHLMONITOR& mon, double elapsedMs, double durationMs, float env);
    SP<Render::ITexture> textTexture();
    static bool matches(std::optional<std::regex>& cache, std::string& cachedPattern, const std::string& pattern, const std::string& s);

    // caption glyphs, re-rendered only when the text/font/size/colour changes
    SP<Render::ITexture>      m_textTex;

    SP<CShader>               m_shader;
    bool                      m_shaderFailed = false; // latch: never retry compiling per-frame
    GLuint                    m_vao          = 0;
    GLuint                    m_vbo          = 0;

    // custom uniform names are not in eShaderUniform, so they are resolved by
    // name once at compile time and set with raw glUniform* calls
    struct {
        GLint tex, hasTex, uvOffset, uvXf, resolution, progress, env, seed;
        GLint strength, aberration, blocks, noiseAmount, scanlines, roll, melt, tear, tearSpeed, ghost, vignette, backdropAlpha;
        GLint colBackdrop, colFringe1, colFringe2;
    } m_uni = {};

    std::vector<SEffect>      m_effects;
    std::vector<SPending>     m_pending;

    // compiled ignore_class/ignore_title regexes, rebuilt only when the
    // pattern string changes (config reload) instead of on every window event
    std::optional<std::regex> m_ignoreClassRe;
    std::string               m_ignoreClassPattern;
    std::optional<std::regex> m_ignoreTitleRe;
    std::string               m_ignoreTitlePattern;
};

inline CGlitchCloseManager g_glitchClose;
