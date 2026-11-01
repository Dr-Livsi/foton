/*
 * SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ShortcutsOptionsPage.h"

#include "GlobalShortcuts.h"
#include "SettingsPageUtils.h"
#include "ShortcutActions.h"

#include <KKeySequenceWidget>
#include <KLocalizedString>
#include <KMessageBox>
#include <KMessageWidget>

#include <QAction>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

ShortcutsOptionsPage::ShortcutsOptionsPage(QWidget *parent)
    : QWidget(parent)
{
    auto mainLayout = new QVBoxLayout(this);

    auto infoMessage = new KMessageWidget(this);
    infoMessage->setMessageType(KMessageWidget::Information);
    infoMessage->setCloseButtonVisible(false);
    infoMessage->setWordWrap(true);
    infoMessage->setText(i18nc("@info", "These shortcuts work in all applications while Foton is running, including when it is running in the system tray."));
    mainLayout->addWidget(infoMessage);

    mFailedMessage = new KMessageWidget(this);
    mFailedMessage->setMessageType(KMessageWidget::Warning);
    mFailedMessage->setCloseButtonVisible(false);
    mFailedMessage->setWordWrap(true);
    mainLayout->addWidget(mFailedMessage);

    auto group = new QGroupBox(i18nc("@title:group", "Global Shortcuts"), this);
    auto grid = new QGridLayout(group);
    mainLayout->addWidget(group);

    const auto actions = ShortcutActions::self()->shortcutActions()->actions();
    // All fields have the same width, wide enough for long key combinations.
    const int editorWidth = fontMetrics().horizontalAdvance(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Print).toString(QKeySequence::NativeText))
        + fontMetrics().averageCharWidth() * 12;
    for (auto action : actions) {
        Row row;
        row.action = action;
        row.defaultShortcut = KActionCollection::defaultShortcut(action);
        row.shortcut = action->shortcut();

        row.editor = new KKeySequenceWidget(group);
        // Global shortcuts are single key combinations. Keys without modifiers are only allowed
        // if they don't type text, e.g. Print or F1, otherwise that text couldn't be typed anymore.
        row.editor->setPatterns(KKeySequenceRecorder::ModifierAndKey);
        row.editor->setMultiKeyShortcutsAllowed(false);
        row.editor->setCheckForConflictsAgainst(KKeySequenceWidget::StandardShortcuts);
        row.editor->setClearButtonShown(true);
        row.editor->setKeySequence(row.shortcut);
        row.editor->setMinimumWidth(editorWidth);

        row.label = new QLabel(i18nc("@label %1 is the name of an action", "%1:", action->text()), group);
        row.label->setBuddy(row.editor);

        row.defaultButton = new QPushButton(QIcon::fromTheme(u"edit-undo"_s), i18nc("@action:button restore the default shortcut", "Default"), group);
        if (row.defaultShortcut.isEmpty()) {
            row.defaultButton->setToolTip(i18nc("@info:tooltip", "Restore the default: no shortcut"));
        } else {
            row.defaultButton->setToolTip(
                i18nc("@info:tooltip %1 is a keyboard shortcut", "Restore the default shortcut: %1", row.defaultShortcut.toString(QKeySequence::NativeText)));
        }
        row.defaultButton->setEnabled(row.shortcut != row.defaultShortcut);

        const int index = mRows.size();
        grid->addWidget(row.label, index, 0);
        grid->addWidget(row.editor, index, 1);
        grid->addWidget(row.defaultButton, index, 2);
        mRows.append(row);

        // Asking about a conflict while the key sequence is still being recorded would interrupt the recording.
        connect(
            row.editor,
            &KKeySequenceWidget::keySequenceChanged,
            this,
            [this, index](const QKeySequence &shortcut) {
                onShortcutEdited(mRows[index], shortcut);
            },
            Qt::QueuedConnection);
        connect(row.defaultButton, &QPushButton::clicked, this, [this, index] {
            onShortcutEdited(mRows[index], mRows[index].defaultShortcut);
        });
    }
    grid->setColumnStretch(3, 1);

    auto hint = new QLabel(i18nc("@info", "Click a field, then press the new key combination."), group);
    hint->setWordWrap(true);
    SettingsPageUtils::makeHint(hint);
    grid->addWidget(hint, mRows.size(), 1, 1, 3);

    mainLayout->addStretch();

    connect(GlobalShortcuts::instance(), &GlobalShortcuts::failedShortcutsChanged, this, &ShortcutsOptionsPage::updateFailedShortcutsMessage);
    updateFailedShortcutsMessage();
}

ShortcutsOptionsPage::~ShortcutsOptionsPage() = default;

void ShortcutsOptionsPage::setShortcut(Row &row, const QKeySequence &shortcut)
{
    const bool changed = row.shortcut != shortcut;
    row.shortcut = shortcut;
    if (row.editor->keySequence() != shortcut) {
        QSignalBlocker blocker(row.editor);
        row.editor->setKeySequence(shortcut);
    }
    row.defaultButton->setEnabled(shortcut != row.defaultShortcut);
    if (changed) {
        Q_EMIT shortCutsChanged();
    }
}

void ShortcutsOptionsPage::onShortcutEdited(Row &row, const QKeySequence &shortcut)
{
    if (shortcut == row.shortcut) {
        // E.g. the default button was used, the field may still show something else.
        setShortcut(row, shortcut);
        return;
    }
    if (!shortcut.isEmpty()) {
        for (auto &other : mRows) {
            if (&other == &row || other.shortcut != shortcut) {
                continue;
            }
            const auto answer = KMessageBox::questionTwoActions(this,
                                                                i18nc("@info %1 is a keyboard shortcut, %2 and %3 are the names of actions",
                                                                      "The shortcut %1 is already used for \"%2\". Do you want to use it for \"%3\" instead?",
                                                                      shortcut.toString(QKeySequence::NativeText),
                                                                      other.action->text(),
                                                                      row.action->text()),
                                                                i18nc("@title:window", "Shortcut Already Used"),
                                                                KGuiItem(i18nc("@action:button", "Reassign"), u"view-refresh"_s),
                                                                KStandardGuiItem::cancel());
            if (answer != KMessageBox::PrimaryAction) {
                // Show the previous shortcut again.
                setShortcut(row, row.shortcut);
                return;
            }
            setShortcut(other, {});
        }
    }
    setShortcut(row, shortcut);
}

void ShortcutsOptionsPage::resetChanges()
{
    for (auto &row : mRows) {
        setShortcut(row, row.action->shortcut());
    }
}

void ShortcutsOptionsPage::saveChanges()
{
    for (const auto &row : std::as_const(mRows)) {
        row.action->setShortcut(row.shortcut);
    }
    ShortcutActions::self()->writeSettings();
    // Check right away whether the new shortcuts can be used.
    GlobalShortcuts::instance()->checkShortcuts();
}

bool ShortcutsOptionsPage::isModified() const
{
    return std::any_of(mRows.cbegin(), mRows.cend(), [](const Row &row) {
        return row.shortcut != row.action->shortcut();
    });
}

bool ShortcutsOptionsPage::isDefault() const
{
    return std::all_of(mRows.cbegin(), mRows.cend(), [](const Row &row) {
        return row.shortcut == row.defaultShortcut;
    });
}

void ShortcutsOptionsPage::defaults()
{
    for (auto &row : mRows) {
        setShortcut(row, row.defaultShortcut);
    }
}

void ShortcutsOptionsPage::updateFailedShortcutsMessage()
{
    const auto failed = GlobalShortcuts::instance()->failedShortcuts();
    if (failed.isEmpty()) {
        mFailedMessage->setVisible(false);
        return;
    }
    QStringList names;
    for (const auto &shortcut : failed) {
        names << shortcut.toString(QKeySequence::NativeText);
    }
    mFailedMessage->setText(i18ncp("@info %2 is a list of keyboard shortcuts",
                                   "This shortcut could not be registered because it is used by Windows or another application: %2",
                                   "These shortcuts could not be registered because they are used by Windows or other applications: %2",
                                   names.size(),
                                   names.join(i18nc("separator of a list of shortcuts", ", "))));
    mFailedMessage->setVisible(true);
}

#include "moc_ShortcutsOptionsPage.cpp"
