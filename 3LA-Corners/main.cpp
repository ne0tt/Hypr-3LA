#include "CornersDecoration.hpp"
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

static std::unordered_map<Desktop::View::CWindow*, CCornersDecoration*> g_decos;

static CHyprSignalListener                                              g_openListener;
static CHyprSignalListener                                              g_destroyListener;
static CHyprSignalListener                                              g_reloadListener;
static CHyprSignalListener                                              g_activeListener;

static PHLWINDOWREF                                                     g_lastActive;

static void damageDeco(const PHLWINDOW& w) {
    if (!w)
        return;

    const auto IT = g_decos.find(w.get());
    if (IT != g_decos.end())
        IT->second->damageEntire();
}

static void addDeco(const PHLWINDOW& w, bool flash = false) {
    if (!w || g_decos.contains(w.get()))
        return;

    auto deco        = makeUnique<CCornersDecoration>(w, flash);
    g_decos[w.get()] = deco.get();
    HyprlandAPI::addWindowDecoration(PHANDLE, w, std::move(deco));
    g_pHyprRenderer->damageWindow(w);
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    if (std::string{__hyprland_api_get_hash()} != __hyprland_api_get_client_hash()) {
        HyprlandAPI::addNotification(PHANDLE, "[3LA-Corners] Failure: version mismatch (rebuild against running Hyprland)", CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
        throw std::runtime_error("[3LA-Corners] version mismatch");
    }

    g_offset    = makeShared<Config::Values::CIntValue>("plugin:3la_corners:offset", "gap between the border and the corner brackets", 10, Config::Values::SIntValueOptions{.min = 0});
    g_length    = makeShared<Config::Values::CIntValue>("plugin:3la_corners:length", "length of each bracket arm", 100, Config::Values::SIntValueOptions{.min = 1});
    g_thickness = makeShared<Config::Values::CIntValue>("plugin:3la_corners:thickness", "thickness of the bracket lines", 2, Config::Values::SIntValueOptions{.min = 1});
    g_color         = makeShared<Config::Values::CColorValue>("plugin:3la_corners:col.active", "bracket color on the focused window (0 = follow general:col.active_border)", 0);
    g_colorInactive = makeShared<Config::Values::CColorValue>("plugin:3la_corners:col.inactive", "bracket color on unfocused windows (0 = follow general:col.inactive_border)", 0);
    g_flashCount    = makeShared<Config::Values::CIntValue>("plugin:3la_corners:flash_count", "times the brackets flash when a window spawns (0 = no flash)", 3, Config::Values::SIntValueOptions{.min = 0});
    g_flashDuration = makeShared<Config::Values::CIntValue>("plugin:3la_corners:flash_duration", "spawn flash on/off phase duration in ms", 150, Config::Values::SIntValueOptions{.min = 16});
    g_flashOnFocus =
        makeShared<Config::Values::CIntValue>("plugin:3la_corners:flash_on_focus", "flash the brackets when a window gains focus (0 = off)", 0, Config::Values::SIntValueOptions{.min = 0, .max = 1});
    g_focusFlashCount =
        makeShared<Config::Values::CIntValue>("plugin:3la_corners:focus_flash_count", "times the brackets flash when a window gains focus (0 = no flash)", 3, Config::Values::SIntValueOptions{.min = 0});
    g_focusFlashDuration =
        makeShared<Config::Values::CIntValue>("plugin:3la_corners:focus_flash_duration", "focus flash on/off phase duration in ms", 150, Config::Values::SIntValueOptions{.min = 16});

    g_glow = makeShared<Config::Values::CIntValue>("plugin:3la_corners:glow", "draw a soft glow behind the brackets while a spawn/focus flash is running (0 = off)", 0,
                                                    Config::Values::SIntValueOptions{.min = 0, .max = 1});
    g_glowSize = makeShared<Config::Values::CIntValue>("plugin:3la_corners:glow.size", "glow spread distance in px", 12, Config::Values::SIntValueOptions{.min = 0});
    g_glowStrength =
        makeShared<Config::Values::CFloatValue>("plugin:3la_corners:glow.strength", "overall glow intensity (0..1)", 0.5F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_colorGlow = makeShared<Config::Values::CColorValue>("plugin:3la_corners:col.glow", "glow color (0 = follow the bracket's own color)", 0);

    HyprlandAPI::addConfigValueV2(PHANDLE, g_offset);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_length);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_thickness);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_color);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorInactive);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_flashCount);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_flashDuration);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_flashOnFocus);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_focusFlashCount);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_focusFlashDuration);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_glow);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_glowSize);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_glowStrength);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_colorGlow);

    g_openListener    = Event::bus()->m_events.window.open.listen([](const PHLWINDOW& w) { addDeco(w, true); });
    g_destroyListener = Event::bus()->m_events.window.destroy.listen([](const PHLWINDOWREF& w) {
        if (w)
            g_decos.erase(w.get());
    });
    g_reloadListener  = Event::bus()->m_events.config.reloaded.listen([] {
        for (const auto& [win, deco] : g_decos)
            g_pDecorationPositioner->repositionDeco(deco);
    });
    g_activeListener  = Event::bus()->m_events.window.active.listen([](const PHLWINDOW& w, Desktop::eFocusReason) {
        damageDeco(g_lastActive.lock());

        if (g_flashOnFocus->value() > 0 && w && w != g_lastActive.lock()) {
            const auto IT = g_decos.find(w.get());
            if (IT != g_decos.end())
                IT->second->flashFocus(); // already damages
            else
                damageDeco(w);
        } else
            damageDeco(w);

        g_lastActive = w;
    });

    g_lastActive = Desktop::focusState()->window();

    for (const auto& w : Desktop::windowState()->windows()) {
        if (Desktop::View::validMapped(w))
            addDeco(w);
    }

    return {"3LA-Corners", "Corner brackets drawn outside window borders", "sispx", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_openListener.reset();
    g_destroyListener.reset();
    g_reloadListener.reset();
    g_activeListener.reset();
    g_lastActive.reset();

    for (const auto& [win, deco] : g_decos)
        HyprlandAPI::removeWindowDecoration(PHANDLE, deco);
    g_decos.clear();

    g_offset.reset();
    g_length.reset();
    g_thickness.reset();
    g_color.reset();
    g_colorInactive.reset();
    g_flashCount.reset();
    g_flashDuration.reset();
    g_flashOnFocus.reset();
    g_focusFlashCount.reset();
    g_focusFlashDuration.reset();
    g_glow.reset();
    g_glowSize.reset();
    g_glowStrength.reset();
    g_colorGlow.reset();
}
