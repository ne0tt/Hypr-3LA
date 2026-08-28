#include "FeedLossEffect.hpp"
#include "globals.hpp"

#include <stdexcept>

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/version.h>

static CHyprSignalListener g_closeListener;
static CHyprSignalListener g_renderListener;
static CHyprSignalListener g_reloadListener;

// exposed as hl.plugin.feedloss.close() for Lua keybinds
static int luaFeedClose(lua_State*) {
    g_feedLoss.dispatchClose();
    return 0;
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    if (std::string{__hyprland_api_get_hash()} != __hyprland_api_get_client_hash()) {
        HyprlandAPI::addNotification(PHANDLE, "[3LA-Feed-Loss] Failure: version mismatch (rebuild against running Hyprland)", CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
        throw std::runtime_error("[3LA-Feed-Loss] version mismatch");
    }

    g_duration      = makeShared<Config::Values::CIntValue>("plugin:3la_feed_loss:duration", "total effect duration in ms", 900, Config::Values::SIntValueOptions{.min = 100});
    g_fade          = makeShared<Config::Values::CIntValue>("plugin:3la_feed_loss:fade", "overlay fade-out tail after the burst, in ms", 300, Config::Values::SIntValueOptions{.min = 0});
    g_staticAlpha   = makeShared<Config::Values::CFloatValue>("plugin:3la_feed_loss:static_alpha", "opacity of the static noise layer", 0.55F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_backdropAlpha = makeShared<Config::Values::CFloatValue>("plugin:3la_feed_loss:backdrop_alpha", "opacity of the black backdrop behind the static", 0.75F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_text          = makeShared<Config::Values::CStringValue>("plugin:3la_feed_loss:text", "caption shown mid-effect (empty = no caption)", "SIGNAL LOST");
    g_textSize      = makeShared<Config::Values::CIntValue>("plugin:3la_feed_loss:text_size", "caption font size in pt", 16, Config::Values::SIntValueOptions{.min = 6});
    g_textAlpha     = makeShared<Config::Values::CFloatValue>("plugin:3la_feed_loss:text_alpha", "opacity of the caption", 1.0F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_textBlink     = makeShared<Config::Values::CIntValue>("plugin:3la_feed_loss:text_blink", "blink the caption (0 = steady)", 1, Config::Values::SIntValueOptions{.min = 0, .max = 1});
    g_textColor     = makeShared<Config::Values::CColorValue>("plugin:3la_feed_loss:col.text", "caption color (0 = white)", 0);
    g_font          = makeShared<Config::Values::CStringValue>("plugin:3la_feed_loss:font", "caption font family", "monospace");
    g_glitch        = makeShared<Config::Values::CIntValue>("plugin:3la_feed_loss:glitch", "glitch strength: 0 = off, 1 = normal, up to 5 = extreme", 1, Config::Values::SIntValueOptions{.min = 0, .max = 5});
    g_minSize       = makeShared<Config::Values::CIntValue>("plugin:3la_feed_loss:min_size", "skip windows smaller than this on either axis (px)", 80, Config::Values::SIntValueOptions{.min = 0});
    g_ignoreChildren = makeShared<Config::Values::CIntValue>("plugin:3la_feed_loss:ignore_children", "skip child windows: dialogs, transients, modals (0 = burst on them too)", 1,
                                                             Config::Values::SIntValueOptions{.min = 0, .max = 1});
    g_ignoreClass   = makeShared<Config::Values::CStringValue>("plugin:3la_feed_loss:ignore_class", "regex of window classes to never burst on (empty = none)", "^(xdg-desktop-portal.*)$");
    g_ignoreTitle   = makeShared<Config::Values::CStringValue>("plugin:3la_feed_loss:ignore_title", "regex of window titles to never burst on (empty = none)", "");
    g_closeAt       = makeShared<Config::Values::CFloatValue>("plugin:3la_feed_loss:close_at", "fraction of duration after which feedloss:close sends the real close", 1.0F,
                                                             Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_staticColor   = makeShared<Config::Values::CColorValue>("plugin:3la_feed_loss:col.static", "tint of the static noise (0 = neutral gray)", 0);
    g_backdropColor = makeShared<Config::Values::CColorValue>("plugin:3la_feed_loss:col.backdrop", "backdrop color (0 = black)", 0);
    g_fringe1       = makeShared<Config::Values::CColorValue>("plugin:3la_feed_loss:col.fringe1", "first glitch-slice fringe color (0 = magenta)", 0);
    g_fringe2       = makeShared<Config::Values::CColorValue>("plugin:3la_feed_loss:col.fringe2", "second glitch-slice fringe color (0 = cyan)", 0);

    HyprlandAPI::addConfigValueV2(PHANDLE, g_duration);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_fade);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_staticAlpha);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_backdropAlpha);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_text);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textSize);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textAlpha);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textBlink);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textColor);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_font);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_glitch);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_minSize);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_ignoreChildren);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_ignoreClass);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_ignoreTitle);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_closeAt);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_staticColor);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_backdropColor);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_fringe1);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_fringe2);

    HyprlandAPI::addDispatcherV2(PHANDLE, "feedloss:close", [](std::string) { return g_feedLoss.dispatchClose(); });
    HyprlandAPI::addLuaFunction(PHANDLE, "feedloss", "close", luaFeedClose); // auto-removed on unload

    g_closeListener  = Event::bus()->m_events.window.close.listen([](const PHLWINDOW& w) { g_feedLoss.onWindowClose(w); });
    g_renderListener = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) { g_feedLoss.onRenderStage(stage); });
    g_reloadListener = Event::bus()->m_events.config.reloaded.listen([] { g_feedLoss.onConfigReloaded(); });

    return {"3LA-Feed-Loss", "CCTV signal-lost static burst where a closed window was", "sispx", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_closeListener.reset();
    g_renderListener.reset();
    g_reloadListener.reset();

    g_feedLoss.reset();

    g_duration.reset();
    g_fade.reset();
    g_staticAlpha.reset();
    g_backdropAlpha.reset();
    g_text.reset();
    g_textSize.reset();
    g_textAlpha.reset();
    g_textBlink.reset();
    g_textColor.reset();
    g_font.reset();
    g_glitch.reset();
    g_minSize.reset();
    g_ignoreChildren.reset();
    g_ignoreClass.reset();
    g_ignoreTitle.reset();
    g_closeAt.reset();
    g_staticColor.reset();
    g_backdropColor.reset();
    g_fringe1.reset();
    g_fringe2.reset();
}
