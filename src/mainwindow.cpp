#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "appsettings.h"
#include "buildinfo.h"
#include "logo.h"
#include "petwidget.h"
#include "theme.h"
#include "pages/aboutpage.h"
#include "pages/buildpage.h"
#include "pages/controlpage.h"
#include "pages/dashboardpage.h"
#include "pages/guidepage.h"
#include "pages/protectpage.h"
#include "pages/settingspage.h"
#include "widgets/fieldrow.h"
#include "widgets/titlebar.h"
#include "widgets/toggleswitch.h"

#include <QAction>
#include <QApplication>
#include <QTimer>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QCursor>
#include <QFrame>
#include <QGuiApplication>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QWindow>

namespace {

constexpr int kResizeMargin = 6;
constexpr int kMinWidth     = 860;
constexpr int kMinHeight    = 500;

// same order as the buttons in mainwindow.ui, the buttons[] array and the
// pages in setupPages(). This doubles as the page index for showPage().
enum NavKind { NavDashboard, NavControl, NavSettings, NavBuild, NavProtect, NavAbout, NavGuide };

QPixmap navPixmap(int kind, const QColor &color, int size = 18)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal u = size / 18.0;
    p.setPen(QPen(color, 1.6 * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    switch (kind) {
    case NavDashboard: {                       // four panes
        const qreal a = 2.0 * u, b = 8.0 * u, g = 9.0 * u;
        p.drawRoundedRect(QRectF(a, a, b - 1.0 * u, b - 1.0 * u), 2 * u, 2 * u);
        p.drawRoundedRect(QRectF(a + g, a, b - 1.0 * u, b - 1.0 * u), 2 * u, 2 * u);
        p.drawRoundedRect(QRectF(a, a + g, b - 1.0 * u, b - 1.0 * u), 2 * u, 2 * u);
        p.drawRoundedRect(QRectF(a + g, a + g, b - 1.0 * u, b - 1.0 * u), 2 * u, 2 * u);
        break;
    }
    case NavBuild: {                           // stacked layers
        for (int i = 0; i < 3; ++i) {
            const qreal y = (4.0 + i * 4.2) * u;
            const qreal inset = i * 1.6 * u;
            p.drawLine(QPointF(3.0 * u + inset, y), QPointF(15.0 * u - inset, y));
        }
        break;
    }
    case NavControl: {                         // paper-plane send
        const qreal t = 5.0 * u, r = 14.0 * u, b = 9.0 * u;
        p.drawLine(QPointF(2.5 * u, t), QPointF(r, t));
        p.drawLine(QPointF(2.5 * u, b), QPointF(r, b));
        p.drawLine(QPointF(2.5 * u, t), QPointF(r, b));
        p.drawLine(QPointF(r, t), QPointF(9.5 * u, t));
        p.drawLine(QPointF(r, b), QPointF(9.5 * u, b));
        break;
    }
    case NavProtect: {                         // shield
        const qreal cx = 9.0 * u;
        QPainterPath sh;
        sh.moveTo(cx, 2.0 * u);
        sh.lineTo(15.0 * u, 4.5 * u);
        sh.lineTo(15.0 * u, 9.0 * u);
        sh.cubicTo(15.0 * u, 13.0 * u, 12.0 * u, 15.2 * u, cx, 16.5 * u);
        sh.cubicTo(6.0 * u, 15.2 * u, 3.0 * u, 13.0 * u, 3.0 * u, 9.0 * u);
        sh.lineTo(3.0 * u, 4.5 * u);
        sh.closeSubpath();
        p.drawPath(sh);
        break;
    }
    case NavSettings: {                        // sliders
        for (int i = 0; i < 3; ++i) {
            const qreal y = (4.5 + i * 4.2) * u;
            p.drawLine(QPointF(3.0 * u, y), QPointF(15.0 * u, y));
            p.setBrush(color);
            p.drawEllipse(QPointF((6.0 + i * 3.0) * u, y), 1.8 * u, 1.8 * u);
            p.setBrush(Qt::NoBrush);
        }
        break;
    }
    case NavGuide: {                            // open book
        QPainterPath book;
        book.moveTo(9.0 * u, 4.0 * u);
        book.cubicTo(6.5 * u, 2.8 * u, 4.5 * u, 3.0 * u, 2.5 * u, 4.0 * u);
        book.lineTo(2.5 * u, 14.0 * u);
        book.cubicTo(5.0 * u, 12.8 * u, 7.0 * u, 13.0 * u, 9.0 * u, 14.2 * u);
        book.cubicTo(11.0 * u, 13.0 * u, 13.0 * u, 12.8 * u, 15.5 * u, 14.0 * u);
        book.lineTo(15.5 * u, 4.0 * u);
        book.cubicTo(13.5 * u, 3.0 * u, 11.5 * u, 2.8 * u, 9.0 * u, 4.0 * u);
        book.closeSubpath();
        p.drawPath(book);
        p.drawLine(QPointF(9.0 * u, 4.0 * u), QPointF(9.0 * u, 14.0 * u));
        p.drawLine(QPointF(4.5 * u, 6.5 * u), QPointF(7.0 * u, 7.2 * u));
        p.drawLine(QPointF(11.0 * u, 7.2 * u), QPointF(13.5 * u, 6.5 * u));
        break;
    }
    default: {                                 // info
        p.drawEllipse(QPointF(9.0 * u, 9.0 * u), 6.4 * u, 6.4 * u);
        p.drawLine(QPointF(9.0 * u, 8.2 * u), QPointF(9.0 * u, 12.4 * u));
        p.setBrush(color);
        p.drawEllipse(QPointF(9.0 * u, 5.6 * u), 0.9 * u, 0.9 * u);
        break;
    }
    }
    return pm;
}

QIcon navIcon(int kind)
{
    QIcon icon;
    icon.addPixmap(navPixmap(kind, Theme::TextDim), QIcon::Normal, QIcon::Off);
    icon.addPixmap(navPixmap(kind, Theme::Text),    QIcon::Normal, QIcon::On);
    icon.addPixmap(navPixmap(kind, Theme::Text),    QIcon::Active, QIcon::Off);
    return icon;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMouseTracking(true);

    // Open centred and never larger than the screen it lands on — a frameless
    // window that overflows has no title bar left to drag it back by.
    QRect avail(0, 0, 1280, 800);
    if (const QScreen *sc = QGuiApplication::primaryScreen())
        avail = sc->availableGeometry();
    setMinimumSize(qMin(kMinWidth,  avail.width()  - 40),
                   qMin(kMinHeight, avail.height() - 40));
    const QSize wanted(qMin(1040, int(avail.width()  * 0.90)),
                       qMin(640, int(avail.height() * 0.90)));
    resize(wanted);
    move(avail.center() - QPoint(wanted.width() / 2, wanted.height() / 2));

    setupSidebar();
    setupPages();
    for (auto *toggle : findChildren<ToggleSwitch *>())
        toggle->setAnimated(AppSettings::i()->getBool(Keys::UiAnimations));

    // ---------------------------------------------------------- title bar --
    connect(ui->titleBar, &TitleBar::moveRequested,     this, &MainWindow::beginWindowMove);
    connect(ui->titleBar, &TitleBar::minimizeRequested, this, &MainWindow::showMinimized);
    connect(ui->titleBar, &TitleBar::closeRequested,    this, &MainWindow::close);
    connect(ui->titleBar, &TitleBar::toggleMaximizeRequested, this, [this] {
        if (isMaximized()) showNormal();
        else               showMaximized();
    });

    // ------------------------------------------------------------- the pet --
    m_pet = new PetWidget;
    connect(m_pet, &PetWidget::openDashboardRequested, this, [this] {
        bringToFront();
        showPage(0);
    });
    connect(m_pet, &PetWidget::followCursorToggled, this, [](bool on) {
        AppSettings::i()->set(Keys::PetFollow, on);
    });
    connect(m_pet, &PetWidget::dismissed, this, [] {
        AppSettings::i()->set(Keys::PetEnabled, false);
    });

    connect(AppSettings::i(), &AppSettings::changed, this,
            [this](const QString &key) {
                if (key == Keys::UiAnimations) {
                    for (auto *toggle : findChildren<ToggleSwitch *>())
                        toggle->setAnimated(AppSettings::i()->getBool(Keys::UiAnimations));
                }
                if (key.startsWith(QLatin1String("pet/")) || key == Keys::UiAccent)
                    applyPetSettings();
                if (key == Keys::NetEndpoint || key == Keys::ConfigUpdateMode
                    || key == Keys::ToxOperatorId || key == Keys::IdTox)
                    updateTitleStatus();
            });

    setupTray();
    applyPetSettings();
    updateFrameStyle();
    updateTitleStatus();

    showPage(AppSettings::i()->getInt(Keys::UiStartPage));   // showPage() clamps

    qApp->installEventFilter(this);
}

MainWindow::~MainWindow()
{
    delete m_pet;      // top-level, so it is not deleted by the parent chain
    m_pet = nullptr;
    delete ui;
}

// ------------------------------------------------------------- chrome ----

void MainWindow::setupSidebar()
{
    ui->versionLabel->setText(QStringLiteral("v%1 · %2")
                                  .arg(BuildInfo::version(), BuildInfo::codename()));

    m_nav = new QButtonGroup(this);
    m_nav->setExclusive(true);

    // index == page index in pageStack; the icons are drawn, not resources
    QPushButton *buttons[7] = { ui->navDashboard, ui->navControl, ui->navSettings,
                                ui->navBuild,     ui->navProtect, ui->navAbout,
                                ui->navGuide };
    for (int i = 0; i < 7; ++i) {
        buttons[i]->setIcon(navIcon(i));
        buttons[i]->setFocusPolicy(Qt::StrongFocus);
        buttons[i]->setMinimumHeight(36);
        buttons[i]->setAccessibleName(buttons[i]->text());
        buttons[i]->setToolTip(tr("%1 (Alt+%2)").arg(buttons[i]->text()).arg(i + 1));
        auto *shortcut = new QShortcut(QKeySequence(QStringLiteral("Alt+%1").arg(i + 1)), this);
        connect(shortcut, &QShortcut::activated, buttons[i], &QPushButton::click);
        m_nav->addButton(buttons[i], i);
        // per-button rather than QButtonGroup::idClicked, which only exists in 5.15+
        connect(buttons[i], &QPushButton::clicked, this, [this, i] { showPage(i); });
    }
}

void MainWindow::setupPages()
{
    // Only the pages showPage() has to notify are kept as members; the
    // stack owns them all. Order here must match the nav buttons in the form.
    m_dashboard = new DashboardPage(ui->pageStack);
    m_control   = new ControlPage(ui->pageStack);
    m_settings  = new SettingsPage(ui->pageStack);

    ui->pageStack->addWidget(m_dashboard);
    ui->pageStack->addWidget(m_control);
    ui->pageStack->addWidget(m_settings);

    ui->pageStack->addWidget(new BuildPage(ui->pageStack));

    auto *cryptPage = new CryptPage(ui->pageStack);
    ui->pageStack->addWidget(cryptPage);
    // the pet heckles the cryptor: reacts when a run starts / succeeds / fails,
    // AND orbits the flask animation for the duration of the crypt run.
    connect(cryptPage, &CryptPage::cryptStarted, this, [this, cryptPage] {
        if (!m_pet) return;
        m_pet->react(PetWidget::Mood::ProtectStart);
        if (auto *flask = cryptPage->flaskWidget()) {
            const QPoint c = flask->mapToGlobal(flask->rect().center());
            // Orbit at ~1.4× the flask half-extent so the pet clears the glow.
            const qreal r = qMax(flask->width(), flask->height()) * 0.75 + 40.0;
            m_pet->orbit(c, r);
        }
    });
    connect(cryptPage, &CryptPage::cryptSucceeded, this, [this] {
        if (!m_pet) return;
        m_pet->stopOrbit();
        const int delay = qMax(0, qRound(m_pet->bubbleLeft() * 1000.0));
        QTimer::singleShot(delay, this, [this] {
            if (m_pet) m_pet->react(PetWidget::Mood::ProtectDone);
        });
    });
    connect(cryptPage, &CryptPage::cryptFailed, this, [this] {
        if (!m_pet) return;
        m_pet->stopOrbit();
        const int delay = qMax(0, qRound(m_pet->bubbleLeft() * 1000.0));
        QTimer::singleShot(delay, this, [this] {
            if (m_pet) m_pet->react(PetWidget::Mood::ProtectFail);
        });
    });

    ui->pageStack->addWidget(new AboutPage(ui->pageStack));
    ui->pageStack->addWidget(new GuidePage(ui->pageStack));

    // Settings lives at index 2 (NavSettings); the stack order above matches
    // the nav buttons and the buttons[] array.
    connect(m_dashboard, &DashboardPage::openSettingsRequested,
            this, [this] { showPage(NavSettings); });

    // When the user navigates away from Settings with unsaved changes, briefly
    // show a reminder in the title bar so they know their work is waiting.
    connect(m_settings, &SettingsPage::unsavedChangesOnLeave, this, [this] {
        ui->titleBar->setStatus(tr("settings have unsaved changes"), Theme::Amber);
        QTimer::singleShot(4000, this, [this] { updateTitleStatus(); });
    });
}

void MainWindow::showPage(int index)
{
    const int prev = ui->pageStack->currentIndex();
    const int i    = qBound(0, index, ui->pageStack->count() - 1);
    if (m_pageInitialized && prev == i)
        return;
    m_pageInitialized = true;

    if (prev == NavSettings && prev != i)
        m_settings->onPageHidden();
    if (prev == 1 && prev != i)
        m_control->onPageHidden();

    ui->pageStack->setCurrentIndex(i);
    if (QAbstractButton *b = m_nav->button(i))
        b->setChecked(true);
    AppSettings::i()->set(Keys::UiStartPage, i);

    if (i == 0)
        m_dashboard->refresh();
    else if (i == 1)
        m_control->onPageShown();
    else if (i == NavSettings)
        m_settings->onPageShown();
}

void MainWindow::bringToFront()
{
    show();
    if (isMinimized())
        showNormal();
    raise();
    activateWindow();
}

// ----------------------------------------------------------------- pet ----

void MainWindow::applyPetSettings()
{
    if (!m_pet)
        return;

    AppSettings *s = AppSettings::i();
    m_pet->setPetScale(s->getInt(Keys::PetScale));
    m_pet->setSpeedFactor(s->getDouble(Keys::PetSpeed));
    m_pet->setChatter(s->getBool(Keys::PetChatter));
    m_pet->setFollowCursor(s->getBool(Keys::PetFollow));
    m_pet->setAlwaysOnTop(s->getBool(Keys::PetOnTop));
    m_pet->setColor(Theme::accent());

    const bool wanted = s->getBool(Keys::PetEnabled);
    if (wanted && !m_pet->isVisible())
        m_pet->show();
    else if (!wanted && m_pet->isVisible())
        m_pet->hide();
}

void MainWindow::setPetEnabled(bool on)
{
    AppSettings::i()->set(Keys::PetEnabled, on);
    applyPetSettings();
}

// ---------------------------------------------------------------- tray ----

void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_tray = new QSystemTrayIcon(Logo::appIcon(), this);
    m_tray->setToolTip(QStringLiteral("Bminer"));

    auto *menu = new QMenu(this);
    connect(menu->addAction(tr("Open Bminer")), &QAction::triggered,
            this, &MainWindow::bringToFront);

    QAction *petAction = menu->addAction(tr("Show the pet"));
    petAction->setCheckable(true);
    petAction->setChecked(AppSettings::i()->getBool(Keys::PetEnabled));
    connect(petAction, &QAction::toggled, this, [this](bool on) { setPetEnabled(on); });
    connect(AppSettings::i(), &AppSettings::changed, this,
            [petAction](const QString &key, const QVariant &value) {
                if (key == Keys::PetEnabled)
                    petAction->setChecked(value.toBool());
            });

    menu->addSeparator();
    connect(menu->addAction(tr("Quit")), &QAction::triggered, qApp, &QApplication::quit);

    m_tray->setContextMenu(menu);
    m_tray->show();

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                    bringToFront();
            });
}

// -------------------------------------------------------------- frame ----

void MainWindow::updateFrameStyle()
{
    const bool max = isMaximized() || isFullScreen();
    ui->root->setObjectName(max ? QStringLiteral("rootMax") : QStringLiteral("root"));
    ui->root->style()->unpolish(ui->root);
    ui->root->style()->polish(ui->root);
    ui->titleBar->setMaximized(max);
}

void MainWindow::updateTitleStatus()
{
    const QString mode = AppSettings::i()->getString(Keys::ConfigUpdateMode);
    const QString effective = mode.isEmpty() ? QStringLiteral("tox") : mode;
    const QString endpoint = AppSettings::i()->getString(Keys::NetEndpoint).trimmed();
    if (!endpoint.isEmpty()) {
        if (Validators::endpointUrl(endpoint).isEmpty()) {
            ui->titleBar->setStatus(Validators::shorten(endpoint, 26, 12), Theme::Teal);
        } else {
            ui->titleBar->setStatus(tr("endpoint needs attention"), Theme::Red);
        }
        return;
    }
    // No endpoint set: only a problem in Endpoint update mode. Otherwise the
    // channel is Tox (or a config link), so show that instead of "no endpoint".
    if (effective == QStringLiteral("endpoint")) {
        ui->titleBar->setStatus(tr("no endpoint"), Theme::TextFaint);
        return;
    }
    const QString tox = AppSettings::i()->operatorToxId().trimmed();
    if (!tox.isEmpty())
        ui->titleBar->setStatus(Validators::shorten(tox, 18, 10), Theme::Teal);
    else
        ui->titleBar->setStatus(effective, Theme::TextFaint);
}

Qt::Edges MainWindow::edgesAt(const QPoint &local) const
{
    Qt::Edges edges;
    if (!rect().contains(local))
        return edges;
    if (local.x() <= kResizeMargin)              edges |= Qt::LeftEdge;
    if (local.x() >= width() - kResizeMargin)    edges |= Qt::RightEdge;
    if (local.y() <= kResizeMargin)              edges |= Qt::TopEdge;
    if (local.y() >= height() - kResizeMargin)   edges |= Qt::BottomEdge;
    return edges;
}

void MainWindow::applyEdgeCursor(Qt::Edges edges)
{
    // Set on the window rather than as an application override: children that
    // do not define their own cursor inherit it, and it cannot get stuck when
    // the pointer leaves the window.
    if (!edges) {
        if (m_cursorOverridden) {
            unsetCursor();
            m_cursorOverridden = false;
        }
        return;
    }

    Qt::CursorShape shape = Qt::ArrowCursor;
    if ((edges & Qt::LeftEdge && edges & Qt::TopEdge) ||
        (edges & Qt::RightEdge && edges & Qt::BottomEdge))
        shape = Qt::SizeFDiagCursor;
    else if ((edges & Qt::RightEdge && edges & Qt::TopEdge) ||
             (edges & Qt::LeftEdge && edges & Qt::BottomEdge))
        shape = Qt::SizeBDiagCursor;
    else if (edges & Qt::LeftEdge || edges & Qt::RightEdge)
        shape = Qt::SizeHorCursor;
    else
        shape = Qt::SizeVerCursor;

    setCursor(shape);
    m_cursorOverridden = true;
}

void MainWindow::beginWindowMove()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    if (QWindow *handle = windowHandle()) {
        if (handle->startSystemMove())
            return;
    }
#endif
    m_moving      = true;
    m_pressGlobal = QCursor::pos();
    m_pressGeom   = geometry();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (watched == this && (type == QEvent::Leave || type == QEvent::WindowDeactivate)) {
        if (type == QEvent::WindowDeactivate) {
            m_resizing = false;
            m_moving = false;
        }
        if (!m_resizing && !m_moving)
            applyEdgeCursor(Qt::Edges());
    }
    if (type != QEvent::MouseMove && type != QEvent::MouseButtonPress
            && type != QEvent::MouseButtonRelease)
        return QWidget::eventFilter(watched, event);

    auto *w = qobject_cast<QWidget *>(watched);
    if (!w || w->window() != this)
        return QWidget::eventFilter(watched, event);

    auto *me = static_cast<QMouseEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint global = me->globalPosition().toPoint();
#else
    const QPoint global = me->globalPos();
#endif
    const QPoint local = mapFromGlobal(global);
    const bool   fixed = isMaximized() || isFullScreen();

    switch (type) {
    case QEvent::MouseMove: {
        if (!(me->buttons() & Qt::LeftButton)) {
            m_resizing = false;
            m_moving = false;
        }
        if (m_resizing) {
            QRect g = m_pressGeom;
            const QPoint d = global - m_pressGlobal;
            if (m_edges & Qt::LeftEdge)
                g.setLeft(qMin(g.left() + d.x(), g.right() - minimumWidth() + 1));
            if (m_edges & Qt::RightEdge)
                g.setRight(qMax(g.right() + d.x(), g.left() + minimumWidth() - 1));
            if (m_edges & Qt::TopEdge)
                g.setTop(qMin(g.top() + d.y(), g.bottom() - minimumHeight() + 1));
            if (m_edges & Qt::BottomEdge)
                g.setBottom(qMax(g.bottom() + d.y(), g.top() + minimumHeight() - 1));
            setGeometry(g);
            return true;
        }
        if (m_moving) {
            move(m_pressGeom.topLeft() + (global - m_pressGlobal));
            return true;
        }
        applyEdgeCursor(fixed ? Qt::Edges() : edgesAt(local));
        break;
    }
    case QEvent::MouseButtonPress: {
        if (fixed || me->button() != Qt::LeftButton)
            break;
        const Qt::Edges edges = edgesAt(local);
        if (!edges)
            break;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        if (QWindow *handle = windowHandle()) {
            if (handle->startSystemResize(edges))
                return true;
        }
#endif
        m_resizing    = true;
        m_edges       = edges;
        m_pressGlobal = global;
        m_pressGeom   = geometry();
        return true;
    }
    case QEvent::MouseButtonRelease: {
        if (m_resizing || m_moving) {
            m_resizing = false;
            m_moving   = false;
            return true;
        }
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange)
        updateFrameStyle();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_cursorOverridden) {
        unsetCursor();
        m_cursorOverridden = false;
    }

    if (m_tray && AppSettings::i()->getBool(Keys::UiTray)) {
        hide();
        if (!m_trayHintShown) {
            m_trayHintShown = true;
            m_tray->showMessage(tr("Bminer is still running"),
                                tr("The colony lives in the notification area. "
                                   "Right-click the icon to quit."),
                                Logo::appIcon(), 4000);
        }
        e->ignore();
        return;
    }

    e->accept();
    qApp->quit();
}
