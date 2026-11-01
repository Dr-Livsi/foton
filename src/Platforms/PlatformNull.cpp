/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-FileCopyrightText: 2019 Boudhayan Gupta <bgupta@kde.org>

 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "PlatformNull.h"

#include <QDebug>
#include <QPixmap>

/* -- Null Platform ---------------------------------------------------------------------------- */

ImagePlatformNull::ImagePlatformNull(QObject *parent)
    : ImagePlatform(parent)
{
}

ImagePlatform::GrabModes ImagePlatformNull::supportedGrabModes() const
{
    return {GrabMode::AllScreens | GrabMode::CurrentScreen | GrabMode::ActiveWindow | GrabMode::WindowUnderCursor | GrabMode::TransientWithParent
            | GrabMode::AllScreensScaled};
}

ImagePlatform::ShutterModes ImagePlatformNull::supportedShutterModes() const
{
    return {ShutterMode::Immediate};
}

void ImagePlatformNull::doGrab(ShutterMode shutterMode, GrabMode grabMode, bool includePointer, bool includeDecorations, bool includeShadow)
{
    Q_UNUSED(shutterMode)
    Q_UNUSED(grabMode)
    Q_UNUSED(includePointer)
    Q_UNUSED(includeDecorations)
    Q_UNUSED(includeShadow)
    Q_EMIT newScreenshotFailed();
}

#include "moc_PlatformNull.cpp"
