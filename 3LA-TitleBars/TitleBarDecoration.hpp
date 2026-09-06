#pragma once

#include <string>

#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

namespace Render {
    class ITexture;
}

class CTitleBarDecoration : public IHyprWindowDecoration {
  public:
    CTitleBarDecoration(PHLWINDOW pWindow);
    virtual ~CTitleBarDecoration() = default;

    virtual SDecorationPositioningInfo getPositioningInfo();
    virtual void                       onPositioningReply(const SDecorationPositioningReply& reply);
    virtual void                       draw(PHLMONITOR pMonitor, float const& a);
    virtual eDecorationType            getDecorationType();
    virtual void                       updateWindow(PHLWINDOW pWindow);
    virtual void                       damageEntire();
    virtual eDecorationLayer           getDecorationLayer();
    virtual uint64_t                   getDecorationFlags();
    virtual std::string                getDisplayName();

    // flips this window's bar on/off, independent of ignore_class/ignore_title;
    // driven by the 3la_titlebars:toggle dispatcher (bound to a keybind, acts
    // on the currently focused window)
    void toggle();

  private:
    // true if this window should get no bar at all: toggled off, or matches
    // ignore_class/ignore_title
    bool hidden() const;

    // re-renders the cached title texture only when the title, resolved
    // color, font, font size or available width actually changed since last frame
    SP<Render::ITexture> titleTexture(const std::string& title, const CHyprColor& color, int64_t colorRaw, const std::string& font, int fontPt, int maxWidthPx);

    // cheap layered-rect shadow behind the bar's own box, independent of
    // Hyprland's core window shadow (see drawShadow's own comment for why)
    void drawShadow(const CBox& scaledBox, float barAlpha) const;

    PHLWINDOWREF          m_window;
    bool                  m_toggledOff = false;

    SP<Render::ITexture>  m_textTex;
    std::string           m_textCacheTitle;
    int64_t               m_textCacheColor  = 0;
    std::string           m_textCacheFont;
    int                   m_textCacheFontPt = 0;
    int                   m_textCacheMaxW   = 0;
};
