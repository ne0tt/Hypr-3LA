#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>

inline HANDLE                           PHANDLE = nullptr;

// timing
inline SP<Config::Values::CIntValue>    g_duration;
inline SP<Config::Values::CIntValue>    g_fade;
inline SP<Config::Values::CFloatValue>  g_closeAt;

// shader knobs
inline SP<Config::Values::CFloatValue>  g_strength;
inline SP<Config::Values::CFloatValue>  g_aberration;
inline SP<Config::Values::CFloatValue>  g_blocks;
inline SP<Config::Values::CFloatValue>  g_noise;
inline SP<Config::Values::CFloatValue>  g_scanlines;
inline SP<Config::Values::CFloatValue>  g_roll;
inline SP<Config::Values::CFloatValue>  g_melt;
inline SP<Config::Values::CFloatValue>  g_tear;
inline SP<Config::Values::CFloatValue>  g_tearSpeed;
inline SP<Config::Values::CFloatValue>  g_ghost;
inline SP<Config::Values::CFloatValue>  g_vignette;
inline SP<Config::Values::CFloatValue>  g_backdropAlpha;
inline SP<Config::Values::CColorValue>  g_backdropColor;
inline SP<Config::Values::CColorValue>  g_fringe1;
inline SP<Config::Values::CColorValue>  g_fringe2;

// caption overlaid on top of the shader output
inline SP<Config::Values::CStringValue> g_text;
inline SP<Config::Values::CStringValue> g_font;
inline SP<Config::Values::CIntValue>    g_textSize;
inline SP<Config::Values::CFloatValue>  g_textAlpha;
inline SP<Config::Values::CIntValue>    g_textBlink;
inline SP<Config::Values::CFloatValue>  g_textAt;
inline SP<Config::Values::CIntValue>    g_textPadding;
inline SP<Config::Values::CIntValue>    g_textBgRound;
inline SP<Config::Values::CFloatValue>  g_textBgAlpha;
inline SP<Config::Values::CColorValue>  g_textColor;
inline SP<Config::Values::CColorValue>  g_textBgColor;

// window filtering
inline SP<Config::Values::CIntValue>    g_minSize;
inline SP<Config::Values::CIntValue>    g_ignoreChildren;
inline SP<Config::Values::CStringValue> g_ignoreClass;
inline SP<Config::Values::CStringValue> g_ignoreTitle;
