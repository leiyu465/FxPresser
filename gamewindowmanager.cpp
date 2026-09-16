#include "gamewindowmanager.h"

GameWindowManager::GameWindowManager()
    : currentHandle(0)
{
}

HWND GameWindowManager::currentWindow() const
{
    return reinterpret_cast<HWND>(currentHandle.load());
}

void GameWindowManager::setCurrentWindow(HWND window)
{
    currentHandle.store(reinterpret_cast<std::uintptr_t>(window));
}

bool GameWindowManager::isCurrentWindowValid() const
{
    HWND window = currentWindow();
    return window && IsWindow(window) != FALSE;
}

bool GameWindowManager::hasFocus(HWND window) const
{
    if (!window || IsWindow(window) == FALSE)
        return false;

    const HWND foreground = GetForegroundWindow();
    if (!foreground || GetAncestor(foreground, GA_ROOT) != window)
        return false;

    const DWORD gameThreadId = GetWindowThreadProcessId(window, nullptr);
    GUITHREADINFO info = {};
    info.cbSize = sizeof(info);
    return GetGUIThreadInfo(gameThreadId, &info) != FALSE && info.hwndFocus &&
        GetAncestor(info.hwndFocus, GA_ROOT) == window;
}

bool GameWindowManager::activate(HWND window) const
{
    if (!window || IsWindow(window) == FALSE)
        return false;
    if (IsIconic(window))
        ShowWindow(window, SW_RESTORE);
    return SetForegroundWindow(window) != FALSE;
}

bool GameWindowManager::setTopmost(HWND window, bool topmost) const
{
    if (!window || IsWindow(window) == FALSE)
        return false;
    return SetWindowPos(window, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
}
