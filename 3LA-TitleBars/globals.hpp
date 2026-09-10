#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>

inline HANDLE                           PHANDLE = nullptr;

inline SP<Config::Values::CIntValue>    g_height;
inline SP<Config::Values::CIntValue>    g_gap;
inline SP<Config::Values::CIntValue>    g_gapBottom;
inline SP<Config::Values::CColorValue>  g_colorActive;
inline SP<Config::Values::CColorValue>  g_colorInactive;
inline SP<Config::Values::CFloatValue>  g_opacityActive;
inline SP<Config::Values::CFloatValue>  g_opacityInactive;
inline SP<Config::Values::CIntValue>    g_shadow;
inline SP<Config::Values::CIntValue>    g_shadowSize;
inline SP<Config::Values::CFloatValue>  g_shadowStrength;
inline SP<Config::Values::CColorValue>  g_shadowColor;
inline SP<Config::Values::CIntValue>    g_textSize;
inline SP<Config::Values::CStringValue> g_textFont;
inline SP<Config::Values::CColorValue>  g_textColorActive;
inline SP<Config::Values::CColorValue>  g_textColorInactive;
inline SP<Config::Values::CStringValue> g_ignoreClass;
inline SP<Config::Values::CStringValue> g_ignoreTitle;
inline SP<Config::Values::CStringValue> g_titleRules;
