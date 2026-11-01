/* SPDX-FileCopyrightText: 2023 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "ExportManager.h"
#include "ImageMetaData.h"

#include <KLocalizedString>

#include <QApplication>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLabel>
#include <QStyle>

using namespace Qt::StringLiterals;

/**
 * A small collection of functions to help prevent duplicating the implementations of the
 * image save options page.
 */

inline void updateFilenamePreview(QLabel *label, const QString &templateFilename, const QUrl saveLocation)
{
    auto exportManager = ExportManager::instance();
    // Likewise, if no timestamp was set yet, we'll produce a new one as placeholder.
    QDateTime timestamp = exportManager->timestamp();
    if (!timestamp.isValid()) {
        exportManager->updateTimestamp();
        timestamp = exportManager->timestamp();
    }
    // If there is no window title, we need to change it to have a placeholder.
    auto title = ImageMetaData::windowTitle(exportManager->image());
    if (title.isEmpty()) {
        title = QGuiApplication::applicationDisplayName();
    }
    const auto filename = ExportManager::sanitizedFilePath(exportManager->formattedFilename(templateFilename, timestamp, title, saveLocation));
    label->setText(xi18nc("@info", "<filename>%1</filename>", filename));

    // Reset any previously empty values that we had temporarily set.
    if (!timestamp.isValid()) {
        exportManager->setTimestamp(timestamp);
    }
}
