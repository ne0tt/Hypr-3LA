#include "TitleBarDecoration.hpp"
#include "globals.hpp"

#include <stdexcept>
#include <unordered_map>

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/version.h>

static std::unordered_map<Desktop::View::CWindow*, CTitleBarDecoration*> g_decos;

static CHyprSignalListener                                               g_openListener;
static CHyprSignalListener                                               g_destroyListener;
static CHyprSignalListener                                               g_reloadListener;
static CHyprSignalListener                                               g_titleListener;
static CHyprSignalListener                                               g_classListener;

static void addDeco(const PHLWINDOW& w) {
    if (!w || g_decos.contains(w.get()))
        return;

    auto deco        = makeUnique<CTitleBarDecoration>(w);
    g_decos[w.get()] = deco.get();
    HyprlandAPI::addWindowDecoration(PHANDLE, w, std::move(deco));
    g_pHyprRenderer->damageWindow(w);
}

// toggles the bar on the currently FOCUSED window only, independent of
// ignore_class/ignore_title. Exposed both as the classic "3la_titlebars:toggle"
// dispatcher and as hl.plugin.titlebars.toggle() for Lua keybinds.
static SDispatchResult dispatchToggleTitleBar(std::string) {
    const auto W = Desktop::focusState()->window();
    if (!W)
        return {.success = false, .error = "no active window"};

    const auto IT = g_decos.find(W.get());
    if (IT == g_decos.end())
        return {.success = false, .error = "focused window has no 3LA-TitleBars decoration"};

    IT->second->toggle();
    g_pDecorationPositioner->repositionDeco(IT->second);
    IT->second->damageEntire();
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
        HyprlandAPI::addNotification(PHANDLE, "[3LA-TitleBars] Failure: version mismatch (rebuild against running Hyprland)", CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
        throw std::runtime_error("[3LA-TitleBars] version mismatch");
    }

    g_height        = makeShared<Config::Values::CIntValue>("plugin:3la_titlebars:height", "height of the title bar in px", 24, Config::Values::SIntValueOptions{.min = 1});
    g_gap           = makeShared<Config::Values::CIntValue>("plugin:3la_titlebars:gap", "gap (px) around the bar: above, below and on both sides", 7, Config::Values::SIntValueOptions{.min = 0});
    g_colorActive   = makeShared<Config::Values::CColorValue>("plugin:3la_titlebars:col.active", "title bar color on the focused window", 0xFF690005);
    g_colorInactive = makeShared<Config::Values::CColorValue>("plugin:3la_titlebars:col.inactive", "title bar color on unfocused windows (0 = follow col.active)", 0);
    g_opacityActive   = makeShared<Config::Values::CFloatValue>("plugin:3la_titlebars:opacity.active", "opacity multiplier (0..1) applied to the bar, its text and its shadow on the focused window",
                                                               1.F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_opacityInactive = makeShared<Config::Values::CFloatValue>(
        "plugin:3la_titlebars:opacity.inactive", "opacity multiplier (0..1) applied to the bar, its text and its shadow on unfocused windows", 1.F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_shadow         = makeShared<Config::Values::CIntValue>("plugin:3la_titlebars:shadow", "draw a soft shadow behind the bar, independent of the window's own shadow (0 = off)", 1,
                                                       Config::Values::SIntValueOptions{.min = 0, .max = 1});
    g_shadowSize     = makeShared<Config::Values::CIntValue>("plugin:3la_titlebars:shadow.size", "shadow spread distance in px", 20, Config::Values::SIntValueOptions{.min = 0});
    g_shadowStrength = makeShared<Config::Values::CFloatValue>("plugin:3la_titlebars:shadow.strength", "overall shadow intensity (0..1)", 0.5F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_shadowColor    = makeShared<Config::Values::CColorValue>("plugin:3la_titlebars:shadow.col", "shadow color", 0xAA000000);
    g_textSize       = makeShared<Config::Values::CIntValue>("plugin:3la_titlebars:text.size", "window title font size in px", 12, Config::Values::SIntValueOptions{.min = 1});
    g_textFont       = makeShared<Config::Values::CStringValue>("plugin:3la_titlebars:text.font", "window title font family (empty = follow misc:font_family)", "");
    g_textColorActive =
        makeShared<Config::Values::CColorValue>("plugin:3la_titlebars:text.col.active", "window title text color on the focused window", 0xFFFFFFFF);
    g_textColorInactive =
        makeShared<Config::Values::CColorValue>("plugin:3la_titlebars:text.col.inactive", "window title text color on unfocused windows (0 = follow text.col.active)", 0);
    g_ignoreClass = makeShared<Config::Values::CStringValue>("plugin:3la_titlebars:ignore_class", "regex of window classes to never give a title bar (empty = none)", "");
    g_ignoreTitle = makeShared<Config::Values::CStringValue>("plugin:3la_titlebars:ignore_title", "regex of window titles to never give a title bar (empty = none)", "");
    g_titleRules  = makeShared<Config::Values::CStringValue>(
        "plugin:3la_titlebars:title_rules",
        "semicolon-separated custom titles: 'class_regex,title_regex,override text;...' (either regex empty = match any; first match wins; empty = no overrides)", "");

    HyprlandAPI::addConfigValueV2(PHANDLE, g_height);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_gap);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorActive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorInactive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_opacityActive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_opacityInactive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_shadow);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_shadowSize);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_shadowStrength);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_shadowColor);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textSize);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textFont);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textColorActive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textColorInactive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_ignoreClass);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_ignoreTitle);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_titleRules);

    HyprlandAPI::addDispatcherV2(PHANDLE, "3la_titlebars:toggle", [](std::string arg) { return dispatchToggleTitleBar(arg); });
    HyprlandAPI::addLuaFunction(PHANDLE, "titlebars", "toggle", luaToggleTitleBar); // auto-removed on unload

    g_openListener    = Event::bus()->m_events.window.open.listen([](const PHLWINDOW& w) { addDeco(w); });
    g_destroyListener = Event::bus()->m_events.window.destroy.listen([](const PHLWINDOWREF& w) {
        if (w)
            g_decos.erase(w.get());
    });
    g_reloadListener  = Event::bus()->m_events.config.reloaded.listen([] {
        // repositionDeco alone doesn't damage: a reload that only changes
        // title_rule (same reserved size) would otherwise not repaint
        for (const auto& [win, deco] : g_decos) {
            g_pDecorationPositioner->repositionDeco(deco);
            deco->damageEntire();
        }
    });
    g_titleListener   = Event::bus()->m_events.window.title.listen([](const PHLWINDOW& w) {
        const auto IT = g_decos.find(w.get());
        if (IT != g_decos.end()) {
            // a title change can flip ignore_title's verdict, which changes
            // how much space is reserved, not just what's painted
            g_pDecorationPositioner->repositionDeco(IT->second);
            IT->second->damageEntire();
        }
    });
    g_classListener   = Event::bus()->m_events.window.class_.listen([](const PHLWINDOW& w) {
        const auto IT = g_decos.find(w.get());
        if (IT != g_decos.end()) {
            g_pDecorationPositioner->repositionDeco(IT->second);
            IT->second->damageEntire();
        }
    });

    for (const auto& w : Desktop::windowState()->windows()) {
        if (Desktop::View::validMapped(w))
            addDeco(w);
    }

    return {"3LA-TitleBars", "Solid-color title bar drawn over the top of every window", "sispx", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_openListener.reset();
    g_destroyListener.reset();
    g_reloadListener.reset();
    g_titleListener.reset();
    g_classListener.reset();

    for (const auto& [win, deco] : g_decos)
        HyprlandAPI::removeWindowDecoration(PHANDLE, deco);
    g_decos.clear();

    g_height.reset();
    g_gap.reset();
    g_colorActive.reset();
    g_colorInactive.reset();
    g_opacityActive.reset();
    g_opacityInactive.reset();
    g_shadow.reset();
    g_shadowSize.reset();
    g_shadowStrength.reset();
    g_shadowColor.reset();
    g_textSize.reset();
    g_textFont.reset();
    g_textColorActive.reset();
    g_textColorInactive.reset();
    g_ignoreClass.reset();
    g_ignoreTitle.reset();
    g_titleRules.reset();
}
