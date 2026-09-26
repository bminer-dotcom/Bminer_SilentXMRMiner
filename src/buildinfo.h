#pragma once

#include <QString>
#include <QSysInfo>
#include <QtGlobal>

#ifndef APP_VERSION
#  define APP_VERSION "1.0.0"
#endif

namespace BuildInfo {

inline QString version()      { return QStringLiteral(APP_VERSION); }
inline QString codename()     { return QStringLiteral("Lysogeny"); }
inline QString tagline()      { return QStringLiteral("what doesn't kill you mutates and tries again"); }

inline QString buildDate()    { return QStringLiteral(__DATE__); }
inline QString buildTime()    { return QStringLiteral(__TIME__); }

inline QString buildType()
{
#ifdef QT_DEBUG
    return QStringLiteral("Debug");
#else
    return QStringLiteral("Release");
#endif
}

inline QString compiler()
{
#if defined(Q_CC_MSVC)
    return QStringLiteral("MSVC %1").arg(_MSC_VER);
#elif defined(Q_CC_CLANG)
    return QStringLiteral("Clang %1.%2").arg(__clang_major__).arg(__clang_minor__);
#elif defined(Q_CC_MINGW)
    return QStringLiteral("MinGW GCC %1.%2").arg(__GNUC__).arg(__GNUC_MINOR__);
#elif defined(Q_CC_GNU)
    return QStringLiteral("GCC %1.%2").arg(__GNUC__).arg(__GNUC_MINOR__);
#else
    return QStringLiteral("unknown");
#endif
}

inline QString cxxStandard()
{
#if __cplusplus >= 202002L
    return QStringLiteral("C++20");
#elif __cplusplus >= 201703L
    return QStringLiteral("C++17");
#elif __cplusplus >= 201402L
    return QStringLiteral("C++14");
#else
    return QStringLiteral("C++11");
#endif
}

inline QString qtBuildVersion()   { return QStringLiteral(QT_VERSION_STR); }
inline QString qtRuntimeVersion() { return QString::fromLatin1(qVersion()); }
inline QString abi()              { return QSysInfo::buildAbi(); }
inline QString arch()             { return QSysInfo::currentCpuArchitecture(); }
inline QString kernel()           { return QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion(); }
inline QString os()               { return QSysInfo::prettyProductName(); }

} // namespace BuildInfo
