#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>

inline HANDLE                          PHANDLE = nullptr;

inline SP<Config::Values::CIntValue>   g_offset;
inline SP<Config::Values::CIntValue>   g_length;
inline SP<Config::Values::CIntValue>   g_thickness;
inline SP<Config::Values::CColorValue> g_color;
inline SP<Config::Values::CColorValue> g_colorInactive;
inline SP<Config::Values::CIntValue>   g_flashCount;
inline SP<Config::Values::CIntValue>   g_flashDuration;
inline SP<Config::Values::CIntValue>   g_flashOnFocus;
inline SP<Config::Values::CIntValue>   g_focusFlashCount;
inline SP<Config::Values::CIntValue>   g_focusFlashDuration;
