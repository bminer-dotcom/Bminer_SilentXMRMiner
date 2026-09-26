#-------------------------------------------------
# Bminer — desktop dashboard + bacterium pet
# Open this file directly in Qt Creator.
# Works with Qt 5.12+ and Qt 6.x
#-------------------------------------------------

QT += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
# svg: QSvgRenderer draws the Documentation page's field-guide artwork (assets/guide).
QT += svg
# multimedia: QMediaPlayer plays the Documentation page's ambient music (GuidePage).
QT += multimedia

CONFIG += c++17
TEMPLATE = app
TARGET   = "bminer builder"
VERSION  = 1.0.0

DEFINES += APP_VERSION=\\\"$$VERSION\\\"
DEFINES += QT_DEPRECATED_WARNINGS

INCLUDEPATH += $$PWD/src

RESOURCES += resources.qrc

# Forms — open these in Qt Designer to change the layout.
# Every screen in the app has one; there is no hand-written layout code left.
FORMS += \
    src/mainwindow.ui \
    src/pages/dashboardpage.ui \
    src/pages/buildpage.ui \
    src/pages/controlpage.ui \
    src/pages/settingspage.ui \
    src/pages/aboutpage.ui

SOURCES += \
    src/buildsystem.cpp \
    src/main.cpp \
    src/theme.cpp \
    src/logo.cpp \
    src/appsettings.cpp \
    src/mainwindow.cpp \
    src/petwidget.cpp \
    src/widgets/titlebar.cpp \
    src/widgets/card.cpp \
    src/widgets/fieldrow.cpp \
    src/widgets/logoview.cpp \
    src/widgets/toggleswitch.cpp \
    src/widgets/flaskwidget.cpp \
    src/pages/dashboardpage.cpp \
    src/pages/buildpage.cpp \
    src/pages/controlpage.cpp \
    src/pages/protectpage.cpp \
    src/pages/guidepage.cpp \
    src/pages/settingspage.cpp \
    src/pages/aboutpage.cpp

HEADERS += \
    src/buildinfo.h \
    src/buildsystem.h \
    src/theme.h \
    src/logo.h \
    src/appsettings.h \
    src/mainwindow.h \
    src/petwidget.h \
    src/widgets/titlebar.h \
    src/widgets/card.h \
    src/widgets/fieldrow.h \
    src/widgets/logoview.h \
    src/widgets/toggleswitch.h \
    src/widgets/flaskwidget.h \
    src/pages/dashboardpage.h \
    src/pages/buildpage.h \
    src/pages/controlpage.h \
    src/pages/protectpage.h \
    src/pages/guidepage.h \
    src/pages/settingspage.h \
    src/pages/aboutpage.h

# dbghelp: UnDecorateSymbolName() for demangling MSVC-mangled function names (Protect page)
win32: LIBS += -ldbghelp
# crypt32: DPAPI (CryptProtectData / CryptUnprotectData) for encrypting Tox savedata at rest
win32: LIBS += -lcrypt32

# Windows: application icon + version metadata.
# NOTE: the icon path is relative to the shadow-build directory (Qt Creator
# default: <project>/build/<kit>/ -> ../../ reaches the project root, where
# app.ico lives). Keep that layout; windres cannot take spaced absolute paths.
win32 {
    RC_FILE = $$PWD/Bminer_resource.rc
}

# ---------------------------------------------------------------------------
# Windows: drop the Qt runtime next to the release binary, so the build folder
# can be zipped and run on a machine without Qt installed.
# ---------------------------------------------------------------------------
win32:CONFIG(release, debug|release) {
    WINDEPLOYQT = $$shell_quote($$shell_path($$[QT_INSTALL_BINS]/windeployqt.exe))
    DEPLOY_EXE  = $$shell_quote($$shell_path($$OUT_PWD/release/$${TARGET}.exe))
    QMAKE_POST_LINK += $$WINDEPLOYQT --release --no-translations --compiler-runtime $$DEPLOY_EXE

    # bundle the BVM protector next to Bminer.exe so it ships as part of Bminer.
    BVM_SRC = $$PWD/bvm/bvm.exe
    BVM_DST = $$OUT_PWD/release/
    QMAKE_POST_LINK += $$escape_expand(\\n\\t) $$QMAKE_COPY $$shell_quote($$shell_path($$BVM_SRC)) $$shell_quote($$shell_path($$BVM_DST))
}

# Default deployment rules
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
