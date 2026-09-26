#include "pages/dashboardpage.h"
#include "ui_dashboardpage.h"

#include "appsettings.h"
#include "theme.h"
#include "widgets/card.h"
#include "widgets/fieldrow.h"

#include <QLabel>
#include <QPushButton>

namespace {

enum Kind { Url, Pool, Xmr, Tox };

QString checkValue(Kind kind, const QString &raw)
{
    switch (kind) {
    case Url:  return Validators::endpointUrl(raw);
    case Pool: return Validators::poolAddress(raw);
    case Xmr:  return Validators::moneroAddress(raw);
    case Tox:  return Validators::toxId(raw);
    }
    return QString();
}

bool shows(const QString &key)
{
    return key == Keys::NetEndpoint  || key == Keys::MinerPool
        || key == Keys::MinerWallet  || key == Keys::IdTox
        || key == Keys::ToxOperatorId
        || key == Keys::ConfigUpdateMode
        || key == Keys::MinerOnIdle  || key == Keys::MinerOnActive
        || key == Keys::MinerTls     || key == Keys::MinerIdleAfter;
}

void setFlag(QLabel *label, bool on, const QString &onText, const QString &offText)
{
    label->setText(on ? onText : offText);
    label->setStyleSheet(Theme::styleFor(Theme::TextRole::Value)
                         + QStringLiteral("color:%1;")
                               .arg(on ? Theme::Teal.name() : Theme::TextFaint.name()));
}

} // namespace

DashboardPage::DashboardPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DashboardPage)
{
    ui->setupUi(this);

    connect(ui->editButton, &QPushButton::clicked,
            this, &DashboardPage::openSettingsRequested);

    connect(AppSettings::i(), &AppSettings::changed, this,
            [this](const QString &key) {
                if (shows(key))
                    refresh();
            });

    refresh();
}

DashboardPage::~DashboardPage()
{
    delete ui;
}

void DashboardPage::refresh()
{
    struct Row {
        KeyValueRow   *row;
        const QString *key;
        Kind           kind;
        bool           required = true; // counted as missing when empty
    };

    AppSettings *s = AppSettings::i();
    // The panel endpoint is only needed in Endpoint update mode — in Tox /
    // Config-link mode an empty endpoint must not count as "not set".
    const QString mode = s->getString(Keys::ConfigUpdateMode);
    const QString effective = mode.isEmpty() ? QStringLiteral("tox") : mode;
    const bool endpointNeeded = (effective == QStringLiteral("endpoint"));

    const Row rows[4] = {
        { ui->endpointRow, &Keys::NetEndpoint, Url, endpointNeeded },
        { ui->poolRow,     &Keys::MinerPool,   Pool },
        { ui->walletRow,   &Keys::MinerWallet, Xmr },
        { ui->toxRow,      &Keys::IdTox,       Tox },
    };

    int missing = 0;
    int invalid = 0;

    for (const Row &slot : rows) {
        const QString raw = slot.kind == Tox
            ? s->operatorToxId()
            : s->getString(*slot.key).trimmed();
        const QString error = raw.isEmpty() ? QString() : checkValue(slot.kind, raw);

        if (raw.isEmpty()) {
            slot.row->setValue(tr("not set"));
            slot.row->setValueColor(Theme::TextFaint);
            slot.row->setToolTip(QString());
            if (slot.required)
                ++missing;
        } else if (!error.isEmpty()) {
            slot.row->setValue(raw);
            slot.row->setValueColor(Theme::Red);
            slot.row->setToolTip(error);
            ++invalid;
        } else {
            slot.row->setValue(raw);
            slot.row->setValueColor(Theme::Text);
            slot.row->setToolTip(QString());
        }
    }

    if (invalid > 0)
        ui->sectionHint->setText(tr("%n value(s) need fixing", nullptr, invalid));
    else if (missing > 0)
        ui->sectionHint->setText(tr("%n value(s) not set", nullptr, missing));
    else
        ui->sectionHint->setText(tr("All configured"));

    setFlag(ui->idleValue, s->getBool(Keys::MinerOnIdle),
            tr("after %1 min").arg(s->getInt(Keys::MinerIdleAfter)), tr("off"));
    setFlag(ui->activeValue, s->getBool(Keys::MinerOnActive), tr("on"), tr("off"));
    setFlag(ui->tlsValue,    s->getBool(Keys::MinerTls),      tr("on"), tr("off"));
}
