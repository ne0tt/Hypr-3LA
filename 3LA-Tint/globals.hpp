#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>

inline HANDLE                           PHANDLE = nullptr;

inline SP<Config::Values::CIntValue>    g_enabled;
inline SP<Config::Values::CStringValue> g_matchTitle;
inline SP<Config::Values::CStringValue> g_matchClass;
inline SP<Config::Values::CColorValue>  g_color;
inline SP<Config::Values::CFloatValue>  g_strength;
inline SP<Config::Values::CFloatValue>  g_satLo;
inline SP<Config::Values::CFloatValue>  g_satHi;
inline SP<Config::Values::CFloatValue>  g_minLuma;
inline SP<Config::Values::CFloatValue>  g_lightLo;
inline SP<Config::Values::CFloatValue>  g_lightHi;
