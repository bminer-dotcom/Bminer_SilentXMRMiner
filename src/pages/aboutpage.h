#pragma once

#include <QWidget>

namespace Ui { class AboutPage; }

/*
 * The badge, the version, and the small print.
 *
 * The layout lives in aboutpage.ui — open it in Qt Designer.
 */
class AboutPage : public QWidget
{
    Q_OBJECT
public:
    explicit AboutPage(QWidget *parent = nullptr);
    ~AboutPage() override;

private:
    void fillBuildInfo();

    Ui::AboutPage *ui = nullptr;
};
