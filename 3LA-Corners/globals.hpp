#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>

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

inline SP<Config::Values::CIntValue>   g_glow;
inline SP<Config::Values::CIntValue>   g_glowSize;
inline SP<Config::Values::CFloatValue> g_glowStrength;
inline SP<Config::Values::CColorValue> g_colorGlow;

inline SP<Config::Values::CIntValue>   g_lines;
inline SP<Config::Values::CIntValue>   g_linesThickness;
inline SP<Config::Values::CIntValue>   g_linesOffset;
inline SP<Config::Values::CColorValue> g_colorLines;

inline SP<Config::Values::CStringValue> g_ignoreClass;
inline SP<Config::Values::CStringValue> g_ignoreTitle;
