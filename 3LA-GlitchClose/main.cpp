#include "GlitchCloseEffect.hpp"
#include "globals.hpp"

#include <stdexcept>

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/version.h>

static CHyprSignalListener g_closeListener;
static CHyprSignalListener g_renderListener;
static CHyprSignalListener g_reloadListener;

// exposed as hl.plugin.glitchclose.close() for Lua keybinds
static int luaGlitchClose(lua_State*) {
    g_glitchClose.dispatchClose();
    return 0;
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    if (std::string{__hyprland_api_get_hash()} != __hyprland_api_get_client_hash()) {
        HyprlandAPI::addNotification(PHANDLE, "[3LA-GlitchClose] Failure: version mismatch (rebuild against running Hyprland)", CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
        throw std::runtime_error("[3LA-GlitchClose] version mismatch");
    }

    g_duration       = makeShared<Config::Values::CIntValue>("plugin:3la_glitch_close:duration", "total effect duration in ms", 700, Config::Values::SIntValueOptions{.min = 100});
    g_fade           = makeShared<Config::Values::CIntValue>("plugin:3la_glitch_close:fade", "overlay fade-out tail in ms, started only once the window is gone", 80, Config::Values::SIntValueOptions{.min = 0});
    g_hold           = makeShared<Config::Values::CIntValue>("plugin:3la_glitch_close:hold", "max ms to hold the finished collapse while waiting for the window to actually vanish, so the fade never dissolves back onto a live window", 1000,
                                                             Config::Values::SIntValueOptions{.min = 0});
    g_closeAt        = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:close_at", "fraction of duration after which glitchclose:close sends the real close (1 = as the burst ends; the hold then covers the app's close latency)", 1.0F,
                                                              Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_strength       = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:strength", "master multiplier on all displacement: 0 = calm, 5 = extreme", 1.0F,
                                                              Config::Values::SFloatValueOptions{.min = 0.F, .max = 5.F});
    g_aberration     = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:aberration", "RGB channel split", 0.5F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_blocks         = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:blocks", "slice tearing and macroblock corruption", 0.6F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_noise          = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:noise", "digital static", 0.5F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_scanlines      = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:scanlines", "CRT scanline darkening", 0.4F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_roll           = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:roll", "rolling bright bar sweeping the window", 0.5F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_melt           = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:melt", "wavy vertical tear boundary (0 = flat shear, no ripple)", 0.5F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_tear           = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:tear", "v-sync style frame tear: a hard seam sweeping down, everything below it shifted", 0.5F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_tearSpeed      = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:tear_speed", "how many times the tear seam sweeps the window during the burst", 3.0F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 12.F});
    g_ghost          = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:ghost", "echo copies of the whole frame offset sideways, so a torn frame reads as a doubled signal", 0.5F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_vignette       = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:vignette", "edge darkening as the feed collapses", 0.4F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_backdropAlpha  = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:backdrop_alpha", "opacity the backdrop collapses to", 0.75F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_backdropColor  = makeShared<Config::Values::CColorValue>("plugin:3la_glitch_close:col.backdrop", "backdrop color (0 = black)", 0);
    g_fringe1        = makeShared<Config::Values::CColorValue>("plugin:3la_glitch_close:col.fringe1", "first glitch-band fringe color (0 = magenta)", 0);
    g_fringe2        = makeShared<Config::Values::CColorValue>("plugin:3la_glitch_close:col.fringe2", "second glitch-band fringe color (0 = cyan)", 0);
    g_text           = makeShared<Config::Values::CStringValue>("plugin:3la_glitch_close:text", "caption drawn over the shader (empty = no caption)", "SIGNAL LOST");
    g_font           = makeShared<Config::Values::CStringValue>("plugin:3la_glitch_close:font", "caption font family", "monospace");
    g_textSize       = makeShared<Config::Values::CIntValue>("plugin:3la_glitch_close:text_size", "caption font size in pt", 16, Config::Values::SIntValueOptions{.min = 6});
    g_textAlpha      = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:text_alpha", "caption opacity (0 = hide the text, keep the plate)", 1.0F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_textBlink      = makeShared<Config::Values::CIntValue>("plugin:3la_glitch_close:text_blink", "caption blink half-period in ms (0 = steady, 300 = classic blink)", 0, Config::Values::SIntValueOptions{.min = 0});
    g_textAt         = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:text_at", "fraction of duration after which the caption appears", 0.4F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_textPadding    = makeShared<Config::Values::CIntValue>("plugin:3la_glitch_close:text_padding", "padding between the caption and its plate, in px", 14, Config::Values::SIntValueOptions{.min = 0});
    g_textBgRound    = makeShared<Config::Values::CIntValue>("plugin:3la_glitch_close:text_bg_round", "corner radius of the caption plate, in px", 4, Config::Values::SIntValueOptions{.min = 0});
    g_textBgAlpha    = makeShared<Config::Values::CFloatValue>("plugin:3la_glitch_close:text_bg_alpha", "caption plate opacity (0 = text with no plate)", 0.85F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    g_textColor      = makeShared<Config::Values::CColorValue>("plugin:3la_glitch_close:col.text", "caption colour (0 = white)", 0);
    g_textBgColor    = makeShared<Config::Values::CColorValue>("plugin:3la_glitch_close:col.text_bg", "caption plate colour (0 = red)", 0);
    g_minSize        = makeShared<Config::Values::CIntValue>("plugin:3la_glitch_close:min_size", "skip windows smaller than this on either axis (px)", 80, Config::Values::SIntValueOptions{.min = 0});
    g_ignoreChildren = makeShared<Config::Values::CIntValue>("plugin:3la_glitch_close:ignore_children", "skip child windows: dialogs, transients, modals (0 = glitch them too)", 1,
                                                             Config::Values::SIntValueOptions{.min = 0, .max = 1});
    g_ignoreClass    = makeShared<Config::Values::CStringValue>("plugin:3la_glitch_close:ignore_class", "regex of window classes to never glitch (empty = none)", "^(xdg-desktop-portal.*)$");
    g_ignoreTitle    = makeShared<Config::Values::CStringValue>("plugin:3la_glitch_close:ignore_title", "regex of window titles to never glitch (empty = none)", "");

    HyprlandAPI::addConfigValueV2(PHANDLE, g_duration);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_fade);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_hold);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_closeAt);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_strength);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_aberration);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_blocks);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_noise);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_scanlines);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_roll);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_melt);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_tear);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_tearSpeed);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_ghost);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_vignette);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_backdropAlpha);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_backdropColor);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_fringe1);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_fringe2);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_text);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_font);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textSize);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textAlpha);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textBlink);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textAt);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textPadding);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textBgRound);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textBgAlpha);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textColor);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_textBgColor);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_minSize);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_ignoreChildren);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_ignoreClass);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_ignoreTitle);

    HyprlandAPI::addDispatcherV2(PHANDLE, "glitchclose:close", [](std::string) { return g_glitchClose.dispatchClose(); });
    HyprlandAPI::addLuaFunction(PHANDLE, "glitchclose", "close", luaGlitchClose); // auto-removed on unload

    g_closeListener  = Event::bus()->m_events.window.close.listen([](const PHLWINDOW& w) { g_glitchClose.onWindowClose(w); });
    g_renderListener = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) { g_glitchClose.onRenderStage(stage); });
    g_reloadListener = Event::bus()->m_events.config.reloaded.listen([] { g_glitchClose.onConfigReloaded(); });

    return {"3LA-GlitchClose", "Shader-driven glitch collapse where a closed window was", "sispx", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_closeListener.reset();
    g_renderListener.reset();
    g_reloadListener.reset();

    g_glitchClose.reset();

    g_duration.reset();
    g_fade.reset();
    g_hold.reset();
    g_closeAt.reset();
    g_strength.reset();
    g_aberration.reset();
    g_blocks.reset();
    g_noise.reset();
    g_scanlines.reset();
    g_roll.reset();
    g_melt.reset();
    g_tear.reset();
    g_tearSpeed.reset();
    g_ghost.reset();
    g_vignette.reset();
    g_backdropAlpha.reset();
    g_backdropColor.reset();
    g_fringe1.reset();
    g_fringe2.reset();
    g_text.reset();
    g_font.reset();
    g_textSize.reset();
    g_textAlpha.reset();
    g_textBlink.reset();
    g_textAt.reset();
    g_textPadding.reset();
    g_textBgRound.reset();
    g_textBgAlpha.reset();
    g_textColor.reset();
    g_textBgColor.reset();
    g_minSize.reset();
    g_ignoreChildren.reset();
    g_ignoreClass.reset();
    g_ignoreTitle.reset();
}
