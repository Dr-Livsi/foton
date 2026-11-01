/*
 * SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SHORTCUTSOPTIONSPAGE_H
#define SHORTCUTSOPTIONSPAGE_H

#include <QKeySequence>
#include <QList>
#include <QWidget>

class KKeySequenceWidget;
class KMessageWidget;
class QAction;
class QLabel;
class QPushButton;

/**
 * One field for the shortcut of each global action, with a button to restore its default.
 * The changes are only applied to the actions when the settings are saved.
 */
class ShortcutsOptionsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ShortcutsOptionsPage(QWidget *parent);
    ~ShortcutsOptionsPage() override;

    bool isModified() const;
    bool isDefault() const;
    void defaults();

Q_SIGNALS:
    void shortCutsChanged();

public Q_SLOTS:

    void saveChanges();
    void resetChanges();

private:
    struct Row {
        QAction *action = nullptr;
        QKeySequence defaultShortcut;
        QKeySequence shortcut;
        QLabel *label = nullptr;
        KKeySequenceWidget *editor = nullptr;
        QPushButton *defaultButton = nullptr;
    };

    void setShortcut(Row &row, const QKeySequence &shortcut);
    void onShortcutEdited(Row &row, const QKeySequence &shortcut);
    void updateFailedShortcutsMessage();

    QList<Row> mRows;
    KMessageWidget *mFailedMessage = nullptr;
};

#endif // SHORTCUTSOPTIONSPAGE_H
