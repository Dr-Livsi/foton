/* SPDX-FileCopyrightText: 2023 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "Geometry.h"
#include "Config.h"

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// Behaves like qBound, which behaves differently from std::clamp,
// but uses the same argument order as std::clamp.
// We don't use qBound or std::clamp because we don't want asserts.
constexpr static inline qreal clamp(qreal value, qreal min, qreal max) noexcept
{
    return std::max(min, std::min(value, max));
}

Geometry::Geometry(QObject *parent)
    : QObject(parent)
{}

qreal Geometry::dpx(qreal dpr)
{
    return 1 / dpr;
}

qreal Geometry::dprRound(qreal value, qreal dpr)
{
    return std::round(value * dpr) / dpr;
}

QPointF Geometry::dprRound(QPointF point, qreal dpr)
{
    return {std::round(point.x() * dpr) / dpr, std::round(point.y() * dpr) / dpr};
}

qreal Geometry::dprCeil(qreal value, qreal dpr)
{
    return std::ceil(value * dpr) / dpr;
}

qreal Geometry::dprFloor(qreal value, qreal dpr)
{
    return std::floor(value * dpr) / dpr;
}

namespace
{
struct ScreenLayout {
    QScreen *screen = nullptr;
    QRect nativeRect;
    qreal dpr = 1;
    QRectF logicalRect;
    bool placed = false;
};

#ifdef Q_OS_WIN
BOOL CALLBACK collectMonitorRect(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
    MONITORINFO info;
    info.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfoW(monitor, &info)) {
        const auto &r = info.rcMonitor;
        reinterpret_cast<QList<QRect> *>(data)->append(QRect(r.left, r.top, r.right - r.left, r.bottom - r.top));
    }
    return TRUE;
}
#endif

#ifdef Q_OS_WIN
// The position of `other` if it is placed next to `placed`, using the physical layout.
// Distances along the shared edge are measured in the logical pixels of the placed screen.
std::optional<QPointF> adjacentLogicalPosition(const ScreenLayout &placed, const ScreenLayout &other)
{
    const QRect &a = placed.nativeRect;
    const QRect &b = other.nativeRect;
    const QSizeF otherSize = QSizeF(b.size()) / other.dpr;
    // QRect::right() and QRect::bottom() are off by one for historical reasons.
    const int aRight = a.x() + a.width();
    const int aBottom = a.y() + a.height();
    const int bRight = b.x() + b.width();
    const int bBottom = b.y() + b.height();
    const bool verticalOverlap = b.y() < aBottom && bBottom > a.y();
    const bool horizontalOverlap = b.x() < aRight && bRight > a.x();
    const QRectF &l = placed.logicalRect;
    if (verticalOverlap && b.x() == aRight) {
        return QPointF{l.x() + l.width(), l.y() + (b.y() - a.y()) / placed.dpr};
    }
    if (verticalOverlap && bRight == a.x()) {
        return QPointF{l.x() - otherSize.width(), l.y() + (b.y() - a.y()) / placed.dpr};
    }
    if (horizontalOverlap && b.y() == aBottom) {
        return QPointF{l.x() + (b.x() - a.x()) / placed.dpr, l.y() + l.height()};
    }
    if (horizontalOverlap && bBottom == a.y()) {
        return QPointF{l.x() + (b.x() - a.x()) / placed.dpr, l.y() - otherSize.height()};
    }
    return std::nullopt;
}
#endif

QList<ScreenLayout> screenLayouts()
{
    QList<ScreenLayout> layouts;
    const auto screens = qGuiApp->screens();
    if (screens.isEmpty()) {
        return layouts;
    }
    layouts.reserve(screens.size());
    for (auto screen : screens) {
        layouts.append({screen, Geometry::nativeScreenRect(screen), screen->devicePixelRatio(), {}, false});
    }

#ifdef Q_OS_WIN
    auto place = [](ScreenLayout &layout, const QPointF &pos) {
        layout.logicalRect = {pos, QSizeF(layout.nativeRect.size()) / layout.dpr};
        layout.placed = true;
    };
    // Start with the primary screen and place the other screens next to the screens
    // that have already been placed, the same way they are arranged in the display settings.
    auto primaryIt = std::find_if(layouts.begin(), layouts.end(), [](const ScreenLayout &layout) {
        return layout.screen == qGuiApp->primaryScreen();
    });
    if (primaryIt == layouts.end()) {
        primaryIt = layouts.begin();
    }
    place(*primaryIt, QPointF(primaryIt->nativeRect.topLeft()) / primaryIt->dpr);
    qsizetype placedCount = 1;
    while (placedCount < layouts.size()) {
        bool placedAny = false;
        for (auto &other : layouts) {
            if (other.placed) {
                continue;
            }
            for (const auto &placed : std::as_const(layouts)) {
                if (!placed.placed) {
                    continue;
                }
                if (const auto pos = adjacentLogicalPosition(placed, other)) {
                    place(other, *pos);
                    ++placedCount;
                    placedAny = true;
                    break;
                }
            }
        }
        if (!placedAny) {
            // A screen that doesn't touch any of the placed screens.
            auto it = std::find_if(layouts.begin(), layouts.end(), [](const ScreenLayout &layout) {
                return !layout.placed;
            });
            place(*it, QPointF(it->nativeRect.topLeft()) / it->dpr);
            ++placedCount;
        }
    }
#else
    // Other platforms are only used for development. Assume Qt already uses a logical layout.
    for (auto &layout : layouts) {
        layout.logicalRect = QRectF(layout.screen->geometry().topLeft(), QSizeF(layout.nativeRect.size()) / layout.dpr);
        layout.placed = true;
    }
#endif
    // The annotation document always puts the screenshot at (0, 0),
    // so the logical coordinate system has to start there too.
    QRectF boundingRect;
    for (const auto &layout : std::as_const(layouts)) {
        boundingRect |= layout.logicalRect;
    }
    for (auto &layout : layouts) {
        layout.logicalRect.translate(-boundingRect.topLeft());
    }
    return layouts;
}
}

QRect Geometry::nativeScreenRect(QScreen *screen)
{
    if (!screen) {
        return {};
    }
    const QRect geometry = screen->geometry();
#ifdef Q_OS_WIN
    // Qt keeps the native position of the screen in QScreen::geometry.
    QList<QRect> monitorRects;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitorRect, reinterpret_cast<LPARAM>(&monitorRects));
    for (const auto &rect : std::as_const(monitorRects)) {
        if (rect.topLeft() == geometry.topLeft()) {
            return rect;
        }
    }
#endif
    return {geometry.topLeft(), (QSizeF(geometry.size()) * screen->devicePixelRatio()).toSize()};
}

QRectF Geometry::logicalScreenRect(QScreen *screen)
{
    const auto layouts = screenLayouts();
    for (const auto &layout : layouts) {
        if (layout.screen == screen) {
            return layout.logicalRect;
        }
    }
    return {};
}

QRectF Geometry::logicalScreensRect()
{
    QRectF rect;
    const auto layouts = screenLayouts();
    for (const auto &layout : layouts) {
        rect |= layout.logicalRect;
    }
    return rect;
}

QPointF Geometry::mapToLogicalPoint(const QPointF &globalPos, QScreen *screen)
{
    if (!screen) {
        return globalPos;
    }
    // Within a screen, Qt's global coordinates are the native position of the screen
    // plus the logical offset from the top left corner of the screen.
    return logicalScreenRect(screen).topLeft() + (globalPos - screen->geometry().topLeft());
}

QPointF Geometry::mapNativeToLogicalPoint(const QPointF &nativePos)
{
    auto screen = screenAtNativePoint(nativePos.toPoint());
    if (!screen) {
        return nativePos;
    }
    const auto layouts = screenLayouts();
    for (const auto &layout : layouts) {
        if (layout.screen == screen) {
            return layout.logicalRect.topLeft() + (nativePos - QPointF(layout.nativeRect.topLeft())) / layout.dpr;
        }
    }
    return nativePos;
}

QScreen *Geometry::screenAtNativePoint(const QPoint &nativePos)
{
    QScreen *nearestScreen = nullptr;
    qreal nearestDistance = std::numeric_limits<qreal>::max();
    const auto screens = qGuiApp->screens();
    for (auto screen : screens) {
        const QRect rect = nativeScreenRect(screen);
        if (rect.contains(nativePos)) {
            return screen;
        }
        const qreal dx = std::max({rect.left() - nativePos.x(), 0, nativePos.x() - rect.right()});
        const qreal dy = std::max({rect.top() - nativePos.y(), 0, nativePos.y() - rect.bottom()});
        const qreal distance = std::hypot(dx, dy);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestScreen = screen;
        }
    }
    return nearestScreen;
}

QSize Geometry::rawSize(const QSizeF &size, qreal dpr)
{
    return (size * dpr).toSize();
}

QRectF Geometry::rectNormalized(const QRectF &rect)
{
    return rect.normalized();
}

QRectF Geometry::rectNormalized(qreal x, qreal y, qreal w, qreal h)
{
    return QRectF(x, y, w, h).normalized();
}

QRectF Geometry::rectAdjusted(const QRectF &rect, qreal xp1, qreal yp1, qreal xp2, qreal yp2)
{
    return rect.adjusted(xp1, yp1, xp2, yp2);
}

QRectF Geometry::rectAdjustedVisually(const QRectF &rect, qreal xp1, qreal yp1, qreal xp2, qreal yp2)
{
    if (rect.width() < 0) {
        std::swap(xp1, xp2);
    }
    if (rect.height() < 0) {
        std::swap(yp1, yp2);
    }
    return rect.adjusted(xp1, yp1, xp2, yp2);
}

QRectF Geometry::rectScaled(const QRectF &rect, qreal scale)
{
    if (scale == 1) {
        return rect;
    }
    return {rect.topLeft() * scale, rect.size() * scale};
}

QRectF Geometry::rectIntersected(const QRectF &rect1, const QRectF &rect2)
{
    return rect1.intersected(rect2);
}

QRectF Geometry::rectBounded(const QRectF &rect, const QRectF &boundsRect,
                             Qt::Orientations orientations)
{
    if (rect == boundsRect) {
        return rect;
    }
    auto newRect = rect;
    const auto &nBoundsRect = boundsRect.normalized(); // normalize to make math easier
    if (orientations & Qt::Horizontal) {
        if (rect.width() >= 0) {
            newRect.moveLeft(clamp(rect.x(), nBoundsRect.x(), nBoundsRect.right() - rect.width()));
        } else {
            newRect.moveRight(clamp(rect.right(), nBoundsRect.x(), nBoundsRect.right() - std::abs(rect.width())));
        }
    }
    if (orientations & Qt::Vertical) {
        if (rect.height() >= 0) {
            newRect.moveTop(clamp(rect.y(), nBoundsRect.y(), nBoundsRect.bottom() - std::abs(rect.height())));
        } else {
            newRect.moveBottom(clamp(rect.bottom(), nBoundsRect.y(), nBoundsRect.bottom() - std::abs(rect.height())));
        }
    }
    return newRect;
}

QRectF Geometry::rectBounded(qreal x, qreal y, qreal width, qreal height, const QRectF &boundsRect,
                             Qt::Orientations orientations)
{
    if (boundsRect.isEmpty()) {
        return {};
    }

    return rectBounded({x, y, width, height}, boundsRect, orientations);
}

QRectF Geometry::rectClipped(const QRectF &rect, const QRectF &clipRect,
                             Qt::Orientations orientations)
{
    if (rect == clipRect) {
        return rect;
    }
    auto newRect = rect;
    const auto &nClipRect = clipRect.normalized(); // normalize to make math easier
    if (orientations & Qt::Horizontal) {
        if (rect.width() >= 0) {
            newRect.setLeft(std::max(rect.x(), nClipRect.x()));
            newRect.setRight(std::min(rect.right(), nClipRect.right()));
        } else {
            newRect.setLeft(std::min(rect.x(), nClipRect.right()));
            newRect.setRight(std::max(rect.right(), nClipRect.x()));
        }
    }
    if (orientations & Qt::Vertical) {
        if (rect.height() >= 0) {
            newRect.setTop(std::max(rect.y(), nClipRect.y()));
            newRect.setBottom(std::min(rect.bottom(), nClipRect.bottom()));
        } else {
            newRect.setTop(std::min(rect.y(), nClipRect.bottom()));
            newRect.setBottom(std::max(rect.bottom(), nClipRect.y()));
        }
    }
    return newRect;
}

bool Geometry::rectContains(const QRectF &rect, qreal v, Qt::Orientations orientations)
{
    Q_ASSERT(orientations != Qt::Orientations{});
    bool contains = true;
    if (orientations.testFlag(Qt::Horizontal)) {
        contains &= std::min(rect.left(), rect.right()) <= v && v <= std::max(rect.left(), rect.right());
    }
    if (contains && orientations.testFlag(Qt::Vertical)) {
        contains &= std::min(rect.top(), rect.bottom()) <= v && v <= std::max(rect.top(), rect.bottom());
    }
    return contains;
}

bool Geometry::rectContains(const QRectF &rect, qreal x, qreal y)
{
    return rect.contains(x, y);
}

bool Geometry::rectContains(const QRectF &rect, const QPointF &point)
{
    return rect.contains(point);
}

bool Geometry::rectContains(const QRectF &rect1, const QRectF& rect2)
{
    return rect1.contains(rect2);
}

bool Geometry::rectContains(const QRectF &rect, qreal x, qreal y, qreal w, qreal h)
{
    return rect.contains({x, y, w, h});
}

bool Geometry::ellipseContains(qreal ellipseX, qreal ellipseY,
                               qreal ellipseWidth, qreal ellipseHeight,
                               qreal x, qreal y)
{
    if (isEmpty(ellipseWidth, ellipseHeight)) {
        return false;
    }
    auto xRadius = ellipseWidth / 2;
    auto yRadius = ellipseHeight / 2;
    auto centerX = ellipseX + xRadius;
    auto centerY = ellipseY + yRadius;
    // is inside or on edge
    return std::pow(x - centerX, 2) / std::pow(xRadius, 2)
         + std::pow(y - centerY, 2) / std::pow(yRadius, 2) <= 1;
}

bool Geometry::ellipseContains(const QRectF &rect, qreal x, qreal y)
{
    return ellipseContains(rect.x(), rect.y(), rect.width(), rect.height(), x, y);
}

bool Geometry::ellipseContains(const QRectF &rect, const QPointF &point)
{
    return ellipseContains(rect, point.x(), point.y());
}

bool Geometry::rectIntersects(const QRectF &rect1, const QRectF& rect2)
{
    return rect1.intersects(rect2);
}

bool Geometry::rectIntersects(const QRectF &rect, qreal x, qreal y, qreal w, qreal h)
{
    return rect.intersects({x, y, w, h});
}

bool Geometry::isEmpty(qreal w, qreal h)
{
    return w <= 0 || h <= 0;
}

bool Geometry::isEmpty(const QSizeF &size)
{
    return size.isEmpty();
}

bool Geometry::isEmpty(const QRectF &rect)
{
    return rect.isEmpty();
}

bool Geometry::isNull(qreal w, qreal h)
{
    return qIsNull(w) && qIsNull(h);
}

bool Geometry::isNull(const QSizeF &size)
{
    return size.isNull();
}

bool Geometry::isNull(const QRectF &rect)
{
    return rect.isNull();
}

#include <moc_Geometry.cpp>
