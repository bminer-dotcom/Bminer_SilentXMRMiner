#include "pages/protectpage.h"
#include "buildsystem.h"
#include "theme.h"
#include "widgets/flaskwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QShowEvent>
#include <QTextStream>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QTimer>
#include <QSet>
#include <algorithm>

namespace {

// Resolution strategy:
//   1. Every function we want to virtualize has a `bvm_*` thunk in the
//      client's src/exports_thunks.cpp, dllexport'd so it lands in the PE
//      export directory. We walk that directory and take every export
//      whose name starts with "bvm_" as an RVA to virtualize.
//   2. main / WinMain cannot be exported (they are the EXE entry point),
//      so those two still come from the .map file if it is present.

// Deliberately opaque prefix - see comment in src/exports_thunks.cpp.
const QByteArray kExportPrefix = QByteArrayLiteral("bnr_");

const QStringList kMapExact = {
    QStringLiteral("main"),
    QStringLiteral("WinMain"),
};

QString phaseDisplayName(const QString &phase)
{
    if (phase == QLatin1String("loading"))      return QStringLiteral("Loading binary...");
    if (phase == QLatin1String("analyzing"))    return QStringLiteral("Analyzing code...");
    if (phase == QLatin1String("virtualizing")) return QStringLiteral("Virtualizing functions...");
    if (phase == QLatin1String("linking"))      return QStringLiteral("Linking VM sections...");
    if (phase == QLatin1String("encrypting"))   return QStringLiteral("Encrypting strings...");
    if (phase == QLatin1String("writing"))      return QStringLiteral("Writing output...");
    return phase;
}

} // namespace

CryptPage::CryptPage(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto *header = new QLabel(QStringLiteral("BVM — crypt the client build"), this);
    header->setStyleSheet(QStringLiteral("color:%1;font-size:13px;font-weight:600;")
                              .arg(Theme::Text.name()));
    root->addWidget(header);

    m_targetPath = QDir::cleanPath(
        BuildSystem::projectRoot() + QStringLiteral("/Client/build/bminer.exe"));
    {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(QStringLiteral("Target:"), this));
        auto *tgt = new QLabel(QDir::toNativeSeparators(m_targetPath), this);
        tgt->setTextInteractionFlags(Qt::TextSelectableByMouse);
        tgt->setStyleSheet(QStringLiteral("color:#8ab;"));
        row->addWidget(tgt, 1);
        root->addLayout(row);
    }

    // Functions to protect list
    m_anchorHdr = new QLabel(QStringLiteral("Functions to protect:"), this);
    m_anchorHdr->setStyleSheet(QStringLiteral("color:%1;font-size:11px;font-weight:600;")
                                   .arg(Theme::Text.name()));
    root->addWidget(m_anchorHdr);

    m_anchorList = new QListWidget(this);
    m_anchorList->setMaximumHeight(120);
    m_anchorList->setStyleSheet(QStringLiteral(
                                    "QListWidget { background: %1; border: 1px solid %2; border-radius: 4px;"
                                    "color: %3; font-size: 11px; padding: 2px; }"
                                    "QListWidget::item { padding: 2px; }"
                                    "QListWidget::item:selected { background: %4; }")
                                    .arg(Theme::Surface.name(), Theme::Border.name(),
                                         Theme::Text.name(), Theme::Teal.name()));
    root->addWidget(m_anchorList);

    m_antiDebug = new QCheckBox(QStringLiteral("Anti-debug"), this);
    root->addWidget(m_antiDebug);

    m_crypt = new QPushButton(QStringLiteral("Crypt"), this);
    m_crypt->setMinimumHeight(34);
    connect(m_crypt, &QPushButton::clicked, this, [this] {
        // Same button doubles as Cancel once a run is in flight, so a stuck
        // linking/encrypting phase can be aborted without killing the app.
        if (m_proc.state() != QProcess::NotRunning)
            cancelCrypt();
        else
            crypt();
    });
    root->addWidget(m_crypt);

    // Shown only after a successful run, so the output folder is one click away.
    m_openOutput = new QPushButton(QStringLiteral("Open output folder"), this);
    m_openOutput->setMinimumHeight(30);
    m_openOutput->setProperty("ghost", true);
    m_openOutput->hide();
    connect(m_openOutput, &QPushButton::clicked, this, [this] {
        if (m_pendingOutput.isEmpty())
            return;
        const QString dir = QFileInfo(m_pendingOutput).absolutePath();
        if (QFileInfo::exists(m_pendingOutput))
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    root->addWidget(m_openOutput);

    // Flask animation — vector-drawn Erlenmeyer with wavy liquid + rising bubbles.
    // Idle: sits empty. Playback (fill + bubbles) starts on Crypt.
    m_flask = new FlaskWidget(this);
    m_flask->setFixedSize(180, 200);
    {
        auto *row = new QHBoxLayout();
        row->addStretch(1);
        row->addWidget(m_flask);
        row->addStretch(1);
        root->addLayout(row);
    }

    m_phaseLabel = new QLabel(QStringLiteral("Ready"), this);
    m_phaseLabel->setAlignment(Qt::AlignCenter);
    m_phaseLabel->setStyleSheet(QStringLiteral("color:%1;font-size:13px;font-weight:600;")
                                    .arg(Theme::Text.name()));
    root->addWidget(m_phaseLabel);

    m_progress = new QProgressBar(this);
    m_progress->setMinimum(0);
    m_progress->setMaximum(1000);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    m_progress->setStyleSheet(QStringLiteral(
                                  "QProgressBar { background: %1; border: none; border-radius: 3px; }"
                                  "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
                                  "stop:0 %2, stop:0.5 %3, stop:1 %4); border-radius: 3px; }")
                                  .arg(Theme::Surface.name(), Theme::Teal.name(),
                                       Theme::Purple.name(), Theme::Pink.name()));
    root->addWidget(m_progress);

    m_status = new QLabel(QStringLiteral("ready"), this);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setStyleSheet(Theme::styleFor(Theme::TextRole::Dim));
    root->addWidget(m_status);

    // resolve bvm.exe
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        dir + "/bvm.exe",
        dir + "/bvm/bvm.exe",
        dir + "/../bvm/bvm.exe",
        dir + "/../../bvm/bvm.exe",
    };
    for (const QString &c : candidates)
        if (QFileInfo::exists(c)) { m_protectorPath = QDir::cleanPath(c); break; }

    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &CryptPage::onReadyRead);
    connect(&m_proc, &QProcess::readyReadStandardError,  this, &CryptPage::onReadyRead);
    connect(&m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &CryptPage::onFinished);

    // Ticks once a second while a run is in flight, so the user sees the
    // elapsed time advance even during phases where bvm emits no lines
    // (linking, encrypting) — otherwise the UI looks frozen for minutes.
    m_heartbeat = new QTimer(this);
    m_heartbeat->setInterval(1000);
    connect(m_heartbeat, &QTimer::timeout, this, &CryptPage::onHeartbeat);

    if (m_protectorPath.isEmpty())
        m_status->setText(QStringLiteral("bvm.exe not found"));

    // Initial refresh of the protected functions list
    refreshProtectedList();
}

QWidget *CryptPage::flaskWidget() const
{
    return m_flask;
}

void CryptPage::setBusy(bool busy)
{
    // Button stays enabled while busy so it can act as Cancel — otherwise a
    // stuck bvm run leaves the user with nothing to click.
    m_crypt->setEnabled(true);
    m_crypt->setText(busy ? tr("Cancel") : tr("Crypt"));
}

void CryptPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    refreshProtectedList();
}

void CryptPage::refreshProtectedList()
{
    if (!m_anchorList) return;

    m_anchorList->clear();

    const QString input = m_targetPath;
    if (!QFileInfo::exists(input)) {
        m_anchorList->addItem(QStringLiteral("⚠ Target not found: %1")
                                  .arg(QDir::toNativeSeparators(input)));
        if (m_anchorHdr) m_anchorHdr->setText(QStringLiteral("Functions to protect: (none)"));
        return;
    }

    QVector<quint32> rvas;
    QVector<QPair<QString, quint32>> details;
    const QString matched = resolvePresetRvas(input, rvas, &details);

    if (rvas.isEmpty()) {
        m_anchorList->addItem(QStringLiteral("⚠ No functions found - rebuild client first"));
        if (m_anchorHdr) m_anchorHdr->setText(QStringLiteral("Functions to protect: (none)"));
        return;
    }

    // Update header with count
    if (m_anchorHdr)
        m_anchorHdr->setText(QStringLiteral("Functions to protect: %1").arg(rvas.size()));

    // Populate list with verification info
    for (const auto &fn : details) {
        m_anchorList->addItem(QStringLiteral("%1  [0x%2]")
                                  .arg(fn.first)
                                  .arg(fn.second, 0, 16));
    }
}

void CryptPage::refreshAnchorPreview()
{
    refreshProtectedList();
}

QString CryptPage::friendlyName(const QString &exportName)
{
    // Remove the opaque prefix
    QString clean = exportName;
    if (clean.startsWith(QString::fromLatin1(kExportPrefix)))
        clean = clean.mid(kExportPrefix.size());

    // Map known export names to friendly descriptions
    static const QHash<QString, QString> nameMap = {
                                                     {"main", "main / WinMain"},
                                                     {"WinMain", "main / WinMain"},
                                                     // Add more mappings as you identify your thunk functions
                                                     // e.g., {"a02", "Persistence::AddToStartup"},
                                                     };

    if (nameMap.contains(clean))
        return nameMap.value(clean);

    // If no mapping exists, show the cleaned name
    return clean;
}

// ---------------------------------------------------------------- .map parsing

int CryptPage::loadMapNames(const QString &pePath, QHash<quint32, QString> &names,
                            QVector<quint32> &extraRvas) const
{
    QFileInfo fi(pePath);
    const QString mapPath = fi.absolutePath() + "/" + fi.completeBaseName() + ".map";
    QFile f(mapPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;

    QTextStream in(&f);
    const QString all = in.readAll();
    f.close();

    int loaded = 0;

    if (all.contains(QStringLiteral("Preferred load address is"))) {
        quint64 base = 0;
        for (const QString &line : all.split(QLatin1Char('\n'))) {
            if (base == 0) {
                const int idx = line.indexOf(QStringLiteral("Preferred load address is"));
                if (idx >= 0) {
                    bool ok = false;
                    const quint64 v = line.mid(idx + 26).trimmed().toULongLong(&ok, 16);
                    if (ok) base = v;
                    continue;
                }
            }
            const QString t = line.trimmed();
            if (t.size() < 14 || t[4] != ':') continue;
            const QStringList parts = t.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (parts.size() < 3) continue;
            const QString name = parts[1];
            quint64 va = 0; bool haveVa = false;
            for (int i = 2; i < parts.size(); i++) {
                if (parts[i].size() == 16) {
                    bool ok = false;
                    const quint64 v = parts[i].toULongLong(&ok, 16);
                    if (ok) { va = v; haveVa = true; break; }
                }
            }
            if (!haveVa || base == 0 || va < base) continue;
            const quint32 rva = static_cast<quint32>(va - base);
            if (!names.contains(rva)) {
                names.insert(rva, name);
                extraRvas.push_back(rva);
                loaded++;
            }
        }
        return loaded;
    }

    static const QRegularExpression re(QStringLiteral("^\\s+0x([0-9a-fA-F]+)\\s+(\\S+)\\s*$"));
    struct Sym { quint64 addr; QString name; };
    QVector<Sym> syms;
    quint64 minAddr = 0;

    for (const QString &line : all.split(QLatin1Char('\n'))) {
        const auto m = re.match(line);
        if (!m.hasMatch()) continue;
        bool ok = false;
        const quint64 addr = m.captured(1).toULongLong(&ok, 16);
        QString name = m.captured(2);
        if (!ok || addr == 0 || name.isEmpty()) continue;
        if (name.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) continue;
        syms.push_back({ addr, name });
        if (minAddr == 0 || addr < minAddr) minAddr = addr;
    }
    if (syms.isEmpty() || minAddr == 0) return 0;
    const quint64 base = minAddr & ~0xFFFFULL;

    for (const Sym &s : syms) {
        if (s.addr < base) continue;
        const quint32 rva = static_cast<quint32>(s.addr - base);
        if (!names.contains(rva)) {
            names.insert(rva, s.name);
            extraRvas.push_back(rva);
            loaded++;
        }
    }
    return loaded;
}

// Walk the PE export directory of `pePath` and collect (name, RVA) for every
// export whose name starts with `prefix`. Returns the number of matches.
static int loadExportRvas(const QString &pePath, const QByteArray &prefix,
                          QHash<QString, quint32> &out)
{
    QFile f(pePath);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const QByteArray blob = f.readAll();
    f.close();
    const uchar *base = reinterpret_cast<const uchar *>(blob.constData());
    const qsizetype size = blob.size();
    if (size < 0x40) return 0;

    auto read16 = [&](qsizetype off) -> quint16 {
        if (off < 0 || off + 2 > size) return 0;
        return quint16(base[off]) | (quint16(base[off + 1]) << 8);
    };
    auto read32 = [&](qsizetype off) -> quint32 {
        if (off < 0 || off + 4 > size) return 0;
        return  quint32(base[off])
               | (quint32(base[off + 1]) << 8)
               | (quint32(base[off + 2]) << 16)
               | (quint32(base[off + 3]) << 24);
    };

    // DOS -> PE header
    if (read16(0) != 0x5A4D) return 0;                              // "MZ"
    const quint32 e_lfanew = read32(0x3C);
    if (read32(e_lfanew) != 0x00004550) return 0;                   // "PE\0\0"

    const qsizetype coff = e_lfanew + 4;
    const quint16 nSect = read16(coff + 2);
    const quint16 optSize = read16(coff + 16);
    const qsizetype opt = coff + 20;
    if (opt + optSize > size) return 0;

    const quint16 magic = read16(opt);
    const bool is64 = (magic == 0x20B);
    const qsizetype ddOff = is64 ? opt + 112 : opt + 96;            // DataDirectory[0] = Export
    const quint32 expRva  = read32(ddOff + 0);
    const quint32 expSize = read32(ddOff + 4);
    if (!expRva || !expSize) return 0;

    // Section table follows the optional header
    const qsizetype sectOff = opt + optSize;
    auto rvaToFile = [&](quint32 rva) -> qsizetype {
        for (int i = 0; i < nSect; ++i) {
            const qsizetype sh = sectOff + i * 40;
            const quint32 vsz = read32(sh + 8);
            const quint32 vrv = read32(sh + 12);
            const quint32 rsz = read32(sh + 16);
            const quint32 rpt = read32(sh + 20);
            const quint32 vend = vrv + qMax(vsz, rsz);
            if (rva >= vrv && rva < vend) return qsizetype(rpt + (rva - vrv));
        }
        return -1;
    };

    const qsizetype expFile = rvaToFile(expRva);
    if (expFile < 0 || expFile + 40 > size) return 0;

    const quint32 nNames  = read32(expFile + 24);
    const quint32 nFuncs  = read32(expFile + 20);
    const quint32 fnsRva  = read32(expFile + 28);
    const quint32 nmsRva  = read32(expFile + 32);
    const quint32 ordsRva = read32(expFile + 36);
    const quint32 ordBase = read32(expFile + 16);

    const qsizetype fnsFile  = rvaToFile(fnsRva);
    const qsizetype nmsFile  = rvaToFile(nmsRva);
    const qsizetype ordsFile = rvaToFile(ordsRva);
    if (fnsFile < 0 || nmsFile < 0 || ordsFile < 0) return 0;

    int hits = 0;
    for (quint32 i = 0; i < nNames; ++i) {
        const quint32 nameRva = read32(nmsFile + i * 4);
        const qsizetype nameFile = rvaToFile(nameRva);
        if (nameFile < 0 || nameFile >= size) continue;

        // Bounded strlen
        qsizetype end = nameFile;
        while (end < size && base[end]) ++end;
        const QByteArray name(reinterpret_cast<const char *>(base + nameFile),
                              int(end - nameFile));
        if (!name.startsWith(prefix)) continue;

        const quint16 ord = read16(ordsFile + i * 2);
        if (ord >= nFuncs) continue;
        const quint32 funcRva = read32(fnsFile + ord * 4);
        if (!funcRva) continue;
        // Skip forwarders (funcRva points inside the export directory).
        if (funcRva >= expRva && funcRva < expRva + expSize) continue;

        out.insert(QString::fromLatin1(name), funcRva);
        ++hits;
    }
    (void)ordBase;
    return hits;
}

QString CryptPage::resolvePresetRvas(const QString &pePath, QVector<quint32> &rvas,
                                     QVector<QPair<QString, quint32>> *labelled) const
{
    QStringList matched;
    rvas.clear();
    if (labelled) labelled->clear();

    // Track RVAs and labels we've already added so the UI list can't show
    // the same function twice (e.g. main appearing both as a bnr_ export
    // and again from the .map file, or two aliases that collapse to the
    // same friendly label like "main / WinMain").
    QSet<quint32> seenRva;
    QSet<QString> seenLabel;

    auto addOne = [&](const QString &rawName, quint32 rva) {
        if (seenRva.contains(rva))
            return;
        const QString label = friendlyName(rawName);
        // Same RVA with a different label still counts as a duplicate visually.
        if (seenLabel.contains(label))
            return;
        seenRva.insert(rva);
        seenLabel.insert(label);
        rvas.push_back(rva);
        matched << rawName;
        if (labelled)
            labelled->push_back({label, rva});
    };

    // 1) Export table: everything with the bnr_ prefix.
    QHash<QString, quint32> exports;
    loadExportRvas(pePath, kExportPrefix, exports);

    // Iterate in a stable, sorted order so the list is deterministic
    // (QHash iteration order is unspecified).
    QStringList exportNames = exports.keys();
    std::sort(exportNames.begin(), exportNames.end());
    for (const QString &name : exportNames)
        addOne(name, exports.value(name));

    // 2) .map fallback for symbols that can't be exported (main / WinMain).
    QHash<quint32, QString> names;
    QVector<quint32> all;
    loadMapNames(pePath, names, all);
    for (const quint32 rva : all) {
        const QString &name = names.value(rva);
        for (const QString &e : kMapExact) {
            if (name == e) {
                addOne(name, rva);
                break;
            }
        }
    }

    std::sort(rvas.begin(), rvas.end());
    rvas.erase(std::unique(rvas.begin(), rvas.end()), rvas.end());
    return matched.join(QStringLiteral(", "));
}

// ---------------------------------------------------------------- process

void CryptPage::crypt()
{
    if (m_protectorPath.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("BVM"),
                             QStringLiteral("BVM protector exe not found. Place bvm.exe next to the app."));
        return;
    }
    const QString input = m_targetPath;
    if (!QFileInfo::exists(input)) {
        QMessageBox::warning(this, QStringLiteral("BVM"),
                             QStringLiteral("Target not found:\n%1").arg(QDir::toNativeSeparators(input)));
        return;
    }

    // Resolve and verify protected functions
    QVector<quint32> rvas;
    QVector<QPair<QString, quint32>> details;
    const QString matched = resolvePresetRvas(input, rvas, &details);

    if (rvas.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("BVM"),
                             QStringLiteral("No bnr_* exports found in the target and no main/WinMain\n"
                                            "resolved from bminer.map. Rebuild the client with the current\n"
                                            "CMakeLists (exports_thunks.cpp must be in SRC_FILES) so the\n"
                                            "PE export directory carries the bnr_* anchors."));
        return;
    }

    // Refresh UI to show exactly what we're protecting
    refreshProtectedList();

    // Build args with verification logging
    QStringList args; args << input;

    // Log verification info
    QString verifyMsg = QStringLiteral("Protecting %1 function(s):").arg(rvas.size());
    for (const auto &fn : details) {
        verifyMsg += QStringLiteral("\n  • %1 @ RVA 0x%2")
                         .arg(fn.first)
                         .arg(fn.second, 0, 16);
    }
    m_status->setText(verifyMsg);

    // Add RVAs to args - ONLY these verified functions
    for (const quint32 rva : rvas) {
        args << QStringLiteral("--protect-rva")
        << QStringLiteral("0x%1").arg(rva, 0, 16);
    }

    if (m_antiDebug->isChecked()) args << QStringLiteral("--anti-debug");

    const QFileInfo fi(input);
    m_pendingOutput = fi.absolutePath() + "/" + fi.completeBaseName() + "_crypted." + fi.suffix();
    args << QStringLiteral("--out") << m_pendingOutput;

    m_phaseLabel->setText(QStringLiteral("Loading binary..."));
    m_progress->setValue(0);
    m_totalFunctions = rvas.size();
    m_currentFunction = 0;
    m_currentPhase = QStringLiteral("loading");
    m_userCancelled = false;
    m_openOutput->hide();

    m_flask->setPhase(QStringLiteral("loading"));
    m_flask->setProgress(0.05);
    m_flask->setActive(true);

    // Start the clock + heartbeat *before* launching bvm so a first-line
    // hang (e.g. bvm can't open the PE) still surfaces elapsed time.
    m_runClock.start();
    m_lastOutputMs = 0;
    m_heartbeat->start();

    setBusy(true);
    emit cryptStarted();
    m_proc.setWorkingDirectory(fi.absolutePath());
    m_proc.start(m_protectorPath, args);
}

void CryptPage::cancelCrypt()
{
    if (m_proc.state() == QProcess::NotRunning)
        return;
    m_userCancelled = true;
    m_status->setText(tr("cancelling..."));
    // Ask nicely, then force. bvm has no signal handler so terminate() usually
    // no-ops on Windows; kill() is the reliable path but we give it a beat first.
    m_proc.terminate();
    QTimer::singleShot(1500, this, [this] {
        if (m_proc.state() != QProcess::NotRunning)
            m_proc.kill();
    });
}

void CryptPage::onHeartbeat()
{
    if (m_proc.state() == QProcess::NotRunning)
        return;

    const qint64 nowMs = m_runClock.elapsed();
    const qint64 sinceOutputMs = nowMs - m_lastOutputMs;
    const int elapsedSec = int(nowMs / 1000);

    QString hint = phaseHintText();
    QString line = tr("%1  •  %2s elapsed").arg(hint).arg(elapsedSec);
    // After 15s of radio silence during a known-slow phase, add a "still
    // working" note so the user knows the UI hasn't frozen.
    if (sinceOutputMs > 15000)
        line += tr("  •  still working (bvm has no per-op progress here)");
    m_status->setText(line);
}

QString CryptPage::phaseHintText() const
{
    // Human-readable expectation per phase, so the elapsed-time line makes
    // sense even when bvm goes quiet for a long stretch.
    if (m_currentPhase == QLatin1String("loading"))
        return tr("loading PE");
    if (m_currentPhase == QLatin1String("analyzing"))
        return tr("analysing code");
    if (m_currentPhase == QLatin1String("virtualizing"))
        return tr("virtualising %1/%2")
            .arg(m_currentFunction).arg(qMax(1, m_totalFunctions));
    if (m_currentPhase == QLatin1String("linking"))
        return tr("linking VM sections");
    if (m_currentPhase == QLatin1String("encrypting"))
        return tr("encrypting strings");
    if (m_currentPhase == QLatin1String("writing"))
        return tr("writing output");
    return m_currentPhase.isEmpty() ? tr("working") : m_currentPhase;
}

void CryptPage::onReadyRead()
{
    const QByteArray raw = m_proc.readAllStandardOutput() + m_proc.readAllStandardError();
    const QString text = QString::fromLocal8Bit(raw);

    // Any output from bvm counts as a "still alive" heartbeat, so the
    // stalled-phase warning only kicks in during true radio silence.
    if (m_runClock.isValid())
        m_lastOutputMs = m_runClock.elapsed();

    for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();

        if (trimmed.startsWith(QLatin1String("@PHASE:"))) {
            const QString ph = trimmed.mid(7);
            m_currentPhase = ph;
            m_phaseLabel->setText(phaseDisplayName(ph));
            if (m_flask) m_flask->setPhase(ph);
        }
        else if (trimmed.startsWith(QLatin1String("@TOTAL:"))) {
            bool ok = false;
            const int tot = trimmed.mid(7).toInt(&ok);
            if (ok && tot >= 0)
                m_totalFunctions = tot;
        }
        else if (trimmed.startsWith(QLatin1String("@PROGRESS:"))) {
            const auto parts = trimmed.mid(10).split('/');
            if (parts.size() == 2) {
                bool ok1 = false, ok2 = false;
                int cur = parts[0].toInt(&ok1);
                int tot = parts[1].toInt(&ok2);
                if (ok1 && ok2 && tot > 0) {
                    m_currentFunction = cur;
                    m_totalFunctions = tot;
                    int mil = qBound(0, cur * 1000 / tot, 990);
                    m_progress->setValue(mil);
                    if (m_flask) m_flask->setProgress((qreal)cur / tot);
                }
            }
        }
        else if (trimmed == QLatin1String("@DONE")) {
            m_progress->setValue(1000);
            if (m_flask) m_flask->setProgress(1.0);
        }
    }
}

void CryptPage::onFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_heartbeat) m_heartbeat->stop();
    setBusy(false);

    const int elapsedSec = m_runClock.isValid() ? int(m_runClock.elapsed() / 1000) : 0;

    if (m_userCancelled) {
        m_phaseLabel->setText(tr("Cancelled"));
        m_status->setText(tr("cancelled after %1s").arg(elapsedSec));
        m_progress->setValue(0);
        if (m_flask) { m_flask->setPhase(QStringLiteral("failed")); m_flask->setActive(false); }
        emit cryptFailed();
        return;
    }

    if (status == QProcess::CrashExit) {
        m_phaseLabel->setText(QStringLiteral("Protection failed"));
        m_status->setText(tr("failed (crash) after %1s").arg(elapsedSec));
        m_progress->setValue(0);
        if (m_flask) { m_flask->setPhase(QStringLiteral("failed")); m_flask->setActive(false); }
        emit cryptFailed();
        return;
    }
    if (exitCode == 0) {
        m_progress->setValue(1000);
        m_phaseLabel->setText(QStringLiteral("Done ✓"));
        m_status->setText(tr("done in %1s: %2").arg(elapsedSec).arg(m_pendingOutput));
        m_openOutput->show();
        if (m_flask) {
            m_flask->setPhase(QStringLiteral("done"));
            m_flask->setProgress(1.0);
            // let the "done" state show for a moment, then reset next time crypt runs.
        }
        emit cryptSucceeded();
    } else {
        m_phaseLabel->setText(QStringLiteral("Protection failed"));
        m_status->setText(tr("failed (exit %1) after %2s").arg(exitCode).arg(elapsedSec));
        m_progress->setValue(0);
        if (m_flask) { m_flask->setPhase(QStringLiteral("failed")); m_flask->setActive(false); }
        emit cryptFailed();
    }
}