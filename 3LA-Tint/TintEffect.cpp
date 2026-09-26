#include "TintEffect.hpp"
#include "globals.hpp"
#include "shader.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <regex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/pass/Pass.hpp>
#include <hyprland/src/render/pass/SurfacePassElement.hpp>
#include <hyprland/src/state/MonitorState.hpp>

// CRenderPass::m_passElements is private and has no accessor. Explicit
// template instantiation is exempt from access checks, which gives a
// standard-conforming way to name the member without `#define private`.
namespace {
    struct SPassTag {};
    auto& passElementsOf(Render::CRenderPass& p, SPassTag);
    template <auto PM>
    struct SRob {
        friend auto& passElementsOf(Render::CRenderPass& p, SPassTag) {
            return p.*PM;
        }
    };
    template struct SRob<&Render::CRenderPass::m_passElements>;

    auto& passElements() {
        return passElementsOf(g_pHyprRenderer->m_renderPass, SPassTag{});
    }

    float unit(const SP<Config::Values::CFloatValue>& v) {
        return std::clamp(static_cast<float>(v->value()), 0.F, 1.F);
    }
}

// recompiles only when the pattern string changes, same as 3LA-TitleBars'
// ignore_class/ignore_title, but a hit here means "tint", not "ignore"
static bool matchesPattern(std::optional<std::regex>& cache, std::string& cachedPattern, const std::string& pattern, const std::string& s) {
    if (pattern.empty() || s.empty())
        return false;

    if (!cache || pattern != cachedPattern) {
        cachedPattern = pattern;
        try {
            cache = std::regex{pattern};
        } catch (const std::regex_error&) {
            cache.reset(); // bad user regex: match nothing
            return false;
        }
    }

    return std::regex_search(s, *cache);
}

bool CTintManager::matches(const PHLWINDOW& w) const {
    static std::optional<std::regex> classRe, titleRe;
    static std::string               classPattern, titlePattern;

    // std::regex is slow and this runs per window per frame: memoise the
    // verdict per window, re-evaluated only when class/title/patterns change.
    // Keys are raw pointers, but the verdict is a pure function of the stored
    // strings, so a reused address can't return a wrong result.
    struct SVerdict {
        std::string cls, title, clsPat, titlePat;
        bool        hit = false;
    };
    static std::unordered_map<const void*, SVerdict> verdicts;

    if (!w || g_enabled->value() == 0)
        return false;

    const std::string& CLSPAT   = g_matchClass->value();
    const std::string& TITLEPAT = g_matchTitle->value();
    if (verdicts.size() > 256)
        verdicts.clear();
    auto& v = verdicts[w.get()];
    if (v.cls != w->m_class || v.title != w->m_title || v.clsPat != CLSPAT || v.titlePat != TITLEPAT) {
        v.cls      = w->m_class;
        v.title    = w->m_title;
        v.clsPat   = CLSPAT;
        v.titlePat = TITLEPAT;
        v.hit      = matchesPattern(classRe, classPattern, CLSPAT, w->m_class) || matchesPattern(titleRe, titlePattern, TITLEPAT, w->m_title);
    }
    return v.hit;
}

void CTintManager::onRenderStage(eRenderStage stage) {
    if (stage == RENDER_PRE_WINDOW) {
        const auto W  = g_pHyprRenderer->renderData().currentWindow.lock();
        m_startIndex  = matches(W) ? static_cast<long>(passElements().size()) : -1;
        if (m_startIndex >= 0)
        return;
    }

    if (stage != RENDER_POST_WINDOW || m_startIndex < 0)
        return;

    auto&      elems = passElements();
    const auto START = static_cast<size_t>(m_startIndex);
    m_startIndex     = -1;

    std::vector<STintBox> boxes;
    for (size_t i = START; i < elems.size(); ++i) {
        auto* el = elems[i].element.get();
        if (!el || el->type() != EK_SURFACE)
            continue;
        auto* s = static_cast<CSurfacePassElement*>(el);
        if (!s->m_data.texture)
            continue;
        const bool ROUND = s->m_data.mainSurface && !s->m_data.popup && !s->m_data.dontRound;
        const auto& TEX   = s->m_data.texture;
        // alpha is only meaningful for plain 2D RGBA buffers drawn untransformed
        // and only needed if the client hasn't declared the whole surface
        // opaque (Chrome's main window does; its menus, with shadows, don't)
        const auto& SURF  = s->m_data.surface;
        const bool  OPAQUE = SURF && CRegion{0, 0, SURF->m_current.size.x, SURF->m_current.size.y}.subtract(SURF->m_current.opaque).empty();
        const bool  ALPHA = !OPAQUE && TEX->m_type == Render::TEXTURE_RGBA && !TEX->m_opaque && TEX->m_transform == HYPRUTILS_TRANSFORM_NORMAL;
        boxes.push_back({s->getTexBox(), ROUND ? static_cast<float>(s->m_data.rounding) : 0.F, s->m_data.roundingPower, ALPHA ? TEX : nullptr});
    }

    // an animated (transformed) window queues its surfaces into a redirected
    // pass instead, so nothing is found here and the tint just skips that frame
    if (!boxes.empty())
        g_pHyprRenderer->m_renderPass.add(makeUnique<CTintPassElement>(std::move(boxes)));
}

bool CTintManager::ensureShader() {
    if (m_shader)
        return true;
    if (m_shaderFailed)
        return false;
    m_shaderFailed = true;

    if (g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL) {
        HyprlandAPI::addNotification(PHANDLE, "[3LA-Tint] requires the GL renderer", CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
        return false;
    }

    auto shader = makeShared<CShader>();
    if (!shader->createProgram(TINT_VERT, TINT_FRAG)) {
        HyprlandAPI::addNotification(PHANDLE, "[3LA-Tint] shader compilation failed", CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
        return false;
    }

    const GLuint PROG = shader->program();
    auto         loc  = [PROG](const char* n) { return glGetUniformLocation(PROG, n); };
    m_uni.tex           = loc("tex");
    m_uni.surfTex       = loc("surfTex");
    m_uni.useSurfAlpha  = loc("useSurfAlpha");
    m_uni.surfM         = loc("surfM");
    m_uni.surfO         = loc("surfO");
    m_uni.uvScale       = loc("uvScale");
    m_uni.uvOffset      = loc("uvOffset");
    m_uni.resolution    = loc("resolution");
    m_uni.radius        = loc("radius");
    m_uni.roundingPower = loc("roundingPower");
    m_uni.tintHS        = loc("tintHS");
    m_uni.strength      = loc("strength");
    m_uni.satLo         = loc("satLo");
    m_uni.satHi         = loc("satHi");
    m_uni.minLuma       = loc("minLuma");
    m_uni.lightLo       = loc("lightLo");
    m_uni.lightHi       = loc("lightHi");

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

    Render::GL::g_pHyprOpenGL->useShader(shader);
    glUniform1i(m_uni.tex, 0);
    glUniform1i(m_uni.surfTex, 1);

    m_shader       = shader;
    m_shaderFailed = false;
    m_tintUploaded = false;
    Log::logger->log(Log::INFO, "[3LA-Tint] tint shader compiled");
    return true;
}

// scratch copy of the framebuffer region; grown only, never shrunk
bool CTintManager::ensureCopy(const Vector2D& size) {
    if (m_copy && m_copy->isAllocated() && m_copy->m_size.x >= size.x && m_copy->m_size.y >= size.y)
        return true;
    const Vector2D NEW = m_copy && m_copy->isAllocated() ? Vector2D{std::max(size.x, m_copy->m_size.x), std::max(size.y, m_copy->m_size.y)} : size;
    m_copy             = g_pHyprRenderer->createFB("3LA-Tint");
    if (!m_copy || !m_copy->alloc(static_cast<int>(NEW.x), static_cast<int>(NEW.y))) {
        m_copy.reset();
        return false;
    }
    return true;
}

// exact float port of the shader's rgb2hsl() (hue and saturation only), so
// the CPU-side value matches what the shader used to compute per pixel
static std::pair<float, float> hueSat(const CHyprColor& col) {
    const float R = col.r, G = col.g, B = col.b;
    const float MX = std::max(R, std::max(G, B));
    const float MN = std::min(R, std::min(G, B));
    const float L  = (MX + MN) * 0.5F;
    const float D  = MX - MN;
    if (D < 1e-5F)
        return {0.F, 0.F};
    const float S = D / (1.F - std::abs(2.F * L - 1.F));
    float       h;
    if (MX == R) {
        const float X = (G - B) / D;
        h             = X - 6.F * std::floor(X / 6.F); // GLSL mod()
    } else if (MX == G)
        h = (B - R) / D + 2.F;
    else
        h = (R - G) / D + 4.F;
    return {h / 6.F, S};
}

void CTintManager::drawBoxes(const std::vector<STintBox>& boxes) {
    const auto MON = g_pHyprRenderer->renderData().pMonitor.lock();
    if (!MON || !ensureShader())
        return;

    const CRegion& DAMAGE = g_pHyprRenderer->renderData().damage;
    if (DAMAGE.empty())
        return;

    // renderData().fbSize can be stale (left at a temp FB's size), so clamp
    // to the monitor's pixel size instead
    const Vector2D fbSize = MON->m_pixelSize;

    const auto       TR    = Math::wlTransformToHyprutils(Math::invertTransform(MON->m_transform));
    if (const int64_t KEY = g_color->value(); KEY != m_tintKey) {
        m_tintKey      = KEY;
        m_tintUploaded = false;
        std::tie(m_tintH, m_tintS) = hueSat(CHyprColor{static_cast<uint64_t>(KEY)});
    }
    const float      LO    = unit(g_lightLo);
    const float      HI    = std::max(LO, unit(g_lightHi));

    GLint            curFB = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &curFB);

    for (const auto& B : boxes) {
        const CBox SCALED = CBox{B.box}.scale(MON->m_scale).round();

        CRegion    region = DAMAGE.copy().intersect(SCALED);
        if (region.empty())
            continue;

        // panel-space (GL) rect of the box, clamped to the framebuffer
        CBox px = SCALED;
        px.transform(TR, MON->m_transformedSize.x, MON->m_transformedSize.y);
        const int X0 = std::clamp(static_cast<int>(px.x), 0, static_cast<int>(fbSize.x));
        const int Y0 = std::clamp(static_cast<int>(px.y), 0, static_cast<int>(fbSize.y));
        const int X1 = std::clamp(static_cast<int>(px.x + px.w), 0, static_cast<int>(fbSize.x));
        const int Y1 = std::clamp(static_cast<int>(px.y + px.h), 0, static_cast<int>(fbSize.y));
        const int W = X1 - X0, H = Y1 - Y0;
        if (W <= 0 || H <= 0)
            continue;

        // only the damaged part needs copying: everything outside it is
        // scissored away below and was already tinted in earlier frames
        CBox dmg = region.getExtents();
        dmg.transform(TR, MON->m_transformedSize.x, MON->m_transformedSize.y);
        const int CX0 = std::clamp(static_cast<int>(std::floor(dmg.x)), X0, X1);
        const int CY0 = std::clamp(static_cast<int>(std::floor(dmg.y)), Y0, Y1);
        const int CX1 = std::clamp(static_cast<int>(std::ceil(dmg.x + dmg.w)), X0, X1);
        const int CY1 = std::clamp(static_cast<int>(std::ceil(dmg.y + dmg.h)), Y0, Y1);
        const int CW = CX1 - CX0, CH = CY1 - CY0;
        if (CW <= 0 || CH <= 0 || !ensureCopy({static_cast<double>(CW), static_cast<double>(CH)}))
            continue;

        auto* copyFB = dynamic_cast<Render::GL::CGLFramebuffer*>(m_copy.get());
        auto  copyTex = m_copy->getTexture();
        if (!copyFB || !copyTex)
            return;

        // 1. copy the already-composited box out (blit obeys scissor: disable it)
        g_pHyprRenderer->disableScissor();
        glBindFramebuffer(GL_READ_FRAMEBUFFER, curFB);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copyFB->getFBID());
        // a few scattered rects (cursor + clock, say): blit each on its own,
        // at its place inside the extents, instead of the whole span between
        std::vector<CBox> rects;
        region.forEachRect([&](const auto& R) { rects.emplace_back(R.x1, R.y1, R.x2 - R.x1, R.y2 - R.y1); });
        if (rects.size() > 1 && rects.size() <= 4) {
            for (auto r : rects) {
                r.transform(TR, MON->m_transformedSize.x, MON->m_transformedSize.y);
                const int RX0 = std::clamp(static_cast<int>(std::floor(r.x)), CX0, CX1);
                const int RY0 = std::clamp(static_cast<int>(std::floor(r.y)), CY0, CY1);
                const int RX1 = std::clamp(static_cast<int>(std::ceil(r.x + r.w)), CX0, CX1);
                const int RY1 = std::clamp(static_cast<int>(std::ceil(r.y + r.h)), CY0, CY1);
                if (RX1 > RX0 && RY1 > RY0)
                    glBlitFramebuffer(RX0, RY0, RX1, RY1, RX0 - CX0, RY0 - CY0, RX1 - CX0, RY1 - CY0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
        } else
            glBlitFramebuffer(CX0, CY0, CX1, CY1, 0, 0, CW, CH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, curFB);

        // 2. draw it back through the shader, one scissor per damage rect
        // raw glViewport: Hyprland's setViewport caches and can skip the call
        // when its cache is stale (CGLFramebuffer::bind sets it raw)
        GLint oldVP[4];
        glGetIntegerv(GL_VIEWPORT, oldVP);
        glViewport(X0, Y0, W, H);
        g_pHyprRenderer->blend(false);
        Render::GL::g_pHyprOpenGL->useShader(m_shader);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, copyTex->m_texID);
        glUniform2f(m_uni.uvScale, 1.0 / m_copy->m_size.x, 1.0 / m_copy->m_size.y);
        glUniform2f(m_uni.uvOffset, CX0 - X0, CY0 - Y0);
        glUniform2f(m_uni.resolution, W, H);
        glUniform1f(m_uni.radius, B.rounding);
        glUniform1f(m_uni.roundingPower, std::max(1.F, B.roundingPower));
        if (!m_tintUploaded) {
            glUniform2f(m_uni.tintHS, m_tintH, m_tintS);
            m_tintUploaded = true;
        }
        glUniform1f(m_uni.strength, unit(g_strength));
        glUniform1f(m_uni.satLo, unit(g_satLo));
        glUniform1f(m_uni.satHi, std::max(unit(g_satLo) + 0.001F, unit(g_satHi)));
        glUniform1f(m_uni.minLuma, unit(g_minLuma));
        glUniform1f(m_uni.lightLo, LO);
        glUniform1f(m_uni.lightHi, HI);

        const bool SURF_ALPHA = B.alphaTex != nullptr;
        glUniform1i(m_uni.useSurfAlpha, SURF_ALPHA);
        if (SURF_ALPHA) {
            // push the box's logical corners through the same transform that
            // placed it in panel space: p0 = uv(0,0), eu/ev = +u/+v directions
            auto corner = [&](double x, double y) {
                CBox c{x, y, 0, 0};
                c.transform(TR, MON->m_transformedSize.x, MON->m_transformedSize.y);
                return Vector2D{c.x, c.y};
            };
            const Vector2D P0 = corner(SCALED.x, SCALED.y);
            const Vector2D EU = corner(SCALED.x + SCALED.w, SCALED.y) - P0;
            const Vector2D EV = corner(SCALED.x, SCALED.y + SCALED.h) - P0;
            const double   DET = EU.x * EV.y - EV.x * EU.y;
            if (std::abs(DET) < 1e-6) {
                glUniform1i(m_uni.useSurfAlpha, 0);
            } else {
                // inverse of [EU EV] (column-major for GL)
                const GLfloat M[4] = {static_cast<GLfloat>(EV.y / DET), static_cast<GLfloat>(-EU.y / DET), static_cast<GLfloat>(-EV.x / DET), static_cast<GLfloat>(EU.x / DET)};
                glUniformMatrix2fv(m_uni.surfM, 1, GL_FALSE, M);
                glUniform2f(m_uni.surfO, X0 - P0.x, Y0 - P0.y);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, B.alphaTex->m_texID);
            }
        }

        glBindVertexArray(m_vao);
        region.forEachRect([](const auto& R) {
            Render::GL::g_pHyprOpenGL->scissor(&R, true);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        });
        glBindVertexArray(0);
        if (SURF_ALPHA) {
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0);
        }
        glBindTexture(GL_TEXTURE_2D, 0);

        glViewport(oldVP[0], oldVP[1], oldVP[2], oldVP[3]);
        g_pHyprRenderer->blend(true);
    }
}

void CTintManager::onConfigReloaded() {
    // colour / match changes take effect on the next full redraw
    for (const auto& m : State::monitorState()->monitors())
        g_pHyprRenderer->damageMonitor(m);
}

void CTintManager::reset() {
    // the pass keeps last frame's elements until the next frame starts; ours
    // must go now, while their vtable/destructor code is still mapped, or
    // Hyprland crashes freeing them after the .so is unloaded
    std::erase_if(passElements(), [](const auto& e) { return dynamic_cast<CTintPassElement*>(e.element.get()) != nullptr; });

    if (m_vao)
        glDeleteVertexArrays(1, &m_vao);
    if (m_vbo)
        glDeleteBuffers(1, &m_vbo);
    m_vao = m_vbo = 0;
    m_shader.reset();
    m_copy.reset();
    m_shaderFailed = false;
    m_tintKey      = -1;
    m_tintUploaded = false;
    m_startIndex   = -1;
}

std::vector<UP<IPassElement>> CTintPassElement::draw() {
    g_tint.drawBoxes(m_boxes);
    return {};
}
