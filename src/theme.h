#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QString>

class QWidget;

/*
 * Palette lifted from the Bminer badge:
 * black plate, crimson-pink primary, phage purple, culture teal.
 */
namespace Theme {

extern const QColor Bg;          // window plate
extern const QColor BgAlt;       // chrome (title bar / sidebar)
extern const QColor Surface;     // cards
extern const QColor SurfaceAlt;  // raised rows / inputs
extern const QColor Border;
extern const QColor BorderSoft;

extern const QColor Text;
extern const QColor TextDim;
extern const QColor TextFaint;

extern const QColor Pink;
extern const QColor Purple;
extern const QColor Teal;
extern const QColor Amber;
extern const QColor Red;

QColor  accent();
QString accentName();                   // "pink" | "purple" | "teal"
void    setAccent(const QString &name);
QColor  colorForAccentName(const QString &name);

QString styleSheet();
QFont   monoFont(int pointSize = 9);

/*
 * The application palette. The stylesheet paints the surfaces it knows about,
 * but the native style still fills anything it does not cover (scroll-area
 * viewports, stacked-widget frames, popups) from the palette — which is white
 * when Windows is in light mode. Install this so the app stays dark no matter
 * what the OS theme is doing. Re-install it after setAccent().
 */
QPalette palette();

/*
 * Spacing used by the widgets that build their own layout in C++.
 *
 * The .ui forms carry their own numbers — Designer cannot read a C++
 * constant — so these are the reference values to type into Designer, not the
 * single source of truth they once were. Keep the two in step: the margins
 * below are what the group-box padding in styleSheet() is matched to.
 */
namespace Metric {
inline constexpr int PagePadding    = 14;   // window edge -> page content
inline constexpr int PageGap        = 10;   // between header and cards, card to card
inline constexpr int CardPadX       = 12;
inline constexpr int CardPadTop     = 9;
inline constexpr int CardPadBottom  = 10;
inline constexpr int RowGap         = 6;    // between rows inside a card
/* Corner radius for every surface in the app — cards, buttons, inputs, menus.
 * The stylesheet routes them all through %RADIUS%, so this one number is the
 * difference between a soft consumer look and a sharp console one.
 * A modest radius keeps the compact layout readable and consistent. */
inline constexpr int Radius         = 6;
inline constexpr int SidebarWidth   = 158;
inline constexpr int TitleBarHeight = 38;
} // namespace Metric

/*
 * Text styling outside the global stylesheet goes through here — never write a
 * colour or a font-size inline. If a widget has an objectName the stylesheet
 * already covers (see styleSheet()), prefer that instead.
 */
enum class TextRole {
    Body,        // default body copy
    Dim,         // secondary copy
    Faint,       // captions, footnotes
    Hint,        // one-line explanation under a control
    FieldLabel,  // the label above an input
    Value,       // an emphasised value
    Ok,          // green confirmation
    Warn,        // amber caution
    Error,       // red problem
    Accent,      // tagline, uses the current accent colour
};

QString styleFor(TextRole role);              // a QSS fragment
void    applyText(QWidget *w, TextRole role); // sets it on the widget

} // namespace Theme
