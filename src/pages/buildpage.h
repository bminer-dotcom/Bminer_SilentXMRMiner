#pragma once

#include <QWidget>
#include <QString>

class BuildSystem;

namespace Ui { class BuildPage; }

/*
 * Build page - only for building the client.
 * All configuration comes from AppSettings (set by SettingsPage).
 * Uses the C++ BuildSystem instead of PowerShell.
 *
 * The layout lives in buildpage.ui — open it in Qt Designer to move things
 * around. This file only wires the widgets up.
 */
class BuildPage : public QWidget
{
    Q_OBJECT
public:
    explicit BuildPage(QWidget *parent = nullptr);
    ~BuildPage() override;

private slots:
    void onBuildClicked();
    void openBuildFolder();
    void clearLog();
    void checkDependencies(bool automatic);
    void showInfo();

private:
    enum Level { Info, Success, Warning, Failure };

    void applyTheme();
    void connectWidgets();
    void appendLog(const QString &line, Level level);
    void setStatus(const QString &text, Level level);
    void setBuilding(bool building);

    Ui::BuildPage *ui = nullptr;
    BuildSystem *m_buildSystem = nullptr;
};