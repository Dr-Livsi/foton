/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 * SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QQuickView>
#include <QQmlContext>

class FotonWindowPrivate;

/**
 * The base window class for Foton's Qt Quick UIs.
 * Adapted from KSMainWindow, a QDialog subclass from the old Qt Widgets UI.
 */
class FotonWindow : public QQuickView
{
    Q_OBJECT
    Q_PROPERTY(bool annotating READ isAnnotating WRITE setAnnotating NOTIFY annotatingChanged FINAL)
    Q_PROPERTY(qreal logicalX READ logicalX NOTIFY logicalXChanged)
    Q_PROPERTY(qreal logicalY READ logicalY NOTIFY logicalYChanged)

public:
    enum TitlePreset {
        Default,
        Timer,
        Unsaved,
        Saved,
        Modified,
        Previous,
    };

    qreal logicalX() const;
    qreal logicalY() const;

    bool isAnnotating() const;
    void setAnnotating(bool annotating);

    /**
     * Makes the window visible and removes the WindowMinimized flag from the WindowStates flags.
     */
    void unminimize();

    static QList<FotonWindow *> instances();

    /**
     * Set the visibility of all FotonWindows created in FotonCore.
     * This will not work until the windows are fully initialized in FotonCore.
     */
    static void setVisibilityForAll(QWindow::Visibility visibility);

    /**
     * For all FotonWindows created in FotonCore, set the title based on the chosen preset.
     * The `fileName` parameter is used for the Saved and Modified presets.
     * This will not work until the windows are fully initialized in FotonCore.
     */
    static void setTitleForAll(TitlePreset preset, const QString &fileName = {});

    /**
     * Close all FotonWindows.
     */
    static void closeAll();

    /**
     * Round values to be physically pixel perfect, based on the device pixel ratio.
     * Meant to be used with coordinates, line widths and shape sizes.
     * This is meant to be used in QML.
     */
    Q_INVOKABLE qreal dprRound(qreal value) const;
    Q_INVOKABLE QPointF dprRound(const QPointF &point) const;
    Q_INVOKABLE qreal dprCeil(qreal value) const;
    Q_INVOKABLE qreal dprFloor(qreal value) const;

    /**
     * Get the basename for a file URL.
     * This is meant to be used in QML.
     */
    Q_INVOKABLE QString baseFileName(const QUrl &url) const;

public Q_SLOTS:
    virtual void save();
    virtual void saveAs();
    virtual void copyImage();
    virtual void copyLocation();

    void showPreferencesDialog();
    void showFontDialog();
    void showColorDialog(int option);

Q_SIGNALS:
    void annotatingChanged();
    void logicalXChanged();
    void logicalYChanged();

protected:
    explicit FotonWindow(QQmlEngine *engine, QWindow *parent = nullptr);
    ~FotonWindow();

    using QQuickView::setTitle;

    static QString titlePresetString(TitlePreset preset, const QString &fileName = {});
    static void deleter(FotonWindow *window);

    // set source, but with a window specific QQmlContext and initial properties
    void setSource(const QUrl &source, const QVariantMap &initialProperties);

    /**
     * Whether the screenshot may be closed, e.g. with Escape.
     * Windows that show an unsaved screenshot ask the user first.
     */
    virtual bool confirmClose()
    {
        return true;
    }

    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

    static QList<FotonWindow *> s_fotonWindowInstances;
    static bool s_synchronizingVisibility;
    static bool s_synchronizingTitle;
    static TitlePreset s_lastTitlePreset;
    static QString s_previousTitle;
    static bool s_synchronizingAnnotating;
    static bool s_isAnnotating;

    const std::unique_ptr<QQmlContext> m_context;
    std::unique_ptr<QQmlComponent> m_component;

    Qt::MouseButtons m_pressedButtons;
    QKeySequence m_pressedKeys;
};
