#pragma once

#include <string>
#include <vector>

#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/types/Geometric.hpp>

namespace Render {
    class ITexture;
}

class CMilSpecDecoration : public IHyprWindowDecoration {
  public:
    CMilSpecDecoration(PHLWINDOW pWindow);
    virtual ~CMilSpecDecoration() = default;

    virtual SDecorationPositioningInfo getPositioningInfo();
    virtual void                       onPositioningReply(const SDecorationPositioningReply& reply);
    virtual void                       draw(Render::CRenderContext& ctx, PHLMONITOR pMonitor, float const& a, const Render::SWindowRenderPresentation& presentation);
    virtual eDecorationType            getDecorationType();
    virtual void                       updateWindow(PHLWINDOW pWindow);
    virtual void                       damageEntire();
    virtual eDecorationLayer           getDecorationLayer();
    virtual uint64_t                   getDecorationFlags();
    virtual std::string                getDisplayName();

    // per-edge distance from the window's edge to the frame's OUTER edge, in
    // layout px: the core border, anything stacked between it and us, and our
    // own offset+thickness(+titlebar) -- plus extra top room reserved by
    // decorations stacked outside us (e.g. 3LA-TitleBars), so the frame
    // encloses those too
    struct SInsets {
        double l = 0, t = 0, r = 0, b = 0;
        double topExtra = 0; // part of `t` that belongs to decorations outside us
    };

    // true if this window should get no frame: plugin disabled, or matches
    // ignore_class/ignore_title
    bool                 hidden() const;
    // flips this window's bar on/off, independent of titlebar.ignore_class/
    // titlebar.ignore_title; driven by the 3la_milspec:toggle dispatcher
    // (bound to a keybind, acts on the currently focused window)
    void                 toggle();

    // true if this window gets the title bar: titlebar on, not toggled off,
    // and not matched by titlebar.ignore_class/titlebar.ignore_title
    bool                 barShown() const;
    // true if this window's frame STROKE is drawn: frame on and not matched by
    // frame.ignore_class/frame.ignore_title. Never affects reserved space.
    bool                 frameShown() const;
    // true if this window sits flush against the top stroke when it has no
    // bar: matched by offset.top_flush_class
    bool                 topFlush() const;
    SInsets              insets(const PHLWINDOW& w) const;
    // layout coords, no floating offset; GEOMETRIC_GOAL = where an animated
    // move/resize ends up
    CBox                 frameBox(const PHLWINDOW& w, Desktop::View::IGeometric::eGeometricValueType type = Desktop::View::IGeometric::GEOMETRIC_CURRENT) const;

  private:

    // the cairo-drawn frame (+ bar fill), rebuilt only when any input changes
    // the bar's rectangle inside the frame texture, in texture pixels; `cut`
    // is the perpendicular distance (px) the bar's own 45 deg cut sits inside
    // the frame stroke's diagonal
    struct SBarGeom {
        int  top = 0, h = 0, left = 0, right = 0, cut = 0;
        bool operator==(const SBarGeom&) const = default;
    };

    // the window (+ core border) cut out of the background fill, in texture
    // pixels; `radius`/`power` follow the border's outer corner
    struct SHoleGeom {
        int    x = 0, y = 0, w = 0, h = 0;
        double radius = 0, power = 2;
        bool   operator==(const SHoleGeom&) const = default;
    };

    // triangles painted over the window's own top-left / bottom-right
    // corners (window + core border box, texture pixels) so the app looks cut
    // like the frame; `tl`/`br` are each corner's leg length (0 = no cover)
    struct SCoverGeom {
        int    x = 0, y = 0, w = 0, h = 0;
        double tl = 0, br = 0;
        bool   operator==(const SCoverGeom&) const = default;
    };

    SP<Render::ITexture> frameTexture(int w, int h, int thick, int chamfer, const SBarGeom& bar, const SHoleGeom& hole, const SCoverGeom& cover, const CHyprColor& frameCol,
                                      const CHyprColor& barCol, const CHyprColor& bgCol, const CHyprColor& coverCol);
    SP<Render::ITexture> titleTexture(const std::string& title, const CHyprColor& color, const std::string& font, int fontPt, int maxWidthPx);
    const std::string&   displayTitle(const PHLWINDOW& w);

    PHLWINDOWREF         m_window;
    bool                 m_toggledOff = false;

    // positioner reply for our 4-edge sticky slot: assignedGeometry is
    // relative to the window's top-left at the time of the reply, and the
    // window size it was computed against is kept so the right/bottom insets
    // can be recovered while the window keeps moving/resizing
    CBox                 m_assigned;
    Vector2D             m_assignedWinSize;

    // last box damageEntire() actually computed while the window was still
    // valid, replayed as-is if damageEntire() runs after the window is
    // already gone (window.destroy fires too late to recompute) -- otherwise
    // the frame's last frame lingers until an unrelated full repaint
    CBox                 m_lastDamageBox;
    bool                 m_hasDamageBox = false;
    PHLMONITORREF        m_lastMonitor;

    SP<Render::ITexture> m_frameTex;
    int                  m_frameW = 0, m_frameH = 0, m_frameThick = 0, m_frameChamfer = 0;
    SBarGeom             m_frameBar;
    SHoleGeom            m_frameHole;
    SCoverGeom           m_frameCover;
    CHyprColor           m_frameCol, m_frameBarCol, m_frameBgCol, m_frameCoverCol;

    SP<Render::ITexture> m_textTex;
    std::string          m_textTitle;
    CHyprColor           m_textCol;
    std::string          m_textFont;
    int                  m_textPt   = 0;
    int                  m_textMaxW = 0;

    std::string          m_displayTitle, m_displayTitleClass, m_displayTitleSrc;
    uint64_t             m_displayTitleGen = 0;
    bool                 m_hasDisplayTitle = false;
};

// frame decoration of a window, or nullptr (defined in main.cpp)
CMilSpecDecoration* milspecFrameFor(const PHLWINDOW& w);

// extra damage margin (layout px) the shadow needs around the frame box;
// 0 when the shadow is off
double milspecShadowPad();

// drop shadow that follows the chamfered frame. Same falloff as Hyprland's
// decoration:shadow (shadow.glsl): alpha = ((range - d) / range)^render_power
// for distance d outside the shadow shape. Drawn on the BOTTOM layer, cut
// out where the frame is so it only shows outside it.
class CMilSpecShadowDecoration : public IHyprWindowDecoration {
  public:
    CMilSpecShadowDecoration(PHLWINDOW pWindow);
    virtual ~CMilSpecShadowDecoration() = default;

    virtual SDecorationPositioningInfo getPositioningInfo();
    virtual void                       onPositioningReply(const SDecorationPositioningReply& reply);
    virtual void                       draw(Render::CRenderContext& ctx, PHLMONITOR pMonitor, float const& a, const Render::SWindowRenderPresentation& presentation);
    virtual eDecorationType            getDecorationType();
    virtual void                       updateWindow(PHLWINDOW pWindow);
    virtual void                       damageEntire();
    virtual eDecorationLayer           getDecorationLayer();
    virtual uint64_t                   getDecorationFlags();
    virtual std::string                getDisplayName();

    // everything the cached tiles depend on, in monitor px
    struct SKey {
        int        fullW = 0, fullH = 0;   // shadow's full box (frame grown by range, scaled, offset)
        int        frameX = 0, frameY = 0; // frame box, relative to the full box
        int        frameW = 0, frameH = 0;
        int        chamfer = 0, range = 0, power = 0;
        int        inset = 0; // cut-out pulled in by this much: under the stroke
        bool       sharp = false;
        CHyprColor color;
        bool       operator==(const SKey&) const = default;
    };

  private:
    bool        shown() const; // shadow on, frame not hidden, not ignored
    SBoxExtents computeExtents(const PHLWINDOW& w) const;

    struct STile {
        SP<Render::ITexture> tex;
        int                  x = 0, y = 0, w = 0, h = 0; // in full-box px
    };

    PHLWINDOWREF       m_window;
    SBoxExtents        m_reportedExtents;
    SKey               m_key;
    std::vector<STile> m_tiles;
    bool               m_hasTiles = false;
};
