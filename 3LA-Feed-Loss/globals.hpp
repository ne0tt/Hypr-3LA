#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>

inline HANDLE                           PHANDLE = nullptr;

inline SP<Config::Values::CIntValue>    g_duration;
inline SP<Config::Values::CFloatValue>  g_staticAlpha;
inline SP<Config::Values::CFloatValue>  g_backdropAlpha;
inline SP<Config::Values::CStringValue> g_text;
inline SP<Config::Values::CIntValue>    g_textSize;
inline SP<Config::Values::CFloatValue>  g_textAlpha;
inline SP<Config::Values::CIntValue>    g_textBlink;
inline SP<Config::Values::CColorValue>  g_textColor;
inline SP<Config::Values::CStringValue> g_font;
inline SP<Config::Values::CIntValue>    g_glitch;
inline SP<Config::Values::CIntValue>    g_minSize;
inline SP<Config::Values::CIntValue>    g_ignoreChildren;
inline SP<Config::Values::CStringValue> g_ignoreClass;
inline SP<Config::Values::CStringValue> g_ignoreTitle;
inline SP<Config::Values::CFloatValue>  g_closeAt;
inline SP<Config::Values::CIntValue>    g_fade;
inline SP<Config::Values::CColorValue>  g_staticColor;
inline SP<Config::Values::CColorValue>  g_backdropColor;
inline SP<Config::Values::CColorValue>  g_fringe1;
inline SP<Config::Values::CColorValue>  g_fringe2;
