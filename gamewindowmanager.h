#ifndef GAMEWINDOWMANAGER_H
#define GAMEWINDOWMANAGER_H

#include <atomic>
#include <cstdint>
#include <windows.h>

class GameWindowManager
{
public:
    GameWindowManager();

    HWND currentWindow() const;
    void setCurrentWindow(HWND window);

    bool isCurrentWindowValid() const;
    bool hasFocus(HWND window) const;
    bool activate(HWND window) const;
    bool setTopmost(HWND window, bool topmost) const;

private:
    std::atomic<std::uintptr_t> currentHandle;
};

#endif // GAMEWINDOWMANAGER_H
