#include "CornersDecoration.hpp"
#include "globals.hpp"

#include <algorithm>

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>

CCornersDecoration::CCornersDecoration(PHLWINDOW pWindow, bool flash) : IHyprWindowDecoration(pWindow), m_window(pWindow) {
    if (flash)
        flashSpawn();
}

// (re)arms the square wave from the current instant. the burst params are
// captured here, so reconfiguring the options mid-burst does not retime it.
void CCornersDecoration::flash(eFlashKind kind, int count, int duration) {
    if (count <= 0)
        return;

    m_flashing      = true;
    m_flashKind     = kind;
    m_flashStart    = std::chrono::steady_clock::now();
    m_flashCount    = count;
    m_flashDuration = std::max(duration, 16);
    damageEntire();
}

void CCornersDecoration::flashSpawn() {
    flash(FLASH_SPAWN, g_flashCount->value(), g_flashDuration->value());
}

void CCornersDecoration::flashFocus() {
    // a spawning window takes focus in the same tick it is opened, so the spawn
    // burst is held: a focus flash is dropped while one is still running.
    if (m_flashKind == FLASH_SPAWN && !flashExpired())
        return;

    flash(FLASH_FOCUS, g_focusFlashCount->value(), g_focusFlashDuration->value());
}

// purely time-based, so a burst expires on schedule even while nothing is being
// drawn — fullscreen and hidden windows never reach draw().
bool CCornersDecoration::flashExpired() const {
    if (!m_flashing)
        return true;

    const auto ELAPSED = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_flashStart).count();
    return m_flashCount <= 0 || ELAPSED / m_flashDuration >= m_flashCount * 2;
}

// square wave while flashing: on/off per phase duration, count on-pulses, then steady on.
// damages while active so the next frame keeps animating.
float CCornersDecoration::flashMultiplier() {
    if (!m_flashing)
        return 1.F;

    if (flashExpired()) {
        m_flashing  = false;
        m_flashKind = FLASH_NONE;
        return 1.F;
    }

    const auto ELAPSED = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_flashStart).count();

    damageEntire();
    return (ELAPSED / m_flashDuration) % 2 == 0 ? 1.F : 0.F;
}

SDecorationPositioningInfo CCornersDecoration::getPositioningInfo() {
    const double               E = g_offset->value() + g_thickness->value();

    SDecorationPositioningInfo info;
    info.policy         = DECORATION_POSITION_STICKY;
    info.edges          = DECORATION_EDGE_TOP | DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT;
    info.priority       = 9990; // below the core border (10000) -> stacked outside it
    info.desiredExtents = {{E, E}, {E, E}};
    info.reserved       = true;
    return info;
}

void CCornersDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {
    ; // geometry is computed from the live window box in draw()
}

static CHyprColor borderGradientColor(bool active) {
    static CConfigValue<Config::IComplexConfigValue> activeBorder("general:col.active_border");
    static CConfigValue<Config::IComplexConfigValue> inactiveBorder("general:col.inactive_border");

    auto* const DATA = active ? activeBorder.ptr() : inactiveBorder.ptr();
    if (DATA && DATA->getDataType() == Config::CVD_TYPE_GRADIENT) {
        const auto* GRADIENT = static_cast<Config::CGradientValueData*>(DATA);
        if (!GRADIENT->m_colors.empty())
            return GRADIENT->m_colors[0];
    }

    return CHyprColor{1.F, 1.F, 1.F, 1.F};
}

static CHyprColor cornerColor(bool active) {
    const auto CONFIGURED = active ? g_color->value() : g_colorInactive->value();
    if (CONFIGURED != 0)
        return CHyprColor{static_cast<uint64_t>(CONFIGURED)};

    // inactive falls back to col.active if that is set explicitly, so a single
    // configured color still applies to every window. only reached when
    // col.inactive is unset, so the extra lookup is off the common path.
    if (!active) {
        const auto ACTIVECOL = g_color->value();
        if (ACTIVECOL != 0)
            return CHyprColor{static_cast<uint64_t>(ACTIVECOL)};
    }

    return borderGradientColor(active);
}

std::array<CBox, 8> CCornersDecoration::cornerBoxes(const Vector2D& pos, const Vector2D& size, double outerDist) const {
    const double D = outerDist;
    const double T = std::max<double>(g_thickness->value(), 1);
    // clamp arm length so opposing brackets meet at most in the middle
    const double LX = std::clamp<double>(g_length->value(), T, (size.x + 2 * D) / 2.0);
    const double LY = std::clamp<double>(g_length->value(), T, (size.y + 2 * D) / 2.0);

    const double L = pos.x - D, R = pos.x + size.x + D; // outer corners
    const double U = pos.y - D, B = pos.y + size.y + D;

    // per corner: horizontal arm (full length), vertical arm inset by T to avoid
    // double-blending the corner square with translucent colors
    return {
        CBox{L, U, LX, T},           CBox{L, U + T, T, LY - T},           // top-left
        CBox{R - LX, U, LX, T},      CBox{R - T, U + T, T, LY - T},       // top-right
        CBox{L, B - T, LX, T},       CBox{L, B - LY, T, LY - T},          // bottom-left
        CBox{R - LX, B - T, LX, T},  CBox{R - T, B - LY, T, LY - T},      // bottom-right
    };
}

void CCornersDecoration::draw(PHLMONITOR pMonitor, float const& a) {
    // retire an elapsed burst before the early-returns below, so the flag does not
    // linger on a window that is fullscreen or hidden for the whole burst
    if (m_flashing && flashExpired()) {
        m_flashing  = false;
        m_flashKind = FLASH_NONE;
    }

    const auto PWINDOW = m_window.lock();
    if (!PWINDOW || !Desktop::View::validMapped(PWINDOW) || PWINDOW->isHidden())
        return;

    if (Fullscreen::controller()->isFullscreen(PWINDOW))
        return;

    // evaluate the flash envelope first: during an off-phase nothing is drawn, so
    // bail before resolving colors or building any boxes/pass elements
    const float FLASH = flashMultiplier();
    if (FLASH <= 0.F)
        return;

    const bool ACTIVE = Desktop::focusState()->isWindowActive(PWINDOW);

    // Brackets are fully decoupled from window opacity: the `a` the renderer hands
    // us folds in decoration:active_opacity/inactive_opacity and window opacity
    // rules, so it is deliberately ignored. Only the configured color's own alpha
    // and the flash envelope affect the final alpha.
    CHyprColor col = cornerColor(ACTIVE);
    col = CHyprColor{static_cast<float>(col.r), static_cast<float>(col.g), static_cast<float>(col.b), static_cast<float>(col.a * FLASH)};
    if (col.a <= 0.F)
        return;

    const double D = PWINDOW->getRealBorderSize() + g_offset->value() + g_thickness->value();

    Vector2D     offset = PWINDOW->m_floatingOffset - pMonitor->m_position;
    if (PWINDOW->m_workspace)
        offset = offset + PWINDOW->m_workspace->m_renderOffset->value();

    for (auto box : cornerBoxes(PWINDOW->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT), //
                                PWINDOW->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT), D)) {
        CRectPassElement::SRectData data;
        data.box   = box.translate(offset).scale(pMonitor->m_scale).round();
        data.color = col;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));
    }
}

eDecorationType CCornersDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CCornersDecoration::updateWindow(PHLWINDOW pWindow) {
    damageEntire();
}

void CCornersDecoration::damageEntire() {
    const auto PWINDOW = m_window.lock();
    if (!PWINDOW)
        return;

    const double D   = PWINDOW->getRealBorderSize() + g_offset->value() + g_thickness->value();
    CBox         box = PWINDOW->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    box.translate(PWINDOW->m_floatingOffset).expand(D);
    g_pHyprRenderer->damageBox(box);
}

eDecorationLayer CCornersDecoration::getDecorationLayer() {
    return DECORATION_LAYER_OVER;
}

uint64_t CCornersDecoration::getDecorationFlags() {
    return DECORATION_PART_OF_MAIN_WINDOW | DECORATION_NON_SOLID;
}

std::string CCornersDecoration::getDisplayName() {
    return "3LA-Corners";
}
