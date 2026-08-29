#pragma once

#include <chrono>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprutils/math/Box.hpp>

namespace Render {
    class ITexture;
    class IFramebuffer;
}

class CEventLoopTimer;

// Plays a CCTV "signal lost" burst over a closing window: its own content
// tears apart (jitter, ghosts, displaced slices) for the whole burst while
// static, backdrop and the caption ramp in on top; a fade tail dissolves it
// after the close. Effects are screen-space and clipped inside the window's
// border. Two entry points:
//  - dispatchClose(): play the burst over the still-alive window, send the real
//    close at close_at * duration — the tile slot stays occupied meanwhile.
//  - onWindowClose(): post-hoc burst for closes we did not initiate.
class CFeedLossManager {
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
        uint32_t                              seed = 0;
        // window content captured at effect start; drives the tearing/ghosting
        // phase. null (capture failed / post-hoc close) = noise-only fallback.
        SP<Render::IFramebuffer>              snapshot;
    };

    // a managed close: effect already playing over the live window, real close
    // scheduled. suppresses the duplicate burst when the close event lands.
    struct SPending {
        PHLWINDOWREF                          window;
        SP<CEventLoopTimer>                   timer;
        std::chrono::steady_clock::time_point start;
    };

    bool                              beginEffect(const PHLWINDOW& w);
    void                              ensureNoiseFrames();
    SP<Render::ITexture>              textTexture();
    void                              drawEffect(const SEffect& e, const PHLMONITOR& mon, double elapsedMs, double durationMs, double fadeMs);
    static bool                       matches(std::optional<std::regex>& cache, std::string& cachedPattern, const std::string& pattern, const std::string& s);

    std::vector<SP<Render::ITexture>> m_noiseFrames;
    SP<Render::ITexture>              m_textTex;
    std::vector<SEffect>              m_effects;
    std::vector<SPending>             m_pending;

    // compiled ignore_class/ignore_title regexes, rebuilt only when the
    // pattern string changes (config reload) instead of on every window event
    std::optional<std::regex>         m_ignoreClassRe;
    std::string                       m_ignoreClassPattern;
    std::optional<std::regex>         m_ignoreTitleRe;
    std::string                       m_ignoreTitlePattern;
};

inline CFeedLossManager g_feedLoss;
