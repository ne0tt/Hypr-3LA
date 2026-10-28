#include "MilSpecDecoration.hpp"
#include "globals.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/types/OverridableVar.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/render/pass/SurfacePassElement.hpp>
#include <hyprland/src/version.h>

static std::unordered_map<Desktop::View::CWindow*, CMilSpecDecoration*> g_decos;
static std::unordered_map<Desktop::View::CWindow*, CMilSpecShadowDecoration*> g_shadows;

CMilSpecDecoration* milspecFrameFor(const PHLWINDOW& w) {
    if (!w)
        return nullptr;

    const auto IT = g_decos.find(w.get());
    return IT != g_decos.end() ? IT->second : nullptr;
}

// ---------------------------------------------------------------------------
// real corner cut: clip the client's surface past the chamfer
// ---------------------------------------------------------------------------
// A client buffer is rectangular, so wherever the cut is deeper than the
// window's insets its corner pokes past the chamfer line. Hyprland draws a
// window surface through CSurfacePassElement: visibleRegion() is the clip the
// element renderer scissors the texture with, and opaqueRegion() is what the
// render pass culls everything underneath (wallpaper, shadows) with. Hooking
// both lets the plugin subtract the two corner triangles, so nothing of the
// client is drawn there and whatever lies beneath shows through -- the
// frame's covers then only have to fill inside the frame. The hooks are
// optional: if a symbol is not found the covers paint over the corner as
// before.
static CFunctionHook* g_visibleRegionHook = nullptr;
static CFunctionHook* g_opaqueRegionHook  = nullptr;
using visibleRegionFn                     = CRegion (*)(CSurfacePassElement*, Render::CRenderContext&, bool&);
using opaqueRegionFn                      = CRegion (*)(CSurfacePassElement*, Render::CRenderContext&);

// rows of pixels of window box `wb` whose centre lies past a 45 deg cut of
// `legs` px at the top-left / bottom-right corner. Pixel (i, j) from the
// corner is past the line when i + j + 1 < legs, so with integer legs the
// kept boundary pixels are exactly the ones the stroke's antialiasing
// straddles.
static void chamferRows(CRegion& out, const CBox& wb, double legsTL, double legsBR) {
    for (int j = 0; j + 1 < legsTL; ++j) {
        const int n = static_cast<int>(std::ceil(legsTL - j - 1));
        if (n <= 0)
            break;
        out.add(wb.x, wb.y + j, n, 1);
    }
    for (int j = 0; j + 1 < legsBR; ++j) {
        const int n = static_cast<int>(std::ceil(legsBR - j - 1));
        if (n <= 0)
            break;
        out.add(wb.x + wb.w - n, wb.y + wb.h - 1 - j, n, 1);
    }
}

// legs (layout px) of this surface's window past the chamfer at each corner;
// false when nothing is to be cut. Popups keep their shape; only the window
// and its subsurfaces are cut.
static bool cornerCutLegs(const CSurfacePassElement::SRenderData& D, double& tl, double& br) {
    if (!D.pWindow || D.popup)
        return false;

    const auto* F = milspecFrameFor(D.pWindow);
    if (!F || F->hidden() || g_corners->value() == 0 || g_chamfer->value() <= 0 || Fullscreen::controller()->isFullscreen(D.pWindow))
        return false;

    // the top inset includes a title bar's band, so with a bar the top-left
    // only cuts when the chamfer reaches below the bar
    const auto   INS = F->insets(D.pWindow);
    const CBox   FB  = F->frameBox(D.pWindow);
    const double C   = std::clamp<double>(g_chamfer->value(), 0.0, std::min(FB.w, FB.h) / 2.0);
    tl               = C - INS.l - INS.t;
    br               = C - INS.r - INS.b;
    return tl > 0 || br > 0;
}

// the cut for this surface in monitor-local px at `scale` (1 = layout px).
// pos / w / h are the window's for every non-popup surface (subsurfaces only
// change localPos), so the same cut applies to all of them; the box is scaled
// and rounded like the element renderer builds it.
static CRegion cornerCut(const CSurfacePassElement::SRenderData& D, const PHLMONITOR& PMONITOR, double scale, double tl, double br) {
    CBox wb{D.pos.x - PMONITOR->m_position.x, D.pos.y - PMONITOR->m_position.y, D.w, D.h};
    wb.scale(scale).round();

    CRegion cut;
    chamferRows(cut, wb, tl * scale, br * scale);
    return cut;
}

// same ABI as the members: hidden return slot, `this`, then the arguments
static CRegion hkVisibleRegion(CSurfacePassElement* self, Render::CRenderContext& ctx, bool& cancel) {
    CRegion r = reinterpret_cast<visibleRegionFn>(g_visibleRegionHook->m_original)(self, ctx, cancel);
    if (cancel)
        return r;

    double tl = 0, br = 0;
    if (!cornerCutLegs(self->m_data, tl, br))
        return r;

    const auto PMONITOR = self->m_data.pMonitor.lock();
    if (!PMONITOR)
        return r;

    const double SCALE = PMONITOR->m_scale;

    // most clients never set a hyprland_surface_v1 visible region, so the
    // original returns an empty region -- which the renderer takes as "no
    // clip" and draws the whole quad. Start from the quad itself then.
    if (r.empty()) {
        CBox tb = self->getTexBox();
        tb.scale(SCALE).round();
        r = CRegion{tb};
    }

    r.subtract(cornerCut(self->m_data, PMONITOR, SCALE, tl, br));
    return r;
}

static CRegion hkOpaqueRegion(CSurfacePassElement* self, Render::CRenderContext& ctx) {
    CRegion r = reinterpret_cast<opaqueRegionFn>(g_opaqueRegionHook->m_original)(self, ctx);
    if (r.empty())
        return r;

    double tl = 0, br = 0;
    if (!cornerCutLegs(self->m_data, tl, br))
        return r;

    const auto PMONITOR = self->m_data.pMonitor.lock();
    if (!PMONITOR)
        return r;

    // layout px: the render pass scales and rounds each rect itself. One
    // extra px of leg so that, after its rounding, the area left uncovered
    // beneath the window is never smaller than the area the surface skips.
    r.subtract(cornerCut(self->m_data, PMONITOR, 1.0, tl + 1, br + 1));
    return r;
}

static CFunctionHook* hookMember(const std::string& demangled, void* replacement) {
    for (const auto& m : HyprlandAPI::findFunctionsByName(PHANDLE, demangled.substr(demangled.rfind("::") + 2))) {
        if (!m.demangled.contains(demangled))
            continue;

        auto* hook = HyprlandAPI::createFunctionHook(PHANDLE, m.address, replacement);
        if (hook && hook->hook())
            return hook;
        if (hook)
            HyprlandAPI::removeFunctionHook(PHANDLE, hook);
        return nullptr;
    }
    return nullptr;
}

static void removeCornerCut() {
    for (auto* hook : {&g_visibleRegionHook, &g_opaqueRegionHook}) {
        if (*hook)
            HyprlandAPI::removeFunctionHook(PHANDLE, *hook);
        *hook = nullptr;
    }
    g_cornerCut = false;

    // the clipped corners are inside each window's box: repaint them
    for (const auto& w : Desktop::windowState()->windows()) {
        if (Desktop::View::validMapped(w))
            g_pHyprRenderer->damageWindow(w);
    }
}

static void installCornerCut() {
    g_visibleRegionHook = hookMember("CSurfacePassElement::visibleRegion", reinterpret_cast<void*>(&hkVisibleRegion));
    g_opaqueRegionHook  = hookMember("CSurfacePassElement::opaqueRegion", reinterpret_cast<void*>(&hkOpaqueRegion));
    g_cornerCut         = g_visibleRegionHook && g_opaqueRegionHook;

    if (!g_cornerCut) {
        removeCornerCut();
        HyprlandAPI::addNotification(PHANDLE, "[3LA-MilSpec] corner cut unavailable on this Hyprland, falling back to painted corner covers", CHyprColor{1.F, 0.6F, 0.2F, 1.F}, 6000);
    }
}

// ---------------------------------------------------------------------------
// Hyprland's own drop shadow on framed windows
// ---------------------------------------------------------------------------
// decoration:shadow hugs the window's rectangle, so at a chamfer its square
// corner sticks out past the cut while the plugin's shadow follows the frame.
// With native_shadow = 0 a framed window gets the `noshadow` property at the
// setprop priority (what `hyprctl setprop noshadow 1` sets), which outlives
// window-rule re-application and is lifted again when the frame goes or the
// plugin unloads. Only windows this plugin flagged are ever un-flagged.
static std::unordered_set<Desktop::View::CWindow*> g_nativeShadowHidden;

static void syncNativeShadow(const PHLWINDOW& w) {
    if (!w || !w->m_ruleApplicator)
        return;

    const auto* F    = milspecFrameFor(w);
    const bool  HIDE = F && !F->hidden() && g_nativeShadow->value() == 0;
    const bool  WAS  = g_nativeShadowHidden.contains(w.get());
    if (HIDE == WAS)
        return;

    if (HIDE) {
        w->m_ruleApplicator->noShadow().set(true, Desktop::Types::PRIORITY_SET_PROP);
        g_nativeShadowHidden.insert(w.get());
    } else {
        w->m_ruleApplicator->noShadow().unset(Desktop::Types::PRIORITY_SET_PROP);
        g_nativeShadowHidden.erase(w.get());
    }
    g_pHyprRenderer->damageWindow(w);
}

static CHyprSignalListener                                              g_openListener;
static CHyprSignalListener                                              g_closeListener;
static CHyprSignalListener                                              g_destroyListener;
static CHyprSignalListener                                              g_reloadListener;
static CHyprSignalListener                                              g_activeListener;
static CHyprSignalListener                                              g_titleListener;
static CHyprSignalListener                                              g_classListener;

static CHyprSignalListener                                              g_propsListener;

static PHLWINDOWREF                                                     g_lastActive;

// every option trips a layout refresh when set. `hyprctl eval 'hl.config(...)'`
// (and any Lua-side hl.config) never emits config.reloaded, so without this a
// live change to offset/thickness/titlebar would keep the old reserved space
// until the next file reload. The refresher recalculates layouts, damages
// every monitor, then emits config.props_refreshed, where the decorations get
// repositioned against the new extents.
static constexpr Config::Supplementary::PropRefreshBits REFRESH = Config::Supplementary::REFRESH_LAYOUTS;

static void damageDeco(const PHLWINDOW& w) {
    if (!w)
        return;

    const auto IT = g_decos.find(w.get());
    if (IT != g_decos.end())
        IT->second->damageEntire();
}

// a title/class change can flip ignore_title/ignore_class, which changes the
// reserved space, not just what's painted
static void repositionAll() {
    for (const auto& [win, deco] : g_decos) {
        g_pDecorationPositioner->repositionDeco(deco);
        deco->damageEntire();
        syncNativeShadow(win->m_self.lock());
    }
    // shadow extents follow the frame's new geometry and the shadow options
    for (const auto& [win, shadow] : g_shadows)
        g_pDecorationPositioner->repositionDeco(shadow);
}

static void repositionAndDamage(const PHLWINDOW& w) {
    if (!w)
        return;

    const auto IT = g_decos.find(w.get());
    if (IT != g_decos.end()) {
        g_pDecorationPositioner->repositionDeco(IT->second);
        IT->second->damageEntire();
    }

    if (const auto SIT = g_shadows.find(w.get()); SIT != g_shadows.end())
        g_pDecorationPositioner->repositionDeco(SIT->second);

    syncNativeShadow(w);
}

static void addDeco(const PHLWINDOW& w) {
    if (!w || g_decos.contains(w.get()))
        return;

    auto deco        = makeShared<CMilSpecDecoration>(w);
    g_decos[w.get()] = deco.get();

    // shadow first: both sit on the OVER layer, which renders in insertion
    // order, and the frame's stroke must land on top so the shadow's
    // antialiased cut-out edge never dims the chamfer's boundary pixels
    auto shadow        = makeShared<CMilSpecShadowDecoration>(w);
    g_shadows[w.get()] = shadow.get();
    HyprlandAPI::addWindowDecoration(PHANDLE, w, std::move(shadow));

    HyprlandAPI::addWindowDecoration(PHANDLE, w, std::move(deco));
    g_pHyprRenderer->damageWindow(w);

    syncNativeShadow(w);
}

// toggles the title bar on the currently FOCUSED window only, independent of
// titlebar.ignore_class/titlebar.ignore_title. Exposed both as the classic
// "3la_milspec:toggle" dispatcher and as hl.plugin.milspec.toggle() for Lua
// keybinds. The frame itself stays; only the bar (and its reserved band) goes.
static SDispatchResult dispatchToggleTitleBar(std::string) {
    const auto W = Desktop::focusState()->window();
    if (!W)
        return {.success = false, .error = "no active window"};

    const auto IT = g_decos.find(W.get());
    if (IT == g_decos.end())
        return {.success = false, .error = "focused window has no 3LA-MilSpec decoration"};

    IT->second->toggle();
    repositionAndDamage(W);
    return {};
}

static int luaToggleTitleBar(lua_State*) {
    dispatchToggleTitleBar("");
    return 0;
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    if (std::string{__hyprland_api_get_hash()} != __hyprland_api_get_client_hash()) {
        HyprlandAPI::addNotification(PHANDLE, "[3LA-MilSpec] Failure: version mismatch (rebuild against running Hyprland)", CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
        throw std::runtime_error("[3LA-MilSpec] version mismatch");
    }

    g_enabled   = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:enabled", "0 = off (no frame, no reserved space), 1 = on", 1,
                                                         Config::Values::SIntValueOptions{.min = 0, .max = 1, .refresh = REFRESH});
    g_offset    = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:offset", "gap (px) between the window border and the frame", 10, Config::Values::SIntValueOptions{.min = 0, .refresh = REFRESH});
    g_thickness = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:thickness", "frame stroke width (px)", 2, Config::Values::SIntValueOptions{.min = 1, .refresh = REFRESH});
    g_chamfer   = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:chamfer", "length (px) of the 45 deg cut on the top-left and bottom-right corners (0 = square)", 15,
                                                         Config::Values::SIntValueOptions{.min = 0, .refresh = REFRESH});
    g_colorActive   = makeShared<Config::Values::CColorValue>("plugin:3la_milspec:col.active", "frame color on the focused window (0 = follow general:col.active_border)", 0,
                                                              Config::Values::SColorValueOptions{.refresh = REFRESH});
    g_colorInactive = makeShared<Config::Values::CColorValue>("plugin:3la_milspec:col.inactive", "frame color on unfocused windows (0 = follow col.active if set, else general:col.inactive_border)", 0,
                                                              Config::Values::SColorValueOptions{.refresh = REFRESH});
    // fill between the frame and the window (+ border); cut out around the
    // window's rounded shape, never painted behind it
    g_colorBackground = makeShared<Config::Values::CColorValue>("plugin:3la_milspec:col.background", "fill between the frame and the window (0 = transparent)", 0,
                                                                Config::Values::SColorValueOptions{.refresh = REFRESH});
    // the window's own chamfered corners: clipped from the client past the
    // frame's edge (see hkVisibleRegion), painted over inside it. Bottom-right
    // always, top-left only when the window has no bar
    g_corners     = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:corners", "cut the window's own top-left (no title bar only) and bottom-right corners (0 = off)", 1,
                                                          Config::Values::SIntValueOptions{.min = 0, .max = 1, .refresh = REFRESH});
    g_cornersSize = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:corners.size",
                                                          "leg length (px) of the painted corner covers inside the frame (-1 = auto: a band `offset` wide along the inside of the chamfer)", -1,
                                                          Config::Values::SIntValueOptions{.min = -1, .refresh = REFRESH});
    g_colorCorners = makeShared<Config::Values::CColorValue>("plugin:3la_milspec:corners.col",
                                                             "corner cover color, always drawn opaque (0 = follow col.background, or 0xff0a0f14 if that is 0 too)", 0,
                                                             Config::Values::SColorValueOptions{.refresh = REFRESH});

    g_titlebar = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:titlebar", "reserve a title bar inside the frame, above the window (0 = off)", 0,
                                                        Config::Values::SIntValueOptions{.min = 0, .max = 1, .refresh = REFRESH});
    g_titlebarHeight = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:titlebar.height", "title bar height (px)", 20, Config::Values::SIntValueOptions{.min = 1, .refresh = REFRESH});
    g_titlebarOffset = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:titlebar.offset", "gap (px) between the frame stroke's inner edge and the bar (top/left/right)", 4,
                                                              Config::Values::SIntValueOptions{.min = 0, .refresh = REFRESH});
    g_titlebarOffsetTop   = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:titlebar.offset.top", "bar inset (px) from the top stroke (-1 = use titlebar.offset)", -1,
                                                                   Config::Values::SIntValueOptions{.min = -1, .refresh = REFRESH});
    g_titlebarOffsetLeft  = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:titlebar.offset.left", "bar inset (px) from the left stroke (-1 = use titlebar.offset)", -1,
                                                                   Config::Values::SIntValueOptions{.min = -1, .refresh = REFRESH});
    g_titlebarOffsetRight = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:titlebar.offset.right", "bar inset (px) from the right stroke (-1 = use titlebar.offset)", -1,
                                                                   Config::Values::SIntValueOptions{.min = -1, .refresh = REFRESH});
    g_titlebarGap = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:titlebar.gap", "gap (px) between the bottom of the bar and the window", 4,
                                                           Config::Values::SIntValueOptions{.min = 0, .refresh = REFRESH});
    g_colorTitlebarActive   = makeShared<Config::Values::CColorValue>("plugin:3la_milspec:col.titlebar.active", "title bar fill color on the focused window (0 = follow the frame color)", 0,
                                                                       Config::Values::SColorValueOptions{.refresh = REFRESH});
    g_colorTitlebarInactive = makeShared<Config::Values::CColorValue>(
        "plugin:3la_milspec:col.titlebar.inactive", "title bar fill color on unfocused windows (0 = follow col.titlebar.active if set, else the frame color)", 0,
        Config::Values::SColorValueOptions{.refresh = REFRESH});
    // bar fill + title text only; the stroke, col.background, corner covers
    // and shadow keep their own alpha
    g_titlebarOpacityActive   = makeShared<Config::Values::CFloatValue>("plugin:3la_milspec:titlebar.opacity.active",
                                                                        "opacity multiplier (0..1) applied to the title bar and its text on the focused window", 1.F,
                                                                        Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F, .refresh = REFRESH});
    g_titlebarOpacityInactive = makeShared<Config::Values::CFloatValue>("plugin:3la_milspec:titlebar.opacity.inactive",
                                                                        "opacity multiplier (0..1) applied to the title bar and its text on unfocused windows", 1.F,
                                                                        Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F, .refresh = REFRESH});
    g_textSize       =makeShared<Config::Values::CIntValue>("plugin:3la_milspec:text.size", "title font size (px)", 12, Config::Values::SIntValueOptions{.min = 1, .refresh = REFRESH});
    g_textFont       = makeShared<Config::Values::CStringValue>("plugin:3la_milspec:text.font", "title font family (empty = follow misc:font_family)", "",
                                                                Config::Values::SStringValueOptions{.refresh = REFRESH});
    g_colorTextActive   = makeShared<Config::Values::CColorValue>("plugin:3la_milspec:col.text.active", "title text color on the focused window (0 = black or white, whichever contrasts with the bar)", 0,
                                                                   Config::Values::SColorValueOptions{.refresh = REFRESH});
    g_colorTextInactive = makeShared<Config::Values::CColorValue>(
        "plugin:3la_milspec:col.text.inactive", "title text color on unfocused windows (0 = follow col.text.active if set, else black/white contrast)", 0,
        Config::Values::SColorValueOptions{.refresh = REFRESH});

    g_ignoreClass = makeShared<Config::Values::CStringValue>("plugin:3la_milspec:ignore_class", "regex of window classes to never frame (empty = none)", "",
                                                             Config::Values::SStringValueOptions{.refresh = REFRESH});
    g_ignoreTitle = makeShared<Config::Values::CStringValue>("plugin:3la_milspec:ignore_title", "regex of window titles to never frame (empty = none)", "",
                                                             Config::Values::SStringValueOptions{.refresh = REFRESH});
    // title bar only: a match keeps the frame but drops the bar and its reserved
    // space. The frame-level ignore_class/ignore_title above take priority.
    g_titlebarIgnoreClass = makeShared<Config::Values::CStringValue>("plugin:3la_milspec:titlebar.ignore_class", "regex of window classes that get no title bar (empty = none)", "",
                                                                     Config::Values::SStringValueOptions{.refresh = REFRESH});
    g_titlebarIgnoreTitle = makeShared<Config::Values::CStringValue>("plugin:3la_milspec:titlebar.ignore_title", "regex of window titles that get no title bar (empty = none)", "",
                                                                     Config::Values::SStringValueOptions{.refresh = REFRESH});
    // opt-in bar: matched classes only get a bar while their title matches
    // titlebar.require_title (e.g. kitty only during an SSH session). Re-checked
    // on every title change via the window.title listener.
    g_titlebarRequireTitleClass = makeShared<Config::Values::CStringValue>(
        "plugin:3la_milspec:titlebar.require_title_class", "regex of window classes that only get a title bar while their title matches titlebar.require_title (empty = none)", "",
        Config::Values::SStringValueOptions{.refresh = REFRESH});
    g_titlebarRequireTitle = makeShared<Config::Values::CStringValue>(
        "plugin:3la_milspec:titlebar.require_title", "regex a titlebar.require_title_class window's title must match to get a bar (empty = never)", "",
        Config::Values::SStringValueOptions{.refresh = REFRESH});
    // same format and matching as 3la_titlebars:title_rules
    g_titleRules = makeShared<Config::Values::CStringValue>(
        "plugin:3la_milspec:title_rules",
        "semicolon-separated custom titles: 'class_regex,title_regex,override text;...' (either regex empty = match any; first match wins; empty = no overrides)", "",
        Config::Values::SStringValueOptions{.refresh = REFRESH});
    // stroke only: hiding it keeps all reserved space and the bar's position
    // (inset and cut included), so toggling never moves a window
    g_frame = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:frame", "draw the frame stroke (0 = hide it on every window; spacing and title bar unchanged)", 1,
                                                     Config::Values::SIntValueOptions{.min = 0, .max = 1, .refresh = REFRESH});
    g_frameIgnoreClass = makeShared<Config::Values::CStringValue>("plugin:3la_milspec:frame.ignore_class", "regex of window classes that get no frame stroke (empty = none)", "",
                                                                  Config::Values::SStringValueOptions{.refresh = REFRESH});
    g_frameIgnoreTitle = makeShared<Config::Values::CStringValue>("plugin:3la_milspec:frame.ignore_title", "regex of window titles that get no frame stroke (empty = none)", "",
                                                                  Config::Values::SStringValueOptions{.refresh = REFRESH});

    // no bar: matched windows sit flush against the top stroke instead of
    // `offset` below it (sides and bottom keep `offset`)
    g_offsetTopFlushClass = makeShared<Config::Values::CStringValue>("plugin:3la_milspec:offset.top_flush_class",
                                                                     "regex of window classes with no top gap when they have no title bar (empty = none)", "",
                                                                     Config::Values::SStringValueOptions{.refresh = REFRESH});

    g_nativeShadow = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:native_shadow",
                                                            "keep Hyprland's decoration:shadow on framed windows (0 = hide it there via the window's noshadow property; its square corner would poke past the chamfer)", 0,
                                                            Config::Values::SIntValueOptions{.min = 0, .max = 1, .refresh = REFRESH});
    g_shadow = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:shadow", "drop shadow following the frame's outer edge (0 = off)", 1,
                                                      Config::Values::SIntValueOptions{.min = 0, .max = 1, .refresh = REFRESH});
    g_shadowRange = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:shadow.range", "shadow size (px), like decoration:shadow:range", 30,
                                                           Config::Values::SIntValueOptions{.min = 1, .refresh = REFRESH});
    g_shadowRenderPower = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:shadow.render_power", "falloff power [1-4], like decoration:shadow:render_power (higher = tighter)", 4,
                                                                 Config::Values::SIntValueOptions{.min = 1, .max = 4, .refresh = REFRESH});
    g_shadowSharp = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:shadow.sharp", "hard-edged shadow (0 = off), like decoration:shadow:sharp", 0,
                                                           Config::Values::SIntValueOptions{.min = 0, .max = 1, .refresh = REFRESH});
    g_shadowOffsetX = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:shadow.offset.x", "horizontal shadow offset (px)", 0, Config::Values::SIntValueOptions{.refresh = REFRESH});
    g_shadowOffsetY = makeShared<Config::Values::CIntValue>("plugin:3la_milspec:shadow.offset.y", "vertical shadow offset (px)", 0, Config::Values::SIntValueOptions{.refresh = REFRESH});
    g_shadowScale   = makeShared<Config::Values::CFloatValue>("plugin:3la_milspec:shadow.scale", "shadow scale [0-1], like decoration:shadow:scale", 1.F,
                                                             Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F, .refresh = REFRESH});
    g_colorShadowActive   = makeShared<Config::Values::CColorValue>("plugin:3la_milspec:col.shadow.active", "shadow color on the focused window", 0xee000000,
                                                                     Config::Values::SColorValueOptions{.refresh = REFRESH});
    g_colorShadowInactive = makeShared<Config::Values::CColorValue>("plugin:3la_milspec:col.shadow.inactive", "shadow color on unfocused windows (0 = follow col.shadow.active)", 0,
                                                                     Config::Values::SColorValueOptions{.refresh = REFRESH});
    g_shadowIgnoreClass = makeShared<Config::Values::CStringValue>("plugin:3la_milspec:shadow.ignore_class", "regex of window classes that get no shadow (empty = none)", "",
                                                                   Config::Values::SStringValueOptions{.refresh = REFRESH});
    g_shadowIgnoreTitle = makeShared<Config::Values::CStringValue>("plugin:3la_milspec:shadow.ignore_title", "regex of window titles that get no shadow (empty = none)", "",
                                                                   Config::Values::SStringValueOptions{.refresh = REFRESH});

    HyprlandAPI::addConfigValueV2(PHANDLE, g_enabled);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_offset);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_thickness);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_chamfer);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorActive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorInactive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorBackground);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_corners);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cornersSize);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorCorners);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titlebar);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titlebarHeight);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titlebarOffset);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titlebarOffsetTop);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titlebarOffsetLeft);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titlebarOffsetRight);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titlebarGap);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorTitlebarActive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorTitlebarInactive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titlebarOpacityActive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titlebarOpacityInactive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textSize);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textFont);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorTextActive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorTextInactive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_ignoreClass);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_ignoreTitle);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titlebarIgnoreClass);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titlebarIgnoreTitle);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titlebarRequireTitleClass);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titlebarRequireTitle);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titleRules);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_frame);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_frameIgnoreClass);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_frameIgnoreTitle);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_offsetTopFlushClass);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_nativeShadow);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_shadow);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_shadowRange);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_shadowRenderPower);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_shadowSharp);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_shadowOffsetX);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_shadowOffsetY);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_shadowScale);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorShadowActive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorShadowInactive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_shadowIgnoreClass);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_shadowIgnoreTitle);

    HyprlandAPI::addDispatcherV2(PHANDLE, "3la_milspec:toggle", [](std::string arg) { return dispatchToggleTitleBar(arg); });
    HyprlandAPI::addLuaFunction(PHANDLE, "milspec", "toggle", luaToggleTitleBar); // auto-removed on unload

    g_openListener = Event::bus()->m_events.window.open.listen([](const PHLWINDOW& w) { addDeco(w); });
    // window.close fires while geometry is still valid -- damage there so the
    // frame's last paint doesn't linger. A client that exits on its own only
    // reaches window.destroy, where damageEntire() falls back to the output.
    g_closeListener   = Event::bus()->m_events.window.close.listen([](const PHLWINDOW& w) { damageDeco(w); });
    g_destroyListener = Event::bus()->m_events.window.destroy.listen([](const PHLWINDOWREF& w) {
        if (!w)
            return;

        const auto IT = g_decos.find(w.get());
        if (IT != g_decos.end()) {
            IT->second->damageEntire();
            g_decos.erase(IT);
        }
        g_shadows.erase(w.get());
        g_nativeShadowHidden.erase(w.get());
    });
    // offset/thickness/titlebar change the reserved space; repositionDeco
    // alone doesn't damage, so a colour-only change needs the damage too
    g_reloadListener = Event::bus()->m_events.config.reloaded.listen([] { repositionAll(); });
    g_propsListener  = Event::bus()->m_events.config.props_refreshed.listen([](bool) { repositionAll(); });
    g_activeListener = Event::bus()->m_events.window.active.listen([](const PHLWINDOW& w, Desktop::eFocusReason) {
        damageDeco(g_lastActive.lock());
        damageDeco(w);
        g_lastActive = w;
    });
    g_titleListener = Event::bus()->m_events.window.title.listen([](const PHLWINDOW& w) { repositionAndDamage(w); });
    g_classListener = Event::bus()->m_events.window.class_.listen([](const PHLWINDOW& w) { repositionAndDamage(w); });

    g_lastActive = Desktop::focusState()->window();

    // before the decorations: their first frame texture reads g_cornerCut
    installCornerCut();

    for (const auto& w : Desktop::windowState()->windows()) {
        if (Desktop::View::validMapped(w))
            addDeco(w);
    }

    return {"3LA-MilSpec", "Chamfered mil-spec HUD frame drawn outside window borders, with an optional title bar", "sispx", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // first: the hook's body looks frames up in g_decos
    removeCornerCut();

    for (const auto& w : Desktop::windowState()->windows()) {
        if (w && w->m_ruleApplicator && g_nativeShadowHidden.contains(w.get())) {
            w->m_ruleApplicator->noShadow().unset(Desktop::Types::PRIORITY_SET_PROP);
            g_pHyprRenderer->damageWindow(w);
        }
    }
    g_nativeShadowHidden.clear();

    g_openListener.reset();
    g_closeListener.reset();
    g_destroyListener.reset();
    g_reloadListener.reset();
    g_propsListener.reset();
    g_activeListener.reset();
    g_titleListener.reset();
    g_classListener.reset();
    g_lastActive.reset();

    // shadows first: their damageEntire() looks the frame up in g_decos
    for (const auto& [win, shadow] : g_shadows)
        HyprlandAPI::removeWindowDecoration(PHANDLE, shadow);
    g_shadows.clear();

    for (const auto& [win, deco] : g_decos)
        HyprlandAPI::removeWindowDecoration(PHANDLE, deco);
    g_decos.clear();

    g_enabled.reset();
    g_offset.reset();
    g_thickness.reset();
    g_chamfer.reset();
    g_colorActive.reset();
    g_colorInactive.reset();
    g_colorBackground.reset();
    g_corners.reset();
    g_cornersSize.reset();
    g_colorCorners.reset();
    g_titlebar.reset();
    g_titlebarHeight.reset();
    g_titlebarOffset.reset();
    g_titlebarOffsetTop.reset();
    g_titlebarOffsetLeft.reset();
    g_titlebarOffsetRight.reset();
    g_titlebarGap.reset();
    g_colorTitlebarActive.reset();
    g_colorTitlebarInactive.reset();
    g_titlebarOpacityActive.reset();
    g_titlebarOpacityInactive.reset();
    g_textSize.reset();
    g_textFont.reset();
    g_colorTextActive.reset();
    g_colorTextInactive.reset();
    g_ignoreClass.reset();
    g_ignoreTitle.reset();
    g_titlebarIgnoreClass.reset();
    g_titlebarIgnoreTitle.reset();
    g_titleRules.reset();
    g_frame.reset();
    g_frameIgnoreClass.reset();
    g_frameIgnoreTitle.reset();
    g_offsetTopFlushClass.reset();
    g_nativeShadow.reset();
    g_shadow.reset();
    g_shadowRange.reset();
    g_shadowRenderPower.reset();
    g_shadowSharp.reset();
    g_shadowOffsetX.reset();
    g_shadowOffsetY.reset();
    g_shadowScale.reset();
    g_colorShadowActive.reset();
    g_colorShadowInactive.reset();
    g_shadowIgnoreClass.reset();
    g_shadowIgnoreTitle.reset();
}
