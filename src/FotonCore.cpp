/*
 *  SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *  SPDX-FileCopyrightText: 2025 Jhair Paris <dev@jhairparis.com>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "FotonCore.h"
#include "CaptureModeModel.h"
#include "CommandLineOptions.h"
#include "Config.h"
#include "ExportManager.h"
#include "Geometry.h"
#include "GlobalShortcuts.h"
#include "Gui/CaptureWindow.h"
#include "Gui/FotonWindow.h"
#include "Gui/InlineMessageModel.h"
#include "Gui/Selection.h"
#include "Gui/SelectionEditor.h"
#include "Gui/TextRecognitionWindow.h"
#include "ImageMetaData.h"
#include "Platforms/PlatformLoader.h"
#include "ShortcutActions.h"
#include "TextRecognition.h"
#include "TrayIcon.h"
#include "WindowsIntegration.h"
// generated
#include "settings.h"

#include <KLocalizedString>
#include <KQuickImageEditor/AnnotationTool>
#include <KMessageBox>
#include <KNotification>

#include <KLocalizedQmlContext>
#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QMimeData>
#include <QObject>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtMath>

#include <optional>

using namespace Qt::StringLiterals;

FotonCore *FotonCore::s_self = nullptr;

static QList<KNotification *> notifications;

FotonCore::FotonCore(QObject *parent)
    : QObject(parent)
{
    // Timer to prevent lots of extra rendering to images
    m_annotationSyncTimer = std::make_unique<QTimer>();
    m_annotationSyncTimer->setInterval(400);
    m_annotationSyncTimer->setSingleShot(true);

    m_delayAnimation = std::make_unique<QVariantAnimation>(this);
    m_delayAnimation->setStartValue(0.0);
    m_delayAnimation->setEndValue(1.0);
    m_delayAnimation->setDuration(1);
    m_delayAnimation->setCurrentTime(0);
    auto delayAnimation = m_delayAnimation.get();
    using State = QVariantAnimation::State;
    auto onStateChanged = [this](State newState, State oldState) {
        Q_UNUSED(oldState)
        if (newState == State::Running) {
            updateTaskbarProgress(true, 0);
        } else if (newState == State::Stopped) {
            updateTaskbarProgress(false);
            m_delayAnimation->setCurrentTime(0);
        }
    };
    auto onValueChanged = [this](const QVariant &value) {
        Q_EMIT captureTimeRemainingChanged();
        Q_EMIT captureProgressChanged();
        updateTaskbarProgress(true, value.toReal());
        const auto windows = FotonWindow::instances();
        if (m_delayAnimation->state() != State::Stopped && !windows.isEmpty()) {
            if (captureTimeRemaining() <= 500 && windows.constFirst()->isVisible()) {
                FotonWindow::setVisibilityForAll(QWindow::Hidden);
            }
            FotonWindow::setTitleForAll(FotonWindow::Timer);
        }
    };
    auto onFinished = [this]() {
        m_imagePlatform->doGrab(ImagePlatform::ShutterMode::Immediate, m_lastGrabMode, m_lastIncludePointer, m_lastIncludeDecorations, m_lastIncludeShadow);
    };
    QObject::connect(delayAnimation, &QVariantAnimation::stateChanged,
                     this, onStateChanged, Qt::QueuedConnection);
    QObject::connect(delayAnimation, &QVariantAnimation::valueChanged,
                     this, onValueChanged, Qt::QueuedConnection);
    QObject::connect(delayAnimation, &QVariantAnimation::finished,
                     this, onFinished, Qt::QueuedConnection);

    m_imagePlatform = loadImagePlatform();
    auto imagePlatform = m_imagePlatform.get();
    m_annotationDocument = std::make_unique<AnnotationDocument>();

    // essential connections
    connect(SelectionEditor::instance(), &SelectionEditor::accepted, this, [this](const QRectF &rect, const ExportManager::Actions &actions) {
        m_returnToViewer = m_startMode == StartMode::Gui;
        FotonWindow::setVisibilityForAll(QWindow::Hidden);
        deleteWindows();
        // Cropping to the selected region is part of taking the screenshot, not an edit.
        const bool edited = hasUnsavedEdits();
        m_annotationDocument->cropCanvas(rect);
        if (!edited) {
            m_savedUndoStackDepth = m_annotationDocument->undoStackDepth();
        }
        syncExportImage();
        auto exportActions = actions & ExportManager::AnyAction ? actions : autoExportActions();
        const bool willQuit = exportActions.testAnyFlags(ExportManager::AnyAction) //
            && exportActions.testFlag(ExportManager::UserAction) //
            && Settings::quitAfterSaveCopyExport();
        m_returnToViewer &= !willQuit;
        showViewerIfGuiMode();
        FotonWindow::setTitleForAll(FotonWindow::Unsaved);
        ExportManager::instance()->scanQRCode();
        ExportManager::instance()->exportImage(exportActions, outputUrl());
    });

    connect(imagePlatform, &ImagePlatform::newScreenshotTaken, this, [this](const QImage &image){
        InlineMessageModel::instance()->clear();
        m_editExistingUrl.clear();
        m_cliOptions[CommandLineOptions::EditExisting] = false;
        startNewDocument();
        m_annotationDocument->setBaseImage(image);
        setExportImage(image);
        ExportManager::instance()->updateTimestamp();
        m_returnToViewer = true;
        showViewerIfGuiMode();
        FotonWindow::setTitleForAll(FotonWindow::Unsaved);
        ExportManager::instance()->scanQRCode();
        ExportManager::instance()->exportImage(autoExportActions(), outputUrl());
    });
    connect(imagePlatform, &ImagePlatform::newCroppableScreenshotTaken, this, [this](const QImage &image) {
        InlineMessageModel::instance()->clear();
        m_editExistingUrl.clear();
        m_cliOptions[CommandLineOptions::EditExisting] = false;
        startNewDocument();
        m_annotationDocument->setBaseImage(image);
        setExportImage(image);
        ExportManager::instance()->updateTimestamp();
        // SelectionEditor::reset() only runs after the windows are shown,
        // clear the old selection before that so that it doesn't appear for a moment.
        SelectionEditor::instance()->selection()->setRect({});
        initCaptureWindows(CaptureWindow::Image);
        FotonWindow::setTitleForAll(FotonWindow::Unsaved);
        FotonWindow::setVisibilityForAll(QWindow::FullScreen);
        // SelectionEditor::reset has to be delayed until after all the DPRs are correct.
        connect(m_captureWindows.front().get(), &CaptureWindow::allExposed, //
                SelectionEditor::instance(), &SelectionEditor::reset, Qt::SingleShotConnection);
    });

    connect(imagePlatform, &ImagePlatform::newScreenshotFailed, this, [this](const QString &message) {
        auto uiMessage = i18nc("@info", "An error occurred while taking a screenshot.");
        if (!message.isEmpty()) {
            uiMessage = uiMessage % u"\n"_s % message;
        }
        const auto &windows = qGuiApp->allWindows();
        const bool hasVisibleWindow = std::any_of(windows.cbegin(), windows.cend(), [](auto *window){
            // Is visible and is a normal window or dialog (no tooltips or menus)
            const auto typeFlags = window->flags() & Qt::WindowType_Mask;
            return window->isVisible() && (typeFlags == Qt::Window || typeFlags == Qt::Dialog);
        });
        switch (m_startMode) {
        case StartMode::Background:
        case StartMode::Tray:
            showErrorMessage(uiMessage);
            if (!hasVisibleWindow) {
                Q_EMIT allDone();
            }
            return;
        case StartMode::Gui: {
            if (!ViewerWindow::instance()) {
                InlineMessageModel::instance()->clear();
                if (hasVisibleWindow) {
                    showErrorMessage(uiMessage);
                    return;
                } else {
                    initViewerWindow(ViewerWindow::Dialog);
                }
            }
            qWarning().noquote() << message;
            auto viewer = ViewerWindow::instance();
            viewer->setVisible(true);
            InlineMessageModel::instance()->push(InlineMessageModel::Error, uiMessage);
            return;
        }
        }
    });
    connect(imagePlatform, &ImagePlatform::newScreenshotCanceled, this, [this]() {
        if (m_startMode != StartMode::Gui || !m_returnToViewer || isGuiNull()) {
            Q_EMIT allDone();
            return;
        }
        FotonWindow::setTitleForAll(FotonWindow::Previous);
        const auto windows = FotonWindow::instances();
        if (windows.empty()) {
            initViewerWindow(ViewerWindow::Viewer);
            return;
        }
        for (auto w : windows) {
            w->setVisible(true);
        }
    });

    // set up the export manager
    auto exportManager = ExportManager::instance();
    auto onImageExported = [this](const ExportManager::Actions &actions, const QUrl &url) {
        if (actions & ExportManager::AnySave) {
            m_editExistingUrl = url;
            m_outputUrl.clear();
            m_savedUndoStackDepth = m_annotationDocument->undoStackDepth();
        }
        if (actions & ExportManager::UserAction && Settings::quitAfterSaveCopyExport()) {
            deleteWindows();
        }

        if (isGuiNull()) {
            if (m_cliOptions[CommandLineOptions::NoNotify]) {
                // if we notify, we Q_EMIT allDone only if the user either dismissed the notification or pressed
                // the "Open" button, otherwise the app closes before it can react to it.
                if (actions & ExportManager::CopyImage) {
                    // Allow some time for clipboard content to transfer if '--nonotify' is used, see Bug #411263
                    // TODO: Find better solution
                    QTimer::singleShot(250, this, &FotonCore::allDone);
                } else {
                    Q_EMIT allDone();
                }
            } else {
                doNotify(actions, url);
            }
            return;
        }

        auto viewerWindow = ViewerWindow::instance();
        if (!viewerWindow) {
            return;
        }

        if (actions & ExportManager::AnySave) {
            FotonWindow::setTitleForAll(FotonWindow::Saved, url.fileName());
            if (actions & ExportManager::CopyImage) {
                auto text =
                    xi18nc("@info", "The screenshot was copied to the clipboard and saved as <link url=\"%1\">%2</link>", url.toString(), url.fileName());
                InlineMessageModel::instance()->push(InlineMessageModel::Saved, text, url);
            } else if (actions & ExportManager::CopyPath) {
                auto text = xi18nc("@info",
                                   "The screenshot has been saved as <link url=\"%1\">%2</link> and its location has been copied to clipboard",
                                   url.toString(),
                                   url.fileName());
                InlineMessageModel::instance()->push(InlineMessageModel::Saved, text, url);
            } else {
                auto text = xi18nc("@info", "The screenshot was saved as <link url=\"%1\">%2</link>", url.toString(), url.fileName());
                InlineMessageModel::instance()->push(InlineMessageModel::Saved, text, url);
            }
        } else if (actions & ExportManager::CopyImage) {
            auto text = i18nc("@info", "The screenshot has been copied to the clipboard.");
            InlineMessageModel::instance()->push(InlineMessageModel::Copied, text);
        }
    };
    connect(exportManager, &ExportManager::imageExported, this, onImageExported);

    auto onQRCodeScanned = [](const QVariant &result) {
        auto viewerWindow = ViewerWindow::instance();
        if (!viewerWindow) {
            return;
        }
        // Thanks to: https://stackoverflow.com/questions/1500260/detect-urls-in-text-with-javascript
        using QRE = QRegularExpression;
        static const QRE urlRegex(u"(\\b(https?|ftp|file)://[-A-Z0-9+&@#/%?=~_|!:,.;]*[-A-Z0-9+&@#/%=~_|])"_s, QRE::CaseInsensitiveOption);
        static const auto linkifier = u"<a href=\"\\1\">\\1</a>"_s;
        auto text = [](const QVariant &result) -> QString {
            if (result.typeId() == QMetaType::QString) {
                // Not using xi18nc because it doesn't work with the link replacement logic.
                // Also see https://invent.kde.org/graphics/spectacle/-/merge_requests/432#note_1111125
                auto text = i18nc("@info", //
                                  "Code found: %1",
                                  result.toString().toHtmlEscaped().replace(urlRegex, linkifier));
                return u"<html>" % text % u"</html>";
            }
            return i18nc("@info", "Found QR code with binary content.");
        }(result);
        InlineMessageModel::instance()->push(InlineMessageModel::Scanned, text, result);
    };
    connect(exportManager, &ExportManager::qrCodeScanned, this, onQRCodeScanned);

    connect(exportManager, &ExportManager::errorMessage, this, &FotonCore::showErrorMessage);

    connect(m_annotationDocument.get(), &AnnotationDocument::repaintNeeded, m_annotationSyncTimer.get(), qOverload<>(&QTimer::start));
    connect(m_annotationSyncTimer.get(), &QTimer::timeout, this, [this] {
        auto image = m_annotationDocument->renderToImage();
        auto canvasPos = m_annotationDocument->canvasRect().topLeft();
        ImageMetaData::setLogicalXY(image, canvasPos.x(), canvasPos.y());
        ExportManager::instance()->setImage(image);
    }, Qt::QueuedConnection); // QueuedConnection to help prevent making the visible render lag.

    // global shortcuts
    connect(GlobalShortcuts::instance(), &GlobalShortcuts::activated, this, [this](QAction *action) {
        if (action == ShortcutActions::self()->openAction()) {
            // Behave like launching the application without arguments.
            activate({QCoreApplication::applicationFilePath()}, {});
        } else {
            activateAction(action->objectName());
        }
    });

    connect(qApp, &QApplication::screenRemoved, this, [this](QScreen *screen) {
        // It's dangerous to erase from within a for loop, so we use std::find_if
        auto hasScreen = [screen](const CaptureWindow::UniquePointer &window) {
            return window->screen() == screen;
        };
        auto it = std::find_if(m_captureWindows.begin(), m_captureWindows.end(), hasScreen);
        if (it != m_captureWindows.end()) {
            m_captureWindows.erase(it);
        }
    });

    // In resident mode, closing the last window must not quit the application,
    // but we can still free the memory used by the windows and the screenshot.
    connect(qApp, &QGuiApplication::lastWindowClosed, this, [this] {
        if (m_resident && m_delayAnimation->state() == QVariantAnimation::Stopped && CaptureWindow::instances().isEmpty()) {
            QTimer::singleShot(0, this, &FotonCore::releaseResources);
        }
    });
}

FotonCore::~FotonCore() noexcept
{
    s_self = nullptr;
}

FotonCore *FotonCore::instance()
{
    if (!s_self) {
        s_self = new FotonCore(qApp);
    }

    return s_self;
}

ImagePlatform *FotonCore::imagePlatform() const
{
    return m_imagePlatform.get();
}

AnnotationDocument *FotonCore::annotationDocument() const
{
    return m_annotationDocument.get();
}

QUrl FotonCore::screenCaptureUrl() const
{
    return m_screenCaptureUrl;
}

void FotonCore::setScreenCaptureUrl(const QUrl &url)
{
    if(m_screenCaptureUrl == url) {
        return;
    }
    m_screenCaptureUrl = url;
    Q_EMIT screenCaptureUrlChanged();
}

void FotonCore::setScreenCaptureUrl(const QString &filePath)
{
    if (QDir::isRelativePath(filePath)) {
        setScreenCaptureUrl(QUrl::fromUserInput(QDir::current().absoluteFilePath(filePath)));
    } else {
        setScreenCaptureUrl(QUrl::fromUserInput(filePath));
    }
}

QUrl FotonCore::outputUrl() const
{
    return m_outputUrl.isEmpty() ? m_editExistingUrl : m_outputUrl;
}

void FotonCore::setOutputUrl(const QUrl &url)
{
    m_outputUrl = url;
}

int FotonCore::captureTimeRemaining() const
{
    int totalDuration = m_delayAnimation->totalDuration();
    int currentTime = m_delayAnimation->currentTime();
    return currentTime > totalDuration || m_delayAnimation->state() == QVariantAnimation::Stopped ?
        0 : totalDuration - currentTime;
}

qreal FotonCore::captureProgress() const
{
    // using currentValue() sometimes gives 1.0 when we don't want it.
    return m_delayAnimation->state() == QVariantAnimation::Stopped ?
        0 : m_delayAnimation->currentValue().toReal();
}

void FotonCore::activate(const QStringList &arguments, const QString &workingDirectory)
{
    if (!workingDirectory.isEmpty()) {
        QDir::setCurrent(workingDirectory);
    }

    // We can't re-use QCommandLineParser instances, it preserves earlier parsed values
    QCommandLineParser parser;
    parser.addOptions(CommandLineOptions::self()->allOptions);
    parser.parse(arguments);

    // Collect parsed command line options
    using Option = CommandLineOptions::Option;
    m_cliOptions.fill(false); // reset all values to false
    int optionsToCheck = parser.optionNames().size();
    for (int i = 0; optionsToCheck > 0 && i < CommandLineOptions::self()->allOptions.size(); ++i) {
        m_cliOptions[i] = parser.isSet(CommandLineOptions::self()->allOptions[i]);
        if (m_cliOptions[i]) {
            --optionsToCheck;
        }
    }

    // Determine start mode
    m_startMode = StartMode::Gui; // Default to Gui
    // Gui is an option that's normally useless since it's the default mode.
    // Make it override the other modes if explicitly set, using the launchonly option,
    // or editing an existing image. Editing an existing image requires a viewer window.
    if (!m_cliOptions[Option::Gui]
        && !m_cliOptions[Option::LaunchOnly]
        && !m_cliOptions[Option::EditExisting]) {
        // Background gets precedence over Tray
        if (m_cliOptions[Option::Background]) {
            m_startMode = StartMode::Background;
        } else if (m_cliOptions[Option::Tray]) {
            m_startMode = StartMode::Tray;
        }
    }

    if (parser.optionNames().size() > 0 || m_startMode != StartMode::Gui || !m_returnToViewer) {
        // Delete windows if we have CLI options or not in GUI mode.
        // We don't want to delete them otherwise because that will mess with the
        // settings for PrintScreen key behavior.
        deleteWindows();
    }

    /* The logic for setting options for each start mode:
     *
     * - Gui/Tray: Prioritise command line options and default to saved settings.
     * - Background: Prioritise command line options and use defaults based on
     *   how command line options are meant to be used.
     *
     * Never start with a delay by default. It is annoying and confuses users
     * when nothing happens immediately after starting Foton.
     */

    bool includeDecorations;
    bool includePointer;
    bool includeShadow;
    if (m_startMode == StartMode::Background) {
        includeDecorations = !m_cliOptions[Option::NoDecoration];
        includePointer = m_cliOptions[Option::Pointer];
        includeShadow = !m_cliOptions[Option::NoShadow];
    } else {
        includeDecorations = Settings::includeDecorations()
                            && !m_cliOptions[Option::NoDecoration];
        includeShadow = Settings::includeShadow() && !m_cliOptions[Option::NoShadow];
        includePointer = Settings::includePointer() || m_cliOptions[Option::Pointer];
    }

    int delayMsec = 0; // default to 0 if cli value parse fails
    if (m_cliOptions[Option::Delay]) {
        bool parseOk = false;
        int value = parser.value(CommandLineOptions::self()->delay).toInt(&parseOk);
        if (parseOk) {
            delayMsec = value;
        }
    }

    if (m_cliOptions[Option::EditExisting]) {
        auto input = parser.value(CommandLineOptions::self()->editExisting);
        m_editExistingUrl = QUrl::fromUserInput(input, QDir::currentPath(), QUrl::AssumeLocalFile);
        // QFileInfo::exists() only works with local files.
        auto existingLocalFile = m_editExistingUrl.toLocalFile();
        if (QFileInfo::exists(existingLocalFile)) {
            InlineMessageModel::instance()->clear();
            // If editing an existing image, open the annotation editor.
            // This QImage constructor only works with local files or Qt resource file names.
            QImage existingImage(existingLocalFile);
            startNewDocument();
            m_annotationDocument->setBaseImage(existingImage);
            m_returnToViewer = true;
            showViewerIfGuiMode();
            FotonWindow::setTitleForAll(FotonWindow::Saved, m_editExistingUrl.fileName());
            if (!existingImage.isNull()) {
                setExportImage(existingImage);
                ExportManager::instance()->scanQRCode();
            }
            return;
        } else {
            m_cliOptions[Option::EditExisting] = false;
            m_editExistingUrl.clear();
        }
    } else {
        m_editExistingUrl.clear();
    }

    if (m_cliOptions[Option::Output]) {
        m_outputUrl = QUrl::fromUserInput(parser.value(CommandLineOptions::self()->output),
                                          QDir::currentPath(), QUrl::AssumeLocalFile);
        if (!m_outputUrl.isValid()) {
            m_cliOptions[Option::Output] = false;
            m_outputUrl.clear();
        }
    } else {
        m_outputUrl.clear();
    }

    // Determine grab mode
    using CaptureMode = CaptureModeModel::CaptureMode;
    using GrabMode = ImagePlatform::GrabMode;
    auto cliGrabMode = [&]() -> std::optional<GrabMode> {
        if (m_cliOptions[Option::Fullscreen]) {
            return m_cliOptions[Option::Scaled] ? GrabMode::AllScreensScaled : GrabMode::AllScreens;
        } else if (m_cliOptions[Option::Current]) {
            return GrabMode::CurrentScreen;
        } else if (m_cliOptions[Option::ActiveWindow]) {
            return GrabMode::ActiveWindow;
        } else if (m_cliOptions[Option::Region]) {
            return GrabMode::PerScreenImageNative;
        } else if (m_cliOptions[Option::WindowUnderCursor]) {
            return toGrabMode(CaptureMode::WindowUnderCursor);
        }
        return std::nullopt;
    };
    auto launchActionGrabMode = [&] {
        switch (Settings::launchAction()) {
        case Settings::TakeRectangularScreenshot:
            return GrabMode::PerScreenImageNative;
        case Settings::TakeFullscreenScreenshot:
            return GrabMode::AllScreens;
        case Settings::UseLastUsedCapturemode:
            return toGrabMode(CaptureMode(Settings::captureMode()));
        default:
            return GrabMode::NoGrabModes;
        }
    };
    auto grabMode = cliGrabMode().value_or(m_startMode == StartMode::Background ? GrabMode::AllScreens : launchActionGrabMode());

    if (m_cliOptions[Option::Scaled] && !m_cliOptions[Option::Fullscreen]) {
        qWarning().noquote() << i18nc("@info:shell", "--scaled is only allowed together with -f/--fullscreen and will be ignored");
    }

    if (m_cliOptions[Option::ReleaseCapture]) {
        SelectionEditor::instance()->forceReleaseCapture(true);
    }

    // If any capture mode is given in the cli options, let it override
    // the setting to not take a screenshot on launch
    // clang-format off
    bool captureModeFromCli =
        m_cliOptions[Option::Fullscreen] ||
        m_cliOptions[Option::Current] ||
        m_cliOptions[Option::ActiveWindow] ||
        m_cliOptions[Option::WindowUnderCursor] ||
        m_cliOptions[Option::Region];
    // clang-format on

    switch (m_startMode) {
    case StartMode::Tray:
        // Wait for a global shortcut or an action from the system tray icon.
        if (captureModeFromCli) {
            takeNewScreenshot(grabMode, delayMsec, includePointer, includeDecorations, includeShadow);
        }
        break;
    case StartMode::Background:
        takeNewScreenshot(grabMode, delayMsec, includePointer, includeDecorations, includeShadow);
        break;
    case StartMode::Gui:
        if (isGuiNull()) {
            if (m_cliOptions[Option::LaunchOnly] || //
                (Settings::launchAction() == Settings::DoNotTakeScreenshot && !captureModeFromCli)) {
                initViewerWindow(ViewerWindow::Dialog);
                ViewerWindow::instance()->setVisible(true);
                ViewerWindow::instance()->requestActivate();
            } else {
                takeNewScreenshot(grabMode, delayMsec, includePointer, includeDecorations, includeShadow);
            }
        } else {
            using Actions = Settings::EnumPrintKeyRunningAction;
            switch (Settings::printKeyRunningAction()) {
            case Actions::TakeNewScreenshot: {
                // takeNewScreenshot switches to on click if immediate is not supported.
                takeNewScreenshot(grabMode, 0, includePointer, includeDecorations, includeShadow);
                break;
            }
            case Actions::FocusWindow: {
                bool isCaptureWindow = !CaptureWindow::instances().isEmpty();
                FotonWindow *window = nullptr;
                if (isCaptureWindow) {
                    window = CaptureWindow::instances().constFirst();
                } else {
                    window = ViewerWindow::instance();
                }
                if (isCaptureWindow) {
                    FotonWindow::setVisibilityForAll(QWindow::FullScreen);
                } else {
                    // Unminimize the window.
                    window->unminimize();
                }
                window->requestActivate();
                break;
            }
            case Actions::StartNewInstance: {
                QProcess newInstance;
                newInstance.setProgram(QCoreApplication::applicationFilePath());
                newInstance.setArguments({
                    CommandLineOptions::toArgument(CommandLineOptions::self()->newInstance)
                });
                newInstance.startDetached();
                break;
            }
            }
        }

        break;
    }
}

void FotonCore::takeNewScreenshot(ImagePlatform::GrabMode grabMode, int timeout, bool includePointer, bool includeDecorations, bool includeWindowShadow)
{
    // Clear when a new screenshot is taken to avoid overwriting
    // the previous file with a completely unrelated image.
    m_editExistingUrl.clear();
    m_cliOptions[CommandLineOptions::EditExisting] = false;

    m_delayAnimation->stop();

    m_lastGrabMode = grabMode;
    m_lastIncludePointer = includePointer;
    m_lastIncludeDecorations = includeDecorations;
    m_lastIncludeShadow = includeWindowShadow;

    timeout = qMax(0, timeout);
    const bool noDelay = timeout == 0;

    // Our own windows have to disappear from the screen before the screenshot is taken.
    // Window animations are disabled for Foton windows, but the desktop compositor
    // still needs a moment to show what is behind them.
    const auto windows = FotonWindow::instances();
    const bool hasVisibleWindow = std::any_of(windows.cbegin(), windows.cend(), [](FotonWindow *window) {
        return window->isVisible();
    });
    if (hasVisibleWindow) {
        timeout = qMax(timeout, 150);
    }

    if (noDelay) {
        FotonWindow::setVisibilityForAll(QWindow::Hidden);
        QTimer::singleShot(timeout, this, [this]() {
            m_imagePlatform->doGrab(ImagePlatform::ShutterMode::Immediate, m_lastGrabMode, m_lastIncludePointer, m_lastIncludeDecorations, m_lastIncludeShadow);
        });
        return;
    }

    m_delayAnimation->setDuration(timeout);
    m_delayAnimation->start();

    // skip minimize animation.
    FotonWindow::setVisibilityForAll(QWindow::Hidden);
    FotonWindow::setVisibilityForAll(QWindow::Minimized);
}

void FotonCore::takeNewScreenshot(int captureMode, int timeout, bool includePointer, bool includeDecorations, bool includeShadow)
{
    using CaptureMode = CaptureModeModel::CaptureMode;
    takeNewScreenshot(toGrabMode(CaptureMode(captureMode)), timeout, includePointer, includeDecorations, includeShadow);
}

void FotonCore::cancelScreenshot()
{
    if (m_startMode != StartMode::Gui || !m_returnToViewer) {
        Q_EMIT allDone();
        return;
    }

    int currentTime = m_delayAnimation->currentTime();
    m_delayAnimation->stop();
    if (currentTime > 0) {
        FotonWindow::setTitleForAll(FotonWindow::Previous);
    } else if (!ViewerWindow::instance()) {
        initViewerWindow(ViewerWindow::Viewer);
        ViewerWindow::instance()->setVisible(true);
    } else if (ViewerWindow::instance()) {
        Q_EMIT allDone();
    }
}

void FotonCore::showErrorMessage(const QString &message)
{
    qWarning().noquote() << message;

    if (m_startMode == StartMode::Gui || m_startMode == StartMode::Tray) {
        KMessageBox::error(nullptr, message);
    }
}

void FotonCore::showViewerIfGuiMode(bool minimized)
{
    if (m_startMode != StartMode::Gui || !m_returnToViewer) {
        return;
    }
    initViewerWindow(ViewerWindow::Viewer);
    if (m_cliOptions[CommandLineOptions::EditExisting]) {
        ViewerWindow::instance()->setAnnotating(true);
    }
    if (minimized) {
        ViewerWindow::instance()->showMinimized();
    } else {
        ViewerWindow::instance()->setVisible(true);
        ViewerWindow::instance()->requestActivate();
    }
}

void FotonCore::doNotify(const ExportManager::Actions &actions, const QUrl &saveUrl)
{
    if (m_cliOptions[CommandLineOptions::NoNotify]) {
        return;
    }

    // ensure program stays alive until the notification finishes.
    if (!m_eventLoopLocker) {
        m_eventLoopLocker = std::make_unique<QEventLoopLocker>();
    }

    // Use nullptr as the notification parent to prevent them from disappearing from history.
    auto notification = new KNotification(u"newScreenshotSaved"_s, KNotification::CloseOnTimeout, nullptr);
    notification->setTitle(CaptureModeModel::captureModeLabel(toCaptureMode(m_lastGrabMode)));

    notifications.append(notification);

    // a speaking message is prettier than a URL, special case for copy image/location to clipboard and the default pictures location
    const QString saveDirPath = QDir::toNativeSeparators(QFileInfo(saveUrl.toLocalFile()).absolutePath());
    const QString picturesPath = QDir::toNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    const QString &saveFileName = saveUrl.fileName();

    using Action = ExportManager::Action;
    if (actions & Action::AnySave && !saveFileName.isEmpty()) {
        if (actions & Action::CopyPath) {
            notification->setText(i18nc("%1 is the filename, %2 the path to the save location", "A screenshot was saved as '%1' to '%2' and the file path of the screenshot has been saved to your clipboard.",
                                    saveFileName, saveDirPath));
        } else if (saveDirPath.compare(picturesPath, Qt::CaseInsensitive) == 0) {
            notification->setText(i18nc("%1 is the filename",
                                        "A screenshot was saved as '%1' to your Pictures folder.",
                                        saveFileName));
        } else {
            notification->setText(i18nc("%1 is the filename, %2 the path to the save location", "A screenshot was saved as '%1' to '%2'.",
                                    saveFileName, saveDirPath));
        }
    } else if (actions & Action::CopyImage) {
        notification->setText(i18n("A screenshot was saved to your clipboard."));
    }

    if (!saveUrl.isEmpty()) {
        notification->setUrls({saveUrl});

        auto open = [saveUrl]() {
            QDesktopServices::openUrl(saveUrl);
        };
        auto defaultAction = notification->addDefaultAction(i18nc("Open the screenshot we just saved", "Open"));
        connect(defaultAction, &KNotificationAction::activated, this, open);

        auto annotate = [this, saveUrl]() {
            const QStringList arguments = {
                CommandLineOptions::toArgument(CommandLineOptions::self()->editExisting),
                saveUrl.toLocalFile(),
            };
            if (m_resident) {
                // We are still running, so we can open the editor directly.
                activate(QStringList{QCoreApplication::applicationFilePath()} + arguments, {});
                return;
            }
            QProcess newInstance;
            newInstance.setProgram(QCoreApplication::applicationFilePath());
            newInstance.setArguments(QStringList{CommandLineOptions::toArgument(CommandLineOptions::self()->newInstance)} + arguments);
            newInstance.startDetached();
        };
        auto annotateAction = notification->addAction(i18n("Annotate"));
        connect(annotateAction, &KNotificationAction::activated, this, annotate);

        auto showInFolderAction = notification->addAction(i18nc("@action:button", "Show in Folder"));
        connect(showInFolderAction, &KNotificationAction::activated, this, [saveUrl] {
            WindowsIntegration::showInFileManager(saveUrl);
        });
    }

    auto onExpired = [this, notification] {
        notifications.removeOne(static_cast<KNotification *>(notification));
        // When there are no more notifications running, we can remove the loop locker.
        if (notifications.empty() && m_eventLoopLocker) {
            QTimer::singleShot(250, this, [this] {
                m_eventLoopLocker.reset();
            });
        }
    };
    connect(notification, &QObject::destroyed, this, onExpired);
    // BUG: https://bugs.kde.org/show_bug.cgi?id=503838
    // We can't call KNotification::close or else the notifications will be removed from history.
    // 10 seconds is roughly the expected default duration.
    QTimer::singleShot(10000, notification, onExpired);

    notification->sendEvent();
}

ExportManager::Actions FotonCore::autoExportActions() const
{
    using Action = ExportManager::Action;
    using Option = CommandLineOptions::Option;
    bool save = m_startMode != StartMode::Gui && m_cliOptions[Option::Output];
    bool copyImage = m_cliOptions[Option::CopyImage];
    bool copyPath = m_cliOptions[Option::CopyPath];
    ExportManager::Actions actions;
    if (m_startMode != StartMode::Background) {
        save |= Settings::autoSaveImage();
        copyImage |= Settings::clipboardGroup() == Settings::PostScreenshotCopyImage;
        copyPath |= Settings::clipboardGroup() == Settings::PostScreenshotCopyLocation;
    }
    if (m_startMode == StartMode::Gui) {
        actions.setFlag(Action::Save, save);
        actions.setFlag(Action::CopyImage, copyImage);
    } else {
        // In background and tray mode, ensure that either save or copy image is enabled.
        actions.setFlag(Action::Save, save || !copyImage);
        actions.setFlag(Action::CopyImage, !actions.testFlag(Action::Save) || copyImage);
    }
    actions.setFlag(Action::CopyPath, actions.testFlag(Action::Save) && copyPath);
    return actions;
}

ImagePlatform::GrabMode FotonCore::toGrabMode(CaptureModeModel::CaptureMode captureMode) const
{
    using GrabMode = ImagePlatform::GrabMode;
    using CaptureMode = CaptureModeModel::CaptureMode;
    const auto &supportedGrabModes = m_imagePlatform->supportedGrabModes();
    if (captureMode == CaptureMode::CurrentScreen
        && supportedGrabModes.testFlag(ImagePlatform::CurrentScreen)) {
        return GrabMode::CurrentScreen;
    } else if (captureMode == CaptureMode::ActiveWindow
        && supportedGrabModes.testFlag(ImagePlatform::ActiveWindow)) {
        return GrabMode::ActiveWindow;
    } else if (captureMode == CaptureMode::WindowUnderCursor
        && supportedGrabModes.testFlag(ImagePlatform::WindowUnderCursor)) {
        if (supportedGrabModes.testFlag(ImagePlatform::TransientWithParent)) {
            return GrabMode::TransientWithParent;
        } else {
            return GrabMode::WindowUnderCursor;
        }
    } else if (captureMode == CaptureMode::RectangularRegion
        && supportedGrabModes.testFlag(ImagePlatform::PerScreenImageNative)) {
        return GrabMode::PerScreenImageNative;
    } else if (captureMode == CaptureMode::AllScreensScaled
        && supportedGrabModes.testFlag(ImagePlatform::AllScreensScaled)) {
        return GrabMode::AllScreensScaled;
    } else if (supportedGrabModes.testFlag(ImagePlatform::AllScreens)) { // default if supported
        return GrabMode::AllScreens;
    } else {
        return GrabMode::NoGrabModes;
    }
}

CaptureModeModel::CaptureMode FotonCore::toCaptureMode(ImagePlatform::GrabMode grabMode) const
{
    using GrabMode = ImagePlatform::GrabMode;
    using CaptureMode = CaptureModeModel::CaptureMode;
    if (grabMode == GrabMode::CurrentScreen) {
        return CaptureMode::CurrentScreen;
    } else if (grabMode == GrabMode::ActiveWindow) {
        return CaptureMode::ActiveWindow;
    } else if (grabMode == GrabMode::WindowUnderCursor || grabMode == GrabMode::TransientWithParent) {
        return CaptureMode::WindowUnderCursor;
    } else if (grabMode == GrabMode::PerScreenImageNative) {
        return CaptureMode::RectangularRegion;
    } else if (grabMode == GrabMode::AllScreensScaled) {
        return CaptureMode::AllScreensScaled;
    } else {
        if (m_imagePlatform->supportedGrabModes().testFlag(ImagePlatform::CurrentScreen)) {
            return CaptureMode::AllScreens;
        } else {
            return CaptureMode::FullScreen;
        }
    }
}

bool FotonCore::isGuiNull() const
{
    return FotonWindow::instances().isEmpty();
}

void FotonCore::initGuiNoScreenshot()
{
    initViewerWindow(ViewerWindow::Dialog);
    ViewerWindow::instance()->setVisible(true);
    ViewerWindow::instance()->requestActivate();
}

// Hurry up the sync if the sync timer is active.
void FotonCore::syncExportImage()
{
    if (!m_annotationSyncTimer->isActive()) {
        return;
    }
    auto image = m_annotationDocument->renderToImage();
    auto canvasPos = m_annotationDocument->canvasRect().topLeft();
    ImageMetaData::setLogicalXY(image, canvasPos.x(), canvasPos.y());
    setExportImage(image);
}

// A convenient way to stop the sync timer and set the export image.
void FotonCore::setExportImage(const QImage &image)
{
    m_annotationSyncTimer->stop();
    ExportManager::instance()->setImage(image);
}

QQmlEngine *FotonCore::getQmlEngine()
{
    if (m_engine == nullptr) {
        m_engine = std::make_unique<QQmlEngine>(this);
        m_engine->rootContext()->setContextObject(new KLocalizedQmlContext(m_engine.get()));
    }
    return m_engine.get();
}

void FotonCore::initCaptureWindows(CaptureWindow::Mode mode)
{
    deleteWindows();

    // The capture windows always show an opaque screenshot, so they don't need transparency.
    // Transparent windows are also slower on Windows.
    QQuickWindow::setDefaultAlphaBuffer(false);

    auto engine = getQmlEngine();
    const auto screens = qApp->screens();
    for (auto *screen : screens) {
        const auto screenRect = Geometry::logicalScreenRect(screen);
        // Don't show windows for screens that don't have an image.
        if (!m_annotationDocument->baseImage().isNull()
            && !screenRect.intersects(m_annotationDocument->canvasRect())) {
            continue;
        }
        m_captureWindows.emplace_back(CaptureWindow::makeUnique(mode, screen, engine));
    }
}

void FotonCore::initViewerWindow(ViewerWindow::Mode mode)
{
    // always switch to gui mode when a viewer window is used.
    m_startMode = FotonCore::StartMode::Gui;
    m_returnToViewer = true;
    deleteWindows();

    // Transparency isn't needed for this window.
    QQuickWindow::setDefaultAlphaBuffer(false);

    m_viewerWindow = ViewerWindow::makeUnique(mode, getQmlEngine());
}

void FotonCore::preloadUserInterface()
{
    if (!CaptureWindow::instances().isEmpty() || !qApp->primaryScreen()) {
        return;
    }
    // A window that is never shown, the QML modules stay loaded after it is deleted.
    const auto window = CaptureWindow::makeUnique(CaptureWindow::Image, qApp->primaryScreen(), getQmlEngine());
}

void FotonCore::startNewDocument()
{
    m_annotationDocument->clearAnnotations();
    m_savedUndoStackDepth = m_annotationDocument->undoStackDepth();

    // The shadow is a setting of each annotation tool, set the default for all of them.
    auto tool = m_annotationDocument->tool();
    const auto currentType = tool->type();
    for (auto type : {AnnotationTool::FreehandTool,
                      AnnotationTool::LineTool,
                      AnnotationTool::ArrowTool,
                      AnnotationTool::RectangleTool,
                      AnnotationTool::EllipseTool,
                      AnnotationTool::TextTool,
                      AnnotationTool::NumberTool}) {
        tool->setType(type);
        tool->setShadow(Settings::annotationShadow());
    }
    tool->setType(currentType);
}

bool FotonCore::hasUnsavedEdits() const
{
    return m_annotationDocument->undoStackDepth() != m_savedUndoStackDepth;
}

bool FotonCore::isTextRecognitionSupported() const
{
    return TextRecognition::isSupported();
}

void FotonCore::recognizeText()
{
    // With the annotations, the text of the arrows and so on is recognized too.
    syncExportImage();
    const auto image = ExportManager::instance()->image();
    if (image.isNull()) {
        return;
    }
    TextRecognitionWindow::showForImage(image, m_viewerWindow.get());
}

void FotonCore::deleteWindows()
{
    m_viewerWindow.reset();
    m_captureWindows.clear();
}

void FotonCore::updateTaskbarProgress(bool visible, qreal progress) const
{
    WindowsIntegration::setTaskbarProgress(ViewerWindow::instance(), visible, progress);
}

bool FotonCore::isResident() const
{
    return m_resident;
}

void FotonCore::setResident(bool resident)
{
    if (m_resident == resident) {
        return;
    }
    m_resident = resident;
    // Without this, releasing the last QEventLoopLocker or closing the last window
    // would quit the application.
    QCoreApplication::setQuitLockEnabled(!resident);
    QGuiApplication::setQuitOnLastWindowClosed(!resident);
    TrayIcon::instance()->setVisible(resident);
    Q_EMIT residentChanged();
}

void FotonCore::setForceResident(bool force)
{
    m_forceResident = force;
}

void FotonCore::applyBackgroundSettings()
{
    setResident(m_forceResident || Settings::runInBackground());
    // Foton is started with --tray, which keeps it running in the background anyway.
    WindowsIntegration::setStartWithSystem(Settings::startWithWindows());
}

void FotonCore::restart()
{
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    // Keep running in the system tray if this instance does, otherwise show the main window.
    process.setArguments({CommandLineOptions::toArgument(m_resident ? CommandLineOptions::self()->tray : CommandLineOptions::self()->launchOnly)});
    auto environment = QProcessEnvironment::systemEnvironment();
    // KXmlGui sets LANGUAGE for this process, the new one has to set it from the new settings.
    environment.remove(u"LANGUAGE"_s);
    // The new instance waits until this one has quit, otherwise it would pass its arguments to this one.
    environment.insert(u"FOTON_RESTART_PID"_s, QString::number(QCoreApplication::applicationPid()));
    process.setProcessEnvironment(environment);
    if (!process.startDetached()) {
        showErrorMessage(i18nc("@info", "Could not restart Foton."));
        return;
    }
    QCoreApplication::quit();
}

void FotonCore::releaseResources()
{
    if (!m_resident) {
        return;
    }
    m_delayAnimation->stop();
    deleteWindows();
    m_returnToViewer = false;
    m_startMode = StartMode::Tray;
    m_cliOptions.fill(false);
    m_editExistingUrl.clear();
    m_outputUrl.clear();
    SelectionEditor::instance()->forceReleaseCapture(false);
    m_annotationDocument->clear();
    setExportImage({});
}

void FotonCore::activateAction(const QString &actionName, const QVariant &parameter)
{
    Q_UNUSED(parameter)
    m_startMode = StartMode::Tray;
    if (actionName == ShortcutActions::self()->fullScreenAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::AllScreens, 0);
    } else if (actionName == ShortcutActions::self()->currentScreenAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::CurrentScreen, 0);
    } else if (actionName == ShortcutActions::self()->activeWindowAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::ActiveWindow, 0);
    } else if (actionName == ShortcutActions::self()->windowUnderCursorAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::WindowUnderCursor, 0);
    } else if (actionName == ShortcutActions::self()->regionAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::RectangularRegion, 0);
    } else if (actionName == ShortcutActions::self()->openWithoutScreenshotAction()->objectName()) {
        initGuiNoScreenshot();
    }
}

#include "moc_FotonCore.cpp"
