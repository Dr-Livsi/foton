/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "WindowsIntegration.h"

#include "foton_debug.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QWindow>

#include <algorithm>

#ifdef Q_OS_WIN
#include <qt_windows.h>

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

using namespace Qt::StringLiterals;

namespace WindowsIntegration
{

#ifdef Q_OS_WIN
static HWND windowHandle(QWindow *window)
{
    // Don't create a native window just for this.
    return window && window->handle() ? reinterpret_cast<HWND>(window->winId()) : nullptr;
}

static const auto s_runKey = u"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"_s;
static const auto s_runValueName = u"Foton"_s;

static QString autostartCommand()
{
    return u"\"%1\" --tray"_s.arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}
#endif

void showInFileManager(const QUrl &url)
{
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        return;
    }
#ifdef Q_OS_WIN
    const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(nativePath.c_str())) {
        const HRESULT result = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
        ILFree(item);
        if (SUCCEEDED(result)) {
            return;
        }
    }
    QProcess::startDetached(u"explorer.exe"_s, {u"/select,"_s + QDir::toNativeSeparators(path)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

void addToRecentDocuments(const QUrl &url)
{
#ifdef Q_OS_WIN
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        return;
    }
    const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    SHAddToRecentDocs(SHARD_PATHW, nativePath.c_str());
#else
    Q_UNUSED(url)
#endif
}

void setTaskbarProgress(QWindow *window, bool visible, qreal progress)
{
#ifdef Q_OS_WIN
    HWND hwnd = windowHandle(window);
    if (!hwnd) {
        return;
    }
    static ITaskbarList3 *taskbar = [] {
        ITaskbarList3 *list = nullptr;
        if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&list)))) {
            return static_cast<ITaskbarList3 *>(nullptr);
        }
        if (FAILED(list->HrInit())) {
            list->Release();
            return static_cast<ITaskbarList3 *>(nullptr);
        }
        return list;
    }();
    if (!taskbar) {
        return;
    }
    if (visible) {
        constexpr ULONGLONG total = 1000;
        taskbar->SetProgressState(hwnd, TBPF_NORMAL);
        taskbar->SetProgressValue(hwnd, ULONGLONG(std::clamp(progress, 0.0, 1.0) * total), total);
    } else {
        taskbar->SetProgressState(hwnd, TBPF_NOPROGRESS);
    }
#else
    Q_UNUSED(window)
    Q_UNUSED(visible)
    Q_UNUSED(progress)
#endif
}

void disableWindowTransitions(QWindow *window)
{
#ifdef Q_OS_WIN
    HWND hwnd = windowHandle(window);
    if (!hwnd) {
        return;
    }
    const BOOL disable = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disable, sizeof(disable));
#else
    Q_UNUSED(window)
#endif
}

void setStartWithSystem(bool enabled)
{
#ifdef Q_OS_WIN
    QSettings runSettings(s_runKey, QSettings::NativeFormat);
    if (enabled) {
        if (runSettings.value(s_runValueName).toString() != autostartCommand()) {
            runSettings.setValue(s_runValueName, autostartCommand());
        }
    } else if (runSettings.contains(s_runValueName)) {
        runSettings.remove(s_runValueName);
    }
#else
    Q_UNUSED(enabled)
#endif
}

bool startsWithSystem()
{
#ifdef Q_OS_WIN
    QSettings runSettings(s_runKey, QSettings::NativeFormat);
    return runSettings.contains(s_runValueName);
#else
    return false;
#endif
}

void allowOtherProcessToActivateWindows()
{
#ifdef Q_OS_WIN
    AllowSetForegroundWindow(ASFW_ANY);
#endif
}

void waitForProcessToQuit(qint64 processId, int timeoutMs)
{
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(processId));
    if (process) {
        WaitForSingleObject(process, static_cast<DWORD>(timeoutMs));
        CloseHandle(process);
    }
#else
    Q_UNUSED(processId)
    Q_UNUSED(timeoutMs)
#endif
}
}
