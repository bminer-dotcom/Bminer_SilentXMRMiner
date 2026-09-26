#pragma once

#include <QWidget>

namespace Ui { class DashboardPage; }

/*
 * Overview of what the operator has configured. It reports state and nothing
 * more — no derived numbers, no background work.
 *
 * The layout lives in dashboardpage.ui — open it in Qt Designer. This file only
 * fills the widgets in and keeps them in step with AppSettings.
 */
class DashboardPage : public QWidget
{
    Q_OBJECT
public:
    explicit DashboardPage(QWidget *parent = nullptr);
    ~DashboardPage() override;

    void refresh();

signals:
    void openSettingsRequested();

private:
    Ui::DashboardPage *ui = nullptr;
};
