/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "BarcodeScanner.h"

#include "foton_debug.h"

#include <ZXing/ReadBarcode.h>

#include <exception>

QVariant BarcodeScanner::scan(const QImage &image)
{
    // ZXing reports errors with exceptions, which are only enabled for this file.
    try {
        const auto grayscale = image.convertToFormat(QImage::Format_Grayscale8);
        const ZXing::ImageView view(grayscale.constBits(), grayscale.width(), grayscale.height(), ZXing::ImageFormat::Lum, grayscale.bytesPerLine());
        const auto barcode = ZXing::ReadBarcode(view);
        if (!barcode.isValid()) {
            return {};
        }
        if (barcode.contentType() == ZXing::ContentType::Binary) {
            const auto &bytes = barcode.bytes();
            return QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
        }
        return QString::fromStdString(barcode.text());
    } catch (const std::exception &error) {
        qCWarning(FOTON_LOG) << "Could not scan for barcodes:" << error.what();
        return {};
    }
}
