/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QUrl>
#include <QWidget>

class QLineEdit;
class QToolButton;

/**
 * A line edit with a button for choosing a local folder.
 * Replaces KUrlRequester, which needs KIO.
 * The url property is used by KConfigDialog to load and save the setting.
 */
class FolderRequester : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QUrl url READ url WRITE setUrl NOTIFY urlChanged USER true)

public:
    explicit FolderRequester(QWidget *parent = nullptr);

    QUrl url() const;
    void setUrl(const QUrl &url);

Q_SIGNALS:
    void urlChanged(const QUrl &url);

private:
    void chooseFolder();

    QLineEdit *const m_lineEdit;
    QToolButton *const m_button;
};
