/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QList>
#include <QWidget>

class FilenameBlockWidget;
class QToolButton;

/**
 * Edits the filename template with blocks instead of placeholders:
 * text, date, time, window title, a sequential number and the other placeholders of ExportManager.
 *
 * The blocks can be added from a menu, dragged to another position and removed.
 * The template is still stored as a string with placeholders, e.g. "Screenshot_<yyyy><MM><dd>".
 */
class FilenameTemplateEditor : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString filenameTemplate READ filenameTemplate WRITE setFilenameTemplate NOTIFY filenameTemplateChanged USER true)

public:
    struct Block {
        enum Type {
            Text,
            Date,
            Time,
            Number,
            // A single placeholder, e.g. the window title or the name of the day.
            Placeholder,
        };
        Type type = Text;
        // The text itself or the placeholders.
        QString value;
        bool operator==(const Block &other) const = default;
    };

    explicit FilenameTemplateEditor(QWidget *parent = nullptr);
    ~FilenameTemplateEditor() override;

    QString filenameTemplate() const;
    void setFilenameTemplate(const QString &filenameTemplate);

    static QList<Block> parse(const QString &filenameTemplate);
    static QString toTemplate(const QList<Block> &blocks);

    /// The formats offered for a block, as placeholders.
    static QStringList formats(Block::Type type);

Q_SIGNALS:
    void filenameTemplateChanged(const QString &filenameTemplate);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuild(int focusIndex = -1);
    void addBlock(const Block &block);
    void removeBlock(int index);
    void moveBlock(int from, int to);
    void setBlockValue(int index, const QString &value);
    int dropIndex(const QPoint &pos) const;
    void emitChanged();

    QList<Block> m_blocks;
    QList<FilenameBlockWidget *> m_blockWidgets;
    QToolButton *m_addButton = nullptr;
    int m_dropIndex = -1;
};
