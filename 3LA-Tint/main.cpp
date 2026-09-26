#include "TintEffect.hpp"
#include "globals.hpp"

#include <stdexcept>

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/version.h>

static CHyprSignalListener g_renderListener;
static CHyprSignalListener g_reloadListener;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    if (std::string{__hyprland_api_get_hash()} != __hyprland_api_get_client_hash()) {
        HyprlandAPI::addNotification(PHANDLE, "[3LA-Tint] Failure: version mismatch (rebuild against running Hyprland)", CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
        throw std::runtime_error("[3LA-Tint] version mismatch");
    }

    using namespace Config::Values;
    g_enabled    = makeShared<CIntValue>("plugin:3la_tint:enabled", "0 = off, 1 = tint matching windows", 1, SIntValueOptions{.min = 0, .max = 1});
    g_matchTitle = makeShared<CStringValue>("plugin:3la_tint:match_title", "regex of window titles to tint (empty = none)", "");
    g_matchClass = makeShared<CStringValue>("plugin:3la_tint:match_class", "regex of window classes to tint (empty = none)", "");
    g_color      = makeShared<CColorValue>("plugin:3la_tint:col.tint", "tint colour (hue + saturation are used)", 0xFF2DECEC);
    g_strength   = makeShared<CFloatValue>("plugin:3la_tint:strength", "blend of the tint over greys (0..1)", 1.0F, SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_satLo      = makeShared<CFloatValue>("plugin:3la_tint:sat_lo", "chroma at or below which a pixel is fully tinted", 0.06F, SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_satHi      = makeShared<CFloatValue>("plugin:3la_tint:sat_hi", "chroma at or above which a pixel is left alone", 0.20F, SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_minLuma    = makeShared<CFloatValue>("plugin:3la_tint:min_luma", "pixels darker than this stay untinted (0 = tint black too)", 0.0F, SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_lightLo    = makeShared<CFloatValue>("plugin:3la_tint:light_lo", "HSL lightness black is remapped to", 0.04F, SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_lightHi    = makeShared<CFloatValue>("plugin:3la_tint:light_hi", "HSL lightness white is remapped to", 0.90F, SFloatValueOptions{.min = 0.F, .max = 1.F});

    for (const auto& v : {SP<IValue>(g_enabled), SP<IValue>(g_matchTitle), SP<IValue>(g_matchClass), SP<IValue>(g_color), SP<IValue>(g_strength), SP<IValue>(g_satLo),
                          SP<IValue>(g_satHi), SP<IValue>(g_minLuma), SP<IValue>(g_lightLo), SP<IValue>(g_lightHi)})
        HyprlandAPI::addConfigValueV2(PHANDLE, v);

    g_renderListener = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) { g_tint.onRenderStage(stage); });
    g_reloadListener = Event::bus()->m_events.config.reloaded.listen([] { g_tint.onConfigReloaded(); });

    return {"3LA-Tint", "Recolours grey pixels of matched windows toward a matugen colour", "sispx", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_renderListener.reset();
    g_reloadListener.reset();
    g_tint.reset();
    g_enabled.reset();
    g_matchTitle.reset();
    g_matchClass.reset();
    g_color.reset();
    g_strength.reset();
    g_satLo.reset();
    g_satHi.reset();
    g_minLuma.reset();
    g_lightLo.reset();
    g_lightHi.reset();
}
