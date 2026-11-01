/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>

/**
 * Recognizes text in images with the text recognition (OCR) built into Windows 10 and later.
 *
 * Windows can recognize the languages whose optical character recognition feature is installed,
 * which is usually the case for the display languages of Windows. On other platforms, nothing is available.
 */
namespace TextRecognition
{
struct Language {
    // A BCP-47 language tag, e.g. "en-US".
    QString tag;
    QString name;
};

struct Word {
    QString text;
    // In the pixels of the image.
    QRectF rect;
    // The position in Result::text.
    qsizetype start = 0;
    qsizetype length = 0;
    int line = 0;
};

struct Result {
    // The lines of text separated by line breaks.
    QString text;
    QList<Word> words;
    // The language that was used.
    QString languageTag;
    // Why the text couldn't be recognized, empty if it could.
    QString error;
};

/// Whether text recognition is supported on this platform at all.
bool isSupported();

/// The languages that can be recognized.
QList<Language> availableLanguages();

/// The language of the user that can be recognized, if there is one.
QString defaultLanguage();

/**
 * Recognize the text in the image. This takes a while, so it should not be called in the GUI thread.
 * If the language can't be recognized, the default language is used.
 */
Result recognize(const QImage &image, const QString &languageTag);
}
