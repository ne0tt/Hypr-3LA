#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>

inline HANDLE                           PHANDLE = nullptr;

inline SP<Config::Values::CIntValue>    g_enabled;
inline SP<Config::Values::CIntValue>    g_offset;
inline SP<Config::Values::CIntValue>    g_thickness;
inline SP<Config::Values::CIntValue>    g_chamfer;
inline SP<Config::Values::CColorValue>  g_colorActive;
inline SP<Config::Values::CColorValue>  g_colorInactive;
inline SP<Config::Values::CColorValue>  g_colorBackground;
inline SP<Config::Values::CIntValue>    g_corners;
inline SP<Config::Values::CIntValue>    g_cornersSize;
inline SP<Config::Values::CColorValue>  g_colorCorners;

inline SP<Config::Values::CIntValue>    g_titlebar;
inline SP<Config::Values::CIntValue>    g_titlebarHeight;
inline SP<Config::Values::CIntValue>    g_titlebarOffset;
inline SP<Config::Values::CIntValue>    g_titlebarOffsetTop;
inline SP<Config::Values::CIntValue>    g_titlebarOffsetLeft;
inline SP<Config::Values::CIntValue>    g_titlebarOffsetRight;
inline SP<Config::Values::CIntValue>    g_titlebarGap;
inline SP<Config::Values::CColorValue>  g_colorTitlebarActive;
inline SP<Config::Values::CColorValue>  g_colorTitlebarInactive;
inline SP<Config::Values::CFloatValue>  g_titlebarOpacityActive;
inline SP<Config::Values::CFloatValue>  g_titlebarOpacityInactive;
inline SP<Config::Values::CIntValue>    g_textSize;
inline SP<Config::Values::CStringValue> g_textFont;
inline SP<Config::Values::CColorValue>  g_colorTextActive;
inline SP<Config::Values::CColorValue>  g_colorTextInactive;

inline SP<Config::Values::CStringValue> g_ignoreClass;
inline SP<Config::Values::CStringValue> g_ignoreTitle;
inline SP<Config::Values::CStringValue> g_titlebarIgnoreClass;
inline SP<Config::Values::CStringValue> g_titlebarIgnoreTitle;
inline SP<Config::Values::CStringValue> g_titleRules;
inline SP<Config::Values::CIntValue>    g_frame;
inline SP<Config::Values::CStringValue> g_frameIgnoreClass;
inline SP<Config::Values::CStringValue> g_frameIgnoreTitle;
inline SP<Config::Values::CStringValue> g_offsetTopFlushClass;

inline SP<Config::Values::CIntValue>    g_shadow;
inline SP<Config::Values::CIntValue>    g_shadowRange;
inline SP<Config::Values::CIntValue>    g_shadowRenderPower;
inline SP<Config::Values::CIntValue>    g_shadowSharp;
inline SP<Config::Values::CIntValue>    g_shadowOffsetX;
inline SP<Config::Values::CIntValue>    g_shadowOffsetY;
inline SP<Config::Values::CFloatValue>  g_shadowScale;
inline SP<Config::Values::CColorValue>  g_colorShadowActive;
inline SP<Config::Values::CColorValue>  g_colorShadowInactive;
inline SP<Config::Values::CStringValue> g_shadowIgnoreClass;
inline SP<Config::Values::CStringValue> g_shadowIgnoreTitle;
