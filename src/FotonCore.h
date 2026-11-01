/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-FileCopyrightText: 2019 Boudhayan Gupta <bgupta@kde.org>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QEventLoopLocker>
#include <QObject>
#include <QQmlEngine>
#include <QQuickItem>
#include <QVariantAnimation>

#include "CaptureModeModel.h"
#include "CommandLineOptions.h"
#include "ExportManager.h"
#include "Gui/CaptureWindow.h"
#include "Gui/ViewerWindow.h"
#include "Platforms/PlatformLoader.h"
#include "settings.h"

#include <KQuickImageEditor/AnnotationDocument>

#include <array>
#include <memory>

class FotonCore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(ImagePlatform *imagePlatform READ imagePlatform CONSTANT FINAL)
    Q_PROPERTY(QUrl screenCaptureUrl READ screenCaptureUrl NOTIFY screenCaptureUrlChanged FINAL)
    Q_PROPERTY(int captureTimeRemaining READ captureTimeRemaining NOTIFY captureTimeRemainingChanged FINAL)
    Q_PROPERTY(qreal captureProgress READ captureProgress NOTIFY captureProgressChanged FINAL)
    Q_PROPERTY(AnnotationDocument *annotationDocument READ annotationDocument CONSTANT FINAL)
    Q_PROPERTY(bool textRecognitionSupported READ isTextRecognitionSupported CONSTANT FINAL)

public:
    enum class StartMode {
        Gui = 0,
        // Started in the system tray or activated by a global shortcut.
        // No viewer window is shown after a capture; the result is exported and a notification is shown.
        Tray = 1,
        Background = 2,
    };

    ~FotonCore() noexcept override;

    static FotonCore *instance();

    ImagePlatform *imagePlatform() const;

    AnnotationDocument *annotationDocument() const;

    /// Whether the screenshot was edited since it was taken or saved.
    bool hasUnsavedEdits() const;

    bool isTextRecognitionSupported() const;
    /// Show the text recognized in the screenshot in a new window.
    Q_INVOKABLE void recognizeText();

    QUrl screenCaptureUrl() const;
    void setScreenCaptureUrl(const QUrl &url);
    // Used when setting the URL from CLI
    void setScreenCaptureUrl(const QString &filePath);

    QUrl outputUrl() const;
    void setOutputUrl(const QUrl &url);

    int captureTimeRemaining() const;
    qreal captureProgress() const;


    void initGuiNoScreenshot();

    void syncExportImage();

    ExportManager::Actions autoExportActions() const;

    void activateAction(const QString &actionName, const QVariant &parameter = {});

    /**
     * Whether the application keeps running in the system tray after all windows are closed.
     * In this mode, global shortcuts are registered and FotonCore::allDone does not quit the application.
     */
    bool isResident() const;
    void setResident(bool resident);

    /**
     * Stay resident even if running in the background is disabled in the settings.
     * Used when the application was started with --tray.
     */
    void setForceResident(bool force);

    /**
     * Apply the settings for running in the system tray and starting automatically with the system.
     */
    void applyBackgroundSettings();

    /**
     * Close all windows and release the memory used by the last screenshot.
     * Used instead of quitting while the application is resident.
     */
    void releaseResources();

    /**
     * Load the user interface for selecting a region in advance. Loading the QML modules for the first time
     * takes a few seconds, which would delay the first screenshot after Foton was started in the system tray.
     */
    void preloadUserInterface();

    /**
     * Starts a new instance of Foton and quits this one, e.g. to use another language.
     */
    void restart();

    static FotonCore *create(QQmlEngine *engine, QJSEngine *)
    {
        auto inst = instance();
        Q_ASSERT(inst);
        Q_ASSERT(inst->thread() == engine->thread());

        if (s_qmlEngine) {
            Q_ASSERT(engine == s_qmlEngine);
        } else {
            s_qmlEngine = engine;
        }

        QJSEngine::setObjectOwnership(inst, QJSEngine::CppOwnership);
        return inst;
    }

public Q_SLOTS:
    void activate(const QStringList &arguments, const QString &workingDirectory);
    void takeNewScreenshot(int captureMode = Settings::captureMode(),
                           int timeout = 0,
                           bool includePointer = Settings::includePointer(),
                           bool includeDecorations = Settings::includeDecorations(),
                           bool includeShadow = Settings::includeShadow());
    void cancelScreenshot();
    void showErrorMessage(const QString &message);

Q_SIGNALS:
    void screenCaptureUrlChanged();
    void captureTimeRemainingChanged();
    void captureProgressChanged();

    void allDone();
    void residentChanged();

private:
    explicit FotonCore(QObject *parent = nullptr);

    void takeNewScreenshot(ImagePlatform::GrabMode grabMode, int timeout, bool includePointer, bool includeDecorations, bool includeShadow);
    void setExportImage(const QImage &image);
    void showViewerIfGuiMode(bool minimized = false);
    void doNotify(const ExportManager::Actions &actions, const QUrl &saveUrl);
    ImagePlatform::GrabMode toGrabMode(CaptureModeModel::CaptureMode captureMode) const;
    CaptureModeModel::CaptureMode toCaptureMode(ImagePlatform::GrabMode grabMode) const;
    bool isGuiNull() const;
    QQmlEngine *getQmlEngine();
    void initCaptureWindows(CaptureWindow::Mode mode);
    void initViewerWindow(ViewerWindow::Mode mode);
    void deleteWindows();
    void updateTaskbarProgress(bool visible, qreal progress = 0) const;
    // Clear the annotations for a new image and apply the default settings for annotating.
    void startNewDocument();

    static FotonCore *s_self;
    std::unique_ptr<AnnotationDocument> m_annotationDocument = nullptr;
    StartMode m_startMode = StartMode::Gui;
    bool m_returnToViewer = false;
    // The number of edits that can be undone when the screenshot was taken or saved.
    int m_savedUndoStackDepth = 0;
    bool m_resident = false;
    bool m_forceResident = false;
    QUrl m_screenCaptureUrl;
    std::unique_ptr<ImagePlatform> m_imagePlatform;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QTimer> m_annotationSyncTimer;
    std::unique_ptr<QVariantAnimation> m_delayAnimation;
    std::unique_ptr<QEventLoopLocker> m_eventLoopLocker;

    // Use ViewerWindow::instance() to get the viewer window.
    ViewerWindow::UniquePointer m_viewerWindow = {nullptr, nullptr};

    // Use CaptureWindow::instances() to get the capture windows.
    // Don't assume that this will never have entries that are null.
    // For some reason, removeIf/erase_if/find_if then erase doesn't work with QList/QList,
    // so we have to use std::vector. Something about use of a deleted unique_ptr function.
    std::vector<CaptureWindow::UniquePointer> m_captureWindows;

    std::array<bool, CommandLineOptions::TotalOptions> m_cliOptions = {};

    QUrl m_editExistingUrl;
    QUrl m_outputUrl;

    ImagePlatform::GrabMode m_lastGrabMode = ImagePlatform::GrabMode::NoGrabModes;
    bool m_lastIncludePointer = false; // cli default value
    bool m_lastIncludeDecorations = true; // cli default value
    bool m_lastIncludeShadow = true; // cli default value

    static inline QQmlEngine *s_qmlEngine = nullptr;
};
