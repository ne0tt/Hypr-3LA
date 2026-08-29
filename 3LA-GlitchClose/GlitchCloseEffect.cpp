#include "GlitchCloseEffect.hpp"
#include "globals.hpp"
#include "shader.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <regex>

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#include <hyprland/src/protocols/XDGDialog.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>
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

static float unit(const SP<Config::Values::CFloatValue>& v) {
    return std::clamp(static_cast<float>(v->value()), 0.F, 1.F);
}

// recompiles only when the pattern string differs from what's cached, so the
// common case (pattern unchanged since last call) is just a regex_search
bool CGlitchCloseManager::matches(std::optional<std::regex>& cache, std::string& cachedPattern, const std::string& pattern, const std::string& s) {
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

// Compiled lazily on first draw: that happens mid-render, so the EGL context is
// guaranteed current. A failure latches -- there is no point recompiling the
// same broken source once per frame for the rest of the session.
bool CGlitchCloseManager::ensureShader() {
    if (m_shader)
        return true;
    if (m_shaderFailed)
        return false;

    m_shaderFailed = true; // cleared only on full success below

    if (g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL) {
        Log::logger->log(Log::ERR, "[3LA-GlitchClose] not a GL renderer, effect disabled");
        HyprlandAPI::addNotification(PHANDLE, "[3LA-GlitchClose] requires the GL renderer", CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
        return false;
    }

    auto shader = makeShared<CShader>();
    if (!shader->createProgram(GLITCH_VERT, GLITCH_FRAG)) {
        Log::logger->log(Log::ERR, "[3LA-GlitchClose] glitch shader failed to compile, effect disabled");
        HyprlandAPI::addNotification(PHANDLE, "[3LA-GlitchClose] shader compilation failed", CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
        return false;
    }

    const GLuint PROG = shader->program();

    // these names are not in eShaderUniform, so CShader::setUniform* cannot
    // reach them -- resolve by name once and set them with raw glUniform*
    auto loc      = [PROG](const char* n) { return glGetUniformLocation(PROG, n); };
    m_uni.tex           = loc("tex");
    m_uni.hasTex        = loc("hasTex");
    m_uni.uvOffset      = loc("uvOffset");
    m_uni.uvScale       = loc("uvScale");
    m_uni.resolution    = loc("resolution");
    m_uni.progress      = loc("progress");
    m_uni.env           = loc("env");
    m_uni.seed          = loc("seed");
    m_uni.strength      = loc("strength");
    m_uni.aberration    = loc("aberration");
    m_uni.blocks        = loc("blocks");
    m_uni.noiseAmount   = loc("noiseAmount");
    m_uni.scanlines     = loc("scanlines");
    m_uni.roll          = loc("roll");
    m_uni.melt          = loc("melt");
    m_uni.tear          = loc("tear");
    m_uni.tearSpeed     = loc("tearSpeed");
    m_uni.ghost         = loc("ghost");
    m_uni.vignette      = loc("vignette");
    m_uni.backdropAlpha = loc("backdropAlpha");
    m_uni.colBackdrop   = loc("colBackdrop");
    m_uni.colFringe1    = loc("colFringe1");
    m_uni.colFringe2    = loc("colFringe2");

    // our own quad rather than CShader's private VAO, so nothing here depends on
    // Hyprland internals we cannot see
    const GLint POS = glGetAttribLocation(PROG, "pos");
    const GLint UV  = glGetAttribLocation(PROG, "texcoord");

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Render::GL::fullVerts), Render::GL::fullVerts.data(), GL_STATIC_DRAW);

    if (POS >= 0) {
        glEnableVertexAttribArray(POS);
        glVertexAttribPointer(POS, 2, GL_FLOAT, GL_FALSE, sizeof(Render::GL::SVertex), reinterpret_cast<void*>(offsetof(Render::GL::SVertex, x)));
    }
    if (UV >= 0) {
        glEnableVertexAttribArray(UV);
        glVertexAttribPointer(UV, 2, GL_FLOAT, GL_FALSE, sizeof(Render::GL::SVertex), reinterpret_cast<void*>(offsetof(Render::GL::SVertex, u)));
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_shader       = shader;
    m_shaderFailed = false;
    Log::logger->log(Log::INFO, "[3LA-GlitchClose] glitch shader compiled");
    return true;
}

bool CGlitchCloseManager::ensureTarget(SEffect& e, const PHLMONITOR& mon) {
    const int W = std::max(1, static_cast<int>(std::lround(e.localBox.w * mon->m_scale)));
    const int H = std::max(1, static_cast<int>(std::lround(e.localBox.h * mon->m_scale)));

    // monitor scale can change under a live effect; reallocate if it does
    if (e.target && e.target->isAllocated() && e.target->m_size == Vector2D{static_cast<double>(W), static_cast<double>(H)})
        return true;

    e.target = g_pHyprRenderer->createFB("3LA-GlitchClose");
    if (!e.target)
        return false;

    if (!e.target->alloc(W, H)) {
        e.target.reset();
        return false;
    }

    return true;
}

bool CGlitchCloseManager::beginEffect(const PHLWINDOW& w) {
    if (!w) {
        Log::logger->log(Log::TRACE, "[3LA-GlitchClose] skip: null window");
        return false;
    }
    if (!Desktop::View::validMapped(w)) {
        Log::logger->log(Log::TRACE, "[3LA-GlitchClose] skip: not validMapped class='{}'", w->m_class);
        return false;
    }
    if (w->isX11OverrideRedirect()) {
        Log::logger->log(Log::TRACE, "[3LA-GlitchClose] skip: X11 override-redirect class='{}'", w->m_class);
        return false;
    }

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

    // Stay strictly INSIDE the border, so the window keeps a clean border ring
    // framing the collapse and no border pixel is ever fed through the shader.
    // Without the inset the effect box sits flush against the border and edge
    // sampling (linear filtering at the 0..1 boundary) drags border colour into
    // the glitch, which shows up as a bright 1px frame around the effect.
    const double BORDER = static_cast<double>(std::max<int64_t>(w->getRealBorderSize(), 0));
    CBox         global = CBox{pos, SIZE}.expand(-BORDER);
    if (global.w < 1.0 || global.h < 1.0)
        return false;

    SEffect  e;
    e.window    = w;
    e.monitor   = MON;
    e.workspace = w->m_workspace;
    e.globalBox = global;
    e.localBox  = CBox{global.pos() - MON->m_position, global.size()};
    e.start     = std::chrono::steady_clock::now();

    uint32_t rng = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(e.start.time_since_epoch()).count()) ^ 0x9E3779B9u;
    // kept small: it lands inside sin() in the shader's hash, where a large
    // argument would cost precision
    e.seed = static_cast<float>(xorshift(rng) % 10000) / 1000.0F;

    if (!ensureTarget(e, MON))
        return false;

    // capture the window's pixels for the shader to tear apart. can fail for a
    // window already on its way out — the shader then falls back to static only.
    e.snapshot = g_pHyprRenderer->makeSnapshotFB(w);
    if (e.snapshot && !e.snapshot->isAllocated())
        e.snapshot.reset();

    Log::logger->log(Log::DEBUG, "[3LA-GlitchClose] glitch on class='{}' title='{}'", w->m_class, w->m_title);

    m_effects.emplace_back(e);
    g_pHyprRenderer->damageBox(global);
    return true;
}

// close the focused window, glitch style: the collapse plays over the live
// window (its tile slot stays occupied), and the real close request goes out at
// close_at * (duration + fade) — by default at the very end of the fade tail,
// so the slot is held for the whole effect and neighbors only re-tile once the
// overlay has fully dissolved, not while it's still fading on top of them.
SDispatchResult CGlitchCloseManager::dispatchClose() {
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
    const double AT    = std::clamp(g_closeAt->value(), 0.0F, 1.0F);
    // fraction of `duration` alone, NOT duration + fade: the close has to land
    // while the burst is still at full strength, so the last frame of the
    // window on screen is a glitched one. the fade tail then dissolves what is
    // left over the re-tiled layout, never over the live window.
    const auto   DELAY = std::chrono::milliseconds(static_cast<int64_t>(DUR * AT));

    auto         timer = makeShared<CEventLoopTimer>(
        DELAY,
        [this, wref = PHLWINDOWREF{W}](SP<CEventLoopTimer> self, void*) {
            if (const auto W = wref.lock())
                W->sendClose();
            // the pending entry stays until the close event lands, so the
            // resulting window.close does not spawn a duplicate effect
            g_pEventLoopManager->removeTimer(self);
        },
        nullptr);

    g_pEventLoopManager->addTimer(timer);
    m_pending.emplace_back(SPending{.window = W, .timer = timer, .start = std::chrono::steady_clock::now()});
    return {};
}

void CGlitchCloseManager::onWindowClose(const PHLWINDOW& w) {
    Log::logger->log(Log::TRACE, "[3LA-GlitchClose] window.close received, class='{}'", w ? w->m_class : "<null>");
    // drop dead refs and, if this close was one we initiated, swallow it —
    // its effect has been playing since the dispatcher fired. a stale entry (the
    // app sat on the close request, e.g. an unsaved-changes dialog) no longer
    // has anything on screen, so it must not swallow this much-later close.
    const auto   NOW     = std::chrono::steady_clock::now();
    const double DUR     = std::max<int64_t>(g_duration->value(), 100);
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

void CGlitchCloseManager::onRenderStage(eRenderStage stage) {
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

        // Hold at full strength until the window has actually gone. Starting
        // the fade while it is still mapped would dissolve the glitch and show
        // the untouched window for the length of the tail. The DUR * 2 cap
        // stops an app that refuses to die (unsaved-changes dialog) from
        // pinning the overlay on screen indefinitely.
        if (!it->fadeStart && ELAPSED >= DUR) {
            const auto W = it->window.lock();
            if (!W || !Desktop::View::validMapped(W) || ELAPSED >= DUR * 2.0)
                it->fadeStart = NOW;
        }

        const double FADED = it->fadeStart ? elapsedMsBetween(*it->fadeStart, NOW) : 0.0;
        if (it->fadeStart && FADED >= FADE) {
            g_pHyprRenderer->damageBox(it->globalBox);
            it = m_effects.erase(it); // releases the effect's framebuffers
            continue;
        }

        // FADE == 0 already expired above, so this cannot divide by zero
        const float ENV = it->fadeStart ? std::max(0.F, 1.F - static_cast<float>(FADED / FADE)) : 1.F;

        if (EMON == MON) {
            // don't paint over a workspace the closed window was never on
            const auto WS = it->workspace.lock();
            if ((!WS || MON->m_activeWorkspace == WS || MON->m_activeSpecialWorkspace == WS) && ensureShader() && ensureTarget(*it, MON)) {
                renderToTarget(*it, ELAPSED, DUR, ENV);
                drawEffect(*it, MON, ELAPSED, DUR, ENV);
            }
        }

        // keep the animation running until the effect expires
        g_pHyprRenderer->damageBox(it->globalBox);
        ++it;
    }
}

void CGlitchCloseManager::onConfigReloaded() {
    // not strictly required (matches() rebuilds on pattern mismatch anyway),
    // but frees the compiled regex promptly if the filter was cleared. the
    // shader reads every other option as a uniform each frame, so there is
    // nothing else to invalidate.
    m_ignoreClassRe.reset();
    m_ignoreTitleRe.reset();
    m_textTex.reset(); // re-render with the new text/font/size/colour
}

void CGlitchCloseManager::reset() {
    // don't leave a "close" the user requested unsent if the plugin unloads
    for (const auto& P : m_pending) {
        g_pEventLoopManager->removeTimer(P.timer);
        if (const auto W = P.window.lock())
            W->sendClose();
    }
    m_pending.clear();
    m_effects.clear();
    m_textTex.reset();

    // GL teardown needs the context current -- PLUGIN_EXIT runs outside a frame
    if (m_shader || m_vao || m_vbo) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();

        if (m_vao)
            glDeleteVertexArrays(1, &m_vao);
        if (m_vbo)
            glDeleteBuffers(1, &m_vbo);
        m_vao = 0;
        m_vbo = 0;

        if (m_shader) {
            m_shader->destroy();
            m_shader.reset();
        }
    }

    m_shaderFailed = false;
}

// Runs the shader into the effect's own framebuffer with an identity NDC quad,
// so none of Hyprland's private projection state is needed. Every piece of
// global GL state touched here is put back before returning: CRenderPass::render()
// runs later in this same frame and assumes it owns the context.
void CGlitchCloseManager::renderToTarget(const SEffect& e, double elapsedMs, double durationMs, float env) {
    const auto MON = e.monitor.lock();
    if (!MON || !e.target || !m_shader)
        return;

    // progress keeps climbing past 1 while the burst is held; the shader clamps
    // it, so the visuals sit pinned at maximum collapse and keep re-tearing
    const float T = elapsedMs / durationMs;

    const auto  SNAP = e.snapshot ? e.snapshot->getTexture() : nullptr;

    // Which sub-rect of the snapshot the window occupies. makeSnapshotFB can
    // hand back a monitor-sized framebuffer instead of a window-sized one, and
    // framebuffer textures are stored bottom-up, so v is measured from the
    // window's bottom edge.
    Vector2D uvOff{0.0, 0.0}, uvScl{1.0, 1.0};
    if (SNAP && (SNAP->m_size - MON->m_pixelSize).size() < 2.0) {
        const double SW = SNAP->m_size.x, SH = SNAP->m_size.y;
        const CBox   PX = CBox{e.localBox}.scale(MON->m_scale);
        uvOff = {PX.x / SW, (SH - PX.y - PX.h) / SH};
        uvScl = {PX.w / SW, PX.h / SH};
    }

    const CHyprColor BG = colorOr(g_backdropColor, CHyprColor{0.F, 0.F, 0.F, 1.F});
    const CHyprColor F1 = colorOr(g_fringe1, CHyprColor{1.F, 0.F, 0.6F, 1.F});
    const CHyprColor F2 = colorOr(g_fringe2, CHyprColor{0.F, 0.9F, 1.F, 1.F});

    const Vector2D   FBSIZE = e.target->m_size;
    Vector2D         prevViewport = g_pHyprRenderer->renderData().fbSize;
    if (prevViewport.x < 1.0 || prevViewport.y < 1.0)
        prevViewport = MON->m_pixelSize;

    auto guard = g_pHyprRenderer->bindTempFB(e.target);

    g_pHyprRenderer->setViewport(0, 0, FBSIZE.x, FBSIZE.y);
    g_pHyprRenderer->disableScissor();
    g_pHyprRenderer->blend(false);

    // never raw glUseProgram: CHyprOpenGLImpl caches the bound program to skip
    // redundant binds, and desyncing that cache breaks the next Hyprland draw.
    // going through useShader keeps the cache truthful, so no restore is needed.
    Render::GL::g_pHyprOpenGL->useShader(m_shader);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, SNAP ? SNAP->m_texID : 0);

    glUniform1i(m_uni.tex, 0);
    glUniform1f(m_uni.hasTex, SNAP ? 1.F : 0.F);
    glUniform2f(m_uni.uvOffset, uvOff.x, uvOff.y);
    glUniform2f(m_uni.uvScale, uvScl.x, uvScl.y);
    glUniform2f(m_uni.resolution, FBSIZE.x, FBSIZE.y);
    glUniform1f(m_uni.progress, T);
    glUniform1f(m_uni.env, env);
    glUniform1f(m_uni.seed, e.seed);
    glUniform1f(m_uni.strength, std::clamp(static_cast<float>(g_strength->value()), 0.F, 5.F));
    glUniform1f(m_uni.aberration, unit(g_aberration));
    glUniform1f(m_uni.blocks, unit(g_blocks));
    glUniform1f(m_uni.noiseAmount, unit(g_noise));
    glUniform1f(m_uni.scanlines, unit(g_scanlines));
    glUniform1f(m_uni.roll, unit(g_roll));
    glUniform1f(m_uni.melt, unit(g_melt));
    glUniform1f(m_uni.tear, unit(g_tear));
    glUniform1f(m_uni.tearSpeed, std::max(0.F, static_cast<float>(g_tearSpeed->value())));
    glUniform1f(m_uni.ghost, unit(g_ghost));
    glUniform1f(m_uni.vignette, unit(g_vignette));
    glUniform1f(m_uni.backdropAlpha, unit(g_backdropAlpha));
    glUniform3f(m_uni.colBackdrop, BG.r, BG.g, BG.b);
    glUniform3f(m_uni.colFringe1, F1.r, F1.g, F1.b);
    glUniform3f(m_uni.colFringe2, F2.r, F2.g, F2.b);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    g_pHyprRenderer->setViewport(0, 0, prevViewport.x, prevViewport.y);
    g_pHyprRenderer->blend(true);
    // `guard` rebinds the previous framebuffer here; the render pass re-scissors
    // per element, so the disabled scissor does not need restoring
}

void CGlitchCloseManager::drawEffect(const SEffect& e, const PHLMONITOR& mon, double elapsedMs, double durationMs, float env) {
    const auto TEX = e.target ? e.target->getTexture() : nullptr;
    if (!TEX)
        return;

    // the fade envelope is already baked into the shader's output alpha, so this
    // is a straight composite of what was just rendered
    CTexPassElement::SRenderData d;
    d.tex          = TEX;
    d.box          = CBox{e.localBox}.scale(mon->m_scale).round();
    d.a            = 1.F;
    d.flipEndFrame = true; // FB textures are y-flipped vs normal draws
    d.damage       = CRegion{d.box};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(d));

    drawCaption(e.localBox, mon, elapsedMs, durationMs, env);
}

// Cached glyph texture. renderText needs a live GL context, so this is only
// ever reached from inside a frame. Rendered at 2x the configured pt and drawn
// at half size, so it stays sharp when scaled up on a hidpi monitor.
SP<Render::ITexture> CGlitchCloseManager::textTexture() {
    if (m_textTex)
        return m_textTex;
    if (g_text->value().empty())
        return nullptr;

    const CHyprColor COL = colorOr(g_textColor, CHyprColor{1.F, 1.F, 1.F, 1.F});
    m_textTex = g_pHyprRenderer->renderText(g_text->value(), COL, g_textSize->value() * 2, false, g_font->value(), 0, 700);
    return m_textTex;
}

// A plate and a caption drawn OVER the shader output -- deliberately not fed
// through the glitch, so it stays legible while everything behind it tears.
void CGlitchCloseManager::drawCaption(const CBox& box, const PHLMONITOR& mon, double elapsedMs, double durationMs, float env) {
    if (g_text->value().empty())
        return;

    // Burst only. By the fade tail the close has gone out and neighbours may
    // have re-tiled into this box: a dissolving glitch over them reads as an
    // effect, a legible caption over them reads as a bug.
    if (elapsedMs >= durationMs)
        return;
    if (static_cast<float>(elapsedMs / durationMs) < std::clamp(g_textAt->value(), 0.F, 1.F))
        return;

    const auto BLINK = std::max<int64_t>(0, g_textBlink->value());
    if (BLINK > 0 && static_cast<int>(elapsedMs / BLINK) % 2 != 0)
        return;

    const auto TEX = textTexture();
    if (!TEX)
        return;

    auto     scaled = [&](CBox b) { return b.scale(mon->m_scale).round(); };

    Vector2D ts = TEX->m_size / 2.0; // rendered at 2x, see textTexture()
    const double FIT = std::min({1.0, box.w * 0.8 / std::max(ts.x, 1.0), box.h * 0.4 / std::max(ts.y, 1.0)});
    ts               = ts * FIT;

    const CBox   TB{box.x + (box.w - ts.x) / 2.0, box.y + (box.h - ts.y) / 2.0, ts.x, ts.y};
    const double PAD = static_cast<double>(std::max<int64_t>(0, g_textPadding->value()));
    const CBox   BG{TB.x - PAD, TB.y - PAD, TB.w + PAD * 2, TB.h + PAD * 2};

    const float BGA = unit(g_textBgAlpha) * env;
    if (BGA > 0.F) {
        const CHyprColor C = colorOr(g_textBgColor, CHyprColor{0.75F, 0.05F, 0.07F, 1.F});
        CRectPassElement::SRectData rd;
        rd.box   = scaled(BG);
        rd.color = CHyprColor{static_cast<float>(C.r), static_cast<float>(C.g), static_cast<float>(C.b), BGA};
        rd.round = static_cast<int>(std::max<int64_t>(0, g_textBgRound->value()));
        g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rd));
    }

    const float TA = unit(g_textAlpha) * env;
    if (TA > 0.F) {
        CTexPassElement::SRenderData td;
        td.tex    = TEX;
        td.box    = scaled(TB);
        td.a      = TA;
        td.damage = CRegion{td.box};
        g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(td));
    }
}
