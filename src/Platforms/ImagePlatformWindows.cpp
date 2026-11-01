/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ImagePlatformWindows.h"

#include "Geometry.h"
#include "ImageMetaData.h"
#include "foton_debug.h"

#include <KLocalizedString>

#include <QGraphicsDropShadowEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRegion>
#include <QScreen>
#include <QTimer>

#include <qt_windows.h>

#include <dwmapi.h>

#include <cwchar>
#include <iterator>
#include <string>

using namespace Qt::StringLiterals;

namespace
{
/* -- Desktop capture -------------------------------------------------------------------------- */

void drawCursor(HDC dc, const QPoint &origin)
{
    CURSORINFO cursorInfo{};
    cursorInfo.cbSize = sizeof(CURSORINFO);
    if (!GetCursorInfo(&cursorInfo) || !(cursorInfo.flags & CURSOR_SHOWING) || !cursorInfo.hCursor) {
        return;
    }
    ICONINFO iconInfo{};
    if (!GetIconInfo(cursorInfo.hCursor, &iconInfo)) {
        return;
    }
    const int x = cursorInfo.ptScreenPos.x - int(iconInfo.xHotspot) - origin.x();
    const int y = cursorInfo.ptScreenPos.y - int(iconInfo.yHotspot) - origin.y();
    // Drawing directly onto the copy of the screen also handles cursors that invert the colors behind them.
    DrawIconEx(dc, x, y, cursorInfo.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
    if (iconInfo.hbmMask) {
        DeleteObject(iconInfo.hbmMask);
    }
    if (iconInfo.hbmColor) {
        DeleteObject(iconInfo.hbmColor);
    }
}

// Copy a rectangle of the virtual desktop. The rectangle is in native pixels.
QImage captureDesktopRect(const QRect &rect, bool includePointer)
{
    if (rect.isEmpty()) {
        return {};
    }
    const int width = rect.width();
    const int height = rect.height();

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        return {};
    }
    HDC memoryDc = CreateCompatibleDC(screenDc);

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height; // top-down
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    QImage image;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap && bits) {
        HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
        // No CAPTUREBLT: it makes the screen and the mouse cursor flicker. Since Windows 8 the screen is always
        // composed by DWM, so layered windows like menus and tooltips are part of the copy anyway.
        if (BitBlt(memoryDc, 0, 0, width, height, screenDc, rect.x(), rect.y(), SRCCOPY)) {
            if (includePointer) {
                drawCursor(memoryDc, rect.topLeft());
            }
            GdiFlush();
            image = QImage(width, height, QImage::Format_RGB32);
            if (!image.isNull()) {
                // The pixel layout of a 32 bit DIB is the same as QImage::Format_RGB32,
                // except that GDI doesn't set the unused alpha byte.
                const auto source = static_cast<const uchar *>(bits);
                const qsizetype sourceStride = qsizetype(width) * 4;
                for (int y = 0; y < height; ++y) {
                    auto sourceLine = reinterpret_cast<const quint32 *>(source + y * sourceStride);
                    auto targetLine = reinterpret_cast<quint32 *>(image.scanLine(y));
                    for (int x = 0; x < width; ++x) {
                        targetLine[x] = sourceLine[x] | 0xff000000;
                    }
                }
            }
        } else {
            qCWarning(FOTON_LOG) << "BitBlt failed:" << GetLastError();
        }
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(bitmap);
    } else {
        qCWarning(FOTON_LOG) << "CreateDIBSection failed:" << GetLastError();
    }
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);
    return image;
}

QRegion desktopRegion()
{
    QRegion region;
    const auto screens = qGuiApp->screens();
    for (auto screen : screens) {
        region += Geometry::nativeScreenRect(screen);
    }
    return region;
}

QPoint nativeCursorPosition()
{
    POINT point{};
    GetCursorPos(&point);
    return {point.x, point.y};
}

/* -- Windows ---------------------------------------------------------------------------------- */

QString windowClassName(HWND window)
{
    wchar_t name[256];
    const int length = GetClassNameW(window, name, std::size(name));
    return length > 0 ? QString::fromWCharArray(name, length) : QString{};
}

QString windowTitle(HWND window)
{
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }
    std::wstring title(size_t(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, title.data(), length + 1);
    return QString::fromWCharArray(title.c_str(), copied);
}

bool isOwnWindow(HWND window)
{
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    return processId == GetCurrentProcessId();
}

bool isCloaked(HWND window)
{
    // Windows on other virtual desktops and suspended UWP apps are "cloaked":
    // they count as visible, but they are not shown.
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

bool isDesktopWindow(HWND window)
{
    if (window == GetDesktopWindow() || window == GetShellWindow()) {
        return true;
    }
    const auto className = windowClassName(window);
    return className == u"Progman" || className == u"WorkerW";
}

bool isMenuWindow(HWND window)
{
    return windowClassName(window) == u"#32768";
}

bool isPopupWindow(HWND window)
{
    const auto style = GetWindowLongPtrW(window, GWL_STYLE);
    return isMenuWindow(window) || ((style & WS_POPUP) && !(style & WS_CAPTION));
}

// Whether the window can be captured as a window.
bool isCapturableWindow(HWND window)
{
    if (!window || !IsWindowVisible(window) || IsIconic(window) || isCloaked(window) || isOwnWindow(window)) {
        return false;
    }
    // Click-through overlays, e.g. from graphics drivers, cover the whole screen and are invisible.
    const auto exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((exStyle & WS_EX_LAYERED) && (exStyle & WS_EX_TRANSPARENT)) {
        return false;
    }
    return true;
}

QRect windowRect(HWND window, bool includeDecorations)
{
    RECT rect{};
    if (includeDecorations) {
        // The extended frame bounds don't include the invisible borders used for resizing.
        if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
            GetWindowRect(window, &rect);
        }
        return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    }
    GetClientRect(window, &rect);
    POINT topLeft{rect.left, rect.top};
    ClientToScreen(window, &topLeft);
    return QRect(topLeft.x, topLeft.y, rect.right - rect.left, rect.bottom - rect.top);
}

// The topmost window at the native position that can be captured.
HWND windowAt(const QPoint &pos)
{
    const POINT point{pos.x(), pos.y()};
    HWND window = WindowFromPoint(point);
    window = window ? GetAncestor(window, GA_ROOT) : nullptr;
    if (isCapturableWindow(window)) {
        return window;
    }
    // Fall back to looking at the top level windows from top to bottom.
    for (HWND w = GetTopWindow(nullptr); w; w = GetWindow(w, GW_HWNDNEXT)) {
        if (isCapturableWindow(w) && windowRect(w, true).contains(pos)) {
            return w;
        }
    }
    return nullptr;
}

HWND ownerWindow(HWND window)
{
    HWND owner = GetWindow(window, GW_OWNER);
    if (!owner && isMenuWindow(window)) {
        // Native menus don't have an owner window, but their thread knows which window opened the menu.
        GUITHREADINFO info{};
        info.cbSize = sizeof(GUITHREADINFO);
        const DWORD threadId = GetWindowThreadProcessId(window, nullptr);
        if (GetGUIThreadInfo(threadId, &info)) {
            owner = info.hwndMenuOwner ? info.hwndMenuOwner : info.hwndActive;
        }
    }
    return owner ? GetAncestor(owner, GA_ROOT) : nullptr;
}

// The window and the windows that belong to it, like menus, tooltips and the parents of menus.
QList<HWND> windowWithTransients(HWND window)
{
    QList<HWND> windows{window};
    // If the window is a popup, also capture the windows it belongs to.
    for (HWND current = window; isPopupWindow(current);) {
        HWND owner = ownerWindow(current);
        if (!owner || owner == current || windows.contains(owner) || !isCapturableWindow(owner)) {
            break;
        }
        windows << owner;
        current = owner;
    }

    QList<DWORD> threads;
    for (auto w : std::as_const(windows)) {
        threads << GetWindowThreadProcessId(w, nullptr);
    }
    // Find the visible popups owned by the windows, e.g. open menus.
    auto belongsToWindows = [&windows, &threads](HWND w) {
        if (isMenuWindow(w)) {
            return threads.contains(GetWindowThreadProcessId(w, nullptr));
        }
        int depth = 0;
        for (HWND owner = GetWindow(w, GW_OWNER); owner && depth < 32; owner = GetWindow(owner, GW_OWNER), ++depth) {
            if (windows.contains(owner)) {
                return true;
            }
        }
        return false;
    };
    for (HWND w = GetTopWindow(nullptr); w; w = GetWindow(w, GW_HWNDNEXT)) {
        if (!windows.contains(w) && isCapturableWindow(w) && isPopupWindow(w) && belongsToWindows(w)) {
            windows << w;
        }
    }
    return windows;
}

/* -- Image processing ------------------------------------------------------------------------- */

QImage addDropShadow(const QImage &image, qreal dpr)
{
    const int margin = qRound(20 * dpr);
    QImage shadowImage(image.size() + QSize(margin * 2, margin * 2), QImage::Format_ARGB32_Premultiplied);
    shadowImage.fill(Qt::transparent);

    QPainter shadowPainter(&shadowImage);

    auto pixmapItem = new QGraphicsPixmapItem;
    pixmapItem->setPixmap(QPixmap::fromImage(image));

    auto shadowEffect = new QGraphicsDropShadowEffect;
    shadowEffect->setOffset(0, qRound(2 * dpr));
    shadowEffect->setBlurRadius(margin);
    shadowEffect->setColor(QColor(0, 0, 0, 160));
    pixmapItem->setGraphicsEffect(shadowEffect);

    // The scene takes ownership of the item.
    QGraphicsScene graphicsScene;
    graphicsScene.addItem(pixmapItem);
    graphicsScene.render(&shadowPainter, QRectF(), QRectF(-margin, -margin, image.width() + margin * 2, image.height() + margin * 2));
    shadowPainter.end();
    return shadowImage;
}
}

/* -- General Plumbing ------------------------------------------------------------------------- */

ImagePlatformWindows::ImagePlatformWindows(QObject *parent)
    : ImagePlatform(parent)
{
    updateSupportedGrabModes();
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &ImagePlatformWindows::updateSupportedGrabModes);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &ImagePlatformWindows::updateSupportedGrabModes);
}

ImagePlatformWindows::~ImagePlatformWindows() = default;

ImagePlatform::GrabModes ImagePlatformWindows::supportedGrabModes() const
{
    return m_grabModes;
}

void ImagePlatformWindows::updateSupportedGrabModes()
{
    GrabModes grabModes = GrabMode::AllScreens | GrabMode::ActiveWindow | GrabMode::WindowUnderCursor //
        | GrabMode::TransientWithParent | GrabMode::PerScreenImageNative;

    if (QGuiApplication::screens().count() > 1) {
        grabModes |= GrabMode::CurrentScreen | GrabMode::AllScreensScaled;
    }

    if (m_grabModes != grabModes) {
        m_grabModes = grabModes;
        Q_EMIT supportedGrabModesChanged();
    }
}

ImagePlatform::ShutterModes ImagePlatformWindows::supportedShutterModes() const
{
    return {ShutterMode::Immediate};
}

void ImagePlatformWindows::doGrab(ShutterMode shutterMode, GrabMode grabMode, bool includePointer, bool includeDecorations, bool includeShadow)
{
    const GrabOptions options{grabMode, includePointer, includeDecorations, includeShadow};
    switch (shutterMode) {
    case ShutterMode::Immediate:
        grabNow(options);
        return;
    case ShutterMode::OnClick:
        // Not supported, see supportedShutterModes().
        Q_EMIT newScreenshotCanceled();
        return;
    }
}

void ImagePlatformWindows::grabNow(const GrabOptions &options)
{
    switch (options.grabMode) {
    case GrabMode::AllScreens:
        grabScreens(options.includePointer, false, false);
        break;
    case GrabMode::AllScreensScaled:
        grabScreens(options.includePointer, false, true);
        break;
    case GrabMode::PerScreenImageNative:
        grabScreens(options.includePointer, true, false);
        break;
    case GrabMode::CurrentScreen:
        grabCurrentScreen(options.includePointer);
        break;
    case GrabMode::ActiveWindow:
        grabActiveWindow(options);
        break;
    case GrabMode::WindowUnderCursor:
        grabWindowUnderCursor(options, false);
        break;
    case GrabMode::TransientWithParent:
        grabWindowUnderCursor(options, true);
        break;
    case GrabMode::NoGrabModes:
        Q_EMIT newScreenshotFailed();
        break;
    }
}

/* -- Grabber Methods -------------------------------------------------------------------------- */

void ImagePlatformWindows::grabScreens(bool includePointer, bool croppable, bool scaled)
{
    QList<QImage> images;
    const auto screens = qGuiApp->screens();
    images.reserve(screens.size());
    for (auto screen : screens) {
        const QRect nativeRect = Geometry::nativeScreenRect(screen);
        const QRectF logicalRect = Geometry::logicalScreenRect(screen);
        QImage image = captureDesktopRect(nativeRect, includePointer);
        if (image.isNull()) {
            Q_EMIT newScreenshotFailed(i18nc("@info, %1 is the name of a screen", "Could not capture the screen %1.", screen->name()));
            return;
        }
        if (scaled) {
            image = image.scaled(logicalRect.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            image.setDevicePixelRatio(1);
        } else {
            image.setDevicePixelRatio(screen->devicePixelRatio());
        }
        ImageMetaData::setLogicalXY(image, logicalRect.x(), logicalRect.y());
        ImageMetaData::setScreen(image, screen->name());
        images << image;
    }

    auto image = combinedImage(images);
    if (croppable) {
        Q_EMIT newCroppableScreenshotTaken(image);
    } else {
        Q_EMIT newScreenshotTaken(image);
    }
}

void ImagePlatformWindows::grabCurrentScreen(bool includePointer)
{
    auto screen = Geometry::screenAtNativePoint(nativeCursorPosition());
    if (!screen) {
        grabScreens(includePointer, false, false);
        return;
    }
    QImage image = captureDesktopRect(Geometry::nativeScreenRect(screen), includePointer);
    if (image.isNull()) {
        Q_EMIT newScreenshotFailed(i18nc("@info, %1 is the name of a screen", "Could not capture the screen %1.", screen->name()));
        return;
    }
    image.setDevicePixelRatio(screen->devicePixelRatio());
    const auto logicalRect = Geometry::logicalScreenRect(screen);
    ImageMetaData::setLogicalXY(image, logicalRect.x(), logicalRect.y());
    ImageMetaData::setScreen(image, screen->name());
    Q_EMIT newScreenshotTaken(image);
}

void ImagePlatformWindows::grabActiveWindow(const GrabOptions &options)
{
    HWND window = GetForegroundWindow();
    if (window) {
        window = GetAncestor(window, GA_ROOT);
    }
    if (window && isOwnWindow(window)) {
        // Use the window that was active before Foton.
        window = nullptr;
        for (HWND w = GetTopWindow(nullptr); w; w = GetWindow(w, GW_HWNDNEXT)) {
            if (isCapturableWindow(w) && !isPopupWindow(w) && !isDesktopWindow(w)) {
                window = w;
                break;
            }
        }
    }
    if (!window || isDesktopWindow(window)) {
        grabCurrentScreen(options.includePointer);
        return;
    }
    grabWindow(reinterpret_cast<quintptr>(window), options, false);
}

void ImagePlatformWindows::grabWindowUnderCursor(const GrabOptions &options, bool withTransients)
{
    HWND window = windowAt(nativeCursorPosition());
    if (!window || isDesktopWindow(window)) {
        grabCurrentScreen(options.includePointer);
        return;
    }
    grabWindow(reinterpret_cast<quintptr>(window), options, withTransients);
}

void ImagePlatformWindows::grabWindow(quintptr windowId, const GrabOptions &options, bool withTransients)
{
    HWND window = reinterpret_cast<HWND>(windowId);
    const auto windows = withTransients ? windowWithTransients(window) : QList<HWND>{window};

    QRegion region;
    for (auto w : windows) {
        // Popups like menus don't have decorations, so always use their visible bounds.
        region += windowRect(w, options.includeDecorations || isPopupWindow(w));
    }
    // Parts of the window can be outside of the screens.
    region &= desktopRegion();
    const QRect bounds = region.boundingRect();
    if (bounds.isEmpty()) {
        Q_EMIT newScreenshotFailed(i18nc("@info", "The window is not visible on any screen."));
        return;
    }

    QImage image = captureDesktopRect(bounds, options.includePointer);
    if (image.isNull()) {
        Q_EMIT newScreenshotFailed(i18nc("@info", "Could not capture the window."));
        return;
    }

    // Make everything that isn't part of the windows transparent.
    if (region != QRegion(bounds)) {
        QImage clippedImage(image.size(), QImage::Format_ARGB32_Premultiplied);
        clippedImage.fill(Qt::transparent);
        QPainter painter(&clippedImage);
        painter.setClipRegion(region.translated(-bounds.topLeft()));
        painter.drawImage(0, 0, image);
        painter.end();
        image = clippedImage;
    }

    auto screen = Geometry::screenAtNativePoint(windowRect(window, true).center());
    const qreal dpr = screen ? screen->devicePixelRatio() : 1;
    QPointF logicalPos = Geometry::mapNativeToLogicalPoint(bounds.topLeft());

    if (options.includeShadow && options.includeDecorations) {
        image = addDropShadow(image, dpr);
        logicalPos -= QPointF(20, 20);
    }

    image.setDevicePixelRatio(dpr);
    ImageMetaData::setLogicalXY(image, logicalPos.x(), logicalPos.y());
    ImageMetaData::setWindowTitle(image, windowTitle(window));
    if (screen) {
        ImageMetaData::setScreen(image, screen->name());
    }
    Q_EMIT newScreenshotTaken(image);
}

#include "moc_ImagePlatformWindows.cpp"
