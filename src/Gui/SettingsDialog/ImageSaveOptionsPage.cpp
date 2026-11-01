/*
 *  SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ImageSaveOptionsPage.h"

#include "ExportManager.h"
#include "FilenameTemplateEditor.h"
#include "SaveOptionsUtils.h"
#include "SettingsPageUtils.h"
#include "ui_ImageSaveOptions.h"

#include <KLocalizedString>

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QImageIOHandler>
#include <QImageWriter>
#include <QLabel>

using namespace Qt::StringLiterals;

static QStringList getFormatsWithoutQuality()
{
    static QStringList formatsWithoutQuality;
    if (!formatsWithoutQuality.empty()) {
        return formatsWithoutQuality;
    }

    formatsWithoutQuality.push_back(u"PNG"_s);
    formatsWithoutQuality.push_back(u"EXR"_s);

    const auto formats = QImageWriter::supportedImageFormats();
    QBuffer buffer;
    buffer.open(QBuffer::ReadWrite);
    QImageWriter writer(&buffer, "");
    for (const auto &format : formats) {
        writer.setDevice(&buffer);
        writer.setFormat(format);
        if (!writer.supportsOption(QImageIOHandler::Quality)) {
            formatsWithoutQuality.push_back(QString::fromLatin1(format));
        }
    }

    return formatsWithoutQuality;
}

ImageSaveOptionsPage::ImageSaveOptionsPage(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui_ImageSaveOptions)
{
    m_ui->setupUi(this);
    // The folder and the file name can be long, so these fields use the whole width.
    SettingsPageUtils::setUpFormLayouts(this, QFormLayout::AllNonFixedFieldsGrow);

    m_ui->imageCompressionQualityHelpLable->setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
    const int sliderSpinboxHeightDiff = m_ui->qualitySpinner->sizeHint().height() - m_ui->kcfg_imageCompressionQuality->sizeHint().height();
    m_ui->qualityVLayout->setContentsMargins({
        0,
        std::max(0, qRound(sliderSpinboxHeightDiff / 2.0)),
        0,
        0,
    });

    // The label is next to the first line of blocks.
    m_ui->filenameLabel->setMinimumHeight(QComboBox().sizeHint().height() + 4);
    SettingsPageUtils::makeHint(m_ui->filenameHint);
    connect(m_ui->kcfg_imageFilenameTemplate, &FilenameTemplateEditor::filenameTemplateChanged, this, &ImageSaveOptionsPage::updateFilenamePreview);

    m_ui->kcfg_preferredImageFormat->addItems([&]() {
        QStringList items;
        const auto formats = QImageWriter::supportedImageFormats();
        items.reserve(formats.count());
        for (const auto &fmt : formats) {
            items.append(QString::fromLocal8Bit(fmt).toUpper());
        }
        return items;
    }());
    connect(m_ui->kcfg_preferredImageFormat, &QComboBox::currentTextChanged, this, &ImageSaveOptionsPage::updateFilenamePreview);
    connect(m_ui->kcfg_preferredImageFormat, &QComboBox::currentTextChanged, this, &ImageSaveOptionsPage::updateImageCompressionQualityEnabled);
    updateImageCompressionQualityEnabled();

    updateFilenamePreview();
}

ImageSaveOptionsPage::~ImageSaveOptionsPage() = default;

void ImageSaveOptionsPage::updateFilenamePreview()
{
    const auto extension = m_ui->kcfg_preferredImageFormat->currentText().toLower();
    const auto templateBasename = m_ui->kcfg_imageFilenameTemplate->filenameTemplate();
    ::updateFilenamePreview(m_ui->preview, templateBasename + u'.' + extension, Settings::imageSaveLocation());
}

void ImageSaveOptionsPage::updateImageCompressionQualityEnabled()
{
    const auto formatsWithoutQuality = getFormatsWithoutQuality();
    const bool enabled = !formatsWithoutQuality.contains(m_ui->kcfg_preferredImageFormat->currentText(), Qt::CaseInsensitive);

    m_ui->qualityLabel->setEnabled(enabled);
    m_ui->kcfg_imageCompressionQuality->setEnabled(enabled);
    m_ui->qualitySpinner->setEnabled(enabled);
    m_ui->imageCompressionQualityHelpLable->setEnabled(enabled);
}

#include "moc_ImageSaveOptionsPage.cpp"
