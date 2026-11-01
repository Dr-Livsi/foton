/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 * SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 * SPDX-FileCopyrightText: 2020 Ahmad Samir <a.samirh78@gmail.com>
 * SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "FotonWindow.h"

#include "ExportManager.h"
#include "FotonCore.h"
#include "Geometry.h"
#include "Gui/HelpMenu.h"
#include "Gui/OptionsMenu.h"
#include "Gui/WidgetWindowUtils.h"
#include "WindowsIntegration.h"

#include <QApplication>
#include <QColorDialog>
#include <QFontDialog>
#include <QPlatformSurfaceEvent>
#include <utility>

using namespace Qt::StringLiterals;
using G = Geometry;

QList<FotonWindow *> FotonWindow::s_fotonWindowInstances = {};
bool FotonWindow::s_synchronizingVisibility = false;
bool FotonWindow::s_synchronizingTitle = false;
FotonWindow::TitlePreset FotonWindow::s_lastTitlePreset = Default;
QString FotonWindow::s_previousTitle = QGuiApplication::applicationDisplayName();
bool FotonWindow::s_synchronizingAnnotating = false;
bool FotonWindow::s_isAnnotating = false;

FotonWindow::FotonWindow(QQmlEngine *engine, QWindow *parent)
    : QQuickView(engine, parent)
    , m_context(new QQmlContext(engine->rootContext(), this))
{
    s_fotonWindowInstances.append(this);

    connect(engine, &QQmlEngine::quit, QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
    connect(this, &QQuickView::statusChanged, this, [](QQuickView::Status status){
        if (status == QQuickView::Error) {
            QCoreApplication::quit();
        }
    });
    connect(this, &FotonWindow::xChanged, this, &FotonWindow::logicalXChanged);
    connect(this, &FotonWindow::yChanged, this, &FotonWindow::logicalYChanged);

    setTextRenderType(QQuickWindow::NativeTextRendering);

    // set up QML
    setResizeMode(QQuickView::SizeRootObjectToView);
    m_context->setContextProperty(u"contextWindow"_s, this);
}

FotonWindow::~FotonWindow()
{
    s_fotonWindowInstances.removeOne(this);
}

qreal FotonWindow::logicalX() const
{
    return G::mapToLogicalPoint(position(), screen()).x();
}

qreal FotonWindow::logicalY() const
{
    return G::mapToLogicalPoint(position(), screen()).y();
}

bool FotonWindow::isAnnotating() const
{
    return s_isAnnotating;
}

void FotonWindow::setAnnotating(bool annotating)
{
    if (s_synchronizingAnnotating || s_isAnnotating == annotating) {
        return;
    }
    s_synchronizingAnnotating = true;
    s_isAnnotating = annotating;
    for (auto window : std::as_const(s_fotonWindowInstances)) {
        Q_EMIT window->annotatingChanged();
    }
    s_synchronizingAnnotating = false;
}

void FotonWindow::unminimize()
{
    setVisible(true);
    setWindowStates(windowStates().setFlag(Qt::WindowMinimized, false));
}

QList<FotonWindow *> FotonWindow::instances()
{
    return s_fotonWindowInstances;
}

void FotonWindow::setVisibilityForAll(QWindow::Visibility visibility)
{
    if (s_synchronizingVisibility || s_fotonWindowInstances.isEmpty()) {
        return;
    }
    s_synchronizingVisibility = true;
    for (auto window : std::as_const(s_fotonWindowInstances)) {
        window->setVisibility(visibility);
    }
    s_synchronizingVisibility = false;
}

void FotonWindow::setTitleForAll(TitlePreset preset, const QString &fileName)
{
    if (s_synchronizingTitle || s_fotonWindowInstances.isEmpty()) {
        return;
    }
    s_synchronizingTitle = true;

    QString newTitle = titlePresetString(preset, fileName);

    if (!newTitle.isEmpty()) {
        if (s_lastTitlePreset != TitlePreset::Timer) {
            s_previousTitle = s_fotonWindowInstances.constFirst()->title();
        }
        s_lastTitlePreset = preset;

        for (auto window : std::as_const(s_fotonWindowInstances)) {
            window->setTitle(newTitle);
        }
    }

    s_synchronizingTitle = false;
}

void FotonWindow::closeAll()
{
    // counting down should prevent invalid memory access
    for (int i = s_fotonWindowInstances.count() - 1; i >= 0; --i) {
        s_fotonWindowInstances[i]->close();
    }
}

qreal FotonWindow::dprRound(qreal value) const
{
    return G::dprRound(value, devicePixelRatio());
}

QPointF FotonWindow::dprRound(const QPointF &point) const
{
    return G::dprRound(point, devicePixelRatio());
}

qreal FotonWindow::dprCeil(qreal value) const
{
    return G::dprCeil(value, devicePixelRatio());
}

qreal FotonWindow::dprFloor(qreal value) const
{
    return G::dprFloor(value, devicePixelRatio());
}

QString FotonWindow::baseFileName(const QUrl &url) const
{
    return url.fileName();
}

QString FotonWindow::titlePresetString(TitlePreset preset, const QString &fileName)
{
    if (preset == TitlePreset::Timer) {
        return i18ncp("@title:window", "%1 second", "%1 seconds",
                      qCeil(FotonCore::instance()->captureTimeRemaining() / 1000.0));
    } else if (preset == TitlePreset::Unsaved) {
        return i18nc("@title:window Unsaved Screenshot", "Unsaved") + u"*"_s;
    } else if (preset == TitlePreset::Saved && !fileName.isEmpty()) {
        return fileName;
    } else if (preset == TitlePreset::Modified && !fileName.isEmpty()) {
        return fileName + u"*"_s;
    } else if (preset == TitlePreset::Previous && !s_previousTitle.isEmpty()) {
        return s_previousTitle;
    }
    return QGuiApplication::applicationDisplayName();
}

void FotonWindow::deleter(FotonWindow *window)
{
    s_fotonWindowInstances.removeOne(window);
    window->deleteLater();
}

void FotonWindow::setSource(const QUrl &source, const QVariantMap &initialProperties)
{
    if (source.isEmpty()) {
        m_component.reset(nullptr);
        QQuickView::setSource(source);
        return;
    }

    m_component.reset(new QQmlComponent(engine(), source, this));
    auto *component = m_component.get();
    QObject *object = nullptr;

    if (component->isLoading()) {
        connect(component, &QQmlComponent::statusChanged,
                this, [this, component, &source, &initialProperties]() {
            disconnect(component, &QQmlComponent::statusChanged, this, nullptr);
            QObject *object = nullptr;
            if (component->isReady()) {
                if (!initialProperties.isEmpty()) {
                    object = component->createWithInitialProperties(initialProperties,
                                                                    m_context.get());
                } else {
                    object = component->create(m_context.get());
                }
            }
            setContent(source, component, object);
        });
    } else if (component->isReady()) {
        if (!initialProperties.isEmpty()) {
            object = component->createWithInitialProperties(initialProperties, m_context.get());
        } else {
            object = component->create(m_context.get());
        }
    }

    setContent(source, component, object);
}

void FotonWindow::save()
{
    FotonCore::instance()->syncExportImage();
    ExportManager::instance()->exportImage(ExportManager::Save | ExportManager::UserAction,
                                           FotonCore::instance()->outputUrl());
}

void FotonWindow::saveAs()
{
    FotonCore::instance()->syncExportImage();
    ExportManager::instance()->exportImage(ExportManager::SaveAs | ExportManager::UserAction,
                                           FotonCore::instance()->outputUrl());
}

void FotonWindow::copyImage()
{
    FotonCore::instance()->syncExportImage();
    ExportManager::instance()->exportImage(ExportManager::CopyImage | ExportManager::UserAction);
}

void FotonWindow::copyLocation()
{
    FotonCore::instance()->syncExportImage();
    ExportManager::instance()->exportImage(ExportManager::CopyPath | ExportManager::UserAction);
}

void FotonWindow::showPreferencesDialog()
{
    OptionsMenu::instance()->showPreferencesDialog();
}

void FotonWindow::showFontDialog()
{
    auto tool = FotonCore::instance()->annotationDocument()->tool();
    auto wrapper = FotonCore::instance()->annotationDocument()->selectedItemWrapper();
    QFont font;
    if (tool->type() == AnnotationTool::SelectTool
        || (tool->type() == AnnotationTool::TextTool
            && wrapper->options().testFlag(AnnotationTool::TextOption))
    ) {
        font = wrapper->font();
    } else {
        font = tool->font();
    }
    QFontDialog *dialog = new QFontDialog(font);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    setWidgetTransientParent(dialog, this);

    if (flags().testFlag(Qt::WindowStaysOnTopHint)) {
        dialog->setWindowFlag(Qt::WindowStaysOnTopHint);
    }

    connect(dialog, &QFontDialog::fontSelected, this, [](const QFont &font) {
        QFont newFont = font;
        // Copied from stripRegularStyleName() in KFontChooserDialog.
        // For more details see:
        // https://bugreports.qt.io/browse/QTBUG-63792
        // https://bugs.kde.org/show_bug.cgi?id=378523
        if (newFont.weight() == QFont::Normal
            && (newFont.styleName() == "Regular"_L1
                || newFont.styleName() == "Normal"_L1
                || newFont.styleName() == "Book"_L1
                || newFont.styleName() == "Roman"_L1)) {
            newFont.setStyleName(QString());
        }
        auto tool = FotonCore::instance()->annotationDocument()->tool();
        auto wrapper = FotonCore::instance()->annotationDocument()->selectedItemWrapper();
        if (tool->type() == AnnotationTool::SelectTool) {
            wrapper->setFont(newFont);
            wrapper->commitChanges();
        } else if (tool->type() == AnnotationTool::TextTool
            && wrapper->options().testFlag(AnnotationTool::TextOption)
        ) {
            tool->setFont(newFont);
            wrapper->setFont(newFont);
            wrapper->commitChanges();
        } else {
            tool->setFont(newFont);
        }
    });

    // BUG https://bugs.kde.org/show_bug.cgi?id=478155:
    // Workaround modal font dialog being unusable.
    // This should probably be fixed in the plasma-integration.
    dialog->setModal(false);
    dialog->show();
}

void FotonWindow::showColorDialog(int option)
{
    QColorDialog *dialog = nullptr;
    auto tool = FotonCore::instance()->annotationDocument()->tool();
    auto wrapper = FotonCore::instance()->annotationDocument()->selectedItemWrapper();

    std::function<QColor()> toolGetter;
    std::function<QColor()> wrapperGetter;
    std::function<void(const QColor &)> toolSetter;
    std::function<void(const QColor &)> wrapperSetter;
    using namespace std::placeholders; // for std::placeholders::_1
    if (option == AnnotationTool::StrokeOption) {
        toolGetter = std::bind(&AnnotationTool::strokeColor, tool);
        wrapperGetter = std::bind(&SelectedItemWrapper::strokeColor, wrapper);
        toolSetter = std::bind(&AnnotationTool::setStrokeColor, tool, _1);
        wrapperSetter = std::bind(&SelectedItemWrapper::setStrokeColor, wrapper, _1);
    } else if (option == AnnotationTool::FillOption) {
        toolGetter = std::bind(&AnnotationTool::fillColor, tool);
        wrapperGetter = std::bind(&SelectedItemWrapper::fillColor, wrapper);
        toolSetter = std::bind(&AnnotationTool::setFillColor, tool, _1);
        wrapperSetter = std::bind(&SelectedItemWrapper::setFillColor, wrapper, _1);
    } else if (option == AnnotationTool::FontOption) {
        toolGetter = std::bind(&AnnotationTool::fontColor, tool);
        wrapperGetter = std::bind(&SelectedItemWrapper::fontColor, wrapper);
        toolSetter = std::bind(&AnnotationTool::setFontColor, tool, _1);
        wrapperSetter = std::bind(&SelectedItemWrapper::setFontColor, wrapper, _1);
    } else {
        qmlWarning(this) << "invalid option argument";
        return;
    }

    QColor color;
    if (tool->type() == AnnotationTool::SelectTool
        || (tool->type() == AnnotationTool::TextTool
            && wrapper->options().testFlag(AnnotationTool::TextOption))
    ) {
        color = wrapperGetter();
    } else {
        color = toolGetter();
    }

    dialog = new QColorDialog(color);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setOption(QColorDialog::ShowAlphaChannel);

    setWidgetTransientParent(dialog, this);

    if (flags().testFlag(Qt::WindowStaysOnTopHint)) {
        dialog->setWindowFlag(Qt::WindowStaysOnTopHint);
    }

    connect(dialog, &QColorDialog::colorSelected, this, [toolSetter, wrapperSetter](const QColor &color){
        auto tool = FotonCore::instance()->annotationDocument()->tool();
        auto wrapper = FotonCore::instance()->annotationDocument()->selectedItemWrapper();
        if (tool->type() == AnnotationTool::SelectTool) {
            wrapperSetter(color);
            wrapper->commitChanges();
        } else if (tool->type() == AnnotationTool::TextTool
            && wrapper->options().testFlag(AnnotationTool::TextOption)
        ) {
            toolSetter(color);
            wrapperSetter(color);
            wrapper->commitChanges();
        } else {
            toolSetter(color);
        }
    });

    dialog->open();
}

bool FotonWindow::event(QEvent *event)
{
    if (event->type() == QEvent::PlatformSurface
        && static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
        // Window animations would show up in screenshots taken right after hiding the window.
        WindowsIntegration::disableWindowTransitions(this);
    }
    return QQuickView::event(event);
}

void FotonWindow::mousePressEvent(QMouseEvent *event)
{
    m_pressedButtons = event->buttons();
    // QMenus need to be closed by hand when used from QML, see plasma-workspace/shellcorona.cpp
    if (auto popup = QApplication::activePopupWidget()) {
        popup->close();
        event->accept();
    } else {
        QQuickView::mousePressEvent(event);
    }
}

void FotonWindow::mouseReleaseEvent(QMouseEvent *event)
{
    m_pressedButtons = {};
    QQuickView::mouseReleaseEvent(event);
}

void FotonWindow::keyPressEvent(QKeyEvent *event)
{
    // Events need to be processed normally first for events to reach items
    QQuickView::keyPressEvent(event);
    if (event->isAccepted()) {
        return;
    }
    m_pressedKeys = event->key() | event->modifiers();
}

void FotonWindow::keyReleaseEvent(QKeyEvent *event)
{
    // Events need to be processed normally first for events to reach items
    QQuickView::keyReleaseEvent(event);
    if (event->isAccepted()) {
        return;
    }
    // Cancel defaults to Escape in QPlatformTheme.
    // Handling this here fixes https://bugs.kde.org/show_bug.cgi?id=428478
    if ((event->matches(QKeySequence::Quit)
        || event->matches(QKeySequence::Close)
        || event->matches(QKeySequence::Cancel))
        // We need to check if these were pressed previously or else pressing escape
        // in a dialog will quit Foton when you release the escape key.
        && m_pressedKeys == (event->key() | event->modifiers())
    ) {
        event->accept();
        if (confirmClose()) {
            FotonCore::instance()->cancelScreenshot();
        }
    } else if (event->matches(QKeySequence::Preferences)) {
        event->accept();
        showPreferencesDialog();
    } else if (event->matches(QKeySequence::New)) {
        event->accept();
        FotonCore::instance()->takeNewScreenshot();
    } else if (event->matches(QKeySequence::HelpContents)) {
        event->accept();
        HelpMenu::instance()->showAppHelp();
    }
    m_pressedKeys = {};
}

#include "moc_FotonWindow.cpp"
