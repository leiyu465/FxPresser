#include "inputmodemanager.h"
#include "gamewindowmanager.h"

#include <chrono>

InputModeManager::InputModeManager(GameWindowManager& windowManager, QObject* parent)
    : QObject(parent), windows(windowManager), automatic(true),
      selectedManualMode(static_cast<int>(InputMode::SharedMessage)),
      effectiveMode(static_cast<int>(InputMode::SharedMessage)), stopRequested(false)
{
    qRegisterMetaType<InputMode>("InputMode");
    startMonitor();
}

InputModeManager::~InputModeManager()
{
    stopMonitor();
}

bool InputModeManager::isAutomatic() const
{
    return automatic.load();
}

InputMode InputModeManager::manualMode() const
{
    return static_cast<InputMode>(selectedManualMode.load());
}

InputMode InputModeManager::currentMode() const
{
    return static_cast<InputMode>(effectiveMode.load());
}

void InputModeManager::setAutomatic(bool enabled)
{
    if (automatic.exchange(enabled) == enabled)
        return;

    if (enabled)
    {
        updateDetectedMode();
        startMonitor();
    }
    else
    {
        stopMonitor();
        storeCurrentMode(manualMode());
    }
    emit automaticChanged(enabled);
}

void InputModeManager::setManualMode(InputMode mode)
{
    selectedManualMode.store(static_cast<int>(mode));
    if (!automatic.load())
        storeCurrentMode(mode);
}

QString InputModeManager::displayName(InputMode mode)
{
    switch (mode)
    {
    case InputMode::SharedMessage:
        return QStringLiteral("共享消息");
    case InputMode::KeyboardAutoWindow:
        return QStringLiteral("按键+自动窗口");
    case InputMode::KeyboardManualWindow:
        return QStringLiteral("按键+手动窗口");
    }
    return QStringLiteral("未知方式");
}

void InputModeManager::startMonitor()
{
    if (monitorThread.joinable())
        return;
    stopRequested.store(false);
    monitorThread = std::thread(&InputModeManager::monitorLoop, this);
}

void InputModeManager::stopMonitor()
{
    stopRequested.store(true);
    waitCondition.notify_all();
    if (monitorThread.joinable())
        monitorThread.join();
}

void InputModeManager::monitorLoop()
{
    while (!stopRequested.load())
    {
        updateDetectedMode();
        std::unique_lock<std::mutex> locker(waitMutex);
        waitCondition.wait_for(locker, std::chrono::milliseconds(100),
            [this]() { return stopRequested.load(); });
    }
}

void InputModeManager::updateDetectedMode()
{
    HWND window = windows.currentWindow();
    if (!window || IsWindow(window) == FALSE)
        return;
    storeCurrentMode(windows.hasFocus(window)
        ? InputMode::KeyboardManualWindow
        : InputMode::SharedMessage);
}

void InputModeManager::storeCurrentMode(InputMode mode)
{
    const int value = static_cast<int>(mode);
    if (effectiveMode.exchange(value) != value)
        emit modeChanged(mode);
}
