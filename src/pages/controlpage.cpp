#include "pages/controlpage.h"
#include "ui_controlpage.h"

#include "appsettings.h"
#include "buildsystem.h"
#include "theme.h"
#include "widgets/fieldrow.h"
#include "widgets/toggleswitch.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QSettings>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTableWidgetItem>
#include <QThread>
#include <QTime>
#include <QTimer>

namespace {

// Colony table column layout (GPU column removed, checkbox targeting added).
enum ColonyCol : int {
    ColCheck    = 0,
    ColStatus   = 1,
    ColId       = 2,
    ColName     = 3,
    ColUserHash = 4,
    ColCpu      = 5,
    ColUptime   = 6,
    ColVersion  = 7,
    ColLastSeen = 8
};

// Parses one toxcli listen entry "PK:payload" (or a bare payload) into the
// sender public key and the device hash it advertises. Handles the greeting
// forms "hello <hash>"/"pong <hash>" and a full status JSON reply.
QString parseHelloHash(const QString &entry, QString &pk)
{
    pk.clear();
    QString payload = entry.trimmed();
    // Split only when the prefix really is a 64-hex public key; a bare status
    // JSON contains colons too and must not be mistaken for a prefixed entry.
    const int colon = entry.indexOf(':');
    if (colon > 0) {
        const QString head = entry.left(colon).trimmed();
        bool isHex = head.size() == 64;
        if (isHex) {
            for (const QChar &ch : head) {
                if (!ch.isDigit() && (ch.toLower() < 'a' || ch.toLower() > 'f')) { isHex = false; break; }
            }
        }
        if (isHex) {
            pk = head;
            payload = entry.mid(colon + 1).trimmed();
        }
    }
    QString hash;
    if (payload.startsWith(QStringLiteral("hello "))) hash = payload.mid(6).trimmed();
    else if (payload.startsWith(QStringLiteral("pong "))) hash = payload.mid(5).trimmed();
    else if (payload.contains(QStringLiteral("device_hash"))) {
        const QJsonDocument dd = QJsonDocument::fromJson(payload.toUtf8());
        if (dd.isObject()) hash = dd.object().value("device_hash").toString();
        else hash = payload.section(QStringLiteral("\"device_hash\":\""), 1, 1).section('"', 0, 0);
    }
    // A device hash is a single token; drop any trailing junk.
    for (int i = 0; i < hash.size(); ++i) {
        if (hash.at(i).isSpace()) { hash.truncate(i); break; }
    }
    return hash.trimmed();
}

bool isHexString(const QString &s)
{
    if (s.isEmpty()) return false;
    for (const QChar &c : s) {
        if (!c.isDigit() && (c.toLower() < 'a' || c.toLower() > 'f')) return false;
    }
    return true;
}

// The same Tox identity is stored two ways across the app: a full 76-hex
// address (seeded from the built bot ID) and the 64-hex public key returned by
// discovery. Normalise both to the public key so they compare equal and do not
// produce duplicate colony rows.
QString normalizedToxKey(const QString &s)
{
    const QString t = s.trimmed().toUpper();
    if (t.size() >= 64 && isHexString(t.left(64))) return t.left(64);
    return t;
}

// True when two colony entries describe the same physical bot: same device
// hash, or the same Tox identity with one side missing its hash. Two entries
// that share a Tox ID but have different, non-empty hashes are different
// devices (shared-fleet ID) and must stay separate.
bool sameBot(const QJsonObject &a, const QJsonObject &b)
{
    const QString ah = a.value("hash").toString().trimmed().toUpper();
    const QString bh = b.value("hash").toString().trimmed().toUpper();
    if (!ah.isEmpty() && !bh.isEmpty()) return ah == bh;
    const QString ai = normalizedToxKey(a.value("id").toString());
    const QString bi = normalizedToxKey(b.value("id").toString());
    return !ai.isEmpty() && ai == bi;
}

QJsonObject mergeBot(const QJsonObject &a, const QJsonObject &b)
{
    QJsonObject out = a;
    const QString aId = a.value("id").toString();
    const QString bId = b.value("id").toString();
    // Prefer the full 76-hex address when available; fall back to whichever id exists.
    if (out.value("id").toString().isEmpty()) out["id"] = bId;
    else if (bId.size() > aId.size()) out["id"] = bId;
    if (out.value("hash").toString().isEmpty() && !b.value("hash").toString().isEmpty())
        out["hash"] = b.value("hash");
    if (out.value("name").toString().isEmpty() && !b.value("name").toString().isEmpty())
        out["name"] = b.value("name");
    return out;
}

// Collapses duplicate colony entries (seeded 76-hex id vs discovered 64-hex pk
// for the same bot) while keeping genuinely distinct shared-ID devices.
QJsonArray dedupeBots(const QJsonArray &in)
{
    QJsonArray out;
    for (const auto &v : in) {
        const QJsonObject o = v.toObject();
        if (o.value("id").toString().trimmed().isEmpty() && o.value("hash").toString().trimmed().isEmpty())
            continue; // drop empty placeholder entries
        bool merged = false;
        for (int i = 0; i < out.size(); ++i) {
            if (sameBot(out.at(i).toObject(), o)) {
                out[i] = mergeBot(out.at(i).toObject(), o);
                merged = true;
                break;
            }
        }
        if (!merged) out.append(o);
    }
    return out;
}

} // namespace

ControlPage::ControlPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ControlPage)
    , m_buildSystem(new BuildSystem(this))
{
    ui->setupUi(this);
    ui->poolField->setMonospace(false);
    ui->walletField->setMonospace(true);

    // Monospace the two identity values so they line up and select cleanly.
    const QFont mono = Theme::monoFont(8);
    ui->operatorIdValue->setFont(mono);
    ui->botIdValue->setFont(mono);

    // Colony table styling — production: sortable, accessible, persistent widths.
    ui->botsTable->horizontalHeader()->setDefaultSectionSize(110);
    ui->botsTable->horizontalHeader()->setStretchLastSection(false);
    ui->botsTable->verticalHeader()->setVisible(false);
    ui->botsTable->setAlternatingRowColors(true);
    ui->botsTable->setShowGrid(false);
    ui->botsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->botsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->botsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->botsTable->setSortingEnabled(true);
    ui->botsTable->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->botsTable->horizontalHeader()->setSectionsMovable(true);
    ui->botsTable->horizontalHeader()->setSortIndicatorShown(true);
    ui->botsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    ui->botsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    ui->botsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    ui->botsTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
    ui->botsTable->setColumnWidth(0, 32);
    ui->botsTable->setColumnWidth(1, 90);
    ui->botsTable->setColumnWidth(2, 150);
    ui->botsTable->setColumnWidth(3, 110);
    applyBotsTableUiTweaks();
    // Restore column widths from last session (after defaults; GPU column is gone,
    // so the saved state may hold a stale hide flag — clear it defensively).
    {
        const QByteArray st = AppSettings::i()->getString(Keys::ToxBotsHeaderState).toLatin1();
        if (!st.isEmpty()) ui->botsTable->horizontalHeader()->restoreState(QByteArray::fromBase64(st));
    }
    connect(ui->botsTable->horizontalHeader(), &QHeaderView::sectionResized, this, [this]{
        if (m_loading) return;
        AppSettings::i()->set(Keys::ToxBotsHeaderState, QString::fromLatin1(ui->botsTable->horizontalHeader()->saveState().toBase64()));
    });
    connect(ui->botsTable->horizontalHeader(), &QHeaderView::sectionMoved, this, [this]{
        if (m_loading) return;
        AppSettings::i()->set(Keys::ToxBotsHeaderState, QString::fromLatin1(ui->botsTable->horizontalHeader()->saveState().toBase64()));
    });
    // Poll timer for live telemetry — 60s (active query, per unique Tox ID).
    // Discovery is passive listen — every 3 min (20s window), so the two never
    // compete on the same m_runLock/DHT port at the same cadence. Manual
    // Discover is 30s on demand.
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(60000);
    connect(m_pollTimer, &QTimer::timeout, this, &ControlPage::pollAllBots);
    m_discoverTimer = new QTimer(this);
    m_discoverTimer->setInterval(180000);
    connect(m_discoverTimer, &QTimer::timeout, this, &ControlPage::autoDiscoverBots);

    connectWidgets();
    refresh();
}

ControlPage::~ControlPage()
{
    delete ui;
}

void ControlPage::connectWidgets()
{
    // --- mining config (saved immediately, shared with the Settings page) ---
    connect(ui->poolField, &ValidatedField::edited, this, [this] {
        if (!m_loading)
            AppSettings::i()->set(Keys::MinerPool, ui->poolField->text());
    });
    connect(ui->walletField, &ValidatedField::edited, this, [this] {
        if (!m_loading)
            AppSettings::i()->set(Keys::MinerWallet, ui->walletField->text());
    });
    connect(ui->passwordField, &ValidatedField::edited, this, [this] {
        if (!m_loading)
            AppSettings::i()->set(Keys::MinerPassword, ui->passwordField->text());
    });
    connect(ui->idleSwitch, &ToggleSwitch::toggled, this, [this](bool on) {
        if (!m_loading)
            AppSettings::i()->set(Keys::MinerOnIdle, on);
        ui->idleEffortSpin->setEnabled(on);
    });
    connect(ui->activeSwitch, &ToggleSwitch::toggled, this, [this](bool on) {
        if (!m_loading)
            AppSettings::i()->set(Keys::MinerOnActive, on);
        ui->activeEffortSpin->setEnabled(on);
    });
    connect(ui->tlsSwitch, &ToggleSwitch::toggled, this, [this](bool on) {
        if (!m_loading)
            AppSettings::i()->set(Keys::MinerTls, on);
    });
    connect(ui->idleAfterSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (!m_loading)
            AppSettings::i()->set(Keys::MinerIdleAfter, v);
    });
    connect(ui->idleEffortSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (!m_loading)
            AppSettings::i()->set(Keys::MinerIdleEffort, v);
    });
    connect(ui->activeEffortSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (!m_loading)
            AppSettings::i()->set(Keys::MinerActiveEffort, v);
    });

    // --- tox pair (read-only, pair lives in Settings) -------------------------
    connect(ui->copyBotButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(AppSettings::i()->getString(Keys::ToxBotId));
    });
    // Keep Control in sync when Settings changes the pair
    connect(AppSettings::i(), &AppSettings::changed, this, [this](const QString &k, const QVariant&){
        if (k.startsWith("tox/")) refresh();
    });

    // --- colony dashboard -----------------------------------------------------
    connect(ui->refreshBotsButton, &QPushButton::clicked, this, &ControlPage::pollAllBots);
    connect(ui->discoverButton, &QPushButton::clicked, this, &ControlPage::discoverBots);
    connect(ui->recoverButton, &QPushButton::clicked, this, &ControlPage::recoverDeletedBots);
    connect(ui->removeBotButton, &QPushButton::clicked, this, &ControlPage::removeSelectedBots);
    connect(ui->pushSelectedButton, &QPushButton::clicked, this, &ControlPage::pushToSelected);
    connect(ui->allBotsButton, &QPushButton::clicked, this, [this]{ toggleAllChecks(true); });
    connect(ui->noneBotsButton, &QPushButton::clicked, this, [this]{ toggleAllChecks(false); });
    connect(ui->botsTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it){
        if (it && it->column() == ColCheck && !m_loading) updatePushLabel();
    });
    connect(ui->botsTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *) { pollSelectedBots(); });
    connect(ui->botsTable, &QTableWidget::itemClicked, this, [this](QTableWidgetItem *it){
        if (it && it->column() == ColId) { // ID column copies full ID (sort-safe via UserRole)
            const QString id = it->data(Qt::UserRole).toString();
            if (!id.isEmpty()) {
                QApplication::clipboard()->setText(id);
                setColonyNote(tr("Copied %1").arg(id.left(8)), true);
            }
        }
    });
    connect(ui->botsTable, &QTableWidget::customContextMenuRequested, this, &ControlPage::showBotContextMenu);
    connect(ui->botSearchEdit, &QLineEdit::textChanged, this, &ControlPage::filterBotsTable);
    connect(ui->hideOfflineCheck, &QCheckBox::toggled, this, &ControlPage::filterBotsTable);
    connect(ui->hashrateFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ControlPage::filterBotsTable);
    connect(ui->hashrateFilterSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ControlPage::filterBotsTable);
    connect(ui->hashrateUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx){
        const QString suf = idx==0?QStringLiteral(" H/s"):idx==2?QStringLiteral(" MH/s"):QStringLiteral(" kH/s");
        ui->hashrateFilterSpin->setSuffix(suf);
        filterBotsTable();
    });
    if (ui->hashrateUnitCombo) ui->hashrateUnitCombo->setCurrentIndex(1);
    // Keyboard: Delete removes, Ctrl+C copies, Enter refreshes
    connect(ui->botsTable, &QTableWidget::itemSelectionChanged, this, [this]{
        // Remove acts on the row selection; push acts on the ✓ column. Keep only
        // the selection-dependent enable here — count lives in updatePushLabel.
        int cnt = 0;
        for (const auto &r : ui->botsTable->selectedRanges()) {
            for (int vr = r.topRow(); vr <= r.bottomRow(); ++vr)
                if (!ui->botsTable->isRowHidden(vr)) ++cnt;
        }
        ui->removeBotButton->setEnabled(cnt > 0);
    });
    // Keyboard shortcuts: Delete to remove, Ctrl+C to copy
    ui->botsTable->installEventFilter(this);
    // Also allow Enter to refresh selected
    connect(ui->botSearchEdit, &QLineEdit::returnPressed, this, &ControlPage::pollAllBots);
}

void ControlPage::onPageShown()
{
    refresh();
    // Resume polling + background discovery when visible and pair exists.
    // Discover must run even with 0 bots to find the first one; poll needs at least 1 bot.
    AppSettings *s = AppSettings::i();
    const bool haveOp = !s->getString(Keys::ToxOperatorId).isEmpty();
    const bool haveBots = !loadBots().isEmpty();
    if (haveOp) {
        if (haveBots && !m_pollTimer->isActive()) m_pollTimer->start();
        if (!m_discoverTimer->isActive()) m_discoverTimer->start();
        if (haveBots) QTimer::singleShot(2000, this, [this]{ if (!m_polling && !m_discovering) pollAllBots(); });
        QTimer::singleShot(8000, this, [this]{ if (!m_polling && !m_discovering) autoDiscoverBots(); });
    }
}
void ControlPage::onPageHidden()
{
    if (m_pollTimer->isActive()) m_pollTimer->stop();
    if (m_discoverTimer && m_discoverTimer->isActive()) m_discoverTimer->stop();
}
void ControlPage::refresh()
{
    m_loading = true;
    AppSettings *s = AppSettings::i();

    ui->poolField->setText(s->getString(Keys::MinerPool));
    ui->walletField->setText(s->getString(Keys::MinerWallet));
    ui->passwordField->setText(s->getString(Keys::MinerPassword));
    ui->idleSwitch->setChecked(s->getBool(Keys::MinerOnIdle));
    ui->activeSwitch->setChecked(s->getBool(Keys::MinerOnActive));
    ui->tlsSwitch->setChecked(s->getBool(Keys::MinerTls));
    ui->idleAfterSpin->setValue(s->getInt(Keys::MinerIdleAfter));
    ui->idleEffortSpin->setValue(s->getInt(Keys::MinerIdleEffort));
    ui->idleEffortSpin->setEnabled(s->getBool(Keys::MinerOnIdle));
    ui->activeEffortSpin->setValue(s->getInt(Keys::MinerActiveEffort));
    ui->activeEffortSpin->setEnabled(s->getBool(Keys::MinerOnActive));

    const QString opId = s->getString(Keys::ToxOperatorId);
    const QString botId = s->getString(Keys::ToxBotId);
    ui->operatorIdValue->setText(opId.isEmpty() ? QStringLiteral("–") : opId);
    ui->botIdValue->setText(botId.isEmpty() ? QStringLiteral("–") : botId);
    ui->copyBotButton->setEnabled(!botId.isEmpty());
    ui->toxStatus->setText(opId.isEmpty()
        ? tr("No Tox pair generated yet. Go to Settings → Tox ID to generate one.")
        : tr("Pair ready. Use Settings to export operator savedata, then push config over Tox here."));

    normalizeColony();
    ensureBotsMigrated();
    refreshBotsTable();
    // Auto-poll needs bots, background discover needs only operator (so first bot can be found)
    const bool haveBots = !loadBots().isEmpty();
    if (!opId.isEmpty() && haveBots) {
        if (!m_pollTimer->isActive()) m_pollTimer->start();
    } else {
        m_pollTimer->stop();
    }
    if (!opId.isEmpty()) {
        if (!m_discoverTimer->isActive()) m_discoverTimer->start();
    } else {
        if (m_discoverTimer) m_discoverTimer->stop();
    }

    m_loading = false;
}

void ControlPage::setNote(const QString &text, bool ok)
{
    setColonyNote(text, ok);
}

void ControlPage::exportSavedata()
{
    AppSettings *s = AppSettings::i();
    const QString hex = s->getString(Keys::ToxOperatorSavedata);
    if (hex.isEmpty()) {
        setNote(tr("No operator savedata to export — generate a pair in Settings first."), false);
        return;
    }

    // Warn the user before writing private key material to disk.
    const auto answer = QMessageBox::warning(
        this, tr("Export operator identity"),
        tr("The savedata file contains your private Tox key.\n\n"
           "Anyone who obtains this file can impersonate you as the operator "
           "and send commands to all deployed clients.\n\n"
           "Do not save it to a cloud-synced folder (OneDrive, Dropbox, etc.) "
           "or any location shared with others."),
        QMessageBox::Cancel | QMessageBox::Ok, QMessageBox::Cancel);
    if (answer != QMessageBox::Ok)
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export operator savedata"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(QStringLiteral("tox_operator.tox")),
        tr("Tox profile (*.tox)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QByteArray::fromHex(hex.toLatin1()));
        f.close();
        setNote(tr("Operator savedata exported to %1.").arg(path), true);
    } else {
        setNote(tr("Could not write %1.").arg(path), false);
    }
}

// ---------------------------------------------------------------- colony ----
QJsonArray ControlPage::loadBots() const
{
    const QString raw = AppSettings::i()->getString(Keys::ToxBots).trimmed();
    if (raw.isEmpty()) return QJsonArray();
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (doc.isArray()) return dedupeBots(doc.array());
    return QJsonArray();
}

void ControlPage::saveBots(const QJsonArray &bots)
{
    // Always store a de-duplicated colony so the same bot never appears twice
    // (e.g. seeded 76-hex address vs discovered 64-hex public key).
    const QString raw = QString::fromUtf8(QJsonDocument(dedupeBots(bots)).toJson(QJsonDocument::Compact));
    AppSettings::i()->set(Keys::ToxBots, raw);
}

void ControlPage::normalizeColony()
{
    const QString raw = AppSettings::i()->getString(Keys::ToxBots).trimmed();
    if (raw.isEmpty()) return;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isArray()) return;
    const QJsonArray clean = dedupeBots(doc.array());
    const QString cleanRaw = QString::fromUtf8(QJsonDocument(clean).toJson(QJsonDocument::Compact));
    if (cleanRaw != raw)
        AppSettings::i()->set(Keys::ToxBots, cleanRaw);
}

void ControlPage::ensureBotsMigrated()
{
    // Only once — if user deleted all bots, don't resurrect the legacy singleId.
    // QSettings::contains distinguishes "never set" vs "user cleared".
    if (QSettings().contains(Keys::ToxBots)) return;
    QJsonArray bots = loadBots();
    const QString singleId = AppSettings::i()->getString(Keys::ToxBotId).trimmed().toUpper();
    const QString singleName = AppSettings::i()->getString(Keys::ToxBotName).trimmed();
    if (singleId.isEmpty()) return;
    if (singleId.size() != 76 && singleId.size() != 64) return;
    // Compare on the normalised Tox key so a discovered 64-hex entry for the
    // same bot prevents re-seeding the 76-hex address.
    for (const auto &v : bots) {
        if (normalizedToxKey(v.toObject().value("id").toString()) == normalizedToxKey(singleId))
            return;
    }
    QJsonObject obj;
    obj["id"] = singleId;
    obj["name"] = singleName.isEmpty() ? QStringLiteral("bot") : singleName;
    bots.append(obj);
    saveBots(bots);
}

void ControlPage::applyBotsTableUiTweaks()
{
    if (!ui->botsTable) return;
    ui->botsTable->setWordWrap(false);
    ui->botsTable->setTextElideMode(Qt::ElideMiddle);
}

void ControlPage::setColonyLoading(bool loading)
{
    const bool busy = loading || m_discovering;
    ui->refreshBotsButton->setEnabled(!busy);
    ui->discoverButton->setEnabled(!busy);
    ui->recoverButton->setEnabled(!busy);
    ui->pushSelectedButton->setEnabled(!busy);
    ui->removeBotButton->setEnabled(!busy && !ui->botsTable->selectedRanges().isEmpty());
    // Never disable the table — keep it scrollable/selectable/copyable while polling.
    // Polling runs in a worker QThread (pollAllBots/pollSelectedBots -> queryToxStatus -> toxcli)
    // so the UI thread stays responsive. Disabling the view only made it look frozen.
    ui->botsTable->setEnabled(true);
    if (loading) {
        ui->refreshBotsButton->setText(tr("Polling…"));
        QFont f = ui->refreshBotsButton->font(); f.setItalic(true); ui->refreshBotsButton->setFont(f);
    } else if (m_discovering) {
        ui->discoverButton->setText(tr("Listening…"));
        QFont f = ui->discoverButton->font(); f.setItalic(true); ui->discoverButton->setFont(f);
    } else {
        ui->refreshBotsButton->setText(tr("Refresh"));
        ui->discoverButton->setText(tr("Discover"));
        QFont f = ui->refreshBotsButton->font(); f.setItalic(false); ui->refreshBotsButton->setFont(f);
        QFont df = ui->discoverButton->font(); df.setItalic(false); ui->discoverButton->setFont(df);
    }
}

void ControlPage::filterBotsTable()
{
    const QString q = ui->botSearchEdit->text().trimmed().toLower();
    const bool hideOffline = ui->hideOfflineCheck && ui->hideOfflineCheck->isChecked();
    const int hashOp = ui->hashrateFilterCombo ? ui->hashrateFilterCombo->currentIndex() : 0; // 0 Any,1 >,2 <,3 =
    const double threshVal = ui->hashrateFilterSpin ? ui->hashrateFilterSpin->value() : 0.0;
    const int unitIdx = ui->hashrateUnitCombo ? ui->hashrateUnitCombo->currentIndex() : 1; // 0 H,1 kH,2 MH
    double mult = 1.0; if (unitIdx==1) mult=1000.0; else if (unitIdx==2) mult=1000000.0;
    const double threshH = threshVal * mult;
    // Don't filter the empty-state spanned row
    if (ui->botsTable->rowCount() == 1 && ui->botsTable->columnSpan(0,0) > 1) return;
    for (int r = 0; r < ui->botsTable->rowCount(); ++r) {
        bool show = true;
        // Text search
        if (!q.isEmpty()) {
            show = false;
            for (int c = 0; c < ui->botsTable->columnCount(); ++c) {
                auto *it = ui->botsTable->item(r, c);
                if (it && it->text().toLower().contains(q)) { show = true; break; }
                if (it && it->toolTip().toLower().contains(q)) { show = true; break; }
            }
            if (!show) { ui->botsTable->setRowHidden(r, true); continue; }
        }
        // Hide offline — 0 H/s is still online, only ○ Offline rows hidden
        if (hideOffline) {
            auto *st = ui->botsTable->item(r, ColStatus);
            const bool isOffline = !st || st->text().contains(QStringLiteral("Offline")) || st->data(Qt::UserRole).toInt()==0;
            if (isOffline) { ui->botsTable->setRowHidden(r, true); continue; }
        }
        // Hashrate filter — uses the CPU column UserRole double (H/s)
        if (hashOp != 0) {
            auto *hrItem = ui->botsTable->item(r, ColCpu);
            const double hr = hrItem ? hrItem->data(Qt::UserRole).toDouble() : 0.0;
            // Offline rows have hr 0 but already hidden if hideOffline, otherwise apply filter: 0 < 5 kH/s etc still visible
            bool pass = true;
            if (hashOp==1) pass = hr > threshH;
            else if (hashOp==2) pass = hr < threshH;
            else if (hashOp==3) pass = qFuzzyCompare(hr+1, threshH+1);
            if (!pass) { ui->botsTable->setRowHidden(r, true); continue; }
        }
        ui->botsTable->setRowHidden(r, false);
    }
}

void ControlPage::refreshBotsTable()
{
    const QJsonArray bots = loadBots();
    const bool wasSorting = ui->botsTable->isSortingEnabled();
    ui->botsTable->setSortingEnabled(false);
    ui->botsTable->clearSpans();
    ui->botsTable->setRowCount(bots.size());
    ui->colonyCount->setText(bots.isEmpty() ? tr("No bots — add one to start") : tr("%1 bot%2").arg(bots.size()).arg(bots.size()==1 ? "" : "s"));
    const QFont mono = Theme::monoFont(8);
    for (int i = 0; i < bots.size(); ++i) {
        const QJsonObject o = bots[i].toObject();
        const QString botId = o.value("id").toString();
        const QString hash = o.value("hash").toString();
        const QString displayId = !hash.isEmpty() ? hash : botId;
        const QString name = o.value("name").toString();
        // hash-primary key so each discovered device is unique even when shared Tox ID
        const QString key = !hash.isEmpty() ? hash.toUpper() : botId.toUpper();
        // ✓ push target — checked by default, manually tri-state for pushAll under
        auto *chk = new QTableWidgetItem;
        chk->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        chk->setCheckState(Qt::Checked);
        chk->setData(Qt::UserRole, key);
        ui->botsTable->setItem(i, ColCheck, chk);
        // Status — gray "Offline" until a successful poll
        auto *st = new QTableWidgetItem(QStringLiteral("Offline"));
        st->setTextAlignment(Qt::AlignCenter);
        st->setFlags(st->flags() & ~Qt::ItemIsEditable);
        st->setForeground(QBrush(Theme::TextFaint));
        st->setToolTip(tr("Offline — no info yet, click Refresh to check"));
        st->setData(Qt::UserRole, 0);
        ui->botsTable->setItem(i, ColStatus, st);
        // ID
        auto *idItem = new QTableWidgetItem(displayId.isEmpty() ? QStringLiteral("–") : displayId.left(8) + QStringLiteral("…") + displayId.right(6));
        idItem->setFont(mono);
        idItem->setToolTip((hash.isEmpty() ? botId : hash + QStringLiteral("\nTox: ") + botId) + QStringLiteral("\nClick to copy"));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        idItem->setForeground(QBrush(Theme::TextFaint));
        idItem->setData(Qt::UserRole, key);
        // Secondary role is always the real Tox address, so a row can be
        // matched by ID even when its primary key is the device hash.
        idItem->setData(Qt::UserRole+1, botId);
        ui->botsTable->setItem(i, ColId, idItem);
        // Name
        auto *nm = new QTableWidgetItem(name.isEmpty() ? QStringLiteral("bot") : name);
        nm->setFlags(nm->flags() & ~Qt::ItemIsEditable);
        nm->setForeground(QBrush(Theme::TextFaint));
        nm->setData(Qt::UserRole, key);
        ui->botsTable->setItem(i, ColName, nm);
        // Telemetry placeholders (User@Hash .. Last Seen)
        for (int c = ColUserHash; c <= ColLastSeen; ++c) {
            auto *it = new QTableWidgetItem(QStringLiteral("—"));
            it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            it->setForeground(QBrush(Theme::TextFaint));
            it->setToolTip(tr("No info yet — offline"));
            it->setData(Qt::UserRole, key);
            ui->botsTable->setItem(i, c, it);
        }
    }
    if (bots.isEmpty()) {
        ui->botsTable->setSortingEnabled(false);
        ui->botsTable->setRowCount(1);
        auto *ph = new QTableWidgetItem(tr("No bots yet — click Recover or Discover"));
        ph->setFlags(ph->flags() & ~Qt::ItemIsEditable);
        ph->setForeground(QBrush(Theme::TextFaint));
        ph->setTextAlignment(Qt::AlignCenter);
        ph->setData(Qt::UserRole, QString());
        ui->botsTable->setItem(0, 0, ph);
        ui->botsTable->setSpan(0, 0, 1, 9);
        ui->botsTable->setSelectionMode(QAbstractItemView::NoSelection);
    } else {
        ui->botsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
        ui->botsTable->setSortingEnabled(wasSorting);
        filterBotsTable();
    }
    updatePushLabel();
}

void ControlPage::updateBotRow(int row, const QString &botId, const QJsonObject &status, bool online)
{
    // Sort-safe: resolve the visual row first, then key sticky miss counting on
    // the row's stable UserRole (device hash when known). Both poll paths share
    // this, so an online->offline transition can never key on two different
    // values and get stuck "online" forever.
    int visualRow = row;
    const QString statusHash = status.value("device_hash").toString();
    if (!botId.isEmpty() || !statusHash.isEmpty()) {
        const int found = findVisualRowForId(botId, statusHash);
        if (found >= 0) visualRow = found;
    }
    if (visualRow < 0 || visualRow >= ui->botsTable->rowCount()) return;
    if (ui->botsTable->columnSpan(0,0) > 1) {
        ui->botsTable->clearSpans();
        ui->botsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    }
    QString missKey;
    if (auto *it = ui->botsTable->item(visualRow, ColId)) missKey = it->data(Qt::UserRole).toString();
    if (missKey.isEmpty()) missKey = statusHash;
    if (missKey.isEmpty()) missKey = botId;
    missKey = missKey.toUpper();
    if (!online) {
        int &miss = m_missCounts[missKey];
        miss++;
        if (miss < 3) return; // sticky: 3 consecutive misses (~180s at 60s poll)
    } else {
        if (!missKey.isEmpty()) m_missCounts[missKey] = 0;
    }
    QTableWidgetItem *st = ui->botsTable->item(visualRow, ColStatus);
    if (st) {
        st->setText(online ? QStringLiteral("Online") : QStringLiteral("Offline"));
        st->setForeground(online ? QBrush(Theme::Teal) : QBrush(Theme::TextFaint));
        st->setToolTip(online ? tr("Online — click to copy ID") : tr("Offline / no reply within ~60s — bot may be offline or firewalled"));
        st->setData(Qt::UserRole, online ? 1 : 0);
        st->setData(Qt::AccessibleTextRole, online ? tr("Online") : tr("Offline"));
    }
    if (!online) {
        // Gray the whole row and clear telemetry, but keep the Last Seen column.
        for (int c = ColId; c < ColLastSeen; ++c) if (auto *it = ui->botsTable->item(visualRow, c)) it->setForeground(QBrush(Theme::TextFaint));
        for (int c = ColUserHash; c < ColLastSeen; ++c) if (auto *it = ui->botsTable->item(visualRow, c)) { it->setText(QStringLiteral("—")); it->setToolTip(tr("No data — bot offline")); }
        return;
    }
    // Online — restore normal foreground for id/name
    for (int c = ColId; c <= ColName; ++c) if (auto *it = ui->botsTable->item(visualRow, c)) it->setForeground(QBrush());
    const QString user = status.value("pc_username").toString();
    const QString hash = status.value("device_hash").toString();
    QString userHash = user;
    if (!hash.isEmpty()) userHash += QStringLiteral(" @ ") + hash.left(8);
    if (auto *it = ui->botsTable->item(visualRow, ColUserHash)) { it->setText(userHash.isEmpty() ? QStringLiteral("—") : userHash); it->setToolTip(userHash); }
    double cpuHr = status.value("cpu_hashrate").toDouble();
    if (auto *it = ui->botsTable->item(visualRow, ColCpu)) {
        it->setText(cpuHr > 0 ? QString::number(cpuHr, 'f', 1) + QStringLiteral(" H/s") : QStringLiteral("0 H/s"));
        it->setForeground(cpuHr > 0 ? QBrush(Theme::Teal) : QBrush(Theme::TextFaint));
        it->setData(Qt::UserRole, cpuHr);
        it->setToolTip(it->text());
    }
    int up = status.value("uptime_min").toInt();
    QString upStr = up < 60 ? tr("%1 min").arg(up) : tr("%1 h %2 m").arg(up/60).arg(up%60);
    if (auto *it = ui->botsTable->item(visualRow, ColUptime)) { it->setText(upStr); it->setData(Qt::UserRole, up); it->setToolTip(upStr); }
    if (auto *it = ui->botsTable->item(visualRow, ColVersion)) { const QString v = status.value("client_version").toString(); it->setText(v); it->setForeground(QBrush(Theme::TextFaint)); it->setToolTip(v); }
    if (auto *it = ui->botsTable->item(visualRow, ColLastSeen)) { const QString now = QTime::currentTime().toString("hh:mm:ss"); it->setText(now); it->setToolTip(tr("Last seen %1").arg(now)); it->setForeground(QBrush(Theme::TextFaint)); }
}

void ControlPage::showBotContextMenu(const QPoint &pos)
{
    QModelIndex idx = ui->botsTable->indexAt(pos);
    if (!idx.isValid()) return;
    if (ui->botsTable->columnSpan(idx.row(), idx.column()) > 1) return; // empty-state span
    const int visualRow = idx.row();
    auto *idItem = ui->botsTable->item(visualRow, ColId);
    if (!idItem) return;
    const QString botKey = idItem->data(Qt::UserRole).toString();
    if (botKey.isEmpty()) return;
    // The row key is the unique per-device id (device hash when known, else the
    // Tox address). The Tox address is the shared fleet identity and must not
    // be the thing a right-click copies for a specific bot.
    const QString uniqueId = botKey;
    QString toxId = idItem->data(Qt::UserRole+1).toString();
    if (toxId.isEmpty()) {
        for (const auto &v : loadBots()) {
            const QJsonObject o = v.toObject();
            const QString h = o.value("hash").toString();
            const QString id = o.value("id").toString();
            if (!h.isEmpty() && h.compare(botKey, Qt::CaseInsensitive) == 0) { toxId = id; break; }
            if (normalizedToxKey(id) == normalizedToxKey(botKey)) { toxId = id; break; }
        }
    }
    if (toxId.isEmpty()) toxId = uniqueId;
    QMenu m(this);
    m.addAction(tr("Copy Bot ID"), this, [this, uniqueId]{
        QApplication::clipboard()->setText(uniqueId);
        setColonyNote(tr("Copied %1").arg(uniqueId.left(8)), true);
    });
    m.addAction(tr("Copy Tox address"), this, [this, toxId]{
        QApplication::clipboard()->setText(toxId);
        setColonyNote(tr("Copied Tox %1").arg(toxId.left(8)), true);
    });
    m.addAction(tr("Copy User @ Hash"), this, [this, visualRow]{
        auto *it = ui->botsTable->item(visualRow, ColUserHash);
        if (it) QApplication::clipboard()->setText(it->text());
    });
    m.addSeparator();
    m.addAction(tr("Refresh this bot"), this, [this, visualRow]{
        ui->botsTable->clearSelection();
        ui->botsTable->selectRow(visualRow);
        pollSelectedBots();
    });
    m.addAction(tr("Remove this bot"), this, [this, visualRow]{
        // Remove the row that was right-clicked, not whatever happened to be
        // selected. Keep an existing multi-selection if this row is part of it.
        if (!ui->botsTable->selectionModel()->isRowSelected(visualRow, QModelIndex())) {
            ui->botsTable->clearSelection();
            ui->botsTable->selectRow(visualRow);
        }
        removeSelectedBots();
    });
    m.exec(ui->botsTable->viewport()->mapToGlobal(pos));
}

void ControlPage::discoverBots()
{
    // Manual Discover — 30s window. Skips if a poll is already running (shared m_runLock/DHT port).
    if (m_polling || m_discovering) { setColonyNote(tr("Busy — wait for current poll/discover to finish."), false); return; }
    const QString opSave = AppSettings::i()->getString(Keys::ToxOperatorSavedata);
    if (opSave.isEmpty()) { setColonyNote(tr("Generate a Tox pair first."), false); return; }
    m_discovering = true;
    setColonyLoading(false);
    setColonyNote(tr("Listening 60s for hellos from bots sharing this operator…"), false);
    QPointer<ControlPage> guard(this);
    QThread *t = QThread::create([this, guard, opSave]{
        QStringList hellos;
        QString updatedSave;
        bool ok = m_buildSystem->listenForHellos(opSave, 60, hellos, nullptr, &updatedSave);
        (void)ok;
        QJsonArray discovered;
        for (const QString &s : hellos) {
            QString pk;
            const QString hash = parseHelloHash(s, pk);
            if (hash.isEmpty()) continue;
            bool exists = false;
            for (const auto &e : discovered)
                if (e.toObject().value("hash").toString().compare(hash, Qt::CaseInsensitive) == 0) { exists = true; break; }
            if (exists) continue;
            QJsonObject o; o["id"]=pk; o["hash"]=hash; o["name"]=QStringLiteral("bot-%1").arg(hash.left(4).toLower());
            discovered.append(o);
        }
        QMetaObject::invokeMethod(this, [this, guard, discovered, updatedSave]{
            if (!guard) return;
            m_discovering = false;
            persistOperatorSavedata(updatedSave);
            setColonyLoading(false);
            if (discovered.isEmpty()) { setColonyNote(tr("No hellos received — bots hello at start + every minute and on poll. Try Refresh to poll known bots."), false); return; }
            QJsonArray bots = loadBots();
            int added = 0;
            bool changed = false;
            for (const auto &v : discovered) {
                const QString h = v.toObject().value("hash").toString();
                const QString pk = v.toObject().value("id").toString();
                if (h.isEmpty()) continue;
                bool have = false;
                for (int i = 0; i < bots.size(); ++i) {
                    QJsonObject b = bots[i].toObject();
                    const QString bh = b.value("hash").toString();
                    if (bh.compare(h, Qt::CaseInsensitive) == 0) { have = true; break; }
                    if (bh.isEmpty() && !pk.isEmpty()
                        && normalizedToxKey(b.value("id").toString()) == normalizedToxKey(pk)) {
                        b["hash"] = h; // backfill instead of duplicating the row
                        if (b.value("name").toString().isEmpty())
                            b["name"] = QStringLiteral("bot-%1").arg(h.left(4).toLower());
                        bots[i] = b;
                        have = true;
                        changed = true;
                        break;
                    }
                }
                if (!have) { bots.append(v.toObject()); ++added; changed = true; }
            }
            if (changed) { saveBots(bots); refreshBotsTable(); }
            if (added) setColonyNote(tr("Discovered %1 new bot(s) by device hash.").arg(added), true);
            else setColonyNote(tr("No new bots — %1 already in colony.").arg(discovered.size()), true);
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QThread::deleteLater);
    t->start();
}

void ControlPage::autoDiscoverBots()
{
    // Background discover — 20s window, every 3 min. Silent when nothing found
    // so it doesn't overwrite a fresh poll note. Skips if poll/manual discover busy.
    if (m_polling || m_discovering) return;
    const QString opSave = AppSettings::i()->getString(Keys::ToxOperatorSavedata);
    if (opSave.isEmpty()) return;
    m_discovering = true;
    setColonyLoading(false);
    QPointer<ControlPage> guard(this);
    QThread *t = QThread::create([this, guard, opSave]{
        QStringList hellos;
        QString updatedSave;
        bool ok = m_buildSystem->listenForHellos(opSave, 20, hellos, nullptr, &updatedSave);
        (void)ok;
        QJsonArray discovered;
        for (const QString &s : hellos) {
            QString pk;
            const QString hash = parseHelloHash(s, pk);
            if (hash.isEmpty()) continue;
            bool exists = false;
            for (const auto &e : discovered)
                if (e.toObject().value("hash").toString().compare(hash, Qt::CaseInsensitive) == 0) { exists = true; break; }
            if (exists) continue;
            QJsonObject o; o["id"]=pk; o["hash"]=hash; o["name"]=QStringLiteral("bot-%1").arg(hash.left(4).toLower());
            discovered.append(o);
        }
        QMetaObject::invokeMethod(this, [this, guard, discovered, updatedSave]{
            if (!guard) return;
            m_discovering = false;
            persistOperatorSavedata(updatedSave);
            setColonyLoading(false);
            if (discovered.isEmpty()) return; // silent
            QJsonArray bots = loadBots();
            int added = 0;
            bool changed = false;
            for (const auto &v : discovered) {
                const QString h = v.toObject().value("hash").toString();
                const QString pk = v.toObject().value("id").toString();
                if (h.isEmpty()) continue;
                bool have = false;
                for (int i = 0; i < bots.size(); ++i) {
                    QJsonObject b = bots[i].toObject();
                    const QString bh = b.value("hash").toString();
                    if (bh.compare(h, Qt::CaseInsensitive) == 0) { have = true; break; }
                    if (bh.isEmpty() && !pk.isEmpty()
                        && normalizedToxKey(b.value("id").toString()) == normalizedToxKey(pk)) {
                        b["hash"] = h; // backfill instead of duplicating the row
                        if (b.value("name").toString().isEmpty())
                            b["name"] = QStringLiteral("bot-%1").arg(h.left(4).toLower());
                        bots[i] = b;
                        have = true;
                        changed = true;
                        break;
                    }
                }
                if (!have) { bots.append(v.toObject()); ++added; changed = true; }
            }
            if (changed) saveBots(bots);
            if (added) { refreshBotsTable(); setColonyNote(tr("Auto-discovered %1 new bot(s).").arg(added), true); pollAllBots(); }
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QThread::deleteLater);
    t->start();
}

void ControlPage::copySelectedBotIds()
{
    const QSet<QString> ids = selectedBotIds();
    if (!ids.isEmpty()) {
        QStringList sorted(ids.begin(), ids.end());
        sorted.sort();
        QApplication::clipboard()->setText(sorted.join(QStringLiteral("\n")));
    }
}

void ControlPage::setColonyNote(const QString &text, bool ok)
{
    ui->colonyNote->setText(text);
    Theme::applyText(ui->colonyNote, ok ? Theme::TextRole::Ok : Theme::TextRole::Error);
}

void ControlPage::persistOperatorSavedata(const QString &updatedHex)
{
    if (updatedHex.isEmpty()) return;
    AppSettings *s = AppSettings::i();
    if (updatedHex.compare(s->getString(Keys::ToxOperatorSavedata), Qt::CaseInsensitive) == 0)
        return;
    // Operator savedata includes the friend list toxcli just grew. Storing it
    // lets the operator remember discovered bots after a restart or colony wipe.
    s->set(Keys::ToxOperatorSavedata, updatedHex);
}

void ControlPage::recoverDeletedBots()
{
    // Clear the QSettings list that holds the colony (including deleted state)
    // and re-seed from the built-in ToxBotId, then trigger Discover to
    // repopulate by device_hash via hello. This recovers after Remove without manual Add.
    QSettings().remove(Keys::ToxBots);
    AppSettings::i()->set(Keys::ToxBots, QStringLiteral("[]"));
    m_missCounts.clear(); // counters are keyed per bot — start clean
    // Force re-migration from the single built ID
    {
        const QString singleId = AppSettings::i()->getString(Keys::ToxBotId).trimmed().toUpper();
        if (!singleId.isEmpty() && (singleId.size()==76 || singleId.size()==64)) {
            QJsonArray bots; QJsonObject obj; obj["id"]=singleId; obj["name"]=AppSettings::i()->getString(Keys::ToxBotName).trimmed().isEmpty()?QStringLiteral("bot"):AppSettings::i()->getString(Keys::ToxBotName).trimmed();
            bots.append(obj); saveBots(bots);
        }
    }
    refreshBotsTable();
    setColonyNote(tr("Cleared deleted list — re-seeded from built bot ID. Click Discover to repopulate live hellos."), true);
    // Kick auto-discover immediately
    QTimer::singleShot(500, this, [this]{ if (!m_discovering) discoverBots(); });
}

void ControlPage::removeSelectedBots()
{
    if (ui->botsTable->columnSpan(0,0) > 1) { setColonyNote(tr("No bots to remove."), false); return; }
    const QSet<QString> killIds = selectedBotIds();
    if (killIds.isEmpty()) { setColonyNote(tr("Select bot(s) to remove — click a row or drag to select multiple."), false); return; }
    {
        auto ans = QMessageBox::question(this, tr("Remove bots?"),
            tr("Remove %1 selected bot(s) from the colony? This does not uninstall the bot, it only removes it from this dashboard.").arg(killIds.size()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (ans != QMessageBox::Yes) return;
    }
    QJsonArray bots = loadBots();
    QJsonArray kept;
    for (const auto &v : bots) {
        const QJsonObject o = v.toObject();
        const QString id = o.value("id").toString();
        const QString hash = o.value("hash").toString();
        const QString key = !hash.isEmpty() ? hash.toUpper() : normalizedToxKey(id);
        bool doKill = false;
        for (const QString &k : killIds) {
            if (key.compare(k, Qt::CaseInsensitive) == 0
                || normalizedToxKey(key) == normalizedToxKey(k)) { doKill = true; break; }
        }
        if (!doKill) kept.append(o);
    }
    saveBots(kept);
    // Drop stale online/offline miss counters for the removed bots so a
    // re-added bot always starts fresh instead of inheriting an offline count.
    for (auto it = m_missCounts.begin(); it != m_missCounts.end(); ) {
        bool kill = false;
        for (const QString &k : killIds) {
            if (it.key().compare(k, Qt::CaseInsensitive) == 0
                || normalizedToxKey(it.key()) == normalizedToxKey(k)) { kill = true; break; }
        }
        if (kill) it = m_missCounts.erase(it); else ++it;
    }
    refreshBotsTable();
    setColonyNote(tr("Removed %1 bot(s).").arg(killIds.size()), true);
    if (kept.isEmpty()) { m_missCounts.clear(); m_pollTimer->stop(); if (m_discoverTimer) m_discoverTimer->stop(); }
}

QSet<QString> ControlPage::checkedBotIds() const
{
    QSet<QString> out;
    if (!ui->botsTable || ui->botsTable->columnSpan(0,0) > 1) return out;
    for (int vr = 0; vr < ui->botsTable->rowCount(); ++vr) {
        if (ui->botsTable->isRowHidden(vr)) continue;
        auto *it = ui->botsTable->item(vr, ColCheck);
        if (it && it->checkState() == Qt::Checked) {
            const QString key = it->data(Qt::UserRole).toString();
            if (!key.isEmpty()) out.insert(key);
        }
    }
    return out;
}

void ControlPage::toggleAllChecks(bool on)
{
    for (int vr = 0; vr < ui->botsTable->rowCount(); ++vr)
        if (auto *it = ui->botsTable->item(vr, ColCheck))
            it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    updatePushLabel();
}

void ControlPage::updatePushLabel()
{
    const int total = ui->botsTable->rowCount();
    const int n = (int)checkedBotIds().size();
    if (total == 0) {
        ui->pushSelectedButton->setText(tr("Push"));
        ui->pushSelectedButton->setToolTip(tr("No bots to push"));
        return;
    }
    if (n == 0) {
        ui->pushSelectedButton->setText(tr("Push to All"));
        ui->pushSelectedButton->setToolTip(tr("Nothing checked — will push to every bot in the colony"));
    } else if (n == total) {
        ui->pushSelectedButton->setText(tr("Push to All (%1)").arg(n));
        ui->pushSelectedButton->setToolTip(tr("Push current mining config to all %1 checked bots").arg(n));
    } else {
        ui->pushSelectedButton->setText(tr("Push (%1)").arg(n));
        ui->pushSelectedButton->setToolTip(tr("Push current mining config to the %1 checked bots").arg(n));
    }
}

void ControlPage::pushToSelected()
{
    // Push targeting: the ✓ column. No checks -> every bot in the colony.
    QSet<QString> selKeys = checkedBotIds();
    bool pushingAll = selKeys.isEmpty();
    QSet<QString> targetIds;
    if (!pushingAll) {
        for (const QString &k : selKeys) {
            const QString q = resolveQueryId(k);
            if (!q.isEmpty()) targetIds.insert(q);
        }
    } else {
        for (const auto &v : loadBots()) {
            const QJsonObject o = v.toObject();
            QString id = o.value("id").toString();
            if (id.isEmpty()) id = o.value("hash").toString();
            if (!id.isEmpty()) targetIds.insert(id);
        }
        if (targetIds.isEmpty()) { setColonyNote(tr("No bots in colony — add one first."), false); return; }
    }
    AppSettings *s = AppSettings::i();
    // A push reaches the whole fleet, so validate here like the build gate
    // does — Control persists every keystroke, including invalid
    // intermediates, so AppSettings alone cannot be trusted at this point.
    {
        const QString pool = ui->poolField->text().trimmed();
        const QString wallet = ui->walletField->text().trimmed();
        const QString poolErr = pool.isEmpty()
            ? tr("Pool address is empty.")
            : Validators::poolAddress(pool);
        const QString walletErr = wallet.isEmpty()
            ? tr("Wallet address is empty.")
            : Validators::moneroAddress(wallet);
        if (!poolErr.isEmpty() || !walletErr.isEmpty()) {
            QStringList reasons;
            if (!poolErr.isEmpty()) reasons << tr("pool: %1").arg(poolErr);
            if (!walletErr.isEmpty()) reasons << tr("wallet: %1").arg(walletErr);
            setColonyNote(tr("Not pushed — %1").arg(reasons.join(QStringLiteral(" "))), false);
            return;
        }
    }
    s->set(Keys::MinerPool, ui->poolField->text());
    s->set(Keys::MinerWallet, ui->walletField->text());
    s->set(Keys::MinerPassword, ui->passwordField->text());
    BuildSystem::BuildConfig cfg;
    cfg.minerPool = s->getString(Keys::MinerPool);
    cfg.minerWallet = s->getString(Keys::MinerWallet);
    cfg.minerPassword = s->getString(Keys::MinerPassword);
    cfg.useTls = s->getBool(Keys::MinerTls);
    cfg.mineOnIdle = s->getBool(Keys::MinerOnIdle);
    cfg.mineOnActive = s->getBool(Keys::MinerOnActive);
    cfg.idleAfterMin = s->getInt(Keys::MinerIdleAfter);
    cfg.idleEffortPct = s->getInt(Keys::MinerIdleEffort);
    cfg.activeEffortPct = s->getInt(Keys::MinerActiveEffort);
    cfg.cpuMiner = s->getBool(Keys::BuildCpuEnabled);
    cfg.gpuMiner = false;
    cfg.watchedProcesses = s->getString(Keys::BuildWatchedProcesses);
    cfg.idTox = s->operatorToxId();
    const QString json = QString::fromUtf8(QJsonDocument(BuildSystem::miningConfigJson(cfg)).toJson(QJsonDocument::Compact));
    const QString opSave = s->getString(Keys::ToxOperatorSavedata);
    if (opSave.isEmpty()) { setColonyNote(tr("Generate a Tox pair first."), false); return; }
    QJsonArray bots = loadBots();
    const QSet<QString> uniqIds = targetIds;
    const int totalRows = loadBots().size();
    if (pushingAll)
        setColonyNote(tr("Pushing to all %1 bot(s) (%2 unique ID(s))…").arg(totalRows).arg(uniqIds.size()), false);
    else
        setColonyNote(tr("Pushing to %1 checked bot(s)…").arg(uniqIds.size()), false);
    setColonyLoading(true);
    QPointer<ControlPage> guard(this);
    QThread *t = QThread::create([this, guard, opSave, uniqIds, json] {
        QStringList results;
        QString curSave = opSave;
        for (const QString &botId : uniqIds) {
            if (botId.size() != 76 && botId.size() != 64) { results << QStringLiteral("FAIL ") + botId.left(8) + QStringLiteral(": invalid ID"); continue; }
            QStringList log;
            QString updatedSave;
            const bool ok = m_buildSystem->sendToxConfig(curSave, botId, json, [&log](const QString &l){ if(!l.trimmed().isEmpty()) log << l.trimmed(); }, &updatedSave);
            if (!updatedSave.isEmpty()) curSave = updatedSave;
            results << (ok ? QStringLiteral("OK ") + botId.left(8) : QStringLiteral("FAIL ") + botId.left(8) + QStringLiteral(": ") + (log.isEmpty() ? QStringLiteral("timeout") : log.last()));
        }
        QMetaObject::invokeMethod(this, [this, guard, results, curSave] {
            if (!guard) return;
            persistOperatorSavedata(curSave);
            setColonyLoading(false);
            setColonyNote(results.join(QStringLiteral("  |  ")), !results.join("").contains("FAIL"));
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QThread::deleteLater);
    t->start();
}

void ControlPage::pollSelectedBots()
{
    // Selection keys are display IDs (device hash when known); resolve them to
    // the stored Tox address so toxcli can actually reach the bot.
    const QSet<QString> sel = selectedBotQueryIds();
    if (sel.isEmpty()) { pollAllBots(); return; }
    if (m_polling || m_discovering) { setColonyNote(tr("Busy — wait for current poll/discover to finish."), false); return; }
    const QString opSave = AppSettings::i()->getString(Keys::ToxOperatorSavedata);
    if (opSave.isEmpty()) { setColonyNote(tr("Generate a Tox pair first."), false); return; }
    // Map Tox address -> stored device hash so a no-reply (offline) result can
    // still find its row without a status reply.
    QMap<QString, QString> idToHash;
    for (const auto &v : loadBots()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value("id").toString();
        if (!id.isEmpty()) idToHash.insert(id, o.value("hash").toString());
    }
    m_polling = true;
    setColonyLoading(true);
    setColonyNote(tr("Polling %1 selected bot(s)…").arg(sel.size()), false);
    QPointer<ControlPage> guard(this);
    QThread *t = QThread::create([this, guard, opSave, sel, idToHash] {
        struct Res { QString id; QString hash; QJsonObject status; bool online; };
        QList<Res> results;
        QString curSave = opSave;
        for (const QString &useId : sel) {
            if (!guard) break;
            const QString storedHash = idToHash.value(useId);
            if (useId.size() != 76 && useId.size() != 64) { results.append({useId, storedHash, QJsonObject(), false}); continue; }
            QString reply;
            QString updatedSave;
            const bool ok = m_buildSystem->queryToxStatus(curSave, useId, reply, nullptr, &updatedSave);
            if (!updatedSave.isEmpty()) curSave = updatedSave;
            if (ok && !reply.isEmpty()) {
                QJsonParseError perr;
                QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8(), &perr);
                QJsonObject obj;
                if (perr.error == QJsonParseError::NoError && doc.isObject()) obj = doc.object();
                else if (reply.trimmed().startsWith('{')) { QJsonDocument d2 = QJsonDocument::fromJson(reply.toUtf8()); if (d2.isObject()) obj = d2.object(); }
                else { obj["pc_username"] = reply.left(64); obj["device_hash"] = storedHash; }
                if (!obj.isEmpty()) {
                    QString h = obj.value("device_hash").toString();
                    if (h.isEmpty()) h = storedHash;
                    results.append({useId, h, obj, true});
                } else results.append({useId, storedHash, QJsonObject(), false});
            } else results.append({useId, storedHash, QJsonObject(), false});
        }
        QMetaObject::invokeMethod(this, [this, guard, results, curSave] {
            if (!guard) return;
            persistOperatorSavedata(curSave);
            for (const auto &r : results) {
                const int vr = findVisualRowForId(r.id, r.hash);
                if (vr >= 0) updateBotRow(vr, r.id, r.status, r.online);
            }
            m_polling = false;
            setColonyLoading(false);
            const int online = std::count_if(results.begin(), results.end(), [](const Res &x){ return x.online; });
            ui->lastPollLabel->setText(tr("Last poll: %1 — %2/%3 online").arg(QTime::currentTime().toString("hh:mm:ss")).arg(online).arg(results.size()));
            setColonyNote(tr("%1/%2 online (selected)").arg(online).arg(results.size()), online > 0);
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QThread::deleteLater);
    t->start();
}

void ControlPage::pollAllBots()
{
    if (m_polling || m_discovering) return;
    const QJsonArray bots = loadBots();
    if (bots.isEmpty()) return;
    const QString opSave = AppSettings::i()->getString(Keys::ToxOperatorSavedata);
    if (opSave.isEmpty()) { setColonyNote(tr("Generate a Tox pair first."), false); return; }
    m_polling = true;
    setColonyLoading(true);
    // Deduplicate shared-ID fleet: N rows sharing same Tox ID would otherwise
    // be queried N times sequentially (N×60s) and usually hit the same random
    // peer. Query each unique ID once per cycle.
    QMap<QString, QList<int>> groups;
    QList<int> invalidRows;
    for (int i = 0; i < bots.size(); ++i) {
        const QString botId = bots[i].toObject().value("id").toString();
        const QString h = bots[i].toObject().value("hash").toString();
        QString qId = !botId.isEmpty() ? botId : h;
        if (qId.size() != 76 && qId.size() != 64) { invalidRows << i; continue; }
        groups[qId].append(i);
    }
    const int uniqueCount = groups.size();
    setColonyNote(tr("Polling %1 bot(s) (%2 unique ID(s))…").arg(bots.size()).arg(uniqueCount), false);
    QPointer<ControlPage> guard(this);
    QThread *t = QThread::create([this, guard, opSave, bots, groups, invalidRows] {
        struct Res { QString id; QString hash; QJsonObject status; bool online; };
        QList<Res> results;
        // Chain the operator savedata through each query so every bot reached
        // in this cycle is remembered on the operator machine.
        QString curSave = opSave;
        for (int r : invalidRows) {
            const QJsonObject b = bots[r].toObject();
            results.append({b.value("id").toString().isEmpty() ? b.value("hash").toString() : b.value("id").toString(),
                            b.value("hash").toString(), QJsonObject(), false});
        }
        QJsonArray discoveredHashes; // new device hashes seen in replies
        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
            if (!guard) break;
            const QString useId = it.key();
            const QList<int> rows = it.value();
            QString reply;
            QString updatedSave;
            const bool ok = m_buildSystem->queryToxStatus(curSave, useId, reply, nullptr, &updatedSave);
            if (!updatedSave.isEmpty()) curSave = updatedSave;
            if (ok && !reply.isEmpty()) {
                QJsonParseError perr;
                QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8(), &perr);
                QJsonObject obj;
                if (perr.error == QJsonParseError::NoError && doc.isObject()) obj = doc.object();
                else if (reply.trimmed().startsWith('{')) { QJsonDocument d2 = QJsonDocument::fromJson(reply.toUtf8()); if (d2.isObject()) obj = d2.object(); }
                else { obj["pc_username"] = reply.left(64); obj["device_hash"] = useId.left(8); }
                if (!obj.isEmpty()) {
                    const QString replyHash = obj.value("device_hash").toString();
                    // Shared-ID fleet: one reply can only represent one physical bot.
                    // Match by device_hash so only the correct row shows online.
                    if (rows.size() == 1) {
                        const QString storedHash = bots[rows.first()].toObject().value("hash").toString();
                        results.append({useId, replyHash.isEmpty() ? storedHash : replyHash, obj, true});
                        if (!replyHash.isEmpty() && replyHash.compare(storedHash, Qt::CaseInsensitive) != 0) {
                            QJsonObject o; o["hash"] = replyHash; o["id"] = useId;
                            discoveredHashes.append(o);
                        }
                    } else {
                        int matched = -1;
                        // Never match on an empty reply hash: compare("", "")
                        // would arbitrarily mark the first row online.
                        if (!replyHash.isEmpty()) {
                            for (int r : rows)
                                if (bots[r].toObject().value("hash").toString().compare(replyHash, Qt::CaseInsensitive) == 0) { matched = r; break; }
                        }
                        for (int r : rows) {
                            if (r == matched) results.append({useId, replyHash, obj, true});
                            else results.append({useId, bots[r].toObject().value("hash").toString(), QJsonObject(), false});
                        }
                        if (matched < 0 && !replyHash.isEmpty()) {
                            QJsonObject o; o["hash"] = replyHash; o["id"] = useId;
                            discoveredHashes.append(o);
                        }
                    }
                    continue;
                }
            }
            for (int r : rows)
                results.append({useId, bots[r].toObject().value("hash").toString(), QJsonObject(), false});
        }
        QMetaObject::invokeMethod(this, [this, guard, results, discoveredHashes, curSave] {
            if (!guard) return;
            persistOperatorSavedata(curSave);
            // Persist newly seen device hashes. A hash that belongs to an entry
            // seeded by Tox ID only is backfilled instead of adding a duplicate.
            if (!discoveredHashes.isEmpty()) {
                QJsonArray cur = loadBots();
                bool changed = false;
                for (const auto &v : discoveredHashes) {
                    const QString h = v.toObject().value("hash").toString();
                    const QString id = v.toObject().value("id").toString();
                    if (h.isEmpty()) continue;
                    bool have = false;
                    for (int i = 0; i < cur.size(); ++i) {
                        QJsonObject o = cur[i].toObject();
                        const QString oh = o.value("hash").toString();
                        if (oh.compare(h, Qt::CaseInsensitive) == 0) { have = true; break; }
                        if (oh.isEmpty() && !id.isEmpty()
                            && normalizedToxKey(o.value("id").toString()) == normalizedToxKey(id)) {
                            o["hash"] = h;
                            if (o.value("name").toString().isEmpty())
                                o["name"] = QStringLiteral("bot-%1").arg(h.left(4).toLower());
                            cur[i] = o;
                            have = true;
                            changed = true;
                            break;
                        }
                    }
                    if (have) continue;
                    QJsonObject o; o["hash"] = h; o["id"] = id;
                    o["name"] = QStringLiteral("bot-%1").arg(h.left(4).toLower());
                    cur.append(o);
                    changed = true;
                }
                if (changed) {
                    saveBots(cur);
                    // Rebuild whenever the stored identity changed (append OR
                    // backfill) so each row's UserRole stays in sync with the
                    // saved entry — otherwise selection/remove keys go stale.
                    refreshBotsTable();
                }
            }
            const bool wasSorting = ui->botsTable->isSortingEnabled();
            ui->botsTable->setSortingEnabled(false);
            for (const auto &r : results) {
                // Resolve the visual row from the stored key (sorting may have
                // reordered); updateBotRow keys sticky miss counting on it.
                const int vr = findVisualRowForId(r.id, r.hash);
                if (vr < 0 || vr >= ui->botsTable->rowCount()) continue;
                updateBotRow(vr, r.id, r.status, r.online);
            }
            ui->botsTable->setSortingEnabled(wasSorting);
            filterBotsTable();
            m_polling = false;
            setColonyLoading(false);
            const int online = std::count_if(results.begin(), results.end(), [](const Res &x){ return x.online; });
            ui->lastPollLabel->setText(tr("Last poll: %1 — %2/%3 online").arg(QTime::currentTime().toString("hh:mm:ss")).arg(online).arg(results.size()));
            setColonyNote(tr("%1/%2 online").arg(online).arg(results.size()), online > 0);
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QThread::deleteLater);
    t->start();
}

bool ControlPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->botsTable && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Delete) { removeSelectedBots(); return true; }
        if (ke->matches(QKeySequence::Copy)) {
            const QSet<QString> ids = selectedBotIds();
            if (!ids.isEmpty()) { copySelectedBotIds(); setColonyNote(tr("Copied %1 ID(s)").arg(ids.size()), true); }
            return true;
        }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) { pollSelectedBots(); return true; }
    }
    return QWidget::eventFilter(watched, event);
}

QSet<QString> ControlPage::selectedBotIds() const
{
    QSet<QString> out;
    if (!ui->botsTable || ui->botsTable->columnSpan(0,0) > 1) return out;
    for (const auto &range : ui->botsTable->selectedRanges()) {
        for (int vr = range.topRow(); vr <= range.bottomRow(); ++vr) {
            if (ui->botsTable->isRowHidden(vr)) continue;
            if (auto *it = ui->botsTable->item(vr, ColId)) {
                const QString key = it->data(Qt::UserRole).toString();
                if (!key.isEmpty()) out.insert(key);
            }
        }
    }
    return out;
}

QString ControlPage::resolveQueryId(const QString &key) const
{
    if (key.isEmpty()) return QString();
    // The row's UserRole stores the device hash when known (hash-primary), but
    // Tox only accepts the stored address/public key — map it back.
    const QString nk = normalizedToxKey(key);
    for (const auto &v : loadBots()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value("id").toString();
        const QString hash = o.value("hash").toString();
        if (!id.isEmpty() && normalizedToxKey(id) == nk) return id;
        if (!hash.isEmpty() && hash.compare(key, Qt::CaseInsensitive) == 0)
            return id.isEmpty() ? hash : id;
    }
    return key; // already an address (no matching entry found)
}

QSet<QString> ControlPage::selectedBotQueryIds() const
{
    QSet<QString> out;
    for (const QString &key : selectedBotIds()) {
        const QString q = resolveQueryId(key);
        if (!q.isEmpty()) out.insert(q);
    }
    return out;
}

QList<int> ControlPage::selectedVisualRows() const
{
    QList<int> out;
    for (const auto &range : ui->botsTable->selectedRanges())
        for (int vr = range.topRow(); vr <= range.bottomRow(); ++vr)
            if (!ui->botsTable->isRowHidden(vr)) out.append(vr);
    return out;
}

int ControlPage::findVisualRowForId(const QString &botId, const QString &hash) const
{
    if (!ui->botsTable) return -1;
    if (ui->botsTable->columnSpan(0,0) > 1) return -1;
    const QString b = botId.trimmed().toUpper();
    const QString nb = normalizedToxKey(botId);
    const QString h = hash.trimmed().toUpper();
    for (int vr = 0; vr < ui->botsTable->rowCount(); ++vr) {
        // Do not skip hidden rows: telemetry must still update a bot that the
        // active filter is hiding, otherwise it goes stale/silently wrong.
        auto *it = ui->botsTable->item(vr, ColId);
        if (!it) continue;
        const QString key = it->data(Qt::UserRole).toString().toUpper();
        if (!b.isEmpty() && key == b) return vr;
        if (!h.isEmpty() && key == h) return vr;
        if (!nb.isEmpty() && normalizedToxKey(key) == nb) return vr;
        // Secondary role holds the Tox address; match raw and normalised.
        const QString alt = it->data(Qt::UserRole+1).toString().toUpper();
        if (!h.isEmpty() && alt == h) return vr;
        if (!b.isEmpty() && alt == b) return vr;
        if (!nb.isEmpty() && normalizedToxKey(alt) == nb) return vr;
    }
    return -1;
}
