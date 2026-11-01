/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "GlobalShortcuts.h"

#include "ShortcutActions.h"
#include "foton_debug.h"

#include <QAction>
#include <QCoreApplication>
#include <QHash>
#include <QPointer>
#include <QTimer>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace Qt::StringLiterals;

class GlobalShortcutsPrivate
{
public:
    bool enabled = false;
    bool suspended = false;
    bool updateScheduled = false;
    QList<QKeySequence> failed;
    // hotkey id -> action
    QHash<int, QPointer<QAction>> hotkeys;

#ifdef Q_OS_WIN
    HWND window = nullptr;

    bool createWindow(GlobalShortcuts *q);
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
#endif
};

#ifdef Q_OS_WIN
static const wchar_t s_windowClassName[] = L"FotonGlobalShortcutsWindow";

LRESULT CALLBACK GlobalShortcutsPrivate::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_HOTKEY) {
        auto q = reinterpret_cast<GlobalShortcuts *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (q) {
            const QPointer<QAction> action = q->d->hotkeys.value(int(wParam));
            // Don't do anything complicated inside of the window procedure,
            // it can be called from nested native event loops.
            QMetaObject::invokeMethod(
                q,
                [q, action] {
                    if (action) {
                        Q_EMIT q->activated(action);
                    }
                },
                Qt::QueuedConnection);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool GlobalShortcutsPrivate::createWindow(GlobalShortcuts *q)
{
    if (window) {
        return true;
    }
    // A message-only window receives WM_HOTKEY even while a native modal loop is running,
    // unlike thread messages, which can get lost.
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = s_windowClassName;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        qCWarning(FOTON_LOG) << "Could not register the window class for global shortcuts:" << GetLastError();
        return false;
    }
    window = CreateWindowExW(0, s_windowClassName, L"Foton global shortcuts", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!window) {
        qCWarning(FOTON_LOG) << "Could not create the window for global shortcuts:" << GetLastError();
        return false;
    }
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(q));
    return true;
}

// Convert a Qt key combination to the modifiers and the virtual key code used by RegisterHotKey().
static bool toNativeHotkey(QKeyCombination combination, UINT *modifiers, UINT *virtualKey)
{
    const Qt::KeyboardModifiers qtModifiers = combination.keyboardModifiers();
    UINT mods = 0;
    if (qtModifiers & Qt::ShiftModifier) {
        mods |= MOD_SHIFT;
    }
    if (qtModifiers & Qt::ControlModifier) {
        mods |= MOD_CONTROL;
    }
    if (qtModifiers & Qt::AltModifier) {
        mods |= MOD_ALT;
    }
    if (qtModifiers & Qt::MetaModifier) {
        mods |= MOD_WIN;
    }
    const bool keypad = qtModifiers & Qt::KeypadModifier;

    const int key = combination.key();
    UINT vk = 0;
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        vk = 'A' + (key - Qt::Key_A);
    } else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        vk = keypad ? VK_NUMPAD0 + (key - Qt::Key_0) : '0' + (key - Qt::Key_0);
    } else if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        vk = VK_F1 + (key - Qt::Key_F1);
    } else {
        switch (key) {
        case Qt::Key_Print:
            vk = VK_SNAPSHOT;
            break;
        case Qt::Key_Pause:
            vk = VK_PAUSE;
            break;
        case Qt::Key_ScrollLock:
            vk = VK_SCROLL;
            break;
        case Qt::Key_Insert:
            vk = VK_INSERT;
            break;
        case Qt::Key_Delete:
            vk = VK_DELETE;
            break;
        case Qt::Key_Home:
            vk = VK_HOME;
            break;
        case Qt::Key_End:
            vk = VK_END;
            break;
        case Qt::Key_PageUp:
            vk = VK_PRIOR;
            break;
        case Qt::Key_PageDown:
            vk = VK_NEXT;
            break;
        case Qt::Key_Left:
            vk = VK_LEFT;
            break;
        case Qt::Key_Up:
            vk = VK_UP;
            break;
        case Qt::Key_Right:
            vk = VK_RIGHT;
            break;
        case Qt::Key_Down:
            vk = VK_DOWN;
            break;
        case Qt::Key_Space:
            vk = VK_SPACE;
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            vk = VK_RETURN;
            break;
        case Qt::Key_Escape:
            vk = VK_ESCAPE;
            break;
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            vk = VK_TAB;
            break;
        case Qt::Key_Backspace:
            vk = VK_BACK;
            break;
        case Qt::Key_VolumeDown:
            vk = VK_VOLUME_DOWN;
            break;
        case Qt::Key_VolumeUp:
            vk = VK_VOLUME_UP;
            break;
        case Qt::Key_VolumeMute:
            vk = VK_VOLUME_MUTE;
            break;
        case Qt::Key_MediaPlay:
        case Qt::Key_MediaTogglePlayPause:
            vk = VK_MEDIA_PLAY_PAUSE;
            break;
        case Qt::Key_MediaStop:
            vk = VK_MEDIA_STOP;
            break;
        case Qt::Key_MediaNext:
            vk = VK_MEDIA_NEXT_TRACK;
            break;
        case Qt::Key_MediaPrevious:
            vk = VK_MEDIA_PREV_TRACK;
            break;
        case Qt::Key_Asterisk:
            vk = keypad ? VK_MULTIPLY : 0;
            break;
        case Qt::Key_Plus:
            vk = keypad ? VK_ADD : 0;
            break;
        case Qt::Key_Minus:
            vk = keypad ? VK_SUBTRACT : 0;
            break;
        case Qt::Key_Slash:
            vk = keypad ? VK_DIVIDE : 0;
            break;
        case Qt::Key_Period:
            vk = keypad ? VK_DECIMAL : 0;
            break;
        default:
            break;
        }
        if (vk == 0 && key > 0 && key <= 0xffff) {
            // Other characters, using the current keyboard layout.
            const SHORT scan = VkKeyScanW(QChar(key).toLower().unicode());
            if (scan != -1) {
                vk = LOBYTE(scan);
            }
        }
    }
    if (vk == 0) {
        return false;
    }
    *modifiers = mods;
    *virtualKey = vk;
    return true;
}
#endif

GlobalShortcuts *GlobalShortcuts::instance()
{
    static QPointer<GlobalShortcuts> s_instance;
    if (!s_instance) {
        s_instance = new GlobalShortcuts;
        s_instance->setParent(QCoreApplication::instance());
    }
    return s_instance;
}

GlobalShortcuts::GlobalShortcuts()
    : QObject(nullptr)
    , d(std::make_unique<GlobalShortcutsPrivate>())
{
    const auto actions = ShortcutActions::self()->shortcutActions()->actions();
    for (auto action : actions) {
        connect(action, &QAction::changed, this, &GlobalShortcuts::scheduleUpdate);
    }
}

GlobalShortcuts::~GlobalShortcuts()
{
    unregisterAll();
#ifdef Q_OS_WIN
    if (d->window) {
        DestroyWindow(d->window);
        d->window = nullptr;
    }
#endif
}

bool GlobalShortcuts::isSupported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void GlobalShortcuts::setEnabled(bool enabled)
{
    if (d->enabled == enabled) {
        return;
    }
    d->enabled = enabled;
    updateRegistrations();
}

bool GlobalShortcuts::isEnabled() const
{
    return d->enabled;
}

void GlobalShortcuts::setSuspended(bool suspended)
{
    if (d->suspended == suspended) {
        return;
    }
    d->suspended = suspended;
    updateRegistrations();
}

bool GlobalShortcuts::isSuspended() const
{
    return d->suspended;
}

QList<QKeySequence> GlobalShortcuts::failedShortcuts() const
{
    return d->failed;
}

void GlobalShortcuts::checkShortcuts()
{
    if (d->enabled && !d->suspended) {
        registerAll(true);
    } else {
        // Register and immediately unregister to find out which shortcuts are available.
        registerAll(false);
    }
}

void GlobalShortcuts::scheduleUpdate()
{
    if (d->updateScheduled) {
        return;
    }
    d->updateScheduled = true;
    QTimer::singleShot(0, this, [this] {
        d->updateScheduled = false;
        updateRegistrations();
    });
}

void GlobalShortcuts::updateRegistrations()
{
    if (d->enabled && !d->suspended) {
        registerAll(true);
    } else {
        unregisterAll();
    }
}

void GlobalShortcuts::registerAll(bool keepRegistered)
{
    unregisterAll();
#ifdef Q_OS_WIN
    if (!d->createWindow(this)) {
        return;
    }
    QList<QKeySequence> failed;
    int nextId = 1;
    const auto actions = ShortcutActions::self()->shortcutActions()->actions();
    for (auto action : actions) {
        const auto shortcuts = action->shortcuts();
        for (const auto &shortcut : shortcuts) {
            if (shortcut.isEmpty()) {
                continue;
            }
            UINT modifiers = 0;
            UINT virtualKey = 0;
            // Hotkeys can't be sequences of multiple key combinations.
            if (shortcut.count() != 1 || !toNativeHotkey(shortcut[0], &modifiers, &virtualKey)) {
                qCWarning(FOTON_LOG) << "Unsupported global shortcut:" << shortcut;
                failed << shortcut;
                continue;
            }
            const int id = nextId++;
            if (RegisterHotKey(d->window, id, modifiers | MOD_NOREPEAT, virtualKey)) {
                if (keepRegistered) {
                    d->hotkeys.insert(id, action);
                } else {
                    UnregisterHotKey(d->window, id);
                }
            } else {
                qCWarning(FOTON_LOG) << "Could not register global shortcut" << shortcut << "for" << action->objectName() << "- error" << GetLastError();
                failed << shortcut;
            }
        }
    }
    setFailedShortcuts(failed);
#else
    Q_UNUSED(keepRegistered)
#endif
}

void GlobalShortcuts::unregisterAll()
{
#ifdef Q_OS_WIN
    if (d->window) {
        for (auto it = d->hotkeys.cbegin(); it != d->hotkeys.cend(); ++it) {
            UnregisterHotKey(d->window, it.key());
        }
    }
#endif
    d->hotkeys.clear();
}

void GlobalShortcuts::setFailedShortcuts(const QList<QKeySequence> &failed)
{
    if (d->failed == failed) {
        return;
    }
    d->failed = failed;
    Q_EMIT failedShortcutsChanged();
}

#include "moc_GlobalShortcuts.cpp"
