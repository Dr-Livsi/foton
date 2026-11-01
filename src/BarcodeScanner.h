/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QImage>
#include <QVariant>

namespace BarcodeScanner
{
/**
 * Finds a QR code or another kind of code, e.g. a Data Matrix code or a barcode, in the image.
 * Returns its text as a QString or its binary content as a QByteArray, or nothing if there is none.
 */
QVariant scan(const QImage &image);
}
