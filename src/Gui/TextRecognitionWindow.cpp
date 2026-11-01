/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "TextRecognitionWindow.h"

#include "settings.h"

#include <KLocalizedString>
#include <KMessageWidget>
#include <KStandardGuiItem>

#include <QAction>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace Qt::StringLiterals;

/* -- Screenshot with the recognized words ----------------------------------------------------- */

/**
 * The screenshot with the recognized words marked like with a highlighter.
 * Words can be selected with the mouse like text.
 */
class RecognizedTextView : public QWidget
{
public:
    explicit RecognizedTextView(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setImage(const QImage &image)
    {
        m_image = image;
        m_words.clear();
        m_anchor = m_current = -1;
        m_zoom = 1;
        updateSize();
    }

    void setWords(const QList<TextRecognition::Word> &words)
    {
        m_words = words;
        m_anchor = m_current = -1;
        update();
    }

    // The available width, the screenshot is made as wide as that, but not larger than its real size.
    void setAvailableWidth(int width)
    {
        m_availableWidth = width;
        updateSize();
    }

    int first() const
    {
        return m_anchor < 0 || m_current < 0 ? -1 : std::min(m_anchor, m_current);
    }

    int last() const
    {
        return m_anchor < 0 || m_current < 0 ? -1 : std::max(m_anchor, m_current);
    }

    // Doesn't call selectionChanged.
    void setSelection(int first, int last)
    {
        m_anchor = first;
        m_current = last;
        update();
        if (first >= 0) {
            ensureVisible(first);
        }
    }

    std::function<void()> selectionChanged;
    std::function<void()> copyRequested;
    std::function<void(int x, int y)> ensureVisibleRequested;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (m_image.isNull()) {
            return;
        }
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(rect()), m_image);

        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        // A light blue highlighter, the selection in the blue of Windows.
        const QColor highlighter(0, 174, 255, 72);
        const QColor selection(0, 120, 215, 120);
        const int first = this->first();
        const int last = this->last();
        for (int i = 0; i < m_words.size(); ++i) {
            QPainterPath path;
            path.addRoundedRect(wordRect(i), 2, 2);
            painter.fillPath(path, i >= first && i <= last && first >= 0 ? selection : highlighter);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }
        m_selecting = true;
        m_pressPos = event->position();
        if (event->modifiers() & Qt::ShiftModifier && m_anchor >= 0) {
            m_current = nearestWord(event->position());
        } else {
            m_anchor = m_current = wordAt(event->position());
        }
        update();
        notifySelectionChanged();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_selecting) {
            setCursor(wordAt(event->position()) >= 0 ? Qt::IBeamCursor : Qt::ArrowCursor);
            return;
        }
        if (m_anchor < 0) {
            // Started next to the words.
            if ((event->position() - m_pressPos).manhattanLength() < 4) {
                return;
            }
            m_anchor = nearestWord(m_pressPos);
        }
        const int current = nearestWord(event->position());
        if (current != m_current) {
            m_current = current;
            update();
            notifySelectionChanged();
        }
    }

    void mouseReleaseEvent(QMouseEvent *) override
    {
        m_selecting = false;
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        // The whole line.
        const int word = wordAt(event->position());
        if (word < 0) {
            return;
        }
        const int line = m_words[word].line;
        m_anchor = m_current = word;
        while (m_anchor > 0 && m_words[m_anchor - 1].line == line) {
            --m_anchor;
        }
        while (m_current < m_words.size() - 1 && m_words[m_current + 1].line == line) {
            ++m_current;
        }
        update();
        notifySelectionChanged();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->matches(QKeySequence::SelectAll)) {
            selectAll();
        } else if (event->matches(QKeySequence::Copy)) {
            copyRequested();
        } else {
            QWidget::keyPressEvent(event);
        }
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        QMenu menu(this);
        auto copyAction = menu.addAction(QIcon::fromTheme(u"edit-copy"_s), i18nc("@action:inmenu", "Copy"), this, [this] {
            copyRequested();
        });
        copyAction->setEnabled(first() >= 0);
        menu.addAction(QIcon::fromTheme(u"edit-select-all"_s), i18nc("@action:inmenu", "Select All"), this, [this] {
            selectAll();
        });
        menu.exec(event->globalPos());
    }

    void wheelEvent(QWheelEvent *event) override
    {
        // Zoom with Ctrl and the mouse wheel, the screenshot is never smaller than the available width.
        if (!(event->modifiers() & Qt::ControlModifier)) {
            event->ignore();
            return;
        }
        const qreal steps = event->angleDelta().y() / 120.0;
        m_zoom = std::clamp(m_zoom * std::pow(1.25, steps), 1.0, 8.0);
        updateSize();
    }

private:
    void selectAll()
    {
        if (m_words.isEmpty()) {
            return;
        }
        m_anchor = 0;
        m_current = m_words.size() - 1;
        update();
        notifySelectionChanged();
    }

    void notifySelectionChanged()
    {
        if (selectionChanged) {
            selectionChanged();
        }
    }

    void updateSize()
    {
        if (m_image.isNull() || m_availableWidth <= 0) {
            return;
        }
        const qreal logicalWidth = m_image.width() / m_image.devicePixelRatio();
        const qreal width = std::min<qreal>(m_availableWidth, logicalWidth) * m_zoom;
        setFixedSize(qRound(width), qRound(width * m_image.height() / m_image.width()));
        update();
    }

    qreal scale() const
    {
        return m_image.isNull() ? 1 : width() / qreal(m_image.width());
    }

    QRectF wordRect(int index) const
    {
        const auto &rect = m_words[index].rect;
        const qreal scale = this->scale();
        return QRectF(rect.x() * scale, rect.y() * scale, rect.width() * scale, rect.height() * scale).adjusted(-2, -1, 2, 1);
    }

    int wordAt(const QPointF &pos) const
    {
        for (int i = 0; i < m_words.size(); ++i) {
            if (wordRect(i).adjusted(-2, -2, 2, 2).contains(pos)) {
                return i;
            }
        }
        return -1;
    }

    int nearestWord(const QPointF &pos) const
    {
        int nearest = -1;
        qreal nearestDistance = 0;
        for (int i = 0; i < m_words.size(); ++i) {
            const auto rect = wordRect(i);
            const qreal dx = std::max({rect.left() - pos.x(), 0.0, pos.x() - rect.right()});
            // Lines are close to each other, so the vertical distance counts more.
            const qreal dy = std::max({rect.top() - pos.y(), 0.0, pos.y() - rect.bottom()}) * 3;
            const qreal distance = dx * dx + dy * dy;
            if (nearest < 0 || distance < nearestDistance) {
                nearest = i;
                nearestDistance = distance;
            }
        }
        return nearest;
    }

    void ensureVisible(int index)
    {
        if (ensureVisibleRequested) {
            const auto center = wordRect(index).center().toPoint();
            ensureVisibleRequested(center.x(), center.y());
        }
    }

    QImage m_image;
    QList<TextRecognition::Word> m_words;
    int m_anchor = -1;
    int m_current = -1;
    bool m_selecting = false;
    QPointF m_pressPos;
    int m_availableWidth = 0;
    qreal m_zoom = 1;
};

namespace
{
// Makes the screenshot as wide as the visible area.
class FitScrollArea : public QScrollArea
{
public:
    FitScrollArea(RecognizedTextView *view, QWidget *parent)
        : QScrollArea(parent)
        , m_view(view)
    {
        setWidget(view);
        setAlignment(Qt::AlignCenter);
        setBackgroundRole(QPalette::Mid);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QScrollArea::resizeEvent(event);
        // Always leave room for the vertical scroll bar, otherwise showing it would change the width again.
        m_view->setAvailableWidth(contentsRect().width() - style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this));
    }

private:
    RecognizedTextView *const m_view;
};
}

/* -- Window ----------------------------------------------------------------------------------- */

QPointer<TextRecognitionWindow> TextRecognitionWindow::s_instance;

void TextRecognitionWindow::showForImage(const QImage &image, QWindow *transientParent)
{
    const bool isNew = !s_instance;
    if (isNew) {
        s_instance = new TextRecognitionWindow;
    }
    auto window = s_instance.data();
    if (isNew) {
        // Over the window of the screenshot.
        auto screen = transientParent ? transientParent->screen() : QGuiApplication::primaryScreen();
        const auto available = screen->availableGeometry();
        QRect geometry(QPoint(), QSize(std::min(1100, available.width() * 4 / 5), std::min(700, available.height() * 4 / 5)));
        geometry.moveCenter(transientParent ? transientParent->geometry().center() : available.center());
        geometry.moveLeft(std::clamp(geometry.left(), available.left(), available.right() - geometry.width()));
        geometry.moveTop(std::clamp(geometry.top(), available.top(), available.bottom() - geometry.height()));
        window->resize(geometry.size());
        window->move(geometry.topLeft());
    }
    window->setImage(image);
    window->show();
    window->raise();
    window->activateWindow();
}

TextRecognitionWindow::TextRecognitionWindow(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(i18nc("@title:window", "Recognized Text"));

    auto mainLayout = new QVBoxLayout(this);

    auto languageLayout = new QHBoxLayout;
    auto languageLabel = new QLabel(i18nc("@label:listbox", "Language of the text:"), this);
    m_languageComboBox = new QComboBox(this);
    languageLabel->setBuddy(m_languageComboBox);
    languageLayout->addWidget(languageLabel);
    languageLayout->addWidget(m_languageComboBox);
    languageLayout->addStretch();
    mainLayout->addLayout(languageLayout);

    m_message = new KMessageWidget(this);
    m_message->setCloseButtonVisible(false);
    m_message->setWordWrap(true);
    m_message->hide();
    mainLayout->addWidget(m_message);

    auto splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    m_view = new RecognizedTextView(this);
    auto scrollArea = new FitScrollArea(m_view, splitter);
    m_view->ensureVisibleRequested = [scrollArea](int x, int y) {
        scrollArea->ensureVisible(x, y, 50, 50);
    };
    splitter->addWidget(scrollArea);

    m_textEdit = new QPlainTextEdit(splitter);
    m_textEdit->setReadOnly(true);
    m_textEdit->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    // The selection is also shown while the screenshot has the focus.
    auto palette = m_textEdit->palette();
    palette.setColor(QPalette::Inactive, QPalette::Highlight, palette.color(QPalette::Active, QPalette::Highlight));
    palette.setColor(QPalette::Inactive, QPalette::HighlightedText, palette.color(QPalette::Active, QPalette::HighlightedText));
    m_textEdit->setPalette(palette);
    splitter->addWidget(m_textEdit);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    // The screenshot gets more room than the text. The sizes are scaled to the width of the window.
    splitter->setSizes({600, 400});
    mainLayout->addWidget(splitter, 1);

    auto buttonLayout = new QHBoxLayout;
    auto hint = new QLabel(this);
    const auto hintText = i18nc("@info", "Select text on the screenshot or next to it to copy a part of it.");
    hint->setText(hintText);
    hint->setWordWrap(true);
    buttonLayout->addWidget(hint, 1);
    m_copyButton = new QPushButton(QIcon::fromTheme(u"edit-copy"_s), i18nc("@action:button", "Copy"), this);
    m_copyAllButton = new QPushButton(QIcon::fromTheme(u"edit-copy"_s), i18nc("@action:button", "Copy All"), this);
    auto closeButton = new QPushButton(this);
    KGuiItem::assign(closeButton, KStandardGuiItem::close());
    buttonLayout->addWidget(m_copyButton);
    buttonLayout->addWidget(m_copyAllButton);
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);

    auto hintTimer = new QTimer(this);
    hintTimer->setSingleShot(true);
    hintTimer->setInterval(2500);
    connect(hintTimer, &QTimer::timeout, hint, [hint, hintText] {
        hint->setText(hintText);
    });
    auto copy = [hint, hintTimer](QString text) {
        // QTextCursor separates the lines with the Unicode paragraph separator.
        text.replace(QChar::ParagraphSeparator, u'\n');
        QGuiApplication::clipboard()->setText(text);
        hint->setText(i18nc("@info", "The text was copied to the clipboard."));
        hintTimer->start();
    };
    connect(m_copyButton, &QPushButton::clicked, this, [this, copy] {
        copy(m_textEdit->textCursor().selectedText());
    });
    connect(m_copyAllButton, &QPushButton::clicked, this, [this, copy] {
        copy(m_result.text);
    });
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    m_view->copyRequested = [this] {
        m_copyButton->click();
    };
    auto escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &QWidget::close);

    m_view->selectionChanged = [this] {
        syncSelectionFromView();
    };
    connect(m_textEdit, &QPlainTextEdit::selectionChanged, this, &TextRecognitionWindow::syncSelectionFromText);

    // The languages that Windows can recognize.
    const auto languages = TextRecognition::availableLanguages();
    for (const auto &language : languages) {
        m_languageComboBox->addItem(language.name, language.tag);
    }
    QString language = Settings::textRecognitionLanguage();
    if (m_languageComboBox->findData(language) < 0) {
        language = TextRecognition::defaultLanguage();
    }
    m_languageComboBox->setCurrentIndex(std::max(0, m_languageComboBox->findData(language)));
    connect(m_languageComboBox, &QComboBox::activated, this, [this] {
        Settings::setTextRecognitionLanguage(m_languageComboBox->currentData().toString());
        Settings::self()->save();
        recognize();
    });
    if (languages.isEmpty()) {
        languageLabel->hide();
        m_languageComboBox->hide();
    }

    updateCopyButton();
}

TextRecognitionWindow::~TextRecognitionWindow() = default;

void TextRecognitionWindow::setImage(const QImage &image)
{
    m_image = image;
    m_view->setImage(image);
    recognize();
}

void TextRecognitionWindow::recognize()
{
    const int request = ++m_request;
    m_result = {};
    m_message->hide();
    m_view->setWords({});
    m_textEdit->clear();
    if (m_languageComboBox->count() == 0) {
        showError(i18nc("@info",
                        "Windows has no language installed for text recognition. "
                        "Add a language in the Windows settings under Time & Language, Language & Region."),
                  true);
        updateCopyButton();
        return;
    }
    m_textEdit->setPlaceholderText(i18nc("@info:placeholder", "Recognizing text…"));
    m_languageComboBox->setEnabled(false);
    setCursor(Qt::BusyCursor);
    updateCopyButton();

    auto watcher = new QFutureWatcher<TextRecognition::Result>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, request] {
        watcher->deleteLater();
        if (request != m_request) {
            return;
        }
        unsetCursor();
        m_languageComboBox->setEnabled(true);
        showResult(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([image = m_image, language = m_languageComboBox->currentData().toString()] {
        return TextRecognition::recognize(image, language);
    }));
}

void TextRecognitionWindow::showResult(const TextRecognition::Result &result)
{
    m_result = result;
    if (!result.error.isEmpty()) {
        m_textEdit->setPlaceholderText({});
        showError(i18nc("@info %1 is the reason", "The text could not be recognized: %1", result.error), false);
        updateCopyButton();
        return;
    }
    m_textEdit->setPlaceholderText(i18nc("@info:placeholder", "No text was found."));
    m_textEdit->setPlainText(result.text);
    m_view->setWords(result.words);
    updateCopyButton();
}

void TextRecognitionWindow::showError(const QString &message, bool offerLanguageSettings)
{
    const auto actions = m_message->actions();
    for (auto action : actions) {
        m_message->removeAction(action);
        action->deleteLater();
    }
    if (offerLanguageSettings) {
        auto action = new QAction(QIcon::fromTheme(u"preferences-desktop-locale"_s), i18nc("@action:button", "Open Language Settings"), m_message);
        connect(action, &QAction::triggered, this, [] {
            QDesktopServices::openUrl(QUrl(u"ms-settings:regionlanguage"_s));
        });
        m_message->addAction(action);
    }
    m_message->setMessageType(KMessageWidget::Error);
    m_message->setText(message);
    m_message->show();
}

void TextRecognitionWindow::syncSelectionFromView()
{
    if (m_syncingSelection) {
        return;
    }
    m_syncingSelection = true;
    auto cursor = m_textEdit->textCursor();
    const int first = m_view->first();
    const int last = m_view->last();
    if (first < 0 || last >= m_result.words.size()) {
        cursor.clearSelection();
    } else {
        const auto &lastWord = m_result.words[last];
        cursor.setPosition(m_result.words[first].start);
        cursor.setPosition(lastWord.start + lastWord.length, QTextCursor::KeepAnchor);
    }
    m_textEdit->setTextCursor(cursor);
    m_textEdit->ensureCursorVisible();
    m_syncingSelection = false;
    updateCopyButton();
}

void TextRecognitionWindow::syncSelectionFromText()
{
    if (m_syncingSelection) {
        return;
    }
    m_syncingSelection = true;
    const auto cursor = m_textEdit->textCursor();
    int first = -1;
    int last = -1;
    if (cursor.hasSelection()) {
        for (int i = 0; i < m_result.words.size(); ++i) {
            const auto &word = m_result.words[i];
            if (word.start + word.length > cursor.selectionStart() && word.start < cursor.selectionEnd()) {
                if (first < 0) {
                    first = i;
                }
                last = i;
            }
        }
    }
    m_view->setSelection(first, last);
    m_syncingSelection = false;
    updateCopyButton();
}

void TextRecognitionWindow::updateCopyButton()
{
    m_copyButton->setEnabled(m_textEdit->textCursor().hasSelection());
    m_copyAllButton->setEnabled(!m_result.text.isEmpty());
}

#include "moc_TextRecognitionWindow.cpp"
