#include "FeedLossEffect.hpp"
#include "globals.hpp"

#include <algorithm>
#include <regex>
#include <drm_fourcc.h>

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#include <hyprland/src/protocols/XDGDialog.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

static uint32_t xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

static double elapsedMsBetween(const std::chrono::steady_clock::time_point& from, const std::chrono::steady_clock::time_point& to) {
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(to - from).count();
}

// config color, where 0 means "use the built-in default"
static CHyprColor colorOr(const SP<Config::Values::CColorValue>& v, const CHyprColor& def) {
    const auto C = v->value();
    return C != 0 ? CHyprColor{static_cast<uint64_t>(C)} : def;
}

// recompiles only when the pattern string differs from what's cached, so the
// common case (pattern unchanged since last call) is just a regex_search
bool CFeedLossManager::matches(std::optional<std::regex>& cache, std::string& cachedPattern, const std::string& pattern, const std::string& s) {
    if (pattern.empty() || s.empty())
        return false;

    if (!cache || pattern != cachedPattern) {
        cachedPattern = pattern;
        try {
            cache = std::regex{pattern};
        } catch (const std::regex_error&) {
            cache.reset(); // bad user regex: ignore the filter
            return false;
        }
    }

    return std::regex_search(s, *cache);
}

bool CFeedLossManager::beginEffect(const PHLWINDOW& w) {
    if (!w || !Desktop::View::validMapped(w) || w->isX11OverrideRedirect())
        return false;

    // child windows (file open/save dialogs etc.): parent() covers xdg-toplevel
    // parents (incl. xdg-foreign) and X11 transient-for, isModal() the X11
    // modal hint, m_dialog the xdg-dialog-v1 modal flag
    if (g_ignoreChildren->value() > 0) {
        if (w->parent() || w->isModal())
            return false;
        if (!w->m_isX11 && w->m_xdgSurface && w->m_xdgSurface->m_toplevel && w->m_xdgSurface->m_toplevel->m_dialog && w->m_xdgSurface->m_toplevel->m_dialog->modal)
            return false;
    }

    // portal file choosers (VS Code / Electron / Flatpak "Open File" dialogs)
    // live in a separate process and mostly never get parented on Wayland —
    // catch them by class/title instead. the CACHED m_class/m_title, not
    // fetchClass()/fetchTitle(): the close event fires from unmapWindow(),
    // when the xdg toplevel resource the fetchers read may already be gone
    if (matches(m_ignoreClassRe, m_ignoreClassPattern, g_ignoreClass->value(), w->m_class.empty() ? w->fetchClass() : w->m_class))
        return false;
    if (matches(m_ignoreTitleRe, m_ignoreTitlePattern, g_ignoreTitle->value(), w->m_title.empty() ? w->fetchTitle() : w->m_title))
        return false;

    const auto MON = w->m_monitor.lock();
    if (!MON)
        return false;

    const auto SIZE = w->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    if (SIZE.x < g_minSize->value() || SIZE.y < g_minSize->value())
        return false;

    Vector2D pos = w->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT) + w->m_floatingOffset;
    if (w->m_workspace)
        pos = pos + w->m_workspace->m_renderOffset->value();

    // stay INSIDE the border: the window keeps its border ring visible,
    // framing the static until the close actually lands
    CBox global{pos, SIZE};

    SEffect e;
    e.monitor   = MON;
    e.workspace = w->m_workspace;
    e.globalBox = global;
    e.localBox  = CBox{global.pos() - MON->m_position, global.size()};
    e.start     = std::chrono::steady_clock::now();
    e.seed      = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(e.start.time_since_epoch()).count()) ^ 0x9E3779B9u;

    // capture the window's pixels so the glitch phase can tear the actual
    // content apart. can fail for a window already on its way out — the effect
    // then falls back to noise-only.
    e.snapshot = g_pHyprRenderer->makeSnapshotFB(w);
    if (e.snapshot && !e.snapshot->isAllocated())
        e.snapshot.reset();

    Log::logger->log(Log::DEBUG, "[3LA-Feed-Loss] burst on class='{}' title='{}'", w->m_class, w->m_title);

    m_effects.emplace_back(e);
    g_pHyprRenderer->damageBox(global);
    return true;
}

// close the focused window, feed-loss style: the burst plays over the live
// window (its tile slot stays occupied), and the real close request goes out at
// close_at * (duration + fade) — by default at the very end of the fade tail,
// so the slot is held for the whole effect and neighbors only re-tile once the
// overlay has fully dissolved, not while it's still fading on top of them.
SDispatchResult CFeedLossManager::dispatchClose() {
    const auto W = Desktop::focusState()->window();
    if (!W)
        return {.success = false, .error = "no active window"};

    for (const auto& P : m_pending) {
        if (P.window.lock() == W)
            return {}; // close already in flight for this window
    }

    if (!beginEffect(W)) {
        W->sendClose(); // too small / not effect-worthy: behave like killactive
        return {};
    }

    const double DUR   = std::max<int64_t>(g_duration->value(), 100);
    const double FADE  = std::max<int64_t>(g_fade->value(), 0);
    const double AT    = std::clamp(g_closeAt->value(), 0.0F, 1.0F);
    const auto   DELAY = std::chrono::milliseconds(static_cast<int64_t>((DUR + FADE) * AT));

    auto         timer = makeShared<CEventLoopTimer>(
        DELAY,
        [this, wref = PHLWINDOWREF{W}](SP<CEventLoopTimer> self, void*) {
            if (const auto W = wref.lock())
                W->sendClose();
            // the pending entry stays until the close event lands, so the
            // resulting window.close does not spawn a duplicate burst
            g_pEventLoopManager->removeTimer(self);
        },
        nullptr);

    g_pEventLoopManager->addTimer(timer);
    m_pending.emplace_back(SPending{.window = W, .timer = timer, .start = std::chrono::steady_clock::now()});
    return {};
}

void CFeedLossManager::onWindowClose(const PHLWINDOW& w) {
    // drop dead refs and, if this close was one we initiated, swallow it —
    // its burst has been playing since the dispatcher fired. a stale entry (the
    // app sat on the close request, e.g. an unsaved-changes dialog) no longer
    // has a burst on screen, so it must not swallow this much-later close.
    const auto   NOW   = std::chrono::steady_clock::now();
    const double DUR   = std::max<int64_t>(g_duration->value(), 100);
    bool         managed = false;
    std::erase_if(m_pending, [&](const SPending& p) {
        const auto PW = p.window.lock();
        if (!PW) {
            g_pEventLoopManager->removeTimer(p.timer);
            return true;
        }
        if (PW == w) {
            g_pEventLoopManager->removeTimer(p.timer); // no-op if already fired
            managed = elapsedMsBetween(p.start, NOW) < DUR * 2.0;
            return true;
        }
        return false;
    });

    if (!managed)
        beginEffect(w);
}

void CFeedLossManager::onRenderStage(eRenderStage stage) {
    if (stage != RENDER_POST_WINDOWS || m_effects.empty())
        return;

    const auto MON = g_pHyprRenderer->renderData().pMonitor.lock();
    if (!MON)
        return;

    const auto   NOW  = std::chrono::steady_clock::now();
    const double DUR  = std::max<int64_t>(g_duration->value(), 100);
    const double FADE = std::max<int64_t>(g_fade->value(), 0);

    for (auto it = m_effects.begin(); it != m_effects.end();) {
        const auto EMON = it->monitor.lock();
        if (!EMON) {
            it = m_effects.erase(it);
            continue;
        }

        const double ELAPSED = elapsedMsBetween(it->start, NOW);
        if (ELAPSED >= DUR + FADE) {
            g_pHyprRenderer->damageBox(it->globalBox);
            it = m_effects.erase(it);
            continue;
        }

        if (EMON == MON) {
            // don't paint over a workspace the closed window was never on
            const auto WS = it->workspace.lock();
            if (!WS || MON->m_activeWorkspace == WS || MON->m_activeSpecialWorkspace == WS) {
                ensureNoiseFrames();
                drawEffect(*it, MON, ELAPSED, DUR, FADE);
            }
        }

        // keep the animation running until the effect expires
        g_pHyprRenderer->damageBox(it->globalBox);
        ++it;
    }
}

void CFeedLossManager::onConfigReloaded() {
    m_textTex.reset();     // re-render with the new text/font/size/color
    m_noiseFrames.clear(); // regenerate with the new static tint
    // not strictly required (matches() rebuilds on pattern mismatch anyway),
    // but frees the compiled regex promptly if the filter was cleared
    m_ignoreClassRe.reset();
    m_ignoreTitleRe.reset();
}

void CFeedLossManager::reset() {
    // don't leave a "close" the user requested unsent if the plugin unloads
    for (const auto& P : m_pending) {
        g_pEventLoopManager->removeTimer(P.timer);
        if (const auto W = P.window.lock())
            W->sendClose();
    }
    m_pending.clear();

    m_effects.clear();
    m_noiseFrames.clear();
    m_textTex.reset();
}

// 8 pre-baked frames of scanlined noise, cycled per frame. Generated once,
// lazily, so the GL context is guaranteed to exist (first use is mid-render).
// col.static tints the noise (regenerated on config reload).
void CFeedLossManager::ensureNoiseFrames() {
    if (!m_noiseFrames.empty())
        return;

    constexpr int        SIZE = 128, FRAMES = 8;
    uint32_t             rng = 0xC0FFEE42u;
    std::vector<uint8_t> px(SIZE * SIZE * 4);

    const CHyprColor     TINT = colorOr(g_staticColor, CHyprColor{1.F, 1.F, 1.F, 1.F});

    for (int f = 0; f < FRAMES; ++f) {
        for (int y = 0; y < SIZE; ++y) {
            for (int x = 0; x < SIZE; ++x) {
                uint8_t v = xorshift(rng) & 0xFF;
                if (y % 3 == 2)
                    v = v * 2 / 5; // scanlines
                uint8_t* p = &px[(y * SIZE + x) * 4];
                p[0] = static_cast<uint8_t>(v * TINT.r);
                p[1] = static_cast<uint8_t>(v * TINT.g);
                p[2] = static_cast<uint8_t>(v * TINT.b);
                p[3] = 255;
            }
        }

        const auto TEX = g_pHyprRenderer->createTexture(DRM_FORMAT_ABGR8888, px.data(), SIZE * 4, {SIZE, SIZE});
        if (TEX)
            m_noiseFrames.emplace_back(TEX);
    }
}

SP<Render::ITexture> CFeedLossManager::textTexture() {
    if (m_textTex)
        return m_textTex;

    const CHyprColor col = colorOr(g_textColor, CHyprColor{0.92F, 0.92F, 0.92F, 1.F});

    // rendered at 2x the configured pt and drawn at half size, so it stays
    // sharp when scaled up on hidpi monitors
    m_textTex = g_pHyprRenderer->renderText(g_text->value(), col, g_textSize->value() * 2, false, g_font->value(), 0, 700);
    return m_textTex;
}

void CFeedLossManager::drawEffect(const SEffect& e, const PHLMONITOR& mon, double elapsedMs, double durationMs, double fadeMs) {
    // T runs 0..1 over the burst itself and past 1 during the fade tail. The
    // overlay holds at full strength for the entire burst — the fade only
    // happens AFTER duration (i.e. after close_at <= 1 has sent the close), so
    // the last thing visible over the window is always the static, never the
    // window resurfacing from under a faded overlay.
    const float T   = elapsedMs / durationMs;
    const float ENV = elapsedMs < durationMs ? 1.F : (fadeMs > 0 ? std::max(0.F, 1.F - static_cast<float>((elapsedMs - durationMs) / fadeMs)) : 0.F);

    const CBox& BOX      = e.localBox;
    const bool  HASSNAP  = e.snapshot && e.snapshot->getTexture();
    auto        scaled   = [&](CBox b) { return b.scale(mon->m_scale).round(); };

    auto        addRect = [&](const CBox& b, const CHyprColor& col) {
        CRectPassElement::SRectData d;
        d.box   = scaled(b);
        d.color = col;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(d));
    };
    auto addTex = [&](SP<Render::ITexture> tex, const CBox& b, float a, const CBox& clip = {}) {
        CTexPassElement::SRenderData d;
        d.tex    = tex;
        d.box    = scaled(b);
        d.a      = a;
        d.damage = CRegion{d.box};
        if (!clip.empty()) {
            d.clipBox = scaled(clip);
            d.damage  = CRegion{d.clipBox};
        }
        g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(d));
    };

    // snapshot FBs can cover the whole monitor (window drawn at its on-screen
    // spot) or just the window; pick the dest box that matches so displaced
    // slices line up with where the window is/was either way
    CBox snapBox = BOX;
    if (HASSNAP && (e.snapshot->m_size - mon->m_pixelSize).size() < 2.0)
        snapBox = CBox{{0, 0}, mon->m_size};
    auto addSnap = [&](double dx, double dy, float a, const CBox& clip) {
        CTexPassElement::SRenderData d;
        d.tex          = e.snapshot->getTexture();
        d.box          = scaled(CBox{snapBox}.translate({dx, dy}));
        d.a            = a;
        d.flipEndFrame = true; // FB textures are y-flipped vs normal draws
        d.clipBox      = scaled(clip);
        d.damage       = CRegion{d.clipBox};
        g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(d));
    };

    // timeline: with a snapshot the window's own content tears apart for the
    // WHOLE burst — static/backdrop/caption ramp in over the tearing, and the
    // feed is still glitching (text on top) when the close goes out at the end.
    // without one (post-hoc close) the static starts immediately, as before.
    const float BACKDROP_ON = HASSNAP ? 0.50F : 0.08F;
    const float STATIC_ON   = HASSNAP ? 0.42F : 0.F;
    const float CAPTION_ON  = HASSNAP ? 0.58F : 0.16F;

    // glitch intensity: eases in, spikes as the feed collapses
    const float INTENSITY = std::min(1.F, 0.25F + T * 1.6F);

    // glitch strength (config, 0..5) scales amplitudes and slice/band counts:
    // 1 = the original look, 5 = extreme, 0 = glitch sections off entirely
    const float GS   = std::clamp(static_cast<float>(g_glitch->value()), 0.F, 5.F);
    const float GMUL = 0.5F + 0.5F * GS; // 1.0 at strength 1, 3.0 at 5

    // per-step RNG: geometry holds for 60ms so slices visibly tear instead of
    // shimmering every frame
    const auto  STEP = static_cast<uint32_t>(elapsedMs / 60.0);
    uint32_t    rng  = e.seed ^ (STEP * 2654435761u);
    auto        frand = [&](double lo, double hi) { return lo + (xorshift(rng) % 10000) / 10000.0 * (hi - lo); };

    // 1) backdrop ramping in as the feed dies (col.backdrop, default black)
    const float RAMP      = std::clamp((T - BACKDROP_ON) / 0.10F, 0.F, 1.F);
    const float BACKDROPA = std::clamp(static_cast<float>(g_backdropAlpha->value()), 0.F, 1.F) * RAMP * ENV;
    if (BACKDROPA > 0.F) {
        const CHyprColor BG = colorOr(g_backdropColor, CHyprColor{0.F, 0.F, 0.F, 1.F});
        addRect(BOX, CHyprColor{static_cast<float>(BG.r), static_cast<float>(BG.g), static_cast<float>(BG.b), BACKDROPA});
    }

    // 2) the window's own content glitching: frame jitter, ghost copies and
    //    displaced horizontal slices, escalating right up to the cut — no
    //    ramp-out, the burst ends mid-glitch and only the fade tail dissolves it
    if (HASSNAP && g_glitch->value() > 0) {
        const float SNAPA = ENV;

        // whole-frame horizontal jitter on some steps
        if (frand(0, 1) < std::min(0.95, (0.35 + INTENSITY * 0.3) * GMUL))
            addSnap(frand(-1, 1) * (2.0 + 8.0 * INTENSITY) * GMUL, 0, SNAPA, BOX);

        // ghost copies, drifting further out as it gets worse
        const double GHOST = (4.0 + 18.0 * INTENSITY) * GMUL;
        addSnap(-GHOST, 0, 0.22F * SNAPA, BOX);
        addSnap(GHOST, frand(-2, 2), 0.16F * SNAPA, BOX);

        // torn slices: bands of the frame shoved left/right
        const int SLICES = static_cast<int>((2 + INTENSITY * 9) * GMUL);
        for (int i = 0; i < SLICES; ++i) {
            const double H  = frand(4.0, std::max(8.0, BOX.h * 0.09));
            const double Y  = BOX.y + frand(0, std::max(BOX.h - H, 1.0));
            const double DX = frand(-1, 1) * (6.0 + 46.0 * INTENSITY) * GMUL;
            const CBox   SLICE{BOX.x, Y, BOX.w, H};
            addSnap(DX, 0, SNAPA, SLICE);

            // digital color fringe on some slices (col.fringe1/2, default magenta/cyan)
            if (i % 2 == 0) {
                const CHyprColor FC = (i / 2) % 2 == 0 ? colorOr(g_fringe1, CHyprColor{1.F, 0.F, 0.6F, 1.F}) : colorOr(g_fringe2, CHyprColor{0.F, 0.9F, 1.F, 1.F});
                addRect(SLICE, CHyprColor{static_cast<float>(FC.r), static_cast<float>(FC.g), static_cast<float>(FC.b), 0.12F * static_cast<float>(FC.a) * SNAPA});
            }
        }
    }

    // 3) animated static, ramping in as the signal degrades
    const size_t FRAME    = static_cast<size_t>(elapsedMs / 45.0);
    const float  STATRAMP = HASSNAP ? std::clamp((T - STATIC_ON) / 0.18F, 0.F, 1.F) : 1.F;
    const float  STATICA  = std::clamp(static_cast<float>(g_staticAlpha->value()), 0.F, 1.F) * STATRAMP * ENV;
    if (STATICA > 0.F && !m_noiseFrames.empty()) {
        addTex(m_noiseFrames[FRAME % m_noiseFrames.size()], BOX, STATICA);

        // stray noise bands over the picture even before the full static hits
        if (g_glitch->value() > 0) {
            const int BANDS = static_cast<int>((T < CAPTION_ON ? 2 : 1) * GMUL);
            for (int i = 0; i < BANDS; ++i) {
                const double H = frand(3.0, BOX.h * 0.05);
                const CBox   BAND{BOX.x, BOX.y + frand(0, std::max(BOX.h - H, 1.0)), BOX.w, H};
                addTex(m_noiseFrames[(FRAME + i + 1) % m_noiseFrames.size()], BAND, std::min(1.F, STATICA * 2.5F + 0.15F) * ENV);
            }
        }
    }

    // 4) the "SIGNAL LOST" caption, centered, blinking. Burst only, never the
    //    fade tail — by then the close has gone out and neighbors may have
    //    re-tiled into the box; fading static over them is fine, text is not.
    const float TEXTA = std::clamp(static_cast<float>(g_textAlpha->value()), 0.F, 1.F);
    if (T >= CAPTION_ON && T < 1.F && TEXTA > 0.F && !g_text->value().empty()) {
        const bool ON = g_textBlink->value() <= 0 || static_cast<int>(elapsedMs / 300.0) % 2 == 0;
        const auto TEX = ON ? textTexture() : nullptr;
        if (TEX) {
            Vector2D     ts = TEX->m_size / 2.0; // rendered at 2x, see textTexture()
            const double S  = std::min({1.0, BOX.w * 0.8 / std::max(ts.x, 1.0), BOX.h * 0.4 / std::max(ts.y, 1.0)});
            ts              = ts * S;
            const CBox TB{BOX.x + (BOX.w - ts.x) / 2.0, BOX.y + (BOX.h - ts.y) / 2.0, ts.x, ts.y};
            addTex(TEX, TB, ENV * TEXTA);
        }
    }
}
