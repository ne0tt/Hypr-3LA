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

// Plays a shader-driven glitch collapse over a closing window. Same mechanism as
// 3LA-Feed-Loss -- snapshot the window, hold its tile slot for the whole effect,
// send the real close only once the overlay is done -- but the entire visual is
// one GLSL fragment shader rather than a stack of rect/texture draws.
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
    };

    // a managed close: effect already playing over the live window, real close
    // scheduled. suppresses the duplicate effect when the close event lands.
    struct SPending {
        PHLWINDOWREF                          window;
        SP<CEventLoopTimer>                   timer;
        std::chrono::steady_clock::time_point start;
    };

    bool        beginEffect(const PHLWINDOW& w);
    bool        ensureShader();
    bool        ensureTarget(SEffect& e, const PHLMONITOR& mon);
    void        renderToTarget(const SEffect& e, double elapsedMs, double durationMs, float env);
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
        GLint tex, hasTex, uvOffset, uvScale, resolution, progress, env, seed;
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
