/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 * SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ExportManager.h"
#include "BarcodeScanner.h"
#include "DebugUtils.h"
#include "ImageMetaData.h"
#include "WindowsIntegration.h"
#include "settings.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageWriter>
#include <QLocale>
#include <QLockFile>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPainter>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSaveFile>
#include <QString>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>
#include <QWindow>

#include <KSharedConfig>
#include <KSystemClipboard>

using namespace Qt::StringLiterals;

ExportManager::ExportManager(QObject *parent)
    : QObject(parent)
    , m_imageSavedNotInTemp(false)
    , m_saveImage(QImage())
    , m_tempFile(QUrl())
{
    connect(this, &ExportManager::imageExported, this, [](Actions actions, const QUrl &url) {
        if (actions & AnySave) {
            Settings::setLastImageSaveLocation(url);
            if (actions & SaveAs) {
                Settings::setLastImageSaveAsLocation(url);
            }
        }
    });
}

ExportManager::~ExportManager() = default;

ExportManager *ExportManager::instance()
{
    static ExportManager instance;
    // Ensure the SystemClipboard is instantiated early enough
    KSystemClipboard::instance();
    return &instance;
}

// screenshot image setter and getter

QImage ExportManager::image() const
{
    return m_saveImage;
}

void ExportManager::setImage(const QImage &image)
{
    m_saveImage = image;

    // reset our saved tempfile
    if (m_tempFile.isValid()) {
        m_usedTempFileNames.append(m_tempFile);
        QFile file(m_tempFile.toLocalFile());
        file.remove();
        m_tempFile = QUrl();
    }

    // since the image was modified, we now consider the image unsaved
    m_imageSavedNotInTemp = false;

    Q_EMIT imageChanged();
}

void ExportManager::updateTimestamp()
{
    m_timestamp = QDateTime::currentDateTime();
}

void ExportManager::setTimestamp(const QDateTime &timestamp)
{
    m_timestamp = timestamp;
}

QDateTime ExportManager::timestamp() const
{
    return m_timestamp;
}

// native file save helpers

static QString ensureDefaultLocationExists(const QUrl &saveUrl)
{
    QString savePath = saveUrl.isRelative() ? saveUrl.toString() : saveUrl.toLocalFile();
    savePath = QDir::cleanPath(savePath);

    QDir savePathDir(savePath);
    if (!(savePathDir.exists())) {
        savePathDir.mkpath(u"."_s);
    }
    return savePath;
}

QString ExportManager::defaultSaveLocation()
{
    return ensureDefaultLocationExists(Settings::imageSaveLocation());
}

QUrl ExportManager::getAutosaveFilename() const
{
    const QString baseDir = defaultSaveLocation();
    const QDir baseDirPath(baseDir);
    const QString filename = sanitizedFilePath(formattedFilename(Settings::imageFilenameTemplate(), m_timestamp, ImageMetaData::windowTitle(m_saveImage), Settings::imageSaveLocation()));
    const QString fullpath = autoIncrementFilename(baseDirPath.filePath(filename),
                                                   Settings::preferredImageFormat().toLower(),
                                                   &ExportManager::isFileExists);

    const QUrl fileNameUrl = QUrl::fromUserInput(fullpath);
    if (fileNameUrl.isValid()) {
        return fileNameUrl;
    } else {
        return QUrl();
    }
}

void removeUnlockedDirs(const QStringList &oldDirs)
{
    for (const auto &dirStr : oldDirs) {
        QDir dir(QDir::tempPath() + u'/' + dirStr);
        QLockFile lockFile(dir.filePath(u"lockfile"_s));
        if (dir.exists() && lockFile.tryLock()) {
            lockFile.unlock();
            dir.removeRecursively();
        }
    }
}

const QTemporaryDir *ExportManager::temporaryDir()
{
    if (!m_tempDir) {
        // Cleanup Foton's temp dirs after startup instead of while quitting.
        const auto filters = QDir::Filter::Dirs | QDir::NoDotAndDotDot | QDir::CaseSensitive | QDir::NoSymLinks;
        // Get old dirs before the async stuff to avoid race conditions when making the new dir.
        const auto oldDirs = QDir::temp().entryList({u"Foton.??????"_s}, filters);
        m_tempDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + u"/Foton.XXXXXX"_s);
        m_tempDir->setAutoRemove(false);
        if (m_tempDir->isValid()) {
            m_tempDirLock = std::make_unique<QLockFile>(m_tempDir->filePath(u"lockfile"_s));
            m_tempDirLock->setStaleLockTime(0);
            m_tempDirLock->tryLock();
        }
        auto future = QtConcurrent::run(removeUnlockedDirs, oldDirs);
    }
    return m_tempDir->isValid() ? m_tempDir.get() : nullptr;
}

QString ExportManager::truncatedFilename(QString const &filename)
{
    QString result = filename;
    constexpr auto maxFilenameLength = 255;
    constexpr auto maxExtensionLength = 5; // For example, ".jpeg"
    constexpr auto maxCounterLength = 20; // std::numeric_limits<quint64>::max() == 18446744073709551615
    constexpr auto maxLength = maxFilenameLength - maxCounterLength - maxExtensionLength;
    result.truncate(maxLength);
    return result;
}

inline static void removePlaceholderAndSeparators(const QString &placeholder, QString &string)
{
    if (string.isEmpty() || placeholder.isEmpty()) {
        return;
    }
    using QRE = QRegularExpression;
    // Exclude word characters from being counted as separators.
    // NOTE: The parentheses are for a raw string literal, not a regex capture group.
    static constexpr auto wordSymbol = uR"(\p{L}\p{M}\p{N})";
    // Match 1 or more separator characters.
    // Exclude dir separators because we don't want to accidentally mess up folder layouts.
    // Exclude potential ends of tokens to protect tokens before the placeholder.
    static const auto separatorsBefore = u"[^%1/>]+"_s.arg(wordSymbol);
    // Exclude potential beginnings of tokens to protect tokens after the placeholder.
    static const auto separatorsAfter = u"[^%1/<]+"_s.arg(wordSymbol);

    // You can use the same placeholder multiple times with different combinations of dirs,
    // separator chars, other characters or just text. Because of that, we need to match all of
    // these. I don't want to make a massive regex because that would be a PITA to read and debug.

    const auto beforeAndAfter = u"%1?%2%3?"_s.arg(separatorsBefore, placeholder, separatorsAfter);
    const QRE wholeString(u"^%1$"_s.arg(beforeAndAfter));
    if (string.contains(wholeString)) {
        // Replace placeholder with default screenshot name.
        string.replace(placeholder, u"Screenshot"_s);
        return; // We parsed the whole string already.
    }
    const QRE betweenDirs(u"/%1/"_s.arg(beforeAndAfter));
    string.replace(betweenDirs, u"/"_s);
    // Remove these first so that text before the placeholder is more likely to be preserved.
    const QRE after(placeholder % separatorsAfter);
    string.remove(after);
    // 
    const QRE before(separatorsBefore % placeholder);
    string.remove(before);
    // Just remove the placeholder if none of the other matches work.
    string.remove(placeholder);
}

QString ExportManager::formattedFilename(const QString &nameTemplate, const QDateTime &timestamp, const QString &windowTitle, const QUrl &saveLocation)
{
    if (nameTemplate.isEmpty()) {
        return u"Screenshot"_s;
    }
    QString result = nameTemplate;
    QString baseDir = saveLocation.isValid() ? ensureDefaultLocationExists(saveLocation) : QString{};

    if (!windowTitle.isEmpty()) {
        QString title = windowTitle;
        title.replace(u'/', u'_'); // POSIX doesn't allow "/" in filenames
        title.replace(u'\\', u'_'); // Windows uses "\" as a directory separator
        result.replace("<title>"_L1, title);
    } else {
        removePlaceholderAndSeparators(u"<title>"_s, result);
    }

    // Remove duplicate dir separators.
    // These could come from user mistakes or empty placeholders being removed.
    result.replace(QRegularExpression(u"/+"_s), u"/"_s);

    if (timestamp.isValid()) {
        const auto &locale = QLocale::system();
        // QDateTime can still be valid when timezone is invalid.
        const bool validTimeZone = timestamp.timeZone().isValid();
        for (auto it = filenamePlaceholders.cbegin(); it != filenamePlaceholders.cend(); ++it) {
            if (it->flags.testFlags(Placeholder::IsQDateTime)) {
                int tCount = 0;
                // KDE BUG: https://bugs.kde.org/show_bug.cgi?id=493191
                // QT BUG: https://bugreports.qt.io/browse/QTBUG-129696
                // QCalendarBackend::dateTimeToString crashes when timezone is
                // unset and we try to use the timezone.
                if (!validTimeZone && (tCount = it->base.count(u't')) && tCount == it->base.size()) {
                    removePlaceholderAndSeparators(it->token, result);
                } else {
                    result.replace(it->token, locale.toString(timestamp, it->base));
                }
            }
        }
        // Manual interpretation
        // `h` and `hh` in QDateTime::toString or QLocale::toString
        // are only 12 hour when paired with an AM/PM display in the same toString call,
        // so we have to get the 12 hour format for `<h>` and `<hh>` manually.
        result.replace("<h>"_L1, timestamp.toString(u"hAP"_s).chopped(2));
        result.replace("<hh>"_L1, timestamp.toString(u"hhAP"_s).chopped(2));
        result.replace("<UnixTime>"_L1, QString::number(timestamp.toSecsSinceEpoch()));
    }

    // check if basename includes %[N]d token for sequential file numbering
    QRegularExpression paddingRE;
    paddingRE.setPattern(u"<(#+)>"_s);
    QRegularExpressionMatchIterator it = paddingRE.globalMatch(result);
    if (it.hasNext()) {
        // strip any subdirectories from the template to construct the filename matching regex
        // we are matching filenames only, not paths
        QString resultCopy = QRegularExpression::escape(result.section(u'/', -1));
        QList<QRegularExpressionMatch> matches;
        while (it.hasNext()) {
            QRegularExpressionMatch paddingMatch = it.next();
            matches.push_back(paddingMatch);
            // determine padding value
            int paddedLength = 1;
            if (!paddingMatch.captured(1).isEmpty()) {
                paddedLength = paddingMatch.captured(1).length();
            }
            QString escapedMatch = QRegularExpression::escape(paddingMatch.captured());
            resultCopy.replace(escapedMatch, u"(\\d{%1,})"_s.arg(QString::number(paddedLength)));
        }
        if (result.contains(u'/')) {
            // In case the filename template contains a subdirectory,
            // we need to search for files in the subdirectory instead of the baseDir.
            // so let's add that to baseDir before we search for files.
            baseDir += u"/%1"_s.arg(result.section(u'/', 0, -2));
        }
        // search save directory for files
        QDir dir(baseDir);
        const QStringList fileNames = dir.entryList(QDir::Files, QDir::Name);
        int highestFileNumber = 0;

        // if there are files in the directory...
        if (fileNames.length() > 0) {
            QRegularExpression fileNumberRE;
            fileNumberRE.setPattern(resultCopy);
            // ... check the file names for string matching token with padding specified in result
            const QStringList filteredFiles = fileNames.filter(fileNumberRE);
            // if there are files in the directory that look like the file name with sequential numbering
            if (filteredFiles.length() > 0) {
                // loop through filtered file names looking for highest number
                for (const QString &filteredFile : filteredFiles) {
                    int currentFileNumber = fileNumberRE.match(filteredFile).captured(1).toInt();
                    if (currentFileNumber > highestFileNumber) {
                        highestFileNumber = currentFileNumber;
                    }
                }
            }
        }
        // replace placeholder with next number padded
        for (const auto &match : matches) {
            int paddedLength = 1;
            if (!match.captured(1).isEmpty()) {
                paddedLength = match.captured(1).length();
            }
            const QString nextFileNumberPadded = QString::number(highestFileNumber + 1).rightJustified(paddedLength, u'0');
            result.replace(match.captured(), nextFileNumberPadded);
        }
    }

    // Remove leading and trailing '/'
    while (result.startsWith(u'/')) {
        result.remove(0, 1);
    }
    while (result.endsWith(u'/')) {
        result.chop(1);
    }

    if (result.isEmpty()) {
        result = u"Screenshot"_s;
    }
    return truncatedFilename(result);
}

QString ExportManager::sanitizedFilePath(const QString &path)
{
#ifdef Q_OS_WIN
    // https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
    static const QString invalidChars = u"<>:\"\\|?*"_s;
    static const QStringList reservedNames = {
        u"CON"_s,  u"PRN"_s,  u"AUX"_s,  u"NUL"_s,  u"COM1"_s, u"COM2"_s, u"COM3"_s, u"COM4"_s, u"COM5"_s, u"COM6"_s, u"COM7"_s,
        u"COM8"_s, u"COM9"_s, u"LPT1"_s, u"LPT2"_s, u"LPT3"_s, u"LPT4"_s, u"LPT5"_s, u"LPT6"_s, u"LPT7"_s, u"LPT8"_s, u"LPT9"_s,
    };
    QStringList components = path.split(u'/');
    for (auto &component : components) {
        for (auto &c : component) {
            if (c.unicode() < 32 || invalidChars.contains(c)) {
                c = u'_';
            }
        }
        // File and folder names can't end with a space or a period.
        while (component.endsWith(u' ') || component.endsWith(u'.')) {
            component.chop(1);
        }
        const auto baseName = component.section(u'.', 0, 0);
        if (reservedNames.contains(baseName, Qt::CaseInsensitive)) {
            component.insert(baseName.size(), u'_');
        }
    }
    components.removeAll(QString());
    auto result = components.join(u'/');
    return result.isEmpty() ? u"Screenshot"_s : result;
#else
    QString result = path;
    result.replace(QChar(0), u'_');
    return result;
#endif
}

QString ExportManager::autoIncrementFilename(const QString &baseName, const QString &extension, FileNameAlreadyUsedCheck isFileNameUsed) const
{
    QString result = truncatedFilename(baseName) + u'.' + extension;
    if (!((this->*isFileNameUsed)(QUrl::fromUserInput(result)))) {
        return result;
    }

    QString fileNameFmt = truncatedFilename(baseName) + u"-%1.";
    for (quint64 i = 1; i < std::numeric_limits<quint64>::max(); i++) {
        result = fileNameFmt.arg(i) + extension;
        if (!((this->*isFileNameUsed)(QUrl::fromUserInput(result)))) {
            return result;
        }
    }

    // unlikely this will ever happen, but just in case we've run
    // out of numbers

    result = fileNameFmt.arg(u"OVERFLOW-" + QString::number(QRandomGenerator::global()->bounded(10000)));
    return truncatedFilename(result) + extension;
}

QString ExportManager::imageFileSuffix(const QUrl &url) const
{
    QMimeDatabase mimedb;
    const QString type = mimedb.mimeTypeForUrl(url).preferredSuffix();

    if (type.isEmpty()) {
        return Settings::self()->preferredImageFormat().toLower();
    }
    return type;
}

QImage scaledImageFromSubGeometry(const QImage &image)
{
    const auto subGeometryList = ImageMetaData::subGeometryList(image);
    if (subGeometryList.empty()) {
        return image;
    }
    const auto imageDpr = image.devicePixelRatio();
    const bool hasIntImageDpr = fmod(imageDpr, 1) == 0;
    const QRectF canvasRect{ImageMetaData::logicalXY(image), image.deviceIndependentSize()};
    qreal maxIntersectingDpr = 1;
    bool allIntIntersectingDprs = hasIntImageDpr;
    QSet<qreal> dprSet; // set of unique device pixel ratios
    for (auto it = subGeometryList.cbegin(); it != subGeometryList.cend(); ++it) {
        const auto rect = ImageMetaData::rectFromSubGeometryPropertyMap(*it);
        const auto dpr = it->value(ImageMetaData::Keys::SubGeometryProperty::DevicePixelRatio);
        if (!rect.intersects(canvasRect)) {
            continue;
        }
        allIntIntersectingDprs = allIntIntersectingDprs && fmod(dpr, 1) == 0;
        maxIntersectingDpr = std::max(maxIntersectingDpr, dpr);
        dprSet.insert(dpr);
    }
    const bool invalidDpr = maxIntersectingDpr <= 0;
    const bool fastScale = !invalidDpr && fmod(imageDpr, maxIntersectingDpr) == 0;
    if (invalidDpr || maxIntersectingDpr == imageDpr //
        || (dprSet.size() > 1 && !fastScale && !allIntIntersectingDprs)) {
        Log::debug() << "Unscaled:" << "imageDpr" << imageDpr << "maxIntersectingDpr" << maxIntersectingDpr << "allIntIntersectingDprs" << allIntIntersectingDprs << "fastScale" << fastScale;
        return image;
    }
    const qreal scale = maxIntersectingDpr / imageDpr;
    Log::debug() << "Scaled:" << "imageDpr" << imageDpr << "maxIntersectingDpr" << maxIntersectingDpr << "allIntIntersectingDprs" << allIntIntersectingDprs << "fastScale" << fastScale << "scale" << scale;
    return image.transformed(QTransform::fromScale(scale, scale), //
                             fastScale ? Qt::FastTransformation : Qt::SmoothTransformation);
}

bool ExportManager::writeImage(QIODevice *device, const QByteArray &suffix, QByteArrayView encodedImage)
{
    if (!encodedImage.isNull()) {
        return device->write(encodedImage.data(), encodedImage.size()) == encodedImage.size();
    }

    // In the documentation for QImageWriter, it is a bit ambiguous what "format" means.
    // From looking at how QImageWriter handles the built-in supported formats internally,
    // "format" basically means the file extension, not the mimetype.
    QImageWriter imageWriter(device, suffix);
    if (imageWriter.supportsOption(QImageIOHandler::Quality)) {
        imageWriter.setQuality(Settings::imageCompressionQuality());
    }
    /** Set compression 50 if the format is png. Otherwise if no compression value is specified
     *  it will fallback to using quality (QTBUG-43618) and produce huge files.
     *  See also qpnghandler.cpp#n1075. The other formats that do compression seem to have it
     *  enabled by default and only disabled if compression is set to 0, also any value except 0
     *  has the same effect for them.
     */
    if (suffix == "png") {
        imageWriter.setCompression(50);
    }
    if (!(imageWriter.canWrite())) {
        Q_EMIT errorMessage(i18nc("%1 is an error message", "QImageWriter cannot write image: %1", imageWriter.errorString()));
        return false;
    }
    // Scale image to original scale if possible.
    // This is done here because we need the highest resolution version in the rest of the app.
    return imageWriter.write(scaledImageFromSubGeometry(m_saveImage));
}

bool ExportManager::localSave(const QUrl &url, const QString &suffix, QByteArrayView encodedImage)
{
    // Create save directory if it doesn't exist
    const QString dirPath = QFileInfo(url.toLocalFile()).absolutePath();
    const QDir dir(dirPath);

    if (!dir.mkpath(u"."_s)) {
        Q_EMIT errorMessage(xi18nc("@info",
                                   "Cannot save screenshot because creating "
                                   "the directory failed:<nl/><filename>%1</filename>",
                                   QDir::toNativeSeparators(dirPath)));
        return false;
    }

    QSaveFile outputFile(url.toLocalFile());

    if (!outputFile.open(QFile::WriteOnly)) {
        Q_EMIT errorMessage(i18n("Cannot save screenshot. Error while opening file."));
        return false;
    }
    if (!writeImage(&outputFile, suffix.toLatin1(), encodedImage) || !outputFile.commit()) {
        Q_EMIT errorMessage(i18n("Cannot save screenshot. Error while writing file."));
        return false;
    }
    return true;
}

QUrl ExportManager::tempSave()
{
    // if we already have a temp file saved, use that
    if (m_tempFile.isValid()) {
        if (QFile(m_tempFile.toLocalFile()).exists()) {
            return m_tempFile;
        }
    }

    auto tempDir = this->temporaryDir();
    if (tempDir) {
        // create the temporary file itself with normal file name and also unique one for this session
        // supports the use-case of creating multiple screenshots in a row
        // and exporting them to the same destination e.g. via clipboard,
        // where the temp file name is used as filename suggestion
        const auto formattedFilename = sanitizedFilePath(this->formattedFilename(Settings::imageFilenameTemplate(), m_timestamp, ImageMetaData::windowTitle(m_saveImage), Settings::imageSaveLocation()));
        const QString baseFileName = m_tempDir->path() + u'/' + QUrl::fromLocalFile(formattedFilename).fileName();

        QString suffix = imageFileSuffix(QUrl(baseFileName));
        const QString fileName = autoIncrementFilename(baseFileName, suffix, &ExportManager::isTempFileAlreadyUsed);
        QSaveFile tmpFile(fileName);
        if (tmpFile.open(QFile::WriteOnly)) {
            if (writeImage(&tmpFile, suffix.toLatin1()) && tmpFile.commit()) {
                m_tempFile = QUrl::fromLocalFile(tmpFile.fileName());
#ifndef Q_OS_WIN
                // try to make sure 3rd-party which gets the url of the temporary file e.g. on export
                // properly treats this as readonly, also hide from other users.
                // Not on Windows, where copies of the file (e.g. after drag and drop) would also be read-only.
                tmpFile.setPermissions(QFile::ReadUser);
#endif
                return m_tempFile;
            }
        }
    }

    Q_EMIT errorMessage(i18n("Cannot save screenshot. Error while writing temporary local file."));
    return QUrl();
}

bool ExportManager::save(const QUrl &url, QByteArrayView encodedImage)
{
    if (!(url.isValid())) {
        Q_EMIT errorMessage(i18n("Cannot save screenshot. The save filename is invalid."));
        return false;
    }

    if (!url.isLocalFile()) {
        Q_EMIT errorMessage(i18n("Cannot save screenshot. Saving to remote locations is not supported."));
        return false;
    }

    const QString suffix = imageFileSuffix(url);
    const bool saveSucceded = localSave(url, suffix, encodedImage);
    if (saveSucceded) {
        m_imageSavedNotInTemp = true;
        WindowsIntegration::addToRecentDocuments(url);
    }
    return saveSucceded;
}

bool ExportManager::isFileExists(const QUrl &url) const
{
    if (!(url.isValid()) || !url.isLocalFile()) {
        return false;
    }
    return QFileInfo::exists(url.toLocalFile());
}

bool ExportManager::isImageSavedNotInTemp() const
{
    return m_imageSavedNotInTemp;
}

bool ExportManager::isTempFileAlreadyUsed(const QUrl &url) const
{
    return m_usedTempFileNames.contains(url);
}

void ExportManager::chooseSaveAsUrl(const QUrl &url, QWindow *transientParent, const std::function<void(const QUrl &)> &callback)
{
    QStringList supportedFilters;

    // construct the supported mimetype list
    const auto mimeTypes = QImageWriter::supportedMimeTypes();
    supportedFilters.reserve(mimeTypes.count());
    for (const auto &mimeType : mimeTypes) {
        supportedFilters.append(QString::fromUtf8(mimeType).trimmed());
    }

    // construct the file name
    const QString filenameExtension = Settings::self()->preferredImageFormat().toLower();
    const QString mimetype = QMimeDatabase().mimeTypeForFile(u"~/fakefile."_s + filenameExtension, QMimeDatabase::MatchExtension).name();
    auto dialog = new QFileDialog();
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->setFileMode(QFileDialog::AnyFile);
    QUrl dirUrl = url.adjusted(QUrl::RemoveFilename);
    if (!dirUrl.isValid()) {
        dirUrl = Settings::self()->lastImageSaveAsLocation().adjusted(QUrl::RemoveFilename);
        if ((dirUrl.isLocalFile() && !QDir(dirUrl.toLocalFile()).exists())
            || (!dirUrl.isLocalFile() && !isFileExists(dirUrl))) {
            dirUrl = QUrl::fromLocalFile(defaultSaveLocation());
        }
    }
    dialog->setDirectoryUrl(dirUrl);
    const auto formattedFilename = this->formattedFilename(Settings::imageFilenameTemplate(), m_timestamp, ImageMetaData::windowTitle(m_saveImage), dirUrl);
    const auto fileName = !url.fileName().isEmpty()
        ? url.fileName()
        : (sanitizedFilePath(formattedFilename) + u"."_s + filenameExtension);
    dialog->selectFile(fileName);
    dialog->setDefaultSuffix(u"."_s + filenameExtension);
    dialog->setMimeTypeFilters(supportedFilters);
    dialog->selectMimeTypeFilter(mimetype);

    // The dialog is shown above this window, e.g. above the full screen windows for selecting a region.
    if (transientParent) {
        dialog->winId();
        dialog->windowHandle()->setTransientParent(transientParent);
    }

    // Don't use exec() like the QFileDialog docs show.
    // It can cause a race condition that leads to a crash when the QML environment is being destroyed.
    connect(dialog, &QFileDialog::finished, this, [callback, dialog](int result) {
        dialog->deleteLater();
        const auto &selectedUrl = dialog->selectedUrls().value(0, QUrl());
        callback(result == QDialog::Accepted && !selectedUrl.fileName().isEmpty() ? selectedUrl : QUrl());
    });
    dialog->open();
}

void ExportManager::exportImage(ExportManager::Actions actions, QUrl url)
{
    if (m_saveImage.isNull() && actions & (Save | SaveAs | CopyImage)) {
        Q_EMIT errorMessage(i18n("Cannot save an empty screenshot image."));
        return;
    }

    bool success = false;
    if (actions & SaveAs) {
        chooseSaveAsUrl(url, QGuiApplication::focusWindow(), [actions, url](const QUrl &selectedUrl) mutable {
            const bool accepted = !selectedUrl.isEmpty();
            if (accepted) {
                url = selectedUrl;
            }
            // Replace SaveAs with Save since we will definitely be using this URL.
            actions.setFlag(SaveAs, false);
            actions.setFlag(Save, accepted);
            Settings::setLastImageSaveAsLocation(url);
            // restart the export now that we've done the save as action
            ExportManager::instance()->exportImage(actions, url);
        });
        return;
    }

    const auto preferredFormat = Settings::preferredImageFormat().toLower();
    QByteArray sharedImageData;
    bool hasSharedImageData = false;
    bool saved = actions & AnySave;
    if (saved) {
        if (!url.isValid()) {
            url = getAutosaveFilename();
        }

        const QString saveFormat = imageFileSuffix(url);
        const QString canonicalPreferredFormat =
            QMimeDatabase().mimeTypeForFile(u"image."_s + preferredFormat, QMimeDatabase::MatchExtension).preferredSuffix();
        if (actions & CopyImage && (saveFormat == preferredFormat || saveFormat == canonicalPreferredFormat)) {
            QBuffer buffer(&sharedImageData);
            if (buffer.open(QIODevice::WriteOnly)) {
                hasSharedImageData = writeImage(&buffer, saveFormat.toLatin1());
            }
        }

        const QByteArrayView encodedImage = hasSharedImageData ? QByteArrayView(sharedImageData) : QByteArrayView();
        saved = success = save(url, encodedImage);
        if (!success) {
            actions.setFlag(Save, false);
            actions.setFlag(SaveAs, false);
        }
    }

    if (actions & CopyImage) {
        if (!url.isValid() && m_imageSavedNotInTemp) {
            url = Settings::self()->lastImageSaveLocation();
        }
        auto data = new QMimeData();
        // TODO: Maybe copy a temp file URL instead? That way we could reliably
        // paste as the preferred format without decompression. The issue with
        // that is that some apps like Discord won't copy temp files when in a
        // Flatpak even if you use KUrlMimeData::exportUrlsToPortal().
        auto image = scaledImageFromSubGeometry(m_saveImage);
        QByteArray encodedImage;
        if (hasSharedImageData) {
            encodedImage = sharedImageData;
        } else {
            QBuffer buffer(&encodedImage);
            buffer.open(QIODevice::WriteOnly);
            QImageWriter writer(&buffer, preferredFormat.toLatin1());
            if (preferredFormat != u"png" && writer.supportsOption(QImageIOHandler::Quality)) {
                writer.setQuality(Settings::imageCompressionQuality());
            }
            writer.write(image);
        }
        // Set first so that it gets chosen first.
        data->setData(u"image/" + preferredFormat, encodedImage);
        // Use the standard way to set images to expose all the other formats.
        // We use the uncompressed image because lossy compressed formats will
        // decompress when turned into QImages and become 2-8x larger than their
        // compressed size.
        data->setImageData(image);
        // "x-kde-force-image-copy" is handled by Klipper.
        // It ensures that the image is copied to Klipper even when
        // "Include in history: Image data copied explicitly" setting
        // is not checked in Klipper.
        data->setData(u"x-kde-force-image-copy"_s, QByteArray());
        QString fileName = url.fileName();
        if (fileName.isEmpty()) {
            fileName = getAutosaveFilename().fileName();
        }
        fileName.truncate(fileName.lastIndexOf(u'.'));
        if (!fileName.isEmpty()) {
            fileName += u'.' + preferredFormat;
        }
        // "application/x-kde-suggestedfilename" is handled by KIO/PasteJob.
        // It should put a useful default filename in the paste dialog.
        data->setData(u"application/x-kde-suggestedfilename"_s, QFile::encodeName(fileName));
        KSystemClipboard::instance()->setMimeData(data, QClipboard::Clipboard);
        success = true;
    }

    if (actions & CopyPath
        // This behavior has no relation to the setting in the config UI,
        // but it was added to solve this feature request:
        // https://bugs.kde.org/show_bug.cgi?id=357423
        || (saved && Settings::clipboardGroup() == Settings::PostScreenshotCopyLocation)) {
        if (!url.isValid()) {
            if (m_imageSavedNotInTemp) {
                // The image has been saved (manually or automatically),
                // we need to choose that file path
                url = Settings::self()->lastImageSaveLocation();
            } else {
                // use a temporary save path, and copy that to clipboard instead
                url = ExportManager::instance()->tempSave();
            }
        }

        // will be deleted for us by the platform's clipboard manager.
        auto data = new QMimeData();
        data->setText(url.toLocalFile());
        KSystemClipboard::instance()->setMimeData(data, QClipboard::Clipboard);
        success = true;
    }

    if (success) {
        // when copying to the clipboard, make sure the process stays long enough for transfer to be seen by klipper and start
        QSharedPointer<QEventLoopLocker> lock(new QEventLoopLocker);
        QTimer::singleShot(2000, [lock](){});

        Q_EMIT imageExported(actions, url);
    }
}

void ExportManager::scanQRCode()
{
    auto scan = [this] {
        const auto content = BarcodeScanner::scan(ExportManager::instance()->image());
        if (content.isValid()) {
            Q_EMIT qrCodeScanned(content);
        }
    };
    auto future = QtConcurrent::run(scan);
}

using enum ExportManager::Placeholder::Flag;
const QList<ExportManager::Placeholder> ExportManager::filenamePlaceholders{
    {Date | Extra | IsQDateTime, u"d"_s, ki18nc("filename placeholder description", "Day (1–31)")},
    {Date | IsQDateTime, u"dd"_s, ki18nc("filename placeholder description", "Day (01–31)")},
    {Date | Extra | IsQDateTime, u"ddd"_s, ki18nc("filename placeholder description", "Day (short name)")},
    {Date | Extra | IsQDateTime, u"dddd"_s, ki18nc("filename placeholder description", "Day (long name)")},
    {Date | Extra | IsQDateTime, u"M"_s, ki18nc("filename placeholder description", "Month (1–12)")},
    {Date | IsQDateTime, u"MM"_s, ki18nc("filename placeholder description", "Month (01–12)")},
    {Date | IsQDateTime, u"MMM"_s, ki18nc("filename placeholder description", "Month (short name)")},
    {Date | IsQDateTime, u"MMMM"_s, ki18nc("filename placeholder description", "Month (long name)")},
    {Date | IsQDateTime, u"yy"_s, ki18nc("filename placeholder description", "Year (2 digit)")},
    {Date | IsQDateTime, u"yyyy"_s, ki18nc("filename placeholder description", "Year (4 digit)")},
    {Time | Extra | IsQDateTime, u"H"_s, ki18nc("filename placeholder description", "Hour (0–23)")},
    {Time | IsQDateTime, u"HH"_s, ki18nc("filename placeholder description", "Hour (00–23)")},
    {Time | Extra, u"h"_s, ki18nc("filename placeholder description", "Hour (1–12)")},
    {Time, u"hh"_s, ki18nc("filename placeholder description", "Hour (01–12)")},
    {Time | Extra | IsQDateTime, u"m"_s, ki18nc("filename placeholder description", "Minute (0–59)")},
    {Time | IsQDateTime, u"mm"_s, ki18nc("filename placeholder description", "Minute (00–59)")},
    {Time | Extra | IsQDateTime, u"s"_s, ki18nc("filename placeholder description", "Second (0–59)")},
    {Time | IsQDateTime, u"ss"_s, ki18nc("filename placeholder description", "Second (00–59)")},
    {Time | Extra | IsQDateTime, u"z"_s, ki18nc("filename placeholder description", "Millisecond (0–999)")},
    {Time | Extra | IsQDateTime, u"zz"_s}, // same as `z`
    {Time | Extra | IsQDateTime, u"zzz"_s, ki18nc("filename placeholder description", "Millisecond (000–999)")},
    {Time | Extra | IsQDateTime, u"AP"_s, ki18nc("filename placeholder description", "AM/PM")},
    {Time | Extra | IsQDateTime, u"A"_s}, // same as `AP`
    {Time | Extra | IsQDateTime, u"ap"_s, ki18nc("filename placeholder description", "am/pm")},
    {Time | Extra | IsQDateTime, u"a"_s}, // same as `ap`
    {Time | IsQDateTime, u"Ap"_s, ki18nc("filename placeholder description", "AM/PM or am/pm (localized)")},
    {Time | Extra | IsQDateTime, u"aP"_s}, // same as `Ap`
    {Time | Extra | IsQDateTime, u"t"_s, ki18nc("filename placeholder description", "Timezone (short name)")},
    {Time | IsQDateTime, u"tt"_s, ki18nc("filename placeholder description", "Timezone offset (±0000)")},
    {Time | Extra | IsQDateTime, u"ttt"_s, ki18nc("filename placeholder description", "Timezone offset (±00:00)")},
    {Time | Extra | IsQDateTime, u"tttt"_s, ki18nc("filename placeholder description", "Timezone (long name)")},
    {Time | Extra, u"UnixTime"_s, ki18nc("filename placeholder description", "Seconds since the Unix epoch")},
    {Other, u"title"_s, ki18nc("filename placeholder description", "Window Title")},
    {Other, u"#"_s, ki18nc("filename placeholder description", "Sequential numbering, padded by inserting additional '#' characters")},
};

#include "moc_ExportManager.cpp"
