/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-FileCopyrightText: 2019 Boudhayan Gupta <bgupta@kde.org>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "ImagePlatform.h"

class ImagePlatformNull final : public ImagePlatform
{
    Q_OBJECT

public:
    explicit ImagePlatformNull(QObject *parent = nullptr);
    ~ImagePlatformNull() override = default;

    GrabModes supportedGrabModes() const override final;
    ShutterModes supportedShutterModes() const override final;

public Q_SLOTS:

    void doGrab(ImagePlatform::ShutterMode shutterMode,
                ImagePlatform::GrabMode grabMode,
                bool includePointer,
                bool includeDecorations,
                bool includeShadow) override final;
};
