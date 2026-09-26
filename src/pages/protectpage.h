#pragma once

#include <QWidget>
#include <QProcess>
#include <QVector>
#include <QHash>
#include <QElapsedTimer>

class QCheckBox;
class QPushButton;
class QLabel;
class QProgressBar;
class QListWidget;
class QTimer;
class FlaskWidget;

class CryptPage : public QWidget
{
    Q_OBJECT
public:
    explicit CryptPage(QWidget *parent = nullptr);

    // Exposes the flask animation widget so the pet can orbit it during crypt.
    QWidget *flaskWidget() const;

signals:
    void cryptStarted();
    void cryptSucceeded();
    void cryptFailed();

protected:
    void showEvent(QShowEvent *e) override;

private slots:
    void crypt();
    void cancelCrypt();
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onHeartbeat();

private:
    void refreshProtectedList();
    void setBusy(bool busy);
    void refreshAnchorPreview();
    QString phaseHintText() const;

    // Given a resolved (name, rva) pair, returns the human-readable label
    // shown in the UI ("Persistence::AddToStartup" for "bnr_a02", etc.).
    static QString friendlyName(const QString &exportName);

    QString resolvePresetRvas(const QString &pePath, QVector<quint32> &rvas,
                              QVector<QPair<QString, quint32>> *labelled = nullptr) const;
    int loadMapNames(const QString &pePath, QHash<quint32, QString> &names,
                     QVector<quint32> &extraRvas) const;

    QString         m_targetPath;
    QCheckBox      *m_antiDebug   = nullptr;
    QPushButton    *m_crypt       = nullptr;
    QPushButton    *m_openOutput  = nullptr;
    QLabel         *m_status      = nullptr;
    QLabel         *m_phaseLabel  = nullptr;
    QLabel         *m_anchorHdr   = nullptr;
    QListWidget    *m_anchorList  = nullptr;
    QProgressBar   *m_progress    = nullptr;
    FlaskWidget    *m_flask       = nullptr;
    int             m_totalFunctions = 0;
    int             m_currentFunction = 0;

    QProcess m_proc;
    QString  m_protectorPath;
    QString  m_pendingOutput;

    // Heartbeat + phase tracking so the UI keeps informing the user during
    // long-running phases (linking / encrypting can take minutes with no
    // per-function progress markers from bvm).
    QTimer         *m_heartbeat = nullptr;
    QElapsedTimer   m_runClock;
    qint64          m_lastOutputMs = 0;
    QString         m_currentPhase;
    bool            m_userCancelled = false;
};