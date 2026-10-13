#include "MilSpecDecoration.hpp"
#include "globals.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <cmath>
#include <numbers>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <cairo/cairo.h>

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

using namespace Render;

// largest texture edge we will ask GL for; GL_MAX_TEXTURE_SIZE is >= 16384 on
// anything that runs Hyprland, and no sane window frame gets near it
constexpr int MAX_TEX_SIZE = 16384;

// corner covers when neither corners.col nor col.background is set: an
// opaque near-black, so a cover is never invisible
constexpr uint64_t COVER_FALLBACK_COL = 0xFF0A0F14;

// horizontal inset (px, before monitor scale) between the title text and the
// frame's inner edges / the top-left chamfer
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

// custom titles, same format as 3la_titlebars:title_rules:
// 'class_regex,title_regex,override text;...' packed into one string value
// (hl.config's Lua bridge only reaches typed config VALUES, not keywords)
struct STitleRule {
    std::optional<std::regex> classRe; // unset = match any class
    std::optional<std::regex> titleRe; // unset = match any title
    std::string               title;
};

static void trimOneLeadingSpace(std::string& s) {
    if (!s.empty() && s.front() == ' ')
        s.erase(0, 1);
}

// splits on the first two commas only, so the override text may contain commas
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

static uint64_t titleRulesGen = 0;

// re-splits/recompiles only when the raw string changes; a bad rule is
// skipped with a notification instead of silently never matching
static const std::vector<STitleRule>& titleRules() {
    static std::string             cachedRaw;
    static std::vector<STitleRule> cachedRules;

    const auto RAW = g_titleRules->value();
    if (RAW == cachedRaw)
        return cachedRules;

    cachedRaw = RAW;
    cachedRules.clear();
    ++titleRulesGen;

    size_t start = 0;
    while (start <= RAW.size()) {
        const auto END = RAW.find(';', start);
        const auto SEG = RAW.substr(start, END == std::string::npos ? std::string::npos : END - start);

        if (!SEG.empty()) {
            std::string error;
            if (auto rule = parseTitleRule(SEG, &error))
                cachedRules.push_back(std::move(*rule));
            else
                HyprlandAPI::addNotification(PHANDLE, "[3LA-MilSpec] title_rules: skipped rule -- " + error, CHyprColor{1.F, 0.6F, 0.F, 1.F}, 6000);
        }

        if (END == std::string::npos)
            break;
        start = END + 1;
    }

    return cachedRules;
}

// the bar text for w: its own title, unless a title_rule overrides it
static std::string resolveDisplayTitle(const std::vector<STitleRule>& rules, const PHLWINDOW& w) {
    for (const auto& rule : rules) {
        if (rule.classRe && !std::regex_search(w->m_class, *rule.classRe))
            continue;
        if (rule.titleRe && !std::regex_search(w->m_title, *rule.titleRe))
            continue;
        return rule.title;
    }

    return w->m_title;
}

CMilSpecDecoration::CMilSpecDecoration(PHLWINDOW pWindow) : IHyprWindowDecoration(pWindow), m_window(pWindow) {
    ;
}

const std::string& CMilSpecDecoration::displayTitle(const PHLWINDOW& w) {
    const auto& RULES = titleRules();
    if (m_hasDisplayTitle && m_displayTitleGen == titleRulesGen && m_displayTitleClass == w->m_class && m_displayTitleSrc == w->m_title)
        return m_displayTitle;

    m_displayTitle      = resolveDisplayTitle(RULES, w);
    m_displayTitleClass = w->m_class;
    m_displayTitleSrc   = w->m_title;
    m_displayTitleGen   = titleRulesGen;
    m_hasDisplayTitle   = true;
    return m_displayTitle;
}

static CHyprColor borderGradientColor(bool active) {
    static CConfigValue<Config::IComplexConfigValue> activeBorder("general:col.active_border");
    static CConfigValue<Config::IComplexConfigValue> inactiveBorder("general:col.inactive_border");

    auto* const DATA = active ? activeBorder.ptr() : inactiveBorder.ptr();
    if (DATA && DATA->getDataType() == Config::CVD_TYPE_GRADIENT) {
        const auto* GRADIENT = static_cast<Config::CGradientValueData*>(DATA);
        if (!GRADIENT->m_colors.empty())
            return GRADIENT->m_colors[0];
    }

    return CHyprColor{1.F, 1.F, 1.F, 1.F};
}

// same resolution order as 3LA-Corners: own value, then (inactive only)
// col.active if that is set explicitly, then the core border gradient
static CHyprColor frameColor(bool active) {
    const auto CONFIGURED = active ? g_colorActive->value() : g_colorInactive->value();
    if (CONFIGURED != 0)
        return CHyprColor{static_cast<uint64_t>(CONFIGURED)};

    if (!active) {
        const auto ACTIVECOL = g_colorActive->value();
        if (ACTIVECOL != 0)
            return CHyprColor{static_cast<uint64_t>(ACTIVECOL)};
    }

    return borderGradientColor(active);
}

// same resolution order as the frame: own value, then (inactive only) the
// active value if set, then the frame color of this window's own state
static CHyprColor titlebarColor(bool active, const CHyprColor& frame) {
    const auto CONFIGURED = active ? g_colorTitlebarActive->value() : g_colorTitlebarInactive->value();
    if (CONFIGURED != 0)
        return CHyprColor{static_cast<uint64_t>(CONFIGURED)};

    if (!active) {
        const auto ACTIVECOL = g_colorTitlebarActive->value();
        if (ACTIVECOL != 0)
            return CHyprColor{static_cast<uint64_t>(ACTIVECOL)};
    }

    return frame;
}

// same order again, bottoming out at black or white, whichever reads better
// on the bar actually drawn behind it
static CHyprColor textColor(bool active, const CHyprColor& bar) {
    auto CONFIGURED = active ? g_colorTextActive->value() : g_colorTextInactive->value();
    if (CONFIGURED == 0 && !active)
        CONFIGURED = g_colorTextActive->value();
    if (CONFIGURED != 0)
        return CHyprColor{static_cast<uint64_t>(CONFIGURED)};

    const double LUMA = 0.2126 * bar.r + 0.7152 * bar.g + 0.0722 * bar.b;
    return LUMA > 0.5 ? CHyprColor{0.F, 0.F, 0.F, 1.F} : CHyprColor{1.F, 1.F, 1.F, 1.F};
}

// corners.col, then col.background, then the opaque fallback; alpha forced to
// 1 so a cover always masks the window's corner instead of letting it show
static CHyprColor coverColor() {
    CHyprColor col{COVER_FALLBACK_COL};
    if (const auto V = g_colorCorners->value(); V != 0)
        col = CHyprColor{static_cast<uint64_t>(V)};
    else if (const auto V = g_colorBackground->value(); V != 0)
        col = CHyprColor{static_cast<uint64_t>(V)};
    col.a = 1.F;
    return col;
}

// recompiles only when the pattern string differs from what's cached; a bad
// user regex disables that filter instead of throwing
static bool matchesIgnorePattern(std::optional<std::regex>& cache, std::string& cachedPattern, const std::string& pattern, const std::string& s) {
    if (pattern.empty() || s.empty())
        return false;

    if (!cache || pattern != cachedPattern) {
        cachedPattern = pattern;
        try {
            cache = std::regex{pattern};
        } catch (const std::regex_error&) {
            cache.reset();
            return false;
        }
    }

    return std::regex_search(s, *cache);
}

bool CMilSpecDecoration::hidden() const {
    static std::optional<std::regex> classRe, titleRe;
    static std::string               classPattern, titlePattern;

    const auto PWINDOW = m_window.lock();
    if (!PWINDOW || !g_enabled->value())
        return true;

    return matchesIgnorePattern(classRe, classPattern, g_ignoreClass->value(), PWINDOW->m_class) ||
        matchesIgnorePattern(titleRe, titlePattern, g_ignoreTitle->value(), PWINDOW->m_title);
}

void CMilSpecDecoration::toggle() {
    m_toggledOff = !m_toggledOff;
}

// separate caches from hidden()'s: the two filters hold different patterns,
// and sharing one cache would recompile on every alternate call
bool CMilSpecDecoration::barShown() const {
    static std::optional<std::regex> classRe, titleRe;
    static std::string               classPattern, titlePattern;

    const auto PWINDOW = m_window.lock();
    if (!PWINDOW || g_titlebar->value() == 0 || m_toggledOff)
        return false;

    return !matchesIgnorePattern(classRe, classPattern, g_titlebarIgnoreClass->value(), PWINDOW->m_class) &&
        !matchesIgnorePattern(titleRe, titlePattern, g_titlebarIgnoreTitle->value(), PWINDOW->m_title);
}

// own caches again, for the same reason as barShown()
bool CMilSpecDecoration::frameShown() const {
    static std::optional<std::regex> classRe, titleRe;
    static std::string               classPattern, titlePattern;

    const auto PWINDOW = m_window.lock();
    if (!PWINDOW || g_frame->value() == 0)
        return false;

    return !matchesIgnorePattern(classRe, classPattern, g_frameIgnoreClass->value(), PWINDOW->m_class) &&
        !matchesIgnorePattern(titleRe, titlePattern, g_frameIgnoreTitle->value(), PWINDOW->m_title);
}

// own caches again, for the same reason as barShown()
bool CMilSpecDecoration::topFlush() const {
    static std::optional<std::regex> classRe;
    static std::string               classPattern;

    const auto PWINDOW = m_window.lock();
    return PWINDOW && matchesIgnorePattern(classRe, classPattern, g_offsetTopFlushClass->value(), PWINDOW->m_class);
}

// per-side bar inset; -1 = use titlebar.offset
static double barOffset(const SP<Config::Values::CIntValue>& side) {
    const auto V = side->value();
    return V >= 0 ? V : std::max<Config::INTEGER>(g_titlebarOffset->value(), 0);
}

// what sits between the top stroke and the window's border: the bar with its
// inset above and gap below when shown, otherwise the plain frame offset
// (or nothing for a top_flush window)
static double topBand(bool barOn, bool flush) {
    if (!barOn)
        return flush ? 0 : std::max<Config::INTEGER>(g_offset->value(), 0);

    return barOffset(g_titlebarOffsetTop) + std::max<Config::INTEGER>(g_titlebarHeight->value(), 1) + std::max<Config::INTEGER>(g_titlebarGap->value(), 0);
}

SDecorationPositioningInfo CMilSpecDecoration::getPositioningInfo() {
    SDecorationPositioningInfo info;
    info.policy   = DECORATION_POSITION_STICKY;
    info.edges    = DECORATION_EDGE_TOP | DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT;
    // below the core border (10000) and 3LA-Corners (9990) -> stacked outside both
    info.priority = 9980;

    if (hidden()) {
        info.desiredExtents = {{0, 0}, {0, 0}};
        info.reserved       = false;
        return info;
    }

    const double T      = std::max<Config::INTEGER>(g_thickness->value(), 1);
    const double E      = std::max<Config::INTEGER>(g_offset->value(), 0) + T;
    info.desiredExtents = {{E, T + topBand(barShown(), topFlush())}, {E, E}};
    info.reserved       = true;
    return info;
}

void CMilSpecDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {
    const auto PWINDOW = m_window.lock();
    m_assigned         = reply.assignedGeometry;
    m_assignedWinSize  = PWINDOW ? PWINDOW->getWindowMainSurfaceBox().size() : Vector2D{};
}

CMilSpecDecoration::SInsets CMilSpecDecoration::insets(const PHLWINDOW& w) const {
    SInsets ins;

    if (m_assigned.w > 0 && m_assigned.h > 0) {
        // 4-edge sticky: assigned box = window box grown by every sticky
        // decoration from the border outward up to and including us
        ins.l = -m_assigned.x;
        ins.t = -m_assigned.y;
        ins.r = m_assigned.w - ins.l - m_assignedWinSize.x;
        ins.b = m_assigned.h - ins.t - m_assignedWinSize.y;
    } else {
        // no reply yet: assume only the core border sits inside us
        const double B = w->getRealBorderSize(), T = std::max<Config::INTEGER>(g_thickness->value(), 1);
        const double E = B + std::max<Config::INTEGER>(g_offset->value(), 0) + T;
        ins            = {.l = E, .t = B + T + topBand(barShown(), topFlush()), .r = E, .b = E};
    }

    // reserved top space beyond our own slot belongs to decorations stacked
    // outside us (e.g. 3LA-TitleBars' bar): grow the frame upward to enclose it
    const double TOTALTOP = g_pDecorationPositioner->getWindowDecorationReserved(m_window).topLeft.y;
    ins.topExtra          = std::max(0.0, TOTALTOP - ins.t);
    ins.t += ins.topExtra;
    return ins;
}

CBox CMilSpecDecoration::frameBox(const PHLWINDOW& w, Desktop::View::IGeometric::eGeometricValueType type) const {
    const auto INS  = insets(w);
    const auto POS  = w->position(type);
    const auto SIZE = w->size(type);
    return {POS.x - INS.l, POS.y - INS.t, SIZE.x + INS.l + INS.r, SIZE.y + INS.t + INS.b};
}

// popin (or any animated resize) changes the window size every frame, and
// rebuilding the cairo frame / per-pixel shadow at each size stalls the
// compositor. While the size animates, textures are built once at the goal
// size and stretched over the current box -- as the client buffer is.
static Desktop::View::IGeometric::eGeometricValueType textureGeometry(const PHLWINDOW& w) {
    const auto GOAL = w->size(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    if (GOAL.x <= 0 || GOAL.y <= 0 || GOAL == w->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT))
        return Desktop::View::IGeometric::GEOMETRIC_CURRENT;
    return Desktop::View::IGeometric::GEOMETRIC_GOAL;
}

// maps r from texture box `from` onto the drawn box `to`, edges rounded so
// adjacent pieces still meet without seams (identity when the boxes match)
static CBox stretchBox(const CBox& r, const CBox& from, const CBox& to) {
    const double SX = to.w / from.w, SY = to.h / from.h;
    const double X0 = std::round(to.x + (r.x - from.x) * SX), Y0 = std::round(to.y + (r.y - from.y) * SY);
    const double X1 = std::round(to.x + (r.x + r.w - from.x) * SX), Y1 = std::round(to.y + (r.y + r.h - from.y) * SY);
    return {X0, Y0, X1 - X0, Y1 - Y0};
}

// x + y = K of the bar's own 45 deg cut, in texture px: the frame's outer
// diagonal is x + y = C, its stroke's inner edge sits thick*sqrt2 further in,
// and the bar's cut another `cut`*sqrt2 -- parallel, at an even margin
static double barCutK(int chamfer, int thick, int cut) {
    return chamfer + (thick + cut) * std::numbers::sqrt2;
}

// rounded rect whose corners follow Hyprland's |x|^p + |y|^p = r^p rounding
// (p = 2 is a circle), as a closed polyline. 24 segments per corner keep the
// chord error far below a pixel at any sane radius.
static void roundedRectPath(cairo_t* cr, double x, double y, double w, double h, double r, double power) {
    constexpr int SEGS = 24;
    r                  = std::clamp(r, 0.0, std::min(w, h) / 2.0);
    const double EXP   = 2.0 / std::max(power, 0.1);

    // corner centres, walked clockwise from the top-right; each corner sweeps
    // a quarter turn starting where the previous edge arrives
    const double CX[4] = {x + w - r, x + w - r, x + r, x + r};
    const double CY[4] = {y + r, y + h - r, y + h - r, y + r};

    cairo_new_sub_path(cr);
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i <= SEGS; ++i) {
            const double T  = (c - 1 + static_cast<double>(i) / SEGS) * std::numbers::pi / 2.0;
            const double CT = std::cos(T), ST = std::sin(T);
            cairo_line_to(cr, CX[c] + r * std::copysign(std::pow(std::abs(CT), EXP), CT), CY[c] + r * std::copysign(std::pow(std::abs(ST), EXP), ST));
        }
    }
    cairo_close_path(cr);
}

SP<ITexture> CMilSpecDecoration::frameTexture(int w, int h, int thick, int chamfer, const SBarGeom& bar, const SHoleGeom& hole, const SCoverGeom& cover, const CHyprColor& frameCol,
                                              const CHyprColor& barCol, const CHyprColor& bgCol, const CHyprColor& coverCol) {
    if (m_frameTex && w == m_frameW && h == m_frameH && thick == m_frameThick && chamfer == m_frameChamfer && bar == m_frameBar && hole == m_frameHole && cover == m_frameCover &&
        frameCol == m_frameCol && barCol == m_frameBarCol && bgCol == m_frameBgCol && coverCol == m_frameCoverCol)
        return m_frameTex;

    m_frameTex.reset();

    auto* const SURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(SURFACE) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(SURFACE);
        return nullptr;
    }

    auto* const CR = cairo_create(SURFACE);

    // the path runs along the stroke's centre line, so the stroke's outer
    // edge lands exactly on the texture edge. The 45 deg cut is shortened on
    // the centre line so that, with a miter join, the OUTER edge of the cut
    // is exactly `chamfer` px long on each leg: insetting a 45 deg edge by
    // t moves its endpoints in by t * (2 - sqrt2) along each axis.
    const double HT = thick / 2.0;
    const double C  = std::clamp<double>(chamfer, 0.0, std::min(w, h) / 2.0);

    // the outline inset by `d`: d = HT is the stroke's centre line, d = thick
    // its inner edge, d = 0 its outer edge
    const auto   tracePath = [&](double d) {
        const double CL = std::max(0.0, C - d * (2.0 - std::numbers::sqrt2));
        const double X0 = d, Y0 = d, X1 = w - d, Y1 = h - d;
        cairo_new_path(CR);
        cairo_move_to(CR, X0 + CL, Y0); // top edge, after the top-left cut
        cairo_line_to(CR, X1, Y0);      // top-right: square
        cairo_line_to(CR, X1, Y1 - CL); // right edge, down to the bottom-right cut
        cairo_line_to(CR, X1 - CL, Y1);
        cairo_line_to(CR, X0, Y1); // bottom-left: square
        cairo_line_to(CR, X0, Y0 + CL);
        cairo_close_path(CR); // closes through the top-left cut
    };

    if (bgCol.a > 0.F) {
        // up to the stroke's inner edge, so a translucent stroke isn't
        // blended over the fill; with the stroke hidden, out to where its
        // outer edge would be
        tracePath(frameCol.a > 0.F ? thick : 0.0);
        cairo_set_source_rgba(CR, bgCol.r, bgCol.g, bgCol.b, bgCol.a);
        cairo_fill(CR);

        // then punch the window + border back out. CLEAR rather than an
        // even-odd hole: with a big chamfer and a small offset the window's
        // corner can poke past the frame's diagonal, and even-odd would fill
        // that part of the window instead of leaving it alone
        if (hole.w > 0 && hole.h > 0) {
            cairo_save(CR);
            cairo_set_operator(CR, CAIRO_OPERATOR_CLEAR);
            cairo_new_path(CR);
            roundedRectPath(CR, hole.x, hole.y, hole.w, hole.h, hole.radius, hole.power);
            cairo_fill(CR);
            cairo_restore(CR);
        }
    }

    // the window's own corners, over the cleared hole and under the bar and
    // stroke (the window's corner can poke past the frame's diagonal, and the
    // stroke must stay on top there). Legs sit on whole pixels, so only the
    // diagonal is antialiased. Painted over the window: a cover matching what
    // is behind it reads as a cut, it is not real transparency.
    if (cover.w > 0 && cover.h > 0 && (cover.tl > 0 || cover.br > 0) && coverCol.a > 0.F) {
        const double MAXLEG = std::min(cover.w, cover.h) / 2.0;
        const double STL = std::min(cover.tl, MAXLEG), SBR = std::min(cover.br, MAXLEG);
        const double L = cover.x, T = cover.y, R = cover.x + cover.w, B = cover.y + cover.h;
        cairo_new_path(CR);
        if (STL > 0) {
            cairo_move_to(CR, L, T);
            cairo_line_to(CR, L + STL, T);
            cairo_line_to(CR, L, T + STL);
            cairo_close_path(CR);
        }
        if (SBR > 0) {
            cairo_move_to(CR, R, B - SBR);
            cairo_line_to(CR, R, B);
            cairo_line_to(CR, R - SBR, B);
            cairo_close_path(CR);
        }
        cairo_set_source_rgba(CR, coverCol.r, coverCol.g, coverCol.b, coverCol.a);
        cairo_fill(CR);
    }

    if (bar.h > 0 && bar.right > bar.left && barCol.a > 0.F) {
        // inset bar, clipped to the half-plane x + y >= K so its top-left
        // corner gets its own cut parallel to the frame's. Once the inset
        // clears the frame's cut, the half-plane misses the bar entirely and
        // its corner stays square.
        cairo_save(CR);
        if (C > 0) {
            const double K = barCutK(static_cast<int>(C), thick, bar.cut);
            cairo_move_to(CR, K, 0);
            cairo_line_to(CR, w + h, 0);
            cairo_line_to(CR, w + h, w + h);
            cairo_line_to(CR, 0, w + h);
            cairo_line_to(CR, 0, K);
            cairo_close_path(CR);
            cairo_clip(CR);
        }
        cairo_rectangle(CR, bar.left, bar.top, bar.right - bar.left, bar.h);
        cairo_set_source_rgba(CR, barCol.r, barCol.g, barCol.b, barCol.a);
        cairo_fill(CR);
        cairo_restore(CR);
    }

    if (frameCol.a > 0.F) {
        tracePath(HT);
        cairo_set_line_width(CR, thick);
        cairo_set_line_join(CR, CAIRO_LINE_JOIN_MITER);
        cairo_set_source_rgba(CR, frameCol.r, frameCol.g, frameCol.b, frameCol.a);
        cairo_stroke(CR);
    }

    cairo_destroy(CR);
    cairo_surface_flush(SURFACE);

    // uploads synchronously (glTexImage2D), so the surface can go right after
    m_frameTex = g_pHyprRenderer->createTexture(SURFACE);
    cairo_surface_destroy(SURFACE);

    m_frameW       = w;
    m_frameH       = h;
    m_frameThick   = thick;
    m_frameChamfer = chamfer;
    m_frameBar     = bar;
    m_frameHole     = hole;
    m_frameCover    = cover;
    m_frameCol      = frameCol;
    m_frameBarCol   = barCol;
    m_frameBgCol    = bgCol;
    m_frameCoverCol = coverCol;
    return m_frameTex;
}

SP<ITexture> CMilSpecDecoration::titleTexture(const std::string& title, const CHyprColor& color, const std::string& font, int fontPt, int maxWidthPx) {
    if (m_textTex && title == m_textTitle && color == m_textCol && font == m_textFont && fontPt == m_textPt && maxWidthPx == m_textMaxW)
        return m_textTex;

    m_textTex   = g_pHyprRenderer->renderText(title, color, fontPt, false, font, maxWidthPx);
    m_textTitle = title;
    m_textCol   = color;
    m_textFont  = font;
    m_textPt    = fontPt;
    m_textMaxW  = maxWidthPx;
    return m_textTex;
}

void CMilSpecDecoration::draw(PHLMONITOR pMonitor, float const& a) {
    const auto PWINDOW = m_window.lock();
    if (!PWINDOW || !pMonitor || !Desktop::View::validMapped(PWINDOW) || PWINDOW->isHidden())
        return;

    m_lastMonitor = pMonitor;

    if (Fullscreen::controller()->isFullscreen(PWINDOW) || hidden())
        return;

    const bool ACTIVE = Desktop::focusState()->isWindowActive(PWINDOW);

    // like 3LA-Corners, the renderer's `a` (window opacity rules,
    // decoration:active_opacity/inactive_opacity) is deliberately ignored:
    // only the configured colors' own alpha matters
    const CHyprColor FRAMECOL = frameColor(ACTIVE);
    const bool       BARON    = barShown();

    // titlebar.opacity.* scales the bar fill and title text only. The faded
    // fill is baked into the frame texture (its color is in the cache key);
    // at 0 neither is drawn, but the bar's reserved band stays.
    const float BAROPACITY = std::clamp(ACTIVE ? g_titlebarOpacityActive->value() : g_titlebarOpacityInactive->value(), 0.F, 1.F);
    CHyprColor  barCol     = titlebarColor(ACTIVE, FRAMECOL);
    barCol.a *= BAROPACITY;

    // a hidden stroke is baked as fully transparent: its color is part of the
    // texture cache key, so toggling it rebuilds, while the bar above still
    // resolves its fallback from the real frame color. Space is untouched.
    const CHyprColor STROKECOL = frameShown() ? FRAMECOL : CHyprColor{0.F, 0.F, 0.F, 0.F};
    const CHyprColor BGCOL{static_cast<uint64_t>(g_colorBackground->value())};
    const CHyprColor COVERCOL = coverColor();
    const bool       COVERON  = g_corners->value() != 0 && COVERCOL.a > 0.F;

    if (STROKECOL.a <= 0.F && (!BARON || barCol.a <= 0.F) && BGCOL.a <= 0.F && !COVERON)
        return;

    Vector2D offset = PWINDOW->m_floatingOffset - pMonitor->m_position;
    if (PWINDOW->m_workspace)
        offset = offset + PWINDOW->m_workspace->m_renderOffset->value();

    const auto   INS     = insets(PWINDOW);
    const double SCALE   = pMonitor->m_scale;
    const auto   GEO     = textureGeometry(PWINDOW);
    const CBox   box     = frameBox(PWINDOW, GEO).translate(offset).scale(SCALE).round();
    const CBox   drawBox = frameBox(PWINDOW).translate(offset).scale(SCALE).round();

    const int    W = static_cast<int>(box.w), H = static_cast<int>(box.h);
    if (W < 2 || H < 2 || W > MAX_TEX_SIZE || H > MAX_TEX_SIZE || drawBox.w < 1 || drawBox.h < 1)
        return;

    // everything below is in monitor pixels, so the texture maps 1:1 and
    // stays crisp at fractional scales
    const int THICK   = std::max(1, static_cast<int>(std::round(std::max<Config::INTEGER>(g_thickness->value(), 1) * SCALE)));
    const int CHAMFER = std::max(0, static_cast<int>(std::round(std::max<Config::INTEGER>(g_chamfer->value(), 0) * SCALE)));
    const auto px      = [SCALE](double v) { return static_cast<int>(std::round(v * SCALE)); };

    SBarGeom   bar;
    if (BARON) {
        const double OFFTOP = barOffset(g_titlebarOffsetTop), OFFLEFT = barOffset(g_titlebarOffsetLeft);
        bar.top   = px(INS.topExtra) + THICK + px(OFFTOP);
        bar.h     = std::clamp(px(std::max<Config::INTEGER>(g_titlebarHeight->value(), 1)), 0, std::max(0, H - bar.top - THICK));
        bar.left  = THICK + px(OFFLEFT);
        bar.right = W - THICK - px(barOffset(g_titlebarOffsetRight));
        // keep an even margin from the frame's diagonal: as far in as the
        // tighter of the two insets it meets
        bar.cut = px(std::min(OFFTOP, OFFLEFT));
    }

    // window + core border in monitor px, shared by the background's hole
    // and the corner covers
    const double BORDER = PWINDOW->getRealBorderSize();
    const CBox   WIN    = CBox{PWINDOW->position(GEO), PWINDOW->size(GEO)}
                           .expand(BORDER)
                           .translate(offset)
                           .scale(SCALE)
                           .round();

    // window + core border, rounded like the border's outer edge
    // (CHyprBorderDecoration::draw): square unless the window is rounded
    SHoleGeom hole;
    if (BGCOL.a > 0.F) {
        const double ROUNDING = PWINDOW->rounding();
        const double POWER    = PWINDOW->roundingPower();
        hole.x                = static_cast<int>(WIN.x - box.x);
        hole.y                = static_cast<int>(WIN.y - box.y);
        hole.w                = static_cast<int>(WIN.w);
        hole.h                = static_cast<int>(WIN.h);
        hole.power            = POWER;
        hole.radius           = ROUNDING > 0 ? std::max(0.0, ROUNDING + BORDER - BORDER * (std::numbers::sqrt2 - 1) * std::max(2.0 - POWER, 0.0)) * SCALE : 0.0;
    }

    // the window's own corners. Top-left only without a bar: with one, the
    // bar carries the top-left cut and the window's corner sits below it.
    SCoverGeom cover;
    if (COVERON) {
        cover.x = static_cast<int>(WIN.x - box.x);
        cover.y = static_cast<int>(WIN.y - box.y);
        cover.w = static_cast<int>(WIN.w);
        cover.h = static_cast<int>(WIN.h);

        const auto SZ = g_cornersSize->value();
        double     tl = 0, br = 0;
        if (SZ >= 0)
            tl = br = px(SZ);
        else {
            // auto: put the window's cut parallel to the frame's at the same
            // perpendicular gap as the tighter of the two straight edges
            // meeting there. In texture px from that corner of the frame, the
            // stroke's inner diagonal is x + y = C + T*sqrt2 and the window's
            // corner sits at (T + gA, T + gB), so the leg along each edge is
            //   s = C + (T + min(gA, gB)) * sqrt2 - (T + gA) - (T + gB)
            // (gA == gB == g gives C - (2 - sqrt2) * (T + g)). Measured from
            // the real geometry: topExtra and the stroke-hidden case included.
            const double C    = std::clamp<double>(CHAMFER, 0.0, std::min(W, H) / 2.0);
            const auto   leg  = [&](double gA, double gB) { return C + (THICK + std::min(gA, gB)) * std::numbers::sqrt2 - (THICK + gA) - (THICK + gB); };
            const double GL   = cover.x - THICK, GT = cover.y - THICK;
            const double GR   = W - (cover.x + cover.w) - THICK, GB = H - (cover.y + cover.h) - THICK;
            tl                = leg(GL, GT);
            br                = leg(GR, GB);
        }
        cover.tl = BARON ? 0.0 : std::max(0.0, tl);
        cover.br = std::max(0.0, br);
    }

    const auto TEX = frameTexture(W, H, THICK, CHAMFER, bar, hole, cover, STROKECOL, barCol, BGCOL, COVERCOL);
    if (!TEX || !TEX->ok())
        return;

    CTexPassElement::SRenderData data;
    data.tex = TEX;
    data.box = drawBox;
    data.a   = 1.F;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(data));

    if (bar.h <= 0 || bar.right <= bar.left || BAROPACITY <= 0.F)
        return;

    const std::string& DISPLAYTITLE = displayTitle(PWINDOW);
    if (DISPLAYTITLE.empty())
        return;

    const CHyprColor TEXTCOL = textColor(ACTIVE, barCol);
    if (TEXTCOL.a <= 0.F)
        return;

    // left padding clears the bar's own cut where it crosses the bar's top edge
    const int PAD    = px(TEXT_PAD_X);
    const int CUTX   = CHAMFER > 0 ? static_cast<int>(std::ceil(barCutK(std::clamp(CHAMFER, 0, std::min(W, H) / 2), THICK, bar.cut))) - bar.top : 0;
    const int PADL   = std::max(bar.left, CUTX) + px(TEXT_PAD_X - 5);
    const int MAXW   = bar.right - PADL - PAD;
    if (MAXW <= 0)
        return;

    const int  FONTPT = std::max(1, static_cast<int>(std::round(std::max<Config::INTEGER>(g_textSize->value(), 1) * SCALE)));
    const auto TEXT   = titleTexture(toUpperAscii(DISPLAYTITLE), TEXTCOL, g_textFont->value(), FONTPT, MAXW);
    if (!TEXT || !TEXT->ok())
        return;

    CBox textBox;
    textBox.w = std::min<double>(TEXT->m_size.x, MAXW);
    textBox.h = TEXT->m_size.y;
    textBox.x = box.x + PADL;
    // floor, not round: an odd leftover pixel goes below the caps, which read low otherwise
    textBox.y = std::floor(box.y + bar.top + (bar.h - textBox.h) / 2.0);

    CTexPassElement::SRenderData textData;
    textData.tex = TEXT;
    textData.box = stretchBox(textBox, box, drawBox);
    textData.a   = BAROPACITY; // fade the text with the bar, without re-rendering it
    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(textData));
}

eDecorationType CMilSpecDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CMilSpecDecoration::updateWindow(PHLWINDOW pWindow) {
    damageEntire();
}

void CMilSpecDecoration::damageEntire() {
    const auto PWINDOW = m_window.lock();
    if (!PWINDOW) {
        // window already torn down (destroy fires this late). A precise
        // sub-box damageBox() was observed in the sibling plugins to not get
        // repainted on non-focused monitors, so damage the whole output the
        // frame was last drawn on instead.
        if (const auto PMONITOR = m_lastMonitor.lock())
            g_pHyprRenderer->damageMonitor(PMONITOR);
        else if (m_hasDamageBox)
            g_pHyprRenderer->damageBox(m_lastDamageBox);
        return;
    }

    // +1 px for the antialiased edge landing on a rounded pixel boundary, plus
    // the shadow's reach so a focus/config change repaints it too
    CBox box = frameBox(PWINDOW).translate(PWINDOW->m_floatingOffset).expand(1 + milspecShadowPad());

    m_lastDamageBox = box;
    m_hasDamageBox  = true;

    g_pHyprRenderer->damageBox(box);
}

eDecorationLayer CMilSpecDecoration::getDecorationLayer() {
    return DECORATION_LAYER_OVER;
}

uint64_t CMilSpecDecoration::getDecorationFlags() {
    // deliberately NOT DECORATION_PART_OF_MAIN_WINDOW: that flag pulls this
    // decoration's box into Hyprland's native shadow geometry, pushing the
    // shadow out to hug the frame instead of the window/border. Same
    // reasoning as 3LA-Corners / 3LA-TitleBars.
    return DECORATION_NON_SOLID;
}

std::string CMilSpecDecoration::getDisplayName() {
    return "3LA-MilSpec";
}

// ---------------------------------------------------------------------------
// drop shadow following the chamfered frame
// ---------------------------------------------------------------------------

double milspecShadowPad() {
    if (!g_shadow->value())
        return 0;

    return std::max<Config::INTEGER>(g_shadowRange->value(), 1) + std::abs(g_shadowOffsetX->value()) + std::abs(g_shadowOffsetY->value()) + 2;
}

// same order as the frame colors: own value, then (inactive only) the active one
static CHyprColor shadowColor(bool active) {
    auto v = active ? g_colorShadowActive->value() : g_colorShadowInactive->value();
    if (v == 0 && !active)
        v = g_colorShadowActive->value();
    return CHyprColor{static_cast<uint64_t>(v)};
}

// Hyprland's own order (CHyprDropShadowDecoration::getRenderData): grow by
// range, scale the grown box about its centre, then shift by offset
static CBox shadowFullBox(CBox frame) {
    const double R = std::max<Config::INTEGER>(g_shadowRange->value(), 1);
    frame.expand(R);
    frame.scaleFromCenter(std::clamp<double>(g_shadowScale->value(), 0.0, 1.0));
    frame.translate({static_cast<double>(g_shadowOffsetX->value()), static_cast<double>(g_shadowOffsetY->value())});
    return frame;
}

CMilSpecShadowDecoration::CMilSpecShadowDecoration(PHLWINDOW pWindow) : IHyprWindowDecoration(pWindow), m_window(pWindow) {
    ;
}

bool CMilSpecShadowDecoration::shown() const {
    static std::optional<std::regex> classRe, titleRe;
    static std::string               classPattern, titlePattern;

    const auto PWINDOW = m_window.lock();
    if (!PWINDOW || !g_shadow->value())
        return false;

    // frame = 0 / frame.ignore_* only hide the stroke: the shadow still
    // traces the frame's shape. A window with no frame at all gets none.
    const auto FRAME = milspecFrameFor(PWINDOW);
    if (!FRAME || FRAME->hidden())
        return false;

    return !matchesIgnorePattern(classRe, classPattern, g_shadowIgnoreClass->value(), PWINDOW->m_class) &&
        !matchesIgnorePattern(titleRe, titlePattern, g_shadowIgnoreTitle->value(), PWINDOW->m_title);
}

SBoxExtents CMilSpecShadowDecoration::computeExtents(const PHLWINDOW& w) const {
    if (!w || !shown())
        return {};

    const auto FULL = shadowFullBox(milspecFrameFor(w)->frameBox(w));
    const auto POS  = w->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const auto SIZE = w->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);

    // +2 like Hyprland's own shadow, for rounding at fractional scales
    return {
        .topLeft     = {std::max(0.0, POS.x - FULL.x + 2), std::max(0.0, POS.y - FULL.y + 2)},
        .bottomRight = {std::max(0.0, FULL.x + FULL.w - POS.x - SIZE.x + 2), std::max(0.0, FULL.y + FULL.h - POS.y - SIZE.y + 2)},
    };
}

SDecorationPositioningInfo CMilSpecShadowDecoration::getPositioningInfo() {
    SDecorationPositioningInfo info;
    info.policy         = DECORATION_POSITION_ABSOLUTE;
    info.edges          = DECORATION_EDGE_TOP | DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT;
    info.desiredExtents = computeExtents(m_window.lock());
    info.reserved       = false;
    m_reportedExtents   = info.desiredExtents;
    return info;
}

void CMilSpecShadowDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {
    ;
}

// distance from p to segment ab
static double segDist(double px, double py, double ax, double ay, double bx, double by) {
    const double DX = bx - ax, DY = by - ay;
    const double L2 = DX * DX + DY * DY;
    const double T  = L2 > 0 ? std::clamp(((px - ax) * DX + (py - ay) * DY) / L2, 0.0, 1.0) : 0.0;
    return std::hypot(px - (ax + T * DX), py - (ay + T * DY));
}

// shadow alpha (0..1, before the color's own alpha) at pixel centre (x, y),
// in full-box px
static double shadowAlphaAt(double x, double y, const CMilSpecShadowDecoration::SKey& k) {
    // cut-out: inside distance to the frame polygon, antialiased over 1 px
    const double FX0 = k.frameX, FY0 = k.frameY, FX1 = k.frameX + k.frameW, FY1 = k.frameY + k.frameH;
    const double FC  = std::clamp<double>(k.chamfer, 0.0, std::min(k.frameW, k.frameH) / 2.0);
    const double IN  = std::min({x - FX0, FX1 - x, y - FY0, FY1 - y, ((x - FX0) + (y - FY0) - FC) / std::numbers::sqrt2, ((FX1 - x) + (FY1 - y) - FC) / std::numbers::sqrt2});
    const double CUT = std::clamp(0.5 - IN, 0.0, 1.0);
    if (CUT <= 0.0)
        return 0.0;

    // shadow core: the full box shrunk back by range (== the frame itself at
    // scale 1 / offset 0), with the frame's chamfer
    const double R   = k.range;
    double       CX0 = R, CY0 = R, CX1 = k.fullW - R, CY1 = k.fullH - R;
    if (CX1 < CX0)
        CX0 = CX1 = k.fullW / 2.0;
    if (CY1 < CY0)
        CY0 = CY1 = k.fullH / 2.0;
    const double CC = std::clamp<double>(k.chamfer, 0.0, std::min(CX1 - CX0, CY1 - CY0) / 2.0);

    if (k.sharp) {
        // mitered distance: square corners, chamfer offset parallel, like
        // Hyprland's flat sharp-shadow rect
        const double M = std::max({CX0 - x, x - CX1, CY0 - y, y - CY1, (CC - ((x - CX0) + (y - CY0))) / std::numbers::sqrt2, (CC - ((CX1 - x) + (CY1 - y))) / std::numbers::sqrt2});
        return std::clamp(R - M + 0.5, 0.0, 1.0) * CUT;
    }

    const bool INSIDE = x >= CX0 && x <= CX1 && y >= CY0 && y <= CY1 && (x - CX0) + (y - CY0) >= CC && (CX1 - x) + (CY1 - y) >= CC;
    double     d      = 0.0;
    if (!INSIDE) {
        const double V[6][2] = {{CX0 + CC, CY0}, {CX1, CY0}, {CX1, CY1 - CC}, {CX1 - CC, CY1}, {CX0, CY1}, {CX0, CY0 + CC}};
        d                    = std::numeric_limits<double>::max();
        for (int i = 0; i < 6; ++i)
            d = std::min(d, segDist(x, y, V[i][0], V[i][1], V[(i + 1) % 6][0], V[(i + 1) % 6][1]));
    }

    if (d >= R)
        return 0.0;

    // shadow.glsl: pow((range - d) / range, shadowPower)
    return std::pow((R - d) / R, k.power) * CUT;
}

static SP<ITexture> shadowTile(const CMilSpecShadowDecoration::SKey& k, int tx, int ty, int tw, int th) {
    auto* const SURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
    if (cairo_surface_status(SURFACE) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(SURFACE);
        return nullptr;
    }

    cairo_surface_flush(SURFACE);
    auto* const DATA   = cairo_image_surface_get_data(SURFACE);
    const int   STRIDE = cairo_image_surface_get_stride(SURFACE);

    for (int y = 0; y < th; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(DATA + static_cast<size_t>(y) * STRIDE);
        for (int x = 0; x < tw; ++x) {
            const double A = k.color.a * shadowAlphaAt(tx + x + 0.5, ty + y + 0.5, k);
            if (A <= 0.0) {
                row[x] = 0;
                continue;
            }
            // premultiplied ARGB32, as cairo stores it
            const auto CH = [A](double c) { return static_cast<uint32_t>(std::lround(std::clamp(c * A, 0.0, 1.0) * 255.0)); };
            row[x]        = (static_cast<uint32_t>(std::lround(std::clamp(A, 0.0, 1.0) * 255.0)) << 24) | (CH(k.color.r) << 16) | (CH(k.color.g) << 8) | CH(k.color.b);
        }
    }

    cairo_surface_mark_dirty(SURFACE);
    auto tex = g_pHyprRenderer->createTexture(SURFACE);
    cairo_surface_destroy(SURFACE);
    return tex;
}

void CMilSpecShadowDecoration::draw(PHLMONITOR pMonitor, float const& a) {
    const auto PWINDOW = m_window.lock();
    if (!PWINDOW || !pMonitor || !Desktop::View::validMapped(PWINDOW) || PWINDOW->isHidden())
        return;

    if (Fullscreen::controller()->isFullscreen(PWINDOW) || !shown())
        return;

    // frame insets / titlebar can change between repositions: keep the
    // reported extents (window damage + bounding box) in step, as
    // Hyprland's own shadow does
    if (computeExtents(PWINDOW) != m_reportedExtents)
        g_pDecorationPositioner->repositionDeco(this);

    const auto       FRAME = milspecFrameFor(PWINDOW);
    const bool       ACTIVE = Desktop::focusState()->isWindowActive(PWINDOW);
    const CHyprColor COL    = shadowColor(ACTIVE);
    if (COL.a <= 0.F)
        return;

    Vector2D offset = PWINDOW->m_floatingOffset - pMonitor->m_position;
    if (PWINDOW->m_workspace)
        offset = offset + PWINDOW->m_workspace->m_renderOffset->value();

    const double SCALE       = pMonitor->m_scale;
    const CBox   LAYOUTFRAME = FRAME->frameBox(PWINDOW, textureGeometry(PWINDOW));
    // same rounding as the frame texture, so the cut-out lines up with it
    const CBox   frame    = CBox{LAYOUTFRAME}.translate(offset).scale(SCALE).round();
    const CBox   full     = shadowFullBox(LAYOUTFRAME).translate(offset).scale(SCALE).round();
    const CBox   drawFull = shadowFullBox(FRAME->frameBox(PWINDOW)).translate(offset).scale(SCALE).round();

    if (full.w < 1 || full.h < 1 || full.w > MAX_TEX_SIZE || full.h > MAX_TEX_SIZE || drawFull.w < 1 || drawFull.h < 1)
        return;

    SKey key;
    key.fullW   = static_cast<int>(full.w);
    key.fullH   = static_cast<int>(full.h);
    key.frameX  = static_cast<int>(frame.x - full.x);
    key.frameY  = static_cast<int>(frame.y - full.y);
    key.frameW  = static_cast<int>(frame.w);
    key.frameH  = static_cast<int>(frame.h);
    key.chamfer = std::max(0, static_cast<int>(std::round(std::max<Config::INTEGER>(g_chamfer->value(), 0) * SCALE)));
    key.range   = std::max(1, static_cast<int>(std::round(std::max<Config::INTEGER>(g_shadowRange->value(), 1) * SCALE)));
    key.power   = std::clamp<int>(g_shadowRenderPower->value(), 1, 4);
    key.sharp   = g_shadowSharp->value() != 0;
    key.color   = COL;

    if (!m_hasTiles || !(key == m_key)) {
        m_tiles.clear();

        // everything well inside the frame is cut out anyway: skip it and
        // only build the ring around it (cost scales with the perimeter)
        const int IX0 = std::max(0, key.frameX + key.chamfer + 1), IY0 = std::max(0, key.frameY + key.chamfer + 1);
        const int IX1 = std::min(key.fullW, key.frameX + key.frameW - key.chamfer - 1), IY1 = std::min(key.fullH, key.frameY + key.frameH - key.chamfer - 1);

        std::vector<std::array<int, 4>> rects;
        if (IX1 <= IX0 || IY1 <= IY0)
            rects.push_back({0, 0, key.fullW, key.fullH});
        else {
            rects.push_back({0, 0, key.fullW, IY0});
            rects.push_back({0, IY1, key.fullW, key.fullH - IY1});
            rects.push_back({0, IY0, IX0, IY1 - IY0});
            rects.push_back({IX1, IY0, key.fullW - IX1, IY1 - IY0});
        }

        for (const auto& [x, y, w, h] : rects) {
            if (w <= 0 || h <= 0)
                continue;
            if (auto tex = shadowTile(key, x, y, w, h); tex && tex->ok())
                m_tiles.push_back({.tex = tex, .x = x, .y = y, .w = w, .h = h});
        }

        m_key      = key;
        m_hasTiles = true;
    }

    // like the frame, the renderer's `a` is ignored so the shadow never
    // fades out from under a frame that stays opaque
    for (const auto& t : m_tiles) {
        CTexPassElement::SRenderData data;
        data.tex = t.tex;
        data.box = stretchBox({full.x + t.x, full.y + t.y, static_cast<double>(t.w), static_cast<double>(t.h)}, full, drawFull);
        data.a   = 1.F;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(data));
    }
}

eDecorationType CMilSpecShadowDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CMilSpecShadowDecoration::updateWindow(PHLWINDOW pWindow) {
    damageEntire();
}

void CMilSpecShadowDecoration::damageEntire() {
    // the frame's damage box already includes the shadow's reach
    if (const auto PWINDOW = m_window.lock()) {
        if (const auto FRAME = milspecFrameFor(PWINDOW))
            FRAME->damageEntire();
    }
}

eDecorationLayer CMilSpecShadowDecoration::getDecorationLayer() {
    return DECORATION_LAYER_BOTTOM;
}

uint64_t CMilSpecShadowDecoration::getDecorationFlags() {
    return DECORATION_NON_SOLID;
}

std::string CMilSpecShadowDecoration::getDisplayName() {
    return "3LA-MilSpec Shadow";
}
