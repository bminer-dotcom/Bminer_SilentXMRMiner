#include "appsettings.h"
#include "buildinfo.h"
#include "buildsystem.h"
#include "logo.h"
#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QDebug>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);

    // QSettings reads these, so they have to be set before AppSettings is touched
    QCoreApplication::setOrganizationName(QStringLiteral("Bminer"));
    QCoreApplication::setApplicationName(QStringLiteral("Bminer"));
    QCoreApplication::setApplicationVersion(BuildInfo::version());

    // the pet and the tray keep the app alive on their own
    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(Logo::appIcon());

    Theme::setAccent(AppSettings::i()->getString(Keys::UiAccent));
    app.setPalette(Theme::palette());
    app.setStyleSheet(Theme::styleSheet());

    // Recover from a build that was killed mid-run: if a stale <clientDir>/backup/
    // exists, the previous build was interrupted after modifying main.cpp but
    // before its qScopeGuard restored it — finish the restore now so the
    // operator's endpoint URL isn't left permanently baked into their source
    // tree. Silent no-op when there's nothing to recover.
    {
        QString msg;
        if (BuildSystem::recoverStaleBackup(&msg))
            qWarning().noquote() << msg;
    }

    MainWindow window;
    window.show();

    return app.exec();
}
