/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "FotonMenu.h"
#include "WidgetWindowUtils.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

// Some styles, like Breeze, use a translucent background for rounded menu corners.
// The native Windows styles don't paint the menu background in that case.
static void setupMenuBackground(QMenu *menu)
{
#ifndef Q_OS_WIN
    menu->setAttribute(Qt::WA_TranslucentBackground);
#else
    Q_UNUSED(menu)
#endif
}

FotonMenu::FotonMenu(const QString &title, QWidget *parent)
    : QMenu(title, parent)
{
    setupMenuBackground(this);
}

FotonMenu::FotonMenu(QWidget *parent)
    : QMenu(parent)
{
    setupMenuBackground(this);
}

void FotonMenu::setVisible(bool visible)
{
    bool oldVisible = isVisible();
    if (oldVisible == visible) {
        return;
    }
    // Workaround for a bug where Qt Quick buttons always open the menu even when the menu is already open
    if (visible) {
        QMenu::setVisible(true);
    } else {
        QTimer::singleShot(200, this, [this] {
            QMenu::setVisible(false);
        });
    }
}

void FotonMenu::popup(QQuickItem *item)
{
    if (!item || !item->window()) {
        return;
    }
    auto itemWindow = item->window();
    auto point = item->mapToGlobal({0, item->height()});
    auto screenRect = itemWindow->screen()->geometry();
    auto sizeHint = this->sizeHint();
    if (point.y() + sizeHint.height() > screenRect.bottom()) {
        point.setY(point.y() - item->height() - sizeHint.height());
    }
    if (point.x() + sizeHint.width() > screenRect.right()) {
        point.setX(point.x() - sizeHint.width() + item->width());
    }
    setWidgetTransientParent(this, itemWindow);
    // Workaround same as plasma to have click anywhereto close the menu
    QTimer::singleShot(0, this, [this, itemWindow, point]() {
        if (itemWindow->mouseGrabberItem()) {
            itemWindow->mouseGrabberItem()->ungrabMouse();
        }
        QMenu::popup(point.toPoint());
    });
}

void FotonMenu::showEvent(QShowEvent *event)
{
    QMenu::showEvent(event);
    Q_EMIT visibleChanged();
}

void FotonMenu::hideEvent(QHideEvent *event)
{
    QMenu::hideEvent(event);
    Q_EMIT visibleChanged();
}

#include "moc_FotonMenu.cpp"
