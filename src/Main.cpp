/*
 *  SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "AppIcon.h"
#include "CommandLineOptions.h"
#include "Config.h"
#include "FotonCore.h"
#include "GlobalShortcuts.h"
#include "ShortcutActions.h"
#include "SingleInstance.h"
#include "WindowsIntegration.h"
#include "settings.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QQuickStyle>
#include <QSessionManager>
#include <QStandardPaths>
#include <QTimer>

#include <KAboutData>
#include <KColorScheme>
#include <KCrash>
#include <KIconTheme>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KStyleManager>

using namespace Qt::StringLiterals;

// Look like other Windows 10 applications instead of KDE's Breeze theme.
static void setUpLook()
{
#ifdef Q_OS_WIN
    // The native style of Windows 10, also used on Windows 11. QT_STYLE_OVERRIDE still takes precedence.
    if (qEnvironmentVariableIsSet("QT_STYLE_OVERRIDE") || !QApplication::setStyle(u"windowsvista"_s)) {
        KStyleManager::initStyle();
    }
#else
    KStyleManager::initStyle();
#endif

    // The colors are also used by the QML user interface (Kirigami), which would use Breeze colors otherwise.
    const auto colorScheme = QStandardPaths::locate(QStandardPaths::GenericDataLocation, u"foton/Windows10.colors"_s);
    if (!colorScheme.isEmpty()) {
        qApp->setProperty("KDE_COLOR_SCHEME_PATH", colorScheme);
        qApp->setPalette(KColorScheme::createApplicationPalette(KSharedConfig::openConfig(colorScheme)));
    }
}

int main(int argc, char **argv)
{
    // set up the application

#ifdef Q_OS_WIN
    // With the flip model of Direct3D, Windows switches full screen windows like the capture windows
    // to a different presentation mode, which makes the screen flicker when they appear.
    if (!qEnvironmentVariableIsSet("QT_D3D_NO_FLIP")) {
        qputenv("QT_D3D_NO_FLIP", "1");
    }
#endif

    // Use the bundled Breeze icons when there is no icon theme, like on Windows.
    // This has to be done before creating the application.
    KIconTheme::initTheme();
    QIcon::setFallbackThemeName(u"breeze"_s);
    QApplication app(argc, argv);
    setUpLook();
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        // The style used by KDE applications, which follows the widget style.
        QQuickStyle::setStyle(u"org.kde.desktop"_s);
    }

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("foton"));

    KAboutData aboutData(u"foton"_s,
                         i18n("Foton"),
                         QStringLiteral(FOTON_VERSION),
                         i18n("Screenshot utility for Windows, a reworked port of KDE Spectacle"),
                         KAboutLicense::GPL_V2,
                         i18n("© 2026 Dr-Livsi (Foton)\n© 2015 Boudhayan Gupta and the Spectacle developers"),
                         {},
                         u"https://github.com/Dr-Livsi/foton-test-repka"_s,
                         // Don't send bug reports for Foton to the KDE bug tracker.
                         {});
    aboutData.setDesktopFileName(u"foton"_s);
    aboutData.addAuthor(u"Dr-Livsi"_s, i18n("Author of the Foton port"), {}, u"https://github.com/Dr-Livsi"_s);
    aboutData.addAuthor(u"Boudhayan Gupta"_s, i18n("Original author of Spectacle"), u"bgupta@kde.org"_s);
    aboutData.addAuthor(u"David Redondo"_s, i18n("Spectacle developer"), u"kde@david-redondo.de"_s);
    aboutData.addAuthor(u"Noah Davis"_s, i18n("Spectacle developer"), u"noahadvs@gmail.com"_s);
    aboutData.setTranslator(i18nc("NAME OF TRANSLATORS", "Your names"), i18nc("EMAIL OF TRANSLATORS", "Your emails"));
    KAboutData::setApplicationData(aboutData);
    app.setWindowIcon(fotonIcon());

    KCrash::initialize();

    QCommandLineParser commandLineParser;
    aboutData.setupCommandLine(&commandLineParser);
    commandLineParser.addOptions(CommandLineOptions::self()->allOptions);

    // first parsing for help-about
    commandLineParser.process(app.arguments());
    aboutData.processCommandLine(&commandLineParser);

    // Prevent session manager from restoring the app on start up.
    // https://bugs.kde.org/show_bug.cgi?id=430411
    auto disableSessionManagement = [](QSessionManager &sm) {
        sm.setRestartHint(QSessionManager::RestartNever);
    };
    QObject::connect(&app, &QGuiApplication::commitDataRequest, disableSessionManagement);
    QObject::connect(&app, &QGuiApplication::saveStateRequest, disableSessionManagement);

    ShortcutActions::self()->readSettings();

    // If the new instance command line option has been specified,
    // use this alternative path for executing Foton.
    if (commandLineParser.isSet(CommandLineOptions::self()->newInstance)) {
        auto fotonCore = FotonCore::instance();

        QObject::connect(qApp, &QApplication::aboutToQuit, Settings::self(), &Settings::save);
        QObject::connect(fotonCore, &FotonCore::allDone, &app, &QCoreApplication::quit, Qt::QueuedConnection);

        // fire it up
        fotonCore->activate(app.arguments(), QDir::currentPath());

        return app.exec();
    }

    // Started by FotonCore::restart(), wait for the old instance to quit.
    if (const int restartPid = qEnvironmentVariableIntValue("FOTON_RESTART_PID")) {
        qunsetenv("FOTON_RESTART_PID");
        WindowsIntegration::waitForProcessToQuit(restartPid, 10000);
    }

    // Pass the arguments to the instance that is already running, if there is one.
    // That instance also owns the global shortcuts and the system tray icon.
    SingleInstance singleInstance;
    if (singleInstance.sendToRunningInstance(app.arguments(), QDir::currentPath())) {
        return 0;
    }
    if (!singleInstance.listen()) {
        // Another instance was started at the same time.
        if (singleInstance.sendToRunningInstance(app.arguments(), QDir::currentPath())) {
            return 0;
        }
        qWarning() << "Could not communicate with other instances of Foton, continuing anyway.";
    }

    auto fotonCore = FotonCore::instance();

    QObject::connect(&singleInstance, &SingleInstance::activateRequested, fotonCore, &FotonCore::activate);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, Settings::self(), &Settings::save);
    QObject::connect(
        fotonCore,
        &FotonCore::allDone,
        &app,
        [fotonCore] {
            if (fotonCore->isResident()) {
                // Keep running in the system tray.
                fotonCore->releaseResources();
            } else {
                QCoreApplication::quit();
            }
        },
        Qt::QueuedConnection);

    // A one-off screenshot in background mode doesn't need to stay in the system tray.
    const bool background = commandLineParser.isSet(CommandLineOptions::self()->background);
    if (!background) {
        fotonCore->setForceResident(commandLineParser.isSet(CommandLineOptions::self()->tray));
        fotonCore->applyBackgroundSettings();
        GlobalShortcuts::instance()->setEnabled(true);
    }

    // fire it up
    fotonCore->activate(app.arguments(), QDir::currentPath());

    // Waiting in the system tray, so there is time to prepare the first screenshot.
    if (commandLineParser.isSet(CommandLineOptions::self()->tray)) {
        QTimer::singleShot(0, fotonCore, &FotonCore::preloadUserInterface);
    }

    return app.exec();
}
