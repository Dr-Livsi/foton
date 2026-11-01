/*
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef GENERALOPTIONSPAGE_H
#define GENERALOPTIONSPAGE_H

#include <QScopedPointer>
#include <QWidget>

class Ui_GeneralOptions;

class GeneralOptionsPage : public QWidget
{
    Q_OBJECT

public:
    explicit GeneralOptionsPage(QWidget *parent = nullptr);
    ~GeneralOptionsPage() override;

    // The language is not stored in the Foton settings, so KConfigDialog doesn't handle it.
    QString selectedLanguage() const;
    bool isLanguageChanged() const;
    bool isLanguageDefault() const;
    void loadLanguage();
    void setDefaultLanguage();

Q_SIGNALS:
    void languageChanged();

private:
    void selectLanguage(const QString &code);

    QScopedPointer<Ui_GeneralOptions> m_ui;
};

#endif // GENERALOPTIONSPAGE_H
