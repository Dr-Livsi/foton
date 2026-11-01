/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-FileCopyrightText: 2019 Boudhayan Gupta <bgupta@kde.org>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ImagePlatform.h"
#include "ImageMetaData.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

ImagePlatform::ImagePlatform(QObject *parent)
    : QObject(parent)
{
}

QImage ImagePlatform::combinedImage(const QList<QImage> &images)
{
    if (images.empty()) {
        return {};
    }
    if (images.size() == 1) {
        return images.constFirst();
    }
    QRectF imageRect;
    qreal maxDpr = 0;
    ImageMetaData::SubGeometryList geometryList;
    for (auto &i : images) {
        const auto dpr = i.devicePixelRatio();
        const auto rect = QRectF{ImageMetaData::logicalXY(i), i.deviceIndependentSize()};
        maxDpr = std::max(maxDpr, dpr);
        imageRect |= rect;
        geometryList << ImageMetaData::subGeometryPropertyMap(rect, dpr);
    }
    static const auto finalFormat = QImage::Format_ARGB32_Premultiplied;
    const bool allSameDpr = std::all_of(images.cbegin(), images.cend(), [maxDpr](const QImage &i){
        return i.devicePixelRatio() == maxDpr;
    });
    // Areas that are not covered by any screen stay transparent.
    if (allSameDpr) {
        QImage finalImage{(imageRect.size() * maxDpr).toSize(), finalFormat};
        finalImage.fill(Qt::transparent);
        QPainter painter(&finalImage);
        for (auto &image : images) {
            // Explicitly setting the position and size so that you don't need to read
            // QPainter source code to understand how this works.
            const auto pos = (ImageMetaData::logicalXY(image) - imageRect.topLeft()) * maxDpr;
            painter.drawImage(QRectF{QPointF{std::round(pos.x()), std::round(pos.y())}, QSizeF(image.size())}, image);
        }
        painter.end();
        // Setting DPR after painting prevents it from affecting the coordinates of the QPainter.
        finalImage.setDevicePixelRatio(maxDpr);
        ImageMetaData::setLogicalXY(finalImage, imageRect.x(), imageRect.y());
        ImageMetaData::setSubGeometryList(finalImage, geometryList);
        return finalImage;
    }
    // We ceil to the next integer size up so that integer DPR images are always crisp.
    const auto finalDpr = std::ceil(maxDpr);
    QImage finalImage{(imageRect.size() * finalDpr).toSize(), finalFormat};
    finalImage.fill(Qt::transparent);
    QPainter painter(&finalImage);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (auto &image : images) {
        const auto pos = (ImageMetaData::logicalXY(image) - imageRect.topLeft()) * finalDpr;
        const auto size = (image.deviceIndependentSize() * finalDpr).toSize();
        const auto imageDpr = image.devicePixelRatio();
        const bool hasIntDpr = static_cast<int>(imageDpr) == imageDpr;
        const auto interpolation = hasIntDpr ? Qt::FastTransformation : Qt::SmoothTransformation;
        painter.drawImage(QRectF{QPointF{std::round(pos.x()), std::round(pos.y())}, QSizeF(size)},
                          size == image.size() //
                              ? image
                              : image.scaled(size, Qt::IgnoreAspectRatio, interpolation));
    }
    painter.end();
    finalImage.setDevicePixelRatio(finalDpr);
    ImageMetaData::setLogicalXY(finalImage, imageRect.x(), imageRect.y());
    ImageMetaData::setSubGeometryList(finalImage, geometryList);
    return finalImage;
}

#include "moc_ImagePlatform.cpp"
