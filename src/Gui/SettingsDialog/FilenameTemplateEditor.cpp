/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "FilenameTemplateEditor.h"

#include "ExportManager.h"

#include <KLocalizedString>

#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDrag>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QToolButton>

#include <functional>

using namespace Qt::StringLiterals;
using Block = FilenameTemplateEditor::Block;

static const auto s_mimeType = u"application/x-foton-filename-block"_s;

/* -- Formats ---------------------------------------------------------------------------------- */

QStringList FilenameTemplateEditor::formats(Block::Type type)
{
    // Colons can't be used in file names on Windows, so times use other separators.
    switch (type) {
    case Block::Date:
        return {
            u"<yyyy>-<MM>-<dd>"_s,
            u"<yyyy><MM><dd>"_s,
            u"<yyyy>_<MM>_<dd>"_s,
            u"<dd>.<MM>.<yyyy>"_s,
            u"<dd>-<MM>-<yyyy>"_s,
            u"<yyyy>-<MM>"_s,
        };
    case Block::Time:
        return {
            u"<HH>-<mm>-<ss>"_s,
            u"<HH><mm><ss>"_s,
            u"<HH>_<mm>_<ss>"_s,
            u"<HH>.<mm>.<ss>"_s,
            u"<HH>-<mm>"_s,
        };
    case Block::Number:
        return {u"<#>"_s, u"<##>"_s, u"<###>"_s, u"<####>"_s};
    case Block::Text:
    case Block::Placeholder:
        break;
    }
    return {};
}

// An example of what a block looks like in a file name.
static QString example(Block::Type type, const QString &value)
{
    if (type == Block::Number) {
        return QString::number(1).rightJustified(value.count(u'#'), u'0');
    }
    // The same time as in the example of the whole file name.
    const auto timestamp = ExportManager::instance()->timestamp();
    return ExportManager::formattedFilename(value, timestamp.isValid() ? timestamp : QDateTime::currentDateTime());
}

static QString placeholderDescription(const QString &token)
{
    for (const auto &placeholder : ExportManager::filenamePlaceholders) {
        if (placeholder.token == token && !placeholder.description.isEmpty()) {
            return placeholder.description.toString();
        }
    }
    return token;
}

// Only separators, e.g. "_" or " - ".
static bool isSeparator(const Block &block)
{
    static const QRegularExpression separator(u"^[\\s_.\\-]+$"_s);
    return block.type == Block::Text && separator.match(block.value).hasMatch();
}

/* -- Parsing ---------------------------------------------------------------------------------- */

QList<Block> FilenameTemplateEditor::parse(const QString &filenameTemplate)
{
    // Longer formats first, so that e.g. <yyyy>-<MM>-<dd> isn't taken for <yyyy>-<MM>.
    QList<std::pair<Block::Type, QString>> formatList;
    for (auto type : {Block::Date, Block::Time}) {
        const auto typeFormats = formats(type);
        for (const auto &format : typeFormats) {
            formatList.append({type, format});
        }
    }
    std::stable_sort(formatList.begin(), formatList.end(), [](const auto &a, const auto &b) {
        return a.second.size() > b.second.size();
    });
    static const QRegularExpression numberExpression(u"<#+>"_s);

    QList<Block> blocks;
    QString text;
    auto addBlock = [&](Block::Type type, const QString &value) {
        if (!text.isEmpty()) {
            blocks.append({Block::Text, text});
            text.clear();
        }
        blocks.append({type, value});
    };

    qsizetype pos = 0;
    while (pos < filenameTemplate.size()) {
        const auto rest = QStringView(filenameTemplate).mid(pos);
        if (!rest.startsWith(u'<')) {
            text += filenameTemplate[pos];
            ++pos;
            continue;
        }
        bool found = false;
        for (const auto &[type, format] : std::as_const(formatList)) {
            if (rest.startsWith(format)) {
                addBlock(type, format);
                pos += format.size();
                found = true;
                break;
            }
        }
        if (found) {
            continue;
        }
        const auto numberMatch = numberExpression.matchView(rest, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        if (numberMatch.hasMatch()) {
            addBlock(Block::Number, numberMatch.captured());
            pos += numberMatch.capturedLength();
            continue;
        }
        for (const auto &placeholder : ExportManager::filenamePlaceholders) {
            if (rest.startsWith(placeholder.token)) {
                addBlock(Block::Placeholder, placeholder.token);
                pos += placeholder.token.size();
                found = true;
                break;
            }
        }
        if (!found) {
            text += filenameTemplate[pos];
            ++pos;
        }
    }
    if (!text.isEmpty()) {
        blocks.append({Block::Text, text});
    }
    return blocks;
}

QString FilenameTemplateEditor::toTemplate(const QList<Block> &blocks)
{
    QString result;
    for (const auto &block : blocks) {
        result += block.value;
    }
    return result;
}

/* -- Flow Layout ------------------------------------------------------------------------------ */

namespace
{
// Places the widgets in lines like words in a paragraph.
class FlowLayout : public QLayout
{
public:
    FlowLayout(QWidget *parent, int spacing)
        : QLayout(parent)
        , m_spacing(spacing)
    {
        setContentsMargins(0, 0, 0, 0);
    }

    ~FlowLayout() override
    {
        while (auto item = takeAt(0)) {
            delete item;
        }
    }

    void addItem(QLayoutItem *item) override
    {
        m_items.append(item);
    }

    void insertWidget(int index, QWidget *widget)
    {
        addChildWidget(widget);
        m_items.insert(index, new QWidgetItem(widget));
        invalidate();
    }

    int count() const override
    {
        return m_items.size();
    }

    QLayoutItem *itemAt(int index) const override
    {
        return m_items.value(index);
    }

    QLayoutItem *takeAt(int index) override
    {
        return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
    }

    Qt::Orientations expandingDirections() const override
    {
        return {};
    }

    bool hasHeightForWidth() const override
    {
        return true;
    }

    int heightForWidth(int width) const override
    {
        return doLayout(QRect(0, 0, width, 0), true);
    }

    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        doLayout(rect, false);
    }

    QSize sizeHint() const override
    {
        // Everything in one line, unless that gets too wide for the settings dialog.
        int width = 0;
        for (auto item : m_items) {
            width += item->sizeHint().width() + m_spacing;
        }
        width = std::min(width, 440);
        return {width, heightForWidth(width)};
    }

    QSize minimumSize() const override
    {
        QSize size;
        for (auto item : m_items) {
            size = size.expandedTo(item->minimumSize());
        }
        return size;
    }

private:
    int doLayout(const QRect &rect, bool testOnly) const
    {
        // Split the items into lines, then center them vertically in their line.
        QList<QList<QLayoutItem *>> lines(1);
        int x = 0;
        for (auto item : m_items) {
            const int width = item->sizeHint().width();
            if (x > 0 && x + width > rect.width()) {
                lines.append(QList<QLayoutItem *>());
                x = 0;
            }
            lines.last().append(item);
            x += width + m_spacing;
        }
        int y = rect.y();
        for (const auto &line : std::as_const(lines)) {
            int lineHeight = 0;
            for (auto item : line) {
                lineHeight = std::max(lineHeight, item->sizeHint().height());
            }
            x = rect.x();
            for (auto item : line) {
                const auto size = item->sizeHint();
                if (!testOnly) {
                    item->setGeometry(QRect(QPoint(x, y + (lineHeight - size.height()) / 2), size));
                }
                x += size.width() + m_spacing;
            }
            y += lineHeight + m_spacing;
        }
        return y - m_spacing - rect.y();
    }

    QList<QLayoutItem *> m_items;
    const int m_spacing;
};
}

/* -- Blocks ----------------------------------------------------------------------------------- */

/**
 * One block of the file name: a frame with rounded corners, a grip to drag it and a button to remove it.
 */
class FilenameBlockWidget : public QWidget
{
public:
    FilenameBlockWidget(const Block &block, int height, QWidget *parent)
        : QWidget(parent)
        , m_type(block.type)
    {
        setCursor(Qt::OpenHandCursor);
        setToolTip(i18nc("@info:tooltip", "Drag to move this block"));
        setFixedHeight(height);

        auto layout = new QHBoxLayout(this);
        // Room for the grip on the left.
        layout->setContentsMargins(s_gripWidth + 4, 0, 1, 0);
        layout->setSpacing(4);

        if (block.type == Block::Text) {
            m_lineEdit = new QLineEdit(block.value, this);
            m_lineEdit->setFrame(false);
            m_lineEdit->setPlaceholderText(i18nc("@info:placeholder a block of the file name", "Text"));
            m_lineEdit->setToolTip(i18nc("@info:tooltip", "Any text. Use / to save the screenshot in a subfolder."));
            // Characters that can't be used in file names on Windows. "/" makes a subfolder.
            m_lineEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(u"[^\\\\:*?\"<>|]*"_s), m_lineEdit));
            auto palette = m_lineEdit->palette();
            palette.setColor(QPalette::Base, Qt::transparent);
            m_lineEdit->setPalette(palette);
            m_lineEdit->setCursor(Qt::IBeamCursor);
            QObject::connect(m_lineEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
                updateLineEditWidth();
                if (valueChanged) {
                    valueChanged(text);
                }
            });
            updateLineEditWidth();
            layout->addWidget(m_lineEdit);
        } else {
            QString caption;
            switch (block.type) {
            case Block::Date:
                caption = i18nc("@label a block of the file name", "Date");
                break;
            case Block::Time:
                caption = i18nc("@label a block of the file name", "Time");
                break;
            case Block::Number:
                caption = i18nc("@label a block of the file name", "Number");
                break;
            default:
                caption = placeholderDescription(block.value);
                break;
            }
            auto label = new QLabel(caption, this);
            layout->addWidget(label);

            auto formatList = FilenameTemplateEditor::formats(block.type);
            if (!formatList.isEmpty()) {
                if (!formatList.contains(block.value)) {
                    formatList.append(block.value);
                }
                auto comboBox = new QComboBox(this);
                comboBox->setCursor(Qt::ArrowCursor);
                for (const auto &format : std::as_const(formatList)) {
                    comboBox->addItem(example(block.type, format), format);
                }
                comboBox->setCurrentIndex(formatList.indexOf(block.value));
                comboBox->setToolTip(block.type == Block::Number ? i18nc("@info:tooltip", "The number of digits") : i18nc("@info:tooltip", "The format"));
                QObject::connect(comboBox, &QComboBox::activated, this, [this, comboBox] {
                    if (valueChanged) {
                        valueChanged(comboBox->currentData().toString());
                    }
                });
                layout->addWidget(comboBox);
            }
        }

        auto removeButton = new QToolButton(this);
        removeButton->setAutoRaise(true);
        removeButton->setCursor(Qt::ArrowCursor);
        removeButton->setIcon(QIcon::fromTheme(u"edit-delete-remove"_s, QIcon::fromTheme(u"list-remove"_s)));
        removeButton->setIconSize(QSize(12, 12));
        removeButton->setToolTip(i18nc("@info:tooltip", "Remove this block"));
        removeButton->setFixedSize(height - 6, height - 6);
        QObject::connect(removeButton, &QToolButton::clicked, this, [this] {
            if (removeRequested) {
                removeRequested();
            }
        });
        layout->addWidget(removeButton);
    }

    void focusEditor()
    {
        if (m_lineEdit) {
            m_lineEdit->setFocus(Qt::OtherFocusReason);
        }
    }

    int index = -1;
    std::function<void(const QString &)> valueChanged;
    std::function<void()> removeRequested;
    std::function<void(int)> moveRequested;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto palette = this->palette();
        const auto base = palette.color(QPalette::Base);
        const auto highlight = palette.color(QPalette::Highlight);
        auto mix = [](const QColor &a, const QColor &b, qreal amount) {
            return QColor::fromRgbF(a.redF() * (1 - amount) + b.redF() * amount,
                                    a.greenF() * (1 - amount) + b.greenF() * amount,
                                    a.blueF() * (1 - amount) + b.blueF() * amount);
        };
        // Text is white like a text field, the placeholders are tinted with the accent color.
        const QColor fill = m_type == Block::Text ? base : mix(base, highlight, 0.12);
        QColor border = m_type == Block::Text ? palette.color(QPalette::Mid) : mix(base, highlight, 0.5);
        if (underMouse() || isAncestorOf(QApplication::focusWidget())) {
            border = highlight;
        }
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        painter.fillPath(path, fill);
        painter.setPen(border);
        painter.drawPath(path);

        // The grip: two columns of dots.
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.color(QPalette::PlaceholderText));
        const qreal centerY = height() / 2.0;
        for (int column = 0; column < 2; ++column) {
            for (int row = -1; row <= 1; ++row) {
                painter.drawEllipse(QPointF(5.5 + column * 3.5, centerY + row * 3.5), 1, 1);
            }
        }
    }

    void enterEvent(QEnterEvent *event) override
    {
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        update();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressPos = event->position().toPoint();
            m_pressed = true;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_pressed = false;
        QWidget::mouseReleaseEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_pressed || (event->position().toPoint() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
            return;
        }
        m_pressed = false;
        auto mimeData = new QMimeData;
        mimeData->setData(s_mimeType, QByteArray::number(index));
        auto drag = new QDrag(this);
        drag->setMimeData(mimeData);
        const auto pixmap = grab();
        drag->setPixmap(pixmap);
        drag->setHotSpot(m_pressPos);
        setCursor(Qt::ClosedHandCursor);
        drag->exec(Qt::MoveAction);
        setCursor(Qt::OpenHandCursor);
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        QMenu menu(this);
        menu.addAction(QIcon::fromTheme(u"go-previous"_s), i18nc("@action:inmenu", "Move Left"), this, [this] {
            moveRequested(-1);
        });
        menu.addAction(QIcon::fromTheme(u"go-next"_s), i18nc("@action:inmenu", "Move Right"), this, [this] {
            moveRequested(1);
        });
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme(u"edit-delete-remove"_s), i18nc("@action:inmenu", "Remove"), this, [this] {
            removeRequested();
        });
        menu.exec(event->globalPos());
    }

private:
    void updateLineEditWidth()
    {
        const auto metrics = m_lineEdit->fontMetrics();
        const auto text = m_lineEdit->text().isEmpty() ? m_lineEdit->placeholderText() : m_lineEdit->text();
        m_lineEdit->setFixedWidth(std::max(metrics.horizontalAdvance(text), metrics.averageCharWidth() * 2) + metrics.averageCharWidth() * 2);
    }

    static constexpr int s_gripWidth = 10;
    const Block::Type m_type;
    QLineEdit *m_lineEdit = nullptr;
    QPoint m_pressPos;
    bool m_pressed = false;
};

/* -- Editor ----------------------------------------------------------------------------------- */

FilenameTemplateEditor::FilenameTemplateEditor(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);
    auto sizePolicy = QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sizePolicy.setHeightForWidth(true);
    setSizePolicy(sizePolicy);

    new FlowLayout(this, 6);

    m_addButton = new QToolButton(this);
    m_addButton->setText(i18nc("@action:button add a block to the file name", "Add"));
    m_addButton->setIcon(QIcon::fromTheme(u"list-add"_s));
    m_addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_addButton->setPopupMode(QToolButton::InstantPopup);
    m_addButton->setToolTip(i18nc("@info:tooltip", "Add a block to the file name"));

    auto menu = new QMenu(m_addButton);
    menu->addAction(i18nc("@action:inmenu a block of the file name", "Text"), this, [this] {
        addBlock({Block::Text, QString()});
    });
    menu->addAction(i18nc("@action:inmenu a block of the file name", "Date"), this, [this] {
        addBlock({Block::Date, formats(Block::Date).constFirst()});
    });
    menu->addAction(i18nc("@action:inmenu a block of the file name", "Time"), this, [this] {
        addBlock({Block::Time, formats(Block::Time).constFirst()});
    });
    menu->addAction(placeholderDescription(u"<title>"_s), this, [this] {
        addBlock({Block::Placeholder, u"<title>"_s});
    });
    menu->addAction(i18nc("@action:inmenu a block of the file name", "Sequential Number"), this, [this] {
        addBlock({Block::Number, formats(Block::Number).constFirst()});
    });
    auto moreMenu = menu->addMenu(i18nc("@action:inmenu more blocks for the file name", "More"));
    using Flag = ExportManager::Placeholder::Flag;
    for (const auto &placeholder : ExportManager::filenamePlaceholders) {
        if (placeholder.flags.testFlag(Flag::Hidden) || placeholder.token == "<title>"_L1 || placeholder.token == "<#>"_L1) {
            continue;
        }
        const auto token = placeholder.token;
        moreMenu->addAction(placeholder.description.toString(), this, [this, token] {
            addBlock({Block::Placeholder, token});
        });
    }
    m_addButton->setMenu(menu);
    layout()->addWidget(m_addButton);
}

FilenameTemplateEditor::~FilenameTemplateEditor() = default;

QString FilenameTemplateEditor::filenameTemplate() const
{
    return toTemplate(m_blocks);
}

void FilenameTemplateEditor::setFilenameTemplate(const QString &filenameTemplate)
{
    if (filenameTemplate == this->filenameTemplate() && !m_blockWidgets.isEmpty()) {
        return;
    }
    m_blocks = parse(filenameTemplate);
    rebuild();
    emitChanged();
}

void FilenameTemplateEditor::rebuild(int focusIndex)
{
    auto flowLayout = static_cast<FlowLayout *>(layout());
    // This can be called by a block, e.g. when its remove button was clicked,
    // so the old blocks are deleted later.
    for (auto widget : std::as_const(m_blockWidgets)) {
        flowLayout->removeWidget(widget);
        widget->hide();
        widget->deleteLater();
    }
    m_blockWidgets.clear();

    // All blocks have the height of a combo box, also the ones without one.
    const int height = QComboBox().sizeHint().height() + 4;
    for (int i = 0; i < m_blocks.size(); ++i) {
        auto widget = new FilenameBlockWidget(m_blocks[i], height, this);
        widget->index = i;
        widget->valueChanged = [this, i](const QString &value) {
            setBlockValue(i, value);
        };
        widget->removeRequested = [this, i] {
            removeBlock(i);
        };
        widget->moveRequested = [this, i](int delta) {
            // The index to move to is counted before the block is taken out.
            moveBlock(i, delta < 0 ? i - 1 : i + 2);
        };
        flowLayout->insertWidget(i, widget);
        widget->show();
        m_blockWidgets.append(widget);
    }
    setTabOrder(m_blockWidgets.isEmpty() ? nullptr : m_blockWidgets.constLast(), m_addButton);
    updateGeometry();

    if (focusIndex >= 0 && focusIndex < m_blockWidgets.size()) {
        m_blockWidgets[focusIndex]->focusEditor();
    }
}

void FilenameTemplateEditor::addBlock(const Block &block)
{
    // Keep fields apart, e.g. the date and the time.
    if (block.type != Block::Text && !m_blocks.isEmpty() && m_blocks.constLast().type != Block::Text) {
        m_blocks.append({Block::Text, u"_"_s});
    }
    m_blocks.append(block);
    rebuild(block.type == Block::Text ? m_blocks.size() - 1 : -1);
    emitChanged();
}

void FilenameTemplateEditor::removeBlock(int index)
{
    if (index < 0 || index >= m_blocks.size()) {
        return;
    }
    const bool wasText = m_blocks[index].type == Block::Text;
    m_blocks.removeAt(index);
    // Also remove the separator that was only needed for the removed block,
    // so that e.g. "Screenshot_<date>_<time>" becomes "Screenshot_<time>" and not "Screenshot__<time>".
    if (!wasText) {
        const bool hasLeft = index > 0;
        const bool hasRight = index < m_blocks.size();
        if (hasRight && isSeparator(m_blocks[index]) && (!hasLeft || m_blocks[index - 1].type == Block::Text)) {
            m_blocks.removeAt(index);
        } else if (hasLeft && isSeparator(m_blocks[index - 1]) && (!hasRight || m_blocks[index].type == Block::Text)) {
            m_blocks.removeAt(index - 1);
        }
    }
    rebuild();
    emitChanged();
}

void FilenameTemplateEditor::moveBlock(int from, int to)
{
    // `to` is the position before the block is taken out.
    to = std::clamp(to, 0, int(m_blocks.size()));
    if (from < 0 || from >= m_blocks.size() || to == from || to == from + 1) {
        return;
    }
    if (to > from) {
        --to;
    }
    m_blocks.move(from, to);
    rebuild();
    emitChanged();
}

void FilenameTemplateEditor::setBlockValue(int index, const QString &value)
{
    if (index < 0 || index >= m_blocks.size() || m_blocks[index].value == value) {
        return;
    }
    m_blocks[index].value = value;
    emitChanged();
}

void FilenameTemplateEditor::emitChanged()
{
    Q_EMIT filenameTemplateChanged(filenameTemplate());
}

int FilenameTemplateEditor::dropIndex(const QPoint &pos) const
{
    // The block in the same line that the position is before, otherwise after the last block of that line.
    int index = -1;
    for (int i = 0; i < m_blockWidgets.size(); ++i) {
        const auto geometry = m_blockWidgets[i]->geometry();
        if (pos.y() < geometry.top() - 3 || pos.y() > geometry.bottom() + 3) {
            continue;
        }
        if (pos.x() < geometry.center().x()) {
            return i;
        }
        index = i + 1;
    }
    return index >= 0 ? index : int(m_blockWidgets.size());
}

void FilenameTemplateEditor::dragEnterEvent(QDragEnterEvent *event)
{
    const bool fromThisEditor = std::any_of(m_blockWidgets.cbegin(), m_blockWidgets.cend(), [event](QWidget *widget) {
        return widget == event->source();
    });
    if (event->mimeData()->hasFormat(s_mimeType) && fromThisEditor) {
        event->acceptProposedAction();
    }
}

void FilenameTemplateEditor::dragMoveEvent(QDragMoveEvent *event)
{
    if (!event->mimeData()->hasFormat(s_mimeType)) {
        return;
    }
    event->acceptProposedAction();
    const int index = dropIndex(event->position().toPoint());
    if (index != m_dropIndex) {
        m_dropIndex = index;
        update();
    }
}

void FilenameTemplateEditor::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropIndex = -1;
    update();
}

void FilenameTemplateEditor::dropEvent(QDropEvent *event)
{
    m_dropIndex = -1;
    update();
    if (!event->mimeData()->hasFormat(s_mimeType)) {
        return;
    }
    event->acceptProposedAction();
    const int from = event->mimeData()->data(s_mimeType).toInt();
    const int to = dropIndex(event->position().toPoint());
    // Not while the drag is still running in the block that is moved.
    QMetaObject::invokeMethod(
        this,
        [this, from, to] {
            moveBlock(from, to);
        },
        Qt::QueuedConnection);
}

void FilenameTemplateEditor::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    if (m_dropIndex < 0) {
        return;
    }
    // A line where the block will be dropped.
    QRect geometry;
    int x = 0;
    if (m_dropIndex < m_blockWidgets.size()) {
        geometry = m_blockWidgets[m_dropIndex]->geometry();
        x = geometry.left() - 4;
    } else if (!m_blockWidgets.isEmpty()) {
        geometry = m_blockWidgets.constLast()->geometry();
        x = geometry.right() + 3;
    } else {
        return;
    }
    QPainter painter(this);
    painter.fillRect(QRect(x, geometry.top(), 2, geometry.height()), palette().color(QPalette::Highlight));
}

#include "moc_FilenameTemplateEditor.cpp"
