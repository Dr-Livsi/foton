/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef OPTIONSMENU_H
#define OPTIONSMENU_H

#include "FotonMenu.h"

#include <QQmlEngine>

/**
 * A menu that allows choosing capture modes and related options.
 */
class OptionsMenu : public FotonMenu
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static OptionsMenu *instance();

    Q_SLOT void showPreferencesDialog();

    static OptionsMenu *create(QQmlEngine *engine, QJSEngine *)
    {
        auto inst = instance();
        Q_ASSERT(inst);
        Q_ASSERT(inst->thread() == engine->thread());
        QJSEngine::setObjectOwnership(inst, QJSEngine::CppOwnership);
        return inst;
    }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

    explicit OptionsMenu(QWidget *parent = nullptr);
    ~OptionsMenu();
};

#endif // OPTIONSMENU_H
