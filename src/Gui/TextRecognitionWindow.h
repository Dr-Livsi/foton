/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "TextRecognition.h"

#include <QImage>
#include <QPointer>
#include <QWidget>

class KMessageWidget;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QWindow;
class RecognizedTextView;

/**
 * Shows the text recognized in a screenshot. The recognized words are marked on the screenshot
 * like with a highlighter, and the text is next to it, so it can be selected and copied in both places.
 */
class TextRecognitionWindow : public QWidget
{
    Q_OBJECT

public:
    /// Show the window for the image, or recognize the text of a new image in the window that is already open.
    static void showForImage(const QImage &image, QWindow *transientParent);

    ~TextRecognitionWindow() override;

private:
    explicit TextRecognitionWindow(QWidget *parent = nullptr);
    void setImage(const QImage &image);
    void recognize();
    void showResult(const TextRecognition::Result &result);
    void showError(const QString &message, bool offerLanguageSettings);
    void syncSelectionFromView();
    void syncSelectionFromText();
    void updateCopyButton();

    static QPointer<TextRecognitionWindow> s_instance;

    QImage m_image;
    TextRecognition::Result m_result;
    int m_request = 0;
    bool m_syncingSelection = false;

    QComboBox *m_languageComboBox = nullptr;
    KMessageWidget *m_message = nullptr;
    RecognizedTextView *m_view = nullptr;
    QPlainTextEdit *m_textEdit = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_copyAllButton = nullptr;
};
