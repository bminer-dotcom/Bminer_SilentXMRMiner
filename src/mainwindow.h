#pragma once

#include <QRect>
#include <QWidget>

namespace Ui { class MainWindow; }

class DashboardPage;
class ControlPage;
class PetWidget;
class SettingsPage;

class QButtonGroup;
class QSystemTrayIcon;

/*
 * Frameless shell: custom title bar, sidebar navigation, stacked pages, tray
 * icon, and ownership of the desktop pet.
 *
 * The chrome lives in mainwindow.ui — open it in Qt Designer to move the
 * sidebar, rename a nav button or change the padding around the pages. The
 * pages themselves are added to `pageStack` here, in setupPages().
 *
 * Window move and resize are handled here rather than by the window manager,
 * so the chrome can be styled — see eventFilter().
 */
class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    void showPage(int index);
    void bringToFront();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private:
    void setupSidebar();
    void setupPages();
    void setupTray();
    void applyPetSettings();
    void setPetEnabled(bool on);
    void updateFrameStyle();
    void updateTitleStatus();

    Qt::Edges edgesAt(const QPoint &local) const;
    void      applyEdgeCursor(Qt::Edges edges);
    void      beginWindowMove();

    Ui::MainWindow *ui = nullptr;

    QButtonGroup    *m_nav  = nullptr;
    PetWidget       *m_pet  = nullptr;
    QSystemTrayIcon *m_tray = nullptr;

    // Only the pages showPage() has to call into; the stack owns every page.
    DashboardPage *m_dashboard = nullptr;
    ControlPage   *m_control   = nullptr;
    SettingsPage  *m_settings  = nullptr;

    // manual move / resize state
    Qt::Edges m_edges;
    QPoint    m_pressGlobal;
    QRect     m_pressGeom;
    bool      m_resizing = false;
    bool      m_moving   = false;
    bool      m_cursorOverridden = false;
    bool      m_trayHintShown    = false;
    bool      m_pageInitialized  = false;
};
