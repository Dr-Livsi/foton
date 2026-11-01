/*
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <KConfigDialog>

class GeneralOptionsPage;
class ImageSaveOptionsPage;
class ShortcutsOptionsPage;

class SettingsDialog : public KConfigDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

protected:
    QSize sizeHint() const override;
    void showEvent(QShowEvent *event) override;

private:
    bool hasChanged() override;
    bool isDefault() override;
    void updateSettings() override;
    void updateWidgets() override;
    void updateWidgetsDefault() override;
    void askForRestart();

    GeneralOptionsPage *const m_generalPage;
    ImageSaveOptionsPage *const m_imagesPage;
    ShortcutsOptionsPage *const m_shortcutsPage;
};

#endif // SETTINGSDIALOG_H
