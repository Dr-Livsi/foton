/* SPDX-FileCopyrightText: 2023 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QQmlEngine>

#include <memory>

class QPoint;
class QPointF;
class QRect;
class QRectF;
class QScreen;
class QSizeF;

/**
 * This is a class for processing geometry.
 *
 * This class has functionality for converting between the logical coordinate system
 * used for screenshots and the screen coordinates used by the platform.
 *
 * On Windows, screens are positioned in a virtual desktop using physical (native) pixels.
 * Qt reports QScreen::geometry with the native position of the screen and the native size
 * scaled down by the device pixel ratio (DPR) of that screen, so the screens usually don't
 * touch each other anymore. The logical coordinate system used by Foton lays out the
 * screens like the physical layout, but with logical sizes and without gaps between screens.
 * When all screens use the same DPR, the logical geometry is just the native geometry
 * divided by the DPR.
 *
 * QScreen::geometry is a QRect, which means some precision is lost when the
 * raw size is scaled down, especially when fractional scale factors are used or the
 * resolution has an odd width/height. The logical geometry uses QRectF to avoid that.
 */
class Geometry : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Geometry(QObject *parent = nullptr);

    /**
     * This returns the logical size of a raw device pixel based on the given device pixel ratio.
     * This has a short name so that it doesn't make lines of code really long.
     */
    [[nodiscard]] Q_INVOKABLE static qreal dpx(qreal dpr);

    /**
     * This rounds a logical axis value to a value that should be aligned to hardware pixels
     * if the given device pixel ratio is correct.
     */
    [[nodiscard]] Q_INVOKABLE static qreal dprRound(qreal value, qreal dpr);

    /**
     * This rounds a point to a point that should be aligned to hardware pixels
     * if the given device pixel ratio is correct.
     */
    [[nodiscard]] Q_INVOKABLE static QPointF dprRound(QPointF point, qreal dpr);

    /**
     * This ceils a logical axis value to a value that should be aligned to hardware pixels
     * if the given device pixel ratio is correct.
     */
    [[nodiscard]] Q_INVOKABLE static qreal dprCeil(qreal value, qreal dpr);

    /**
     * This floors a logical axis value to a value that should be aligned to hardware pixels
     * if the given device pixel ratio is correct.
     */
    [[nodiscard]] Q_INVOKABLE static qreal dprFloor(qreal value, qreal dpr);

    /**
     * The geometry of the screen in the logical coordinate system used for screenshots.
     * The size is the native size divided by the device pixel ratio of the screen.
     */
    [[nodiscard]] Q_INVOKABLE static QRectF logicalScreenRect(QScreen *screen);

    /**
     * The geometry of the screen in native (physical) pixels of the virtual desktop.
     */
    [[nodiscard]] static QRect nativeScreenRect(QScreen *screen);

    /**
     * This returns the union of all logical screen rects.
     */
    [[nodiscard]] Q_INVOKABLE static QRectF logicalScreensRect();

    /**
     * Map a position in Qt's global coordinate system (like QWindow::position())
     * on the given screen to the logical coordinate system.
     */
    [[nodiscard]] Q_INVOKABLE static QPointF mapToLogicalPoint(const QPointF &globalPos, QScreen *screen);

    /**
     * Map a position in native pixels of the virtual desktop to the logical coordinate system.
     */
    [[nodiscard]] static QPointF mapNativeToLogicalPoint(const QPointF &nativePos);

    /**
     * The screen containing the position in native pixels of the virtual desktop,
     * or the nearest screen if no screen contains it.
     */
    [[nodiscard]] static QScreen *screenAtNativePoint(const QPoint &nativePos);

    /**
     * This returns a raw size for a logical size by multiplying by the DPR.
     * NOTE: The value will only be correct if no precision was lost prior to running the function.
     */
    [[nodiscard]] Q_INVOKABLE static QSize rawSize(const QSizeF &size, qreal dpr);

    [[nodiscard]] Q_INVOKABLE static QRectF rectNormalized(const QRectF &rect);
    [[nodiscard]] Q_INVOKABLE static QRectF rectNormalized(qreal x, qreal y, qreal w, qreal h);

    /// Get the rectangle with adjusted left, top, right and bottom sides.
    [[nodiscard]] Q_INVOKABLE static QRectF rectAdjusted(const QRectF &rect, qreal xp1, qreal yp1, qreal xp2, qreal yp2);

    /**
     * Get the rectangle with adjusted left, top, right and bottom sides.
     * This affects the sides of invalid rectangles as if they were normalized.
     * If the rect is invalid, that will be preserved.
     */
    [[nodiscard]] Q_INVOKABLE static QRectF rectAdjustedVisually(const QRectF &rect, qreal xp1, qreal yp1, qreal xp2, qreal yp2);

    /// Get a rect with a size and position multiplied by the scale.
    [[nodiscard]] Q_INVOKABLE static QRectF rectScaled(const QRectF &rect, qreal scale);

    /// Get the intersection of two rectangles.
    [[nodiscard]] Q_INVOKABLE static QRectF rectIntersected(const QRectF &rect1, const QRectF &rect2);

    /**
     * Try to make the rect positioned fully inside boundsRect without clipping on the given axes.
     * The rect may still be out of bounds if the size is too large.
     * If the rect is invalid, that will be preserved.
     */
    [[nodiscard]] Q_INVOKABLE static QRectF rectBounded(const QRectF &rect, const QRectF &boundsRect, //
                                                        Qt::Orientations orientations = Qt::Horizontal | Qt::Vertical);
    [[nodiscard]] Q_INVOKABLE static QRectF rectBounded(qreal x, qreal y, qreal width, qreal height, const QRectF &boundsRect, //
                                                        Qt::Orientations orientations = Qt::Horizontal | Qt::Vertical);

    /**
     * Clip the rect to the clipRect on the given axes.
     * If the rect does not intersect with the clipRect,
     * it will become an empty rect on the edge of clipRect nearest to the original position.
     * If the rect is invalid, that will be preserved.
     */
    [[nodiscard]] Q_INVOKABLE static QRectF rectClipped(const QRectF &rect, const QRectF &clipRect, //
                                                        Qt::Orientations orientations = Qt::Horizontal | Qt::Vertical);

    /// Check if the rectangle contains the value on the given axes.
    [[nodiscard]] Q_INVOKABLE static bool rectContains(const QRectF &rect, qreal v, //
                                                       Qt::Orientations orientations = Qt::Horizontal | Qt::Vertical);
    /// Check if the rectangle contains the point.
    [[nodiscard]] Q_INVOKABLE static bool rectContains(const QRectF &rect, qreal x, qreal y);
    [[nodiscard]] Q_INVOKABLE static bool rectContains(const QRectF &rect, const QPointF& point);
    /// Check if the first rectangle contains the second rectangle.
    [[nodiscard]] Q_INVOKABLE static bool rectContains(const QRectF &rect1, const QRectF& rect2);
    [[nodiscard]] Q_INVOKABLE static bool rectContains(const QRectF &rect, qreal x, qreal y, qreal w, qreal h);

    /// Check if the ellipse contains the point.
    [[nodiscard]] Q_INVOKABLE static bool ellipseContains(qreal ellipseX, qreal ellipseY,
                                                          qreal ellipseWidth, qreal ellipseHeight,
                                                          qreal x, qreal y);
    /// Assuming the rectangle represents an ellipse's bounding box, check if it contains the point.
    [[nodiscard]] Q_INVOKABLE static bool ellipseContains(const QRectF &rect, qreal x, qreal y);
    [[nodiscard]] Q_INVOKABLE static bool ellipseContains(const QRectF &rect, const QPointF &point);

    /// Check if two rectangles intersect.
    [[nodiscard]] Q_INVOKABLE static bool rectIntersects(const QRectF &rect1, const QRectF& rect2);
    [[nodiscard]] Q_INVOKABLE static bool rectIntersects(const QRectF &rect, qreal x, qreal y, qreal w, qreal h);

    /// Is width or height less than or equal to 0
    [[nodiscard]] Q_INVOKABLE static bool isEmpty(qreal w, qreal h);
    [[nodiscard]] Q_INVOKABLE static bool isEmpty(const QSizeF &size);
    [[nodiscard]] Q_INVOKABLE static bool isEmpty(const QRectF &rect);

    /// Are width and height equal to 0
    [[nodiscard]] Q_INVOKABLE static bool isNull(qreal w, qreal h);
    [[nodiscard]] Q_INVOKABLE static bool isNull(const QSizeF &size);
    [[nodiscard]] Q_INVOKABLE static bool isNull(const QRectF &rect);

private:
    Geometry(const Geometry &) = delete;
    Geometry(Geometry &&) = delete;
    Geometry &operator=(const Geometry &) = delete;
    Geometry &operator=(Geometry &&) = delete;
    friend class GeometrySingleton;
};
