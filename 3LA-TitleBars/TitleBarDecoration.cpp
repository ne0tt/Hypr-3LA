#include "TitleBarDecoration.hpp"
#include "globals.hpp"

#include <algorithm>
#include <optional>
#include <regex>
#include <vector>

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/Types.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

using namespace Render;

// horizontal inset (px) the title text is kept clear of the bar's own edges
constexpr int TEXT_PAD_X = 8;

// ASCII-only uppercase: leaves UTF-8 multi-byte sequences (accents, icons in
// a window title) untouched rather than mangling them byte-by-byte
static std::string toUpperAscii(std::string s) {
    for (auto& c : s) {
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
    }
    return s;
}

CTitleBarDecoration::CTitleBarDecoration(PHLWINDOW pWindow) : IHyprWindowDecoration(pWindow), m_window(pWindow) {
    ;
}

// 0 = follow col.active, so a single configured color still paints every
// window the same until the user deliberately splits focused/unfocused.
static CHyprColor titleBarColor(bool active) {
    const auto CONFIGURED = active ? g_colorActive->value() : g_colorInactive->value();
    if (CONFIGURED != 0)
        return CHyprColor{static_cast<uint64_t>(CONFIGURED)};

    if (!active)
        return CHyprColor{static_cast<uint64_t>(g_colorActive->value())};

    return CHyprColor{1.F, 0.F, 0.F, 1.F};
}

// same 0 = follow text.col.active convention, independent of the bar's own colors
static CHyprColor titleTextColor(bool active) {
    const auto CONFIGURED = active ? g_textColorActive->value() : g_textColorInactive->value();
    if (CONFIGURED != 0)
        return CHyprColor{static_cast<uint64_t>(CONFIGURED)};

    if (!active)
        return CHyprColor{static_cast<uint64_t>(g_textColorActive->value())};

    return CHyprColor{1.F, 1.F, 1.F, 1.F};
}

// recompiles only when the pattern string differs from what's cached, so the
// common case (pattern unchanged since last call) is just a regex_search.
// Shared across every window's decoration since ignore_class/ignore_title are
// single global config values, same approach as 3LA-GlitchClose.
static bool matchesIgnorePattern(std::optional<std::regex>& cache, std::string& cachedPattern, const std::string& pattern, const std::string& s) {
    if (pattern.empty() || s.empty())
        return false;

    if (!cache || pattern != cachedPattern) {
        cachedPattern = pattern;
        try {
            cache = std::regex{pattern};
        } catch (const std::regex_error&) {
            cache.reset(); // bad user regex: ignore the filter
            return false;
        }
    }

    return std::regex_search(s, *cache);
}

static bool titleBarIgnored(const PHLWINDOW& w) {
    static std::optional<std::regex> classRe, titleRe;
    static std::string               classPattern, titlePattern;

    if (!w)
        return true;

    if (matchesIgnorePattern(classRe, classPattern, g_ignoreClass->value(), w->m_class))
        return true;
    if (matchesIgnorePattern(titleRe, titlePattern, g_ignoreTitle->value(), w->m_title))
        return true;

    return false;
}

// custom titles: 'class_regex,title_regex,override text;...', packed into a
// single string config value (a plugin-defined repeatable keyword has no path
// in from hl.config's Lua bridge, which only reaches typed config VALUES)
struct STitleRule {
    std::optional<std::regex> classRe; // unset = match any class
    std::optional<std::regex> titleRe; // unset = match any title
    std::string               title;
};

static void trimOneLeadingSpace(std::string& s) {
    if (!s.empty() && s.front() == ' ')
        s.erase(0, 1);
}

// splits "class_regex,title_regex,override text" on the first two commas
// only, so the override text may itself contain commas. On failure, *error is
// set to why (missing field vs. a regex that didn't compile) so the caller can
// surface it instead of the rule just silently never matching.
static std::optional<STitleRule> parseTitleRule(const std::string& raw, std::string* error) {
    const auto FIRSTCOMMA  = raw.find(',');
    const auto SECONDCOMMA = FIRSTCOMMA == std::string::npos ? std::string::npos : raw.find(',', FIRSTCOMMA + 1);
    if (FIRSTCOMMA == std::string::npos || SECONDCOMMA == std::string::npos) {
        *error = "needs 3 comma-separated fields (class_regex,title_regex,override text) -- got '" + raw + "'";
        return std::nullopt;
    }

    std::string classPattern  = raw.substr(0, FIRSTCOMMA);
    std::string titlePattern  = raw.substr(FIRSTCOMMA + 1, SECONDCOMMA - FIRSTCOMMA - 1);
    std::string overrideTitle = raw.substr(SECONDCOMMA + 1);
    trimOneLeadingSpace(titlePattern);
    trimOneLeadingSpace(overrideTitle);

    STitleRule rule;
    rule.title = overrideTitle;

    try {
        if (!classPattern.empty())
            rule.classRe = std::regex{classPattern};
        if (!titlePattern.empty())
            rule.titleRe = std::regex{titlePattern};
    } catch (const std::regex_error& e) {
        *error = "bad regex in '" + raw + "': " + e.what();
        return std::nullopt;
    }

    return rule;
}

// re-splits/recompiles the whole list only when the raw string differs from
// what's cached, same "cheap on the common case" approach as matchesIgnorePattern
static const std::vector<STitleRule>& titleRules() {
    static std::string             cachedRaw;
    static std::vector<STitleRule> cachedRules;

    const auto RAW = g_titleRules->value();
    if (RAW == cachedRaw)
        return cachedRules;

    cachedRaw = RAW;
    cachedRules.clear();

    size_t start = 0;
    while (start <= RAW.size()) {
        const auto END = RAW.find(';', start);
        const auto SEG = RAW.substr(start, END == std::string::npos ? std::string::npos : END - start);

        if (!SEG.empty()) {
            std::string error;
            if (auto rule = parseTitleRule(SEG, &error))
                cachedRules.push_back(std::move(*rule));
            else
                HyprlandAPI::addNotification(PHANDLE, "[3LA-TitleBars] title_rules: skipped rule -- " + error, CHyprColor{1.F, 0.6F, 0.F, 1.F}, 6000);
        }

        if (END == std::string::npos)
            break;
        start = END + 1;
    }

    return cachedRules;
}

// the text to display for w: its own title, unless a title_rule overrides it
static std::string resolveDisplayTitle(const PHLWINDOW& w) {
    if (!w)
        return "";

    for (const auto& rule : titleRules()) {
        if (rule.classRe && !std::regex_search(w->m_class, *rule.classRe))
            continue;
        if (rule.titleRe && !std::regex_search(w->m_title, *rule.titleRe))
            continue;
        return rule.title;
    }

    return w->m_title;
}

void CTitleBarDecoration::toggle() {
    m_toggledOff = !m_toggledOff;
}

bool CTitleBarDecoration::hidden() const {
    return m_toggledOff || titleBarIgnored(m_window.lock());
}

SDecorationPositioningInfo CTitleBarDecoration::getPositioningInfo() {
    // reserved space above the window's top edge, so the bar sits above the
    // application instead of painting over its own top pixels. The reserved
    // slot includes the gap margin on top of and below the bar itself --
    // draw() insets the painted rectangle back out of it by `gap`.
    SDecorationPositioningInfo info;
    info.policy = DECORATION_POSITION_STICKY;
    info.edges  = DECORATION_EDGE_TOP;

    if (hidden()) {
        // no bar, no reserved space -- the window keeps its plain geometry
        info.desiredExtents = {Vector2D{0.0, 0.0}, Vector2D{0.0, 0.0}};
        info.reserved       = false;
        return info;
    }

    info.desiredExtents = {Vector2D{0.0, static_cast<double>(g_height->value() + 2 * g_gap->value())}, Vector2D{0.0, 0.0}};
    info.reserved       = true;
    return info;
}

void CTitleBarDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {
    ; // draw()/damageEntire() re-fetch the assigned box from the positioner directly
}

// mirrors Hyprland's own decoration:shadow:enabled -- so turning off shadows
// globally also turns off this plugin's own drawn shadow, on top of its
// separate `shadow` toggle. Reads the live core config value directly (same
// mechanism Hyprland's own code uses internally) rather than a plugin config
// value, since this isn't something 3LA-TitleBars owns.
static bool hyprShadowsEnabled() {
    static CConfigValue<Config::BOOL> ENABLED{"decoration:shadow:enabled"};
    return *ENABLED;
}

// cheap layered-rect shadow: a handful of expanded, fading-alpha copies of the
// box drawn behind it -- there's no shader access from a plugin, so this
// stands in for a true soft blur. Kept deliberately independent of Hyprland's
// own window shadow (decoration:shadow:*, which 3LA-Corners' brackets and this
// bar's own DECORATION_PART_OF_MAIN_WINDOW-less flags stay out of) so it can
// be sized/colored on its own terms.
//
// Each layer's alpha is NOT divided by LAYERS again on top of the (1-T)
// taper: the layers are drawn largest-first so they compose (over-blend)
// near the bar's edge, and skipping the extra division means
// shadow_strength=1.0 actually composites to near-opaque right at the bar,
// fading out smoothly over shadow_size -- rather than topping out around
// ~35% alpha regardless of the configured strength.
void CTitleBarDecoration::drawShadow(const CBox& scaledBox, float barAlpha) const {
    static constexpr int LAYERS = 6;

    if (!g_shadow->value() || !hyprShadowsEnabled())
        return;

    const float SIZE     = std::max<Config::INTEGER>(g_shadowSize->value(), 0);
    const float STRENGTH = std::clamp(g_shadowStrength->value(), 0.F, 1.F);
    if (SIZE <= 0.F || STRENGTH <= 0.F || barAlpha <= 0.F)
        return;

    const CHyprColor col = CHyprColor{static_cast<uint64_t>(g_shadowColor->value())};

    for (int i = LAYERS; i >= 1; --i) {
        const float T          = static_cast<float>(i) / LAYERS; // 1.0 (outermost) .. 1/LAYERS (innermost)
        const float EXPAND     = SIZE * T;
        const float LAYERALPHA = col.a * STRENGTH * barAlpha * (1.F - T);
        if (LAYERALPHA <= 0.F)
            continue;

        CRectPassElement::SRectData data;
        data.box   = CBox{scaledBox}.expand(EXPAND);
        data.color = CHyprColor{static_cast<float>(col.r), static_cast<float>(col.g), static_cast<float>(col.b), LAYERALPHA};
        g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));
    }
}

SP<ITexture> CTitleBarDecoration::titleTexture(const std::string& title, const CHyprColor& color, int64_t colorRaw, const std::string& font, int fontPt, int maxWidthPx) {
    if (m_textTex && title == m_textCacheTitle && colorRaw == m_textCacheColor && font == m_textCacheFont && fontPt == m_textCacheFontPt && maxWidthPx == m_textCacheMaxW)
        return m_textTex;

    m_textTex         = g_pHyprRenderer->renderText(title, color, fontPt, false, font, maxWidthPx);
    m_textCacheTitle  = title;
    m_textCacheColor  = colorRaw;
    m_textCacheFont   = font;
    m_textCacheFontPt = fontPt;
    m_textCacheMaxW   = maxWidthPx;
    return m_textTex;
}

void CTitleBarDecoration::draw(PHLMONITOR pMonitor, float const& a) {
    const auto PWINDOW = m_window.lock();
    if (!PWINDOW || !Desktop::View::validMapped(PWINDOW) || PWINDOW->isHidden())
        return;

    if (Fullscreen::controller()->isFullscreen(PWINDOW))
        return;

    if (hidden())
        return;

    const bool ACTIVE = Desktop::focusState()->isWindowActive(PWINDOW);

    // deliberately ignores the renderer's opacity multiplier `a` (window
    // opacity rules, decoration active/inactive opacity) -- only the
    // configured color's own alpha, scaled by this plugin's own
    // opacity.active/opacity.inactive, affects the bar, same as 3LA-Corners
    CHyprColor  col     = titleBarColor(ACTIVE);
    const float OPACITY = std::clamp(ACTIVE ? g_opacityActive->value() : g_opacityInactive->value(), 0.F, 1.F);
    col.a *= OPACITY;
    if (col.a <= 0.F)
        return;

    Vector2D offset = PWINDOW->m_floatingOffset - pMonitor->m_position;
    if (PWINDOW->m_workspace)
        offset = offset + PWINDOW->m_workspace->m_renderOffset->value();

    // the reserved slot is height + 2*gap tall and full window width; inset it
    // by `gap` on every side so the bar floats with a margin around it
    const double GAP = std::max<Config::INTEGER>(g_gap->value(), 0);
    CBox         box = g_pDecorationPositioner->getWindowDecorationBox(this);
    box.x += GAP;
    box.y += GAP;
    box.w = std::max(0.0, box.w - 2 * GAP);
    box.h = std::max(0.0, box.h - 2 * GAP);

    // 3LA-Corners' top brackets frame the outside of the reserved slot, not
    // the bar itself -- nudge the bar up so its top edge lines up with them,
    // matching the margin the brackets already sit at on the left/right sides
    box.y -= 1.0;

    // shrink the gap between the bar's bottom edge and the window below it
    box.h += 5.0;

    CBox scaledBox = box.translate(offset).scale(pMonitor->m_scale).round();

    drawShadow(scaledBox, col.a);

    CRectPassElement::SRectData data;
    data.box   = scaledBox;
    data.color = col;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));

    const std::string DISPLAYTITLE = resolveDisplayTitle(PWINDOW);
    if (DISPLAYTITLE.empty() || scaledBox.w <= 2 * TEXT_PAD_X || scaledBox.h <= 1)
        return;

    const CHyprColor TEXTCOL    = titleTextColor(ACTIVE);
    const int64_t    TEXTCOLRAW = ACTIVE ? g_textColorActive->value() : g_textColorInactive->value();
    if (TEXTCOL.a <= 0.F)
        return;

    const int FONTPT   = std::max(1, static_cast<int>(std::round(g_textSize->value() * pMonitor->m_scale)));
    const int MAXWIDTH = static_cast<int>(scaledBox.w) - 2 * TEXT_PAD_X;

    const auto TEX = titleTexture(toUpperAscii(DISPLAYTITLE), TEXTCOL, TEXTCOLRAW, g_textFont->value(), FONTPT, MAXWIDTH);
    if (!TEX || !TEX->ok())
        return;

    CBox textBox;
    textBox.w = std::min<double>(TEX->m_size.x, MAXWIDTH);
    textBox.h = TEX->m_size.y;
    textBox.x = scaledBox.x + TEXT_PAD_X;
    textBox.y = scaledBox.y + (scaledBox.h - textBox.h) / 2.0;
    textBox.round();

    CTexPassElement::SRenderData texData;
    texData.tex = TEX;
    texData.box = textBox;
    texData.a   = col.a; // fade the text with the bar itself
    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(texData));
}

eDecorationType CTitleBarDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CTitleBarDecoration::updateWindow(PHLWINDOW pWindow) {
    damageEntire();
}

void CTitleBarDecoration::damageEntire() {
    const auto PWINDOW = m_window.lock();
    if (!PWINDOW)
        return;

    // shadow layers extend past the bar's own box, so damage must grow by
    // shadow.size too or its outer edge leaves trails when moving/resizing
    const double SHADOWEXPAND = (g_shadow->value() != 0 && hyprShadowsEnabled()) ? std::max<Config::INTEGER>(g_shadowSize->value(), 0) : 0;

    CBox box = g_pDecorationPositioner->getWindowDecorationBox(this);
    box.translate(PWINDOW->m_floatingOffset);
    box.expand(SHADOWEXPAND);
    g_pHyprRenderer->damageBox(box);
}

eDecorationLayer CTitleBarDecoration::getDecorationLayer() {
    return DECORATION_LAYER_OVER;
}

uint64_t CTitleBarDecoration::getDecorationFlags() {
    // deliberately NOT DECORATION_PART_OF_MAIN_WINDOW: that flag is what tells
    // Hyprland's drop shadow to grow and include this decoration's box, and
    // the shadow should keep hugging the window/border, not the bar above it
    return 0;
}

std::string CTitleBarDecoration::getDisplayName() {
    return "3LA-TitleBars";
}
