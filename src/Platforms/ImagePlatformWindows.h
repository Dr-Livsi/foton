/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "ImagePlatform.h"

/**
 * Takes screenshots on Windows using GDI and the Desktop Window Manager (DWM).
 *
 * Screens are copied from the screen device context in native (physical) pixels.
 * Foton is DPI aware per monitor, so every screen keeps its full resolution and the
 * images get the device pixel ratio of their screen.
 */
class ImagePlatformWindows final : public ImagePlatform
{
    Q_OBJECT

public:
    explicit ImagePlatformWindows(QObject *parent = nullptr);
    ~ImagePlatformWindows() override;

    GrabModes supportedGrabModes() const override final;
    ShutterModes supportedShutterModes() const override final;

public Q_SLOTS:
    void doGrab(ImagePlatform::ShutterMode shutterMode,
                ImagePlatform::GrabMode grabMode,
                bool includePointer,
                bool includeDecorations,
                bool includeShadow) override final;

private Q_SLOTS:
    void updateSupportedGrabModes();

private:
    struct GrabOptions {
        GrabMode grabMode = GrabMode::AllScreens;
        bool includePointer = false;
        bool includeDecorations = true;
        bool includeShadow = false;
    };

    void grabNow(const GrabOptions &options);

    void grabScreens(bool includePointer, bool croppable, bool scaled);
    void grabCurrentScreen(bool includePointer);
    void grabActiveWindow(const GrabOptions &options);
    void grabWindowUnderCursor(const GrabOptions &options, bool withTransients);
    // `window` is a HWND. We don't want windows.h in this header.
    void grabWindow(quintptr window, const GrabOptions &options, bool withTransients);

    GrabModes m_grabModes;
};
