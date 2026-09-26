#include "theme.h"

#include <QFontDatabase>
#include <QHash>
#include <QStringList>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace Theme {

const QColor Bg         (0x0A, 0x0A, 0x0C);
const QColor BgAlt      (0x0E, 0x0E, 0x12);
const QColor Surface    (0x14, 0x14, 0x19);
const QColor SurfaceAlt (0x1B, 0x1B, 0x22);
const QColor Border     (0x38, 0x38, 0x46);
const QColor BorderSoft (0x1C, 0x1C, 0x24);

const QColor Text       (0xEC, 0xEC, 0xF2);
const QColor TextDim    (0xB0, 0xB0, 0xBE);
const QColor TextFaint  (0x94, 0x94, 0xA5);

const QColor Pink       (0xE6, 0x3F, 0x77);
const QColor Purple     (0x7E, 0x60, 0xB0);
const QColor Teal       (0x46, 0xC0, 0xA2);
const QColor Amber      (0xE2, 0xA6, 0x4B);
const QColor Red        (0xE0, 0x4F, 0x53);

static QString s_accentName = QStringLiteral("pink");

QColor colorForAccentName(const QString &name)
{
    if (name.compare(QLatin1String("purple"), Qt::CaseInsensitive) == 0) return Purple;
    if (name.compare(QLatin1String("teal"),   Qt::CaseInsensitive) == 0) return Teal;
    return Pink;
}

QColor  accent()     { return colorForAccentName(s_accentName); }
QString accentName() { return s_accentName; }

void setAccent(const QString &name)
{
    const QString n = name.toLower();
    if (n == QLatin1String("pink") || n == QLatin1String("purple") || n == QLatin1String("teal"))
        s_accentName = n;
}

QPalette palette()
{
    const QColor acc = accent();
    const QColor ink = [&] {
        const auto linear = [](qreal v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        const auto lum = [&](const QColor &c) {
            return 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
        };
        const QColor dark(16, 16, 24);
        return ((lum(acc) + 0.05) / (lum(dark) + 0.05) > 1.05 / (lum(acc) + 0.05))
                   ? dark : QColor(Qt::white);
    }();

    QPalette p;
    p.setColor(QPalette::Window,          Bg);
    p.setColor(QPalette::WindowText,      Text);
    p.setColor(QPalette::Base,            Surface);      // viewports, item views
    p.setColor(QPalette::AlternateBase,   SurfaceAlt);
    p.setColor(QPalette::Text,            Text);
    p.setColor(QPalette::PlaceholderText, TextFaint);
    p.setColor(QPalette::Button,          SurfaceAlt);
    p.setColor(QPalette::ButtonText,      Text);
    p.setColor(QPalette::BrightText,      Red);
    p.setColor(QPalette::ToolTipBase,     SurfaceAlt);
    p.setColor(QPalette::ToolTipText,     Text);
    p.setColor(QPalette::Link,            acc);
    p.setColor(QPalette::LinkVisited,     acc.darker(120));
    p.setColor(QPalette::Highlight,       acc);
    p.setColor(QPalette::HighlightedText, ink);

    p.setColor(QPalette::Disabled, QPalette::WindowText,      TextFaint);
    p.setColor(QPalette::Disabled, QPalette::Text,            TextFaint);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,      TextFaint);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, TextFaint);

    return p;
}

QFont monoFont(int pointSize)
{
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setStyleHint(QFont::Monospace);
    f.setPointSize(pointSize);
    return f;
}

QString styleFor(TextRole role)
{
    switch (role) {
    case TextRole::Body:       return QStringLiteral("color:%1;font-size:12px;").arg(Text.name());
    case TextRole::Dim:        return QStringLiteral("color:%1;font-size:11px;").arg(TextDim.name());
    case TextRole::Faint:      return QStringLiteral("color:%1;font-size:11px;").arg(TextFaint.name());
    case TextRole::Hint:       return QStringLiteral("color:%1;font-size:11px;").arg(TextFaint.name());
    case TextRole::FieldLabel: return QStringLiteral("color:%1;font-size:12px;font-weight:600;").arg(Text.name());
    case TextRole::Value:      return QStringLiteral("color:%1;font-size:12px;font-weight:600;").arg(Text.name());
    case TextRole::Ok:         return QStringLiteral("color:%1;font-size:10px;").arg(Teal.name());
    case TextRole::Warn:       return QStringLiteral("color:%1;font-size:10px;").arg(Amber.name());
    case TextRole::Error:      return QStringLiteral("color:%1;font-size:10px;").arg(Red.name());
    case TextRole::Accent:     return QStringLiteral("color:%1;font-size:12px;font-style:italic;").arg(accent().name());
    }
    return QString();
}

void applyText(QWidget *w, TextRole role)
{
    if (w)
        w->setStyleSheet(styleFor(role));
}

static QString rgba(const QColor &c, int alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

QString styleSheet()
{
    QString qss = QStringLiteral(R"QSS(
* { outline: 0; }

QWidget {
    color: %TEXT%;
    font-family: "Segoe UI", "Inter", "Noto Sans", sans-serif;
    font-size: 12px;
}

#root            { background: %BG%; border: 1px solid %BORDER%; }
#rootMax         { background: %BG%; border: 0px; }
/* Containers between #root and the page content must not paint their own
   surface, or the native style fills them from the (light) OS palette. */
#content, QStackedWidget, QStackedWidget > QWidget { background: transparent; }

/* ---------- title bar ---------- */
#titleBar        { background: %BGALT%; border-bottom: 1px solid %BORDER%; }
#appTitle        { font-size: 12px; font-weight: 600; letter-spacing: 1px; }
#appTitleDim     { color: %FAINT%; font-size: 10px; letter-spacing: 1px; }
#winBtn, #winBtnClose { background: transparent; border: 1px solid transparent; border-radius: %RADIUS%; color: %DIM%; font-size: 13px; padding: 0; }
#winBtn:hover    { background: %SURFACEALT%; color: %TEXT%; }
#winBtn:focus, #winBtnClose:focus { border-color: %ACCENT%; }
#winBtnClose:hover { background: #D93A54; color: #ffffff; }

/* ---------- sidebar ---------- */
#sidebar         { background: %BGALT%; border-right: 1px solid %BORDER%; }
#sidebarCaption  { color: %FAINT%; font-size: 10px; font-weight: 700; letter-spacing: 1.4px; padding: 0 12px; }

QPushButton[nav="true"] {
    text-align: left;
    padding: 8px 9px;
    border: 1px solid transparent;
    border-radius: %RADIUS%;
    background: transparent;
    color: %DIM%;
    font-size: 12px;
}
QPushButton[nav="true"]:hover   { background: %SURFACE%; color: %TEXT%; }
QPushButton[nav="true"]:checked { background: %ACCENT_SOFT%; border-left: 3px solid %ACCENT%; padding-left: 7px; color: %TEXT%; font-weight: 600; }
QPushButton[nav="true"]:focus { border-color: %ACCENT%; }

/* ---------- cards ---------- */
/* A card is a QGroupBox (see the rule at the bottom of this sheet). StatCard
   is the tile that goes inside one, matched BY TYPE: a widget placed in a .ui
   form has its objectName overwritten with the form's name, so styling keyed
   to an objectName set in a constructor would silently stop applying. */
StatCard {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%;
}
#sectionTitle { font-size: 15px; font-weight: 600; }
#statLabel    { font-size: 10px; font-weight: 700; letter-spacing: 1px; color: %FAINT%; }
#statValue    { font-size: 19px; font-weight: 600; }
#statUnit     { font-size: 11px; color: %DIM%; }
#statFoot     { font-size: 10px; color: %FAINT%; }
#kvKey        { color: %DIM%; font-size: 11px; }
#kvVal        { color: %TEXT%; font-size: 11px; }

QLabel[muted="true"] { color: %DIM%; }
QLabel[faint="true"] { color: %FAINT%; font-size: 11px; }

/* ---------- label roles ----------
   Add a String property named "role" to a QLabel in Qt Designer and it is
   themed with no C++ at all. These mirror Theme::TextRole, which stays for
   labels that have to change role while running. */
QLabel[role="body"]       { color: %TEXT%;   font-size: 12px; }
QLabel[role="dim"]        { color: %DIM%;    font-size: 11px; }
QLabel[role="faint"]      { color: %FAINT%;  font-size: 11px; }
QLabel[role="hint"]       { color: %FAINT%;  font-size: 11px; }
QLabel[role="fieldLabel"] { color: %TEXT%;   font-size: 12px; font-weight: 600; }
QLabel[role="value"]      { color: %TEXT%;   font-size: 12px; font-weight: 600; }
QLabel[role="ok"]         { color: %TEAL%;   font-size: 10px; }
QLabel[role="warn"]       { color: %AMBER%;  font-size: 10px; }
QLabel[role="error"]      { color: %RED%;    font-size: 10px; }
QLabel[role="accent"]     { color: %ACCENT%; font-size: 12px; font-style: italic; }
QLabel[role="display"]    { color: %TEXT%;   font-size: 24px; font-weight: 600; letter-spacing: 1px; }

/* Pills: a String property named "pill" picks the colour. */
QLabel[pill="teal"], QLabel[pill="purple"],
QLabel[pill="pink"], QLabel[pill="accent"] {
    border-radius: %RADIUS%;
    padding: 2px 8px;
    font-size: 10px;
    font-weight: 700;
    letter-spacing: 0.8px;
}
QLabel[pill="teal"]   { color: %TEAL%;   background: %TEAL_SOFT%; }
QLabel[pill="purple"] { color: %PURPLE%; background: %PURPLE_SOFT%; }
QLabel[pill="pink"]   { color: %PINK%;   background: %PINK_SOFT%; }
QLabel[pill="accent"] { color: %ACCENT%; background: %ACCENT_SOFT%; }

/* ---------- inputs ---------- */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
    background: %SURFACEALT%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%;
    padding: 4px 8px;
    selection-background-color: %ACCENT_A60%;
    selection-color: #ffffff;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus {
    border: 1px solid %ACCENT%;
}
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled,
QPlainTextEdit:disabled, QTextEdit:disabled { color: %FAINT%; background: %SURFACE%; border-color: %BORDERSOFT%; }

QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow {
    width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid %DIM%;
    margin-right: 8px;
}
QComboBox QAbstractItemView {
    background: %SURFACEALT%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%;
    padding: 4px;
    selection-background-color: %ACCENT_A45%;
    outline: 0;
}
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 16px; border: none; background: transparent; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
    width: 0; height: 0;
    border-left: 3px solid transparent; border-right: 3px solid transparent;
    border-bottom: 4px solid %DIM%;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
    width: 0; height: 0;
    border-left: 3px solid transparent; border-right: 3px solid transparent;
    border-top: 4px solid %DIM%;
}

/* ---------- buttons ---------- */
QPushButton {
    background: %SURFACEALT%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%;
    padding: 4px 11px;
    color: %TEXT%;
}
QPushButton:hover    { border-color: %ACCENT_A70%; }
QPushButton:focus    { border-color: %ACCENT%; }
QPushButton:pressed  { background: %SURFACE%; }
QPushButton:disabled { color: %FAINT%; border-color: %BORDERSOFT%; }

QPushButton[primary="true"] {
    background: %ACCENT%;
    border: 1px solid %ACCENT%;
    color: %ACCENT_INK%;
    font-weight: 600;
}
QPushButton[primary="true"]:hover   { background: %ACCENT_HOVER%; }
QPushButton[primary="true"]:pressed { background: %ACCENT_PRESSED%; }
QPushButton[primary="true"]:focus   { border-color: %ACCENT_INK%; }
QPushButton[primary="true"]:disabled { background: %SURFACEALT%; color: %FAINT%; border-color: %BORDER%; }
QPushButton[ghost="true"] {
    background: transparent;
    border: 1px solid %BORDER%;
    color: %DIM%;
}
QPushButton[ghost="true"]:hover { color: %TEXT%; border-color: %DIM%; }

/* ---------- field-guide flipbook ---------- */
#guideCaption { font-size: 10px; color: %DIM%; }
#guideSpread { font-size: 10px; font-weight: 700; letter-spacing: 1px; color: %DIM%; }
#guideProgress { background: %SURFACEALT%; border: none; border-radius: 2px; }
#guideProgress::chunk { background: %ACCENT%; border-radius: 2px; }
#guidePrevious, #guideNext { min-height: 27px; }

/* ---------- checks / sliders ---------- */
QCheckBox, QRadioButton { spacing: 7px; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 14px; height: 14px;
    border: 1px solid %BORDER%;
    background: %SURFACEALT%;
}
QCheckBox::indicator          { border-radius: %RADIUS%; }
QRadioButton::indicator       { border-radius: 7px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: %ACCENT%; border-color: %ACCENT%;
}
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: %ACCENT%; }
QCheckBox::indicator:focus, QRadioButton::indicator:focus { border: 2px solid %TEXT%; }
QCheckBox:disabled, QRadioButton:disabled { color: %FAINT%; }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { background: %SURFACE%; border-color: %BORDER%; }
QCheckBox::indicator:checked:disabled, QRadioButton::indicator:checked:disabled { background: %FAINT%; border-color: %FAINT%; }

QSlider::groove:horizontal   { height: 4px; background: %SURFACEALT%; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %ACCENT%; border-radius: 2px; }
QSlider::handle:horizontal   { background: %TEXT%; width: 9px; height: 16px; margin: -6px 0; border-radius: %RADIUS%; }
QSlider::handle:horizontal:hover { background: %ACCENT%; }
QSlider::handle:horizontal:focus { background: %ACCENT%; border: 1px solid %TEXT%; }
QSlider::sub-page:horizontal:disabled { background: %BORDER%; }
QSlider::handle:horizontal:disabled { background: %FAINT%; }

QProgressBar {
    background: %SURFACEALT%;
    border: none; border-radius: %RADIUS%;
    height: 8px; text-align: center; color: transparent;
}
QProgressBar::chunk { background: %ACCENT%; border-radius: %RADIUS%; }

/* ---------- tables / lists ---------- */
QTableWidget, QTableView, QListWidget, QTreeWidget {
    background: transparent;
    border: none;
    gridline-color: %BORDERSOFT%;
    selection-background-color: %ACCENT_A45%;
    selection-color: #ffffff;
}
QTableWidget::item, QListWidget::item { padding: 4px 5px; border: none; }
QTableWidget::item:selected, QListWidget::item:selected { background: %ACCENT_A45%; }
QHeaderView::section {
    background: transparent;
    color: %FAINT%;
    border: none;
    border-bottom: 1px solid %BORDER%;
    padding: 6px;
    font-size: 10px; font-weight: 700; letter-spacing: 1px;
}
QTableCornerButton::section { background: transparent; border: none; }

QPlainTextEdit#console, QListWidget#logList {
    background: #08080B;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%;
    padding: 4px;
}
QListWidget#logList::item { padding: 1px 4px; }

/* ---------- scroll bars ---------- */
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget#qt_scrollarea_viewport { background: transparent; }
QScrollBar:vertical   { background: transparent; width: 12px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
    background: %BORDER%; border-radius: %RADIUS%; min-height: 28px; min-width: 28px;
}
QScrollBar::handle:hover { background: %FAINT%; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ---------- misc ---------- */
QToolTip {
    background: %SURFACEALT%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    padding: 5px 7px;
    border-radius: %RADIUS%;
}
QMenu {
    background: %SURFACEALT%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%;
    padding: 5px;
}
QMenu::item { padding: 6px 22px 6px 12px; border-radius: %RADIUS%; }
QMenu::item:selected { background: %ACCENT_A45%; }
QMenu::separator { height: 1px; background: %BORDER%; margin: 4px 8px; }
QFrame[hline="true"] { background: %BORDER%; max-height: 1px; min-height: 1px; border: none; }
/* A group box IS a card. Designer can drop widgets straight into one, which a
   promoted Card cannot do, so forms built in Designer use these. */
QGroupBox {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%;
    margin-top: 0px;
    padding: 25px 11px 10px 11px;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 0.8px;
}
QGroupBox::title {
    subcontrol-origin: padding;
    subcontrol-position: top left;
    left: 12px;
    top: 9px;
    padding: 0;
    color: %TEXT%;
}
QGroupBox[flat="true"] { background: transparent; border: none; padding: 0; }
)QSS");

    const QColor acc = accent();
    const auto luminance = [](const QColor &c) {
        const auto linear = [](qreal v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
    };
    const QColor darkInk(16, 16, 24);
    const bool useDarkInk = (luminance(acc) + 0.05) / (luminance(darkInk) + 0.05)
                         > 1.05 / (luminance(acc) + 0.05);

    QHash<QString, QString> tok;
    tok.insert(QStringLiteral("%BG%"),         Bg.name());
    tok.insert(QStringLiteral("%BGALT%"),      BgAlt.name());
    tok.insert(QStringLiteral("%SURFACEALT%"), SurfaceAlt.name());
    tok.insert(QStringLiteral("%SURFACE%"),    Surface.name());
    tok.insert(QStringLiteral("%BORDERSOFT%"), BorderSoft.name());
    tok.insert(QStringLiteral("%BORDER%"),     Border.name());
    tok.insert(QStringLiteral("%TEXT%"),       Text.name());
    tok.insert(QStringLiteral("%DIM%"),        TextDim.name());
    tok.insert(QStringLiteral("%FAINT%"),      TextFaint.name());
    // One number decides how sharp the whole app is — see Metric::Radius.
    tok.insert(QStringLiteral("%RADIUS%"),
               QString::number(Metric::Radius) + QStringLiteral("px"));
    tok.insert(QStringLiteral("%TEAL_SOFT%"),   rgba(Teal, 40));
    tok.insert(QStringLiteral("%PURPLE_SOFT%"), rgba(Purple, 40));
    tok.insert(QStringLiteral("%PINK_SOFT%"),   rgba(Pink, 40));
    tok.insert(QStringLiteral("%ACCENT_SOFT%"), rgba(acc, 40));
    tok.insert(QStringLiteral("%TEAL%"),        Teal.name());
    tok.insert(QStringLiteral("%PURPLE%"),      Purple.name());
    tok.insert(QStringLiteral("%PINK%"),        Pink.name());
    tok.insert(QStringLiteral("%AMBER%"),       Amber.name());
    tok.insert(QStringLiteral("%RED%"),         Red.name());
    tok.insert(QStringLiteral("%ACCENT_A85%"), rgba(acc, 217));
    tok.insert(QStringLiteral("%ACCENT_A70%"), rgba(acc, 178));
    tok.insert(QStringLiteral("%ACCENT_A60%"), rgba(acc, 153));
    tok.insert(QStringLiteral("%ACCENT_A45%"), rgba(acc, 115));
    tok.insert(QStringLiteral("%ACCENT%"),     acc.name());
    tok.insert(QStringLiteral("%ACCENT_INK%"), (useDarkInk ? darkInk : QColor(Qt::white)).name());
    tok.insert(QStringLiteral("%ACCENT_HOVER%"), (useDarkInk ? acc.lighter(112) : acc.darker(112)).name());
    tok.insert(QStringLiteral("%ACCENT_PRESSED%"), (useDarkInk ? acc.lighter(105) : acc.darker(120)).name());

    // Longest tokens first so prefixes never eat their longer siblings.
    QStringList keys = tok.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
        return a.size() > b.size();
    });
    for (const QString &k : keys)
        qss.replace(k, tok.value(k));

    return qss;
}

} // namespace Theme
