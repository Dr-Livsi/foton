/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-FileCopyrightText: 2019 Boudhayan Gupta <bgupta@kde.org>

 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QFlags>
#include <QImage>
#include <QObject>
#include <qqmlregistration.h>

class ImagePlatform : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by FotonCore")

    Q_PROPERTY(GrabModes supportedGrabModes READ supportedGrabModes NOTIFY supportedGrabModesChanged)
    // Currently, supportedShutterModes never changes.
    // Be sure to add a changed signal if it is ever able to change.
    Q_PROPERTY(ShutterModes supportedShutterModes READ supportedShutterModes CONSTANT)

public:
    enum GrabMode {
        NoGrabModes =           0b0000000,
        AllScreens =            0b0000001,
        CurrentScreen =         0b0000010,
        ActiveWindow =          0b0000100,
        WindowUnderCursor =     0b0001000,
        TransientWithParent =   0b0010000,
        AllScreensScaled =      0b0100000,
        PerScreenImageNative =  0b1000000,
    };
    Q_DECLARE_FLAGS(GrabModes, GrabMode)
    Q_FLAG(GrabModes)

    enum ShutterMode { Immediate = 0x01, OnClick = 0x02 };
    Q_DECLARE_FLAGS(ShutterModes, ShutterMode)
    Q_FLAG(ShutterModes)

    explicit ImagePlatform(QObject *parent = nullptr);
    ~ImagePlatform() override = default;

    virtual GrabModes supportedGrabModes() const = 0;
    virtual ShutterModes supportedShutterModes() const = 0;

public Q_SLOTS:
    virtual void
    doGrab(ImagePlatform::ShutterMode shutterMode, ImagePlatform::GrabMode grabMode, bool includePointer, bool includeDecorations, bool includeShadow) = 0;

protected:
    /**
     * Combine images of multiple screens into one image.
     * Every image needs the logical position of its screen (see ImageMetaData::setLogicalXY())
     * and the device pixel ratio of its screen. Information about the screens is stored in the
     * resulting image (see ImageMetaData::setSubGeometryList()).
     */
    static QImage combinedImage(const QList<QImage> &images);

Q_SIGNALS:
    void supportedGrabModesChanged();

    void newScreenshotTaken(const QImage &image = {});
    void newCroppableScreenshotTaken(const QImage &image);

    void newScreenshotFailed(const QString &message = {});
    void newScreenshotCanceled();
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ImagePlatform::GrabModes)
Q_DECLARE_OPERATORS_FOR_FLAGS(ImagePlatform::ShutterModes)
