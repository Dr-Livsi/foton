/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

// Text recognition with Windows.Media.Ocr, which is part of Windows 10 and later.

// The headers of C++/WinRT come first, the keywords that Qt defines as macros would break them.
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include "TextRecognition.h"
#include "foton_debug.h"

#include <KLocalizedString>

#include <QPainter>

namespace WinGlobalization = winrt::Windows::Globalization;
namespace WinImaging = winrt::Windows::Graphics::Imaging;
namespace WinOcr = winrt::Windows::Media::Ocr;
namespace WinStreams = winrt::Windows::Storage::Streams;

static QString toQString(const winrt::hstring &string)
{
    return QString::fromWCharArray(string.c_str(), qsizetype(string.size()));
}

static winrt::hstring toHString(const QString &string)
{
    return winrt::hstring(reinterpret_cast<const wchar_t *>(string.utf16()), uint32_t(string.size()));
}

namespace TextRecognition
{
bool isSupported()
{
    return true;
}

QList<Language> availableLanguages()
{
    QList<Language> languages;
    try {
        const auto recognizerLanguages = WinOcr::OcrEngine::AvailableRecognizerLanguages();
        for (const auto &language : recognizerLanguages) {
            QString name = toQString(language.NativeName());
            if (!name.isEmpty()) {
                name[0] = name[0].toUpper();
            }
            languages.append({toQString(language.LanguageTag()), name});
        }
    } catch (const winrt::hresult_error &error) {
        qCWarning(FOTON_LOG) << "Could not get the languages for text recognition:" << toQString(error.message());
    }
    return languages;
}

QString defaultLanguage()
{
    try {
        const auto engine = WinOcr::OcrEngine::TryCreateFromUserProfileLanguages();
        if (engine) {
            return toQString(engine.RecognizerLanguage().LanguageTag());
        }
    } catch (const winrt::hresult_error &error) {
        qCWarning(FOTON_LOG) << "Could not create the text recognition engine:" << toQString(error.message());
    }
    return {};
}

Result recognize(const QImage &image, const QString &languageTag)
{
    Result result;
    if (image.isNull()) {
        result.error = i18nc("@info", "There is no image.");
        return result;
    }
    try {
        WinOcr::OcrEngine engine{nullptr};
        if (!languageTag.isEmpty()) {
            const WinGlobalization::Language language(toHString(languageTag));
            if (WinOcr::OcrEngine::IsLanguageSupported(language)) {
                engine = WinOcr::OcrEngine::TryCreateFromLanguage(language);
            }
        }
        if (!engine) {
            engine = WinOcr::OcrEngine::TryCreateFromUserProfileLanguages();
        }
        if (!engine) {
            result.error = i18nc("@info", "No language for text recognition is installed in Windows.");
            return result;
        }
        result.languageTag = toQString(engine.RecognizerLanguage().LanguageTag());

        // Text on the screen is small for text recognition, it is recognized much better when enlarged.
        const int maxDimension = int(WinOcr::OcrEngine::MaxImageDimension());
        const int longSide = std::max(image.width(), image.height());
        const qint64 area = qint64(image.width()) * image.height();
        qreal scale = 1;
        if (longSide * 3 <= maxDimension && area <= 500'000) {
            scale = 3;
        } else if (longSide * 2 <= maxDimension && area <= 2'600'000) {
            scale = 2;
        }
        // A margin, because text that touches the edges is often not recognized.
        const int margin = 16;
        if (longSide * scale + margin * 2 > maxDimension) {
            scale = qreal(maxDimension - margin * 2) / longSide;
        }
        const QSize scaledSize(qRound(image.width() * scale), qRound(image.height() * scale));

        // BGRA in memory, as SoftwareBitmap expects it.
        QImage input(scaledSize + QSize(margin * 2, margin * 2), QImage::Format_ARGB32_Premultiplied);
        // The margin has the color of the background, the color of the corner is a good guess.
        auto background = image.pixelColor(0, 0);
        background.setAlpha(255);
        input.fill(background);
        {
            QPainter painter(&input);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            auto source = image;
            source.setDevicePixelRatio(1);
            painter.drawImage(QRect(QPoint(margin, margin), scaledSize), source);
        }

        WinStreams::DataWriter writer;
        writer.WriteBytes(winrt::array_view<const uint8_t>(input.constBits(), input.constBits() + input.sizeInBytes()));
        const auto bitmap = WinImaging::SoftwareBitmap::CreateCopyFromBuffer(writer.DetachBuffer(),
                                                                             WinImaging::BitmapPixelFormat::Bgra8,
                                                                             input.width(),
                                                                             input.height(),
                                                                             WinImaging::BitmapAlphaMode::Premultiplied);
        const auto ocrResult = engine.RecognizeAsync(bitmap).get();

        int lineIndex = 0;
        for (const auto &line : ocrResult.Lines()) {
            if (!result.text.isEmpty()) {
                result.text += u'\n';
            }
            const QString lineText = toQString(line.Text());
            const qsizetype lineStart = result.text.size();
            result.text += lineText;
            qsizetype position = 0;
            for (const auto &word : line.Words()) {
                Word recognizedWord;
                recognizedWord.text = toQString(word.Text());
                // The text of a line is made of its words, with spaces between them in most languages.
                qsizetype index = lineText.indexOf(recognizedWord.text, position);
                if (index < 0) {
                    index = std::min(position, lineText.size());
                }
                recognizedWord.start = lineStart + index;
                recognizedWord.length = std::min(recognizedWord.text.size(), lineText.size() - index);
                recognizedWord.line = lineIndex;
                const auto rect = word.BoundingRect();
                recognizedWord.rect = QRectF((rect.X - margin) / scale, (rect.Y - margin) / scale, rect.Width / scale, rect.Height / scale);
                result.words.append(recognizedWord);
                position = index + recognizedWord.length;
            }
            ++lineIndex;
        }
    } catch (const winrt::hresult_error &error) {
        qCWarning(FOTON_LOG) << "Text recognition failed:" << toQString(error.message());
        result.error = toQString(error.message());
    } catch (...) {
        qCWarning(FOTON_LOG) << "Text recognition failed";
        result.error = i18nc("@info", "An unknown error occurred.");
    }
    return result;
}
}
