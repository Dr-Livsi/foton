/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-FileCopyrightText: 2019 Boudhayan Gupta <bgupta@kde.org>

 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "PlatformLoader.h"
#include "Config.h"

#include "PlatformNull.h"

#ifdef Q_OS_WIN
#include "ImagePlatformWindows.h"
#endif

#include <QDebug>

ImagePlatformPtr getForcedImagePlatform()
{
    // This environment variable is only for testing purposes.
    auto platformName = qgetenv("FOTON_IMAGE_PLATFORM");
    if (platformName.isEmpty()) {
        return nullptr;
    }

#ifdef Q_OS_WIN
    if (platformName == ImagePlatformWindows::staticMetaObject.className()) {
        return std::make_unique<ImagePlatformWindows>();
    }
#endif
    if (platformName == ImagePlatformNull::staticMetaObject.className()) {
        return std::make_unique<ImagePlatformNull>();
    }
    qWarning() << "FOTON_IMAGE_PLATFORM:" << platformName << "is invalid";
    return nullptr;
}

ImagePlatformPtr loadImagePlatform()
{
    if (auto platform = getForcedImagePlatform()) {
        return platform;
    }
#ifdef Q_OS_WIN
    return std::make_unique<ImagePlatformWindows>();
#else
    // Screenshots are only supported on Windows.
    return std::make_unique<ImagePlatformNull>();
#endif
}
