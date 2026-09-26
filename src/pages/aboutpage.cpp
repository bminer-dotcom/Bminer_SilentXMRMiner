#include "pages/aboutpage.h"
#include "ui_aboutpage.h"

#include "appsettings.h"
#include "buildinfo.h"
#include "theme.h"
#include "widgets/card.h"
#include "widgets/logoview.h"

#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QPushButton>

AboutPage::AboutPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::AboutPage)
{
    ui->setupUi(this);
    fillBuildInfo();

    ui->logoView->setAnimated(AppSettings::i()->getBool(Keys::UiAnimations));

    const QString donationWallet = QStringLiteral(
        "82xyverX5w331ggEyNANPgJbPq19HpGxe5Wfwr6Bvvo9dMQVYDF697STWT88hC7URsVbrQjtiJNJRPystEvNwvdZUYGnjxd");
    // Break the display into two lines so the long wallet cannot force the
    // side-by-side cards wider than the About page. The copy button keeps the
    // unbroken address on the clipboard.
    ui->donationWalletLabel->setText(donationWallet.left(48) + QLatin1Char('\n')
                                     + donationWallet.mid(48));
    ui->donationWalletLabel->setFont(Theme::monoFont(8));

    connect(ui->copyDonationButton, &QPushButton::clicked, this, [donationWallet] {
        QApplication::clipboard()->setText(donationWallet);
    });

    connect(ui->copyVersionButton, &QPushButton::clicked, this, [] {
        QApplication::clipboard()->setText(
            QStringLiteral("Bminer %1 (%2) · Qt %3 · %4")
                .arg(BuildInfo::version(), BuildInfo::codename(),
                     BuildInfo::qtRuntimeVersion(), BuildInfo::arch()));
    });

    // The accent-coloured tagline follows Keys::UiAccent on its own: the
    // stylesheet is regenerated and reapplied app-wide when the accent changes.
    connect(AppSettings::i(), &AppSettings::changed, this,
            [this](const QString &key) {
                if (key == Keys::UiAnimations)
                    ui->logoView->setAnimated(AppSettings::i()->getBool(Keys::UiAnimations));
            });
}

AboutPage::~AboutPage()
{
    delete ui;
}

/* Everything the compiler knew, poured into the widgets the form declares.
 * There is no styling code on this page — every label carries a `role` or
 * `pill` property in aboutpage.ui and the stylesheet does the rest. */
void AboutPage::fillBuildInfo()
{
    ui->taglineLabel->setText(BuildInfo::tagline());

    ui->versionPill->setText(QStringLiteral("V%1").arg(BuildInfo::version()));
    ui->codenamePill->setText(BuildInfo::codename().toUpper());
    ui->buildPill->setText(BuildInfo::buildType().toUpper());

    ui->versionRow->setValue(BuildInfo::version());
    ui->qtRow->setValue(BuildInfo::qtRuntimeVersion());

    // The form carries the sentence with a %1 in it, so it stays translatable
    // and editable in Designer while the path comes from here. Guarded because
    // arg() consumes the placeholder: without the check, a second call would
    // warn and leave a stale path on screen.
    const QString storage = ui->creditsStorage->text();
    if (storage.contains(QLatin1String("%1")))
        ui->creditsStorage->setText(storage.arg(AppSettings::i()->storageLocation()));
}
