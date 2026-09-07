#pragma once

#include <array>
#include <chrono>
#include <string>

#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

class CCornersDecoration : public IHyprWindowDecoration {
  public:
    CCornersDecoration(PHLWINDOW pWindow, bool flash = false);
    virtual ~CCornersDecoration() = default;

    virtual SDecorationPositioningInfo getPositioningInfo();
    virtual void                       onPositioningReply(const SDecorationPositioningReply& reply);
    virtual void                       draw(PHLMONITOR pMonitor, float const& a);
    virtual eDecorationType            getDecorationType();
    virtual void                       updateWindow(PHLWINDOW pWindow);
    virtual void                       damageEntire();
    virtual eDecorationLayer           getDecorationLayer();
    virtual uint64_t                   getDecorationFlags();
    virtual std::string                getDisplayName();

    void                               flashSpawn();
    void                               flashFocus();

  private:
    // spawn outranks focus; a new window takes focus in the tick it opens
    enum eFlashKind : uint8_t {
        FLASH_NONE = 0,
        FLASH_FOCUS,
        FLASH_SPAWN,
    };

    std::array<CBox, 8> cornerBoxes(const Vector2D& pos, const Vector2D& size, double outerDist, double topExtra) const;
    double              extraTopReserved() const;
    void                drawGlow(const CBox& box, const CHyprColor& color, float alpha) const;
    void                flash(eFlashKind kind, int count, int duration);
    bool                flashExpired() const;
    float               flashMultiplier();

    PHLWINDOWREF                          m_window;

    // last box damageEntire() actually computed while the window was still
    // valid, replayed as-is if damageEntire() runs after the window is
    // already gone (window.destroy fires once geometry for it no longer
    // exists, too late to recompute) -- otherwise the brackets' last frame
    // is never cleared and lingers until an unrelated full repaint
    CBox                                  m_lastDamageBox;
    bool                                  m_hasDamageBox = false;
    PHLMONITORREF                         m_lastMonitor;

    bool                                  m_flashing  = false;
    eFlashKind                            m_flashKind = FLASH_NONE;
    std::chrono::steady_clock::time_point m_flashStart;
    int                                   m_flashCount    = 0;
    int                                   m_flashDuration = 0;
};
