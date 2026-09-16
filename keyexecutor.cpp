#include "keyexecutor.h"
#include "gamewindowmanager.h"
#include "runtimeconfig.h"

#include <QThread>
#include <QtMath>
#include <random>

namespace
{
int releaseMilliseconds(TimeConfigManager& times)
{
    const int configured = qRound(times.releaseIntervalSeconds() * 1000.0);
    thread_local std::mt19937 generator(static_cast<unsigned int>(
        GetTickCount() ^ GetCurrentThreadId() ^ GetCurrentProcessId()));
    std::uniform_int_distribution<int> jitter(-2, 2);
    const int randomized = configured + jitter(generator);
    return randomized < 1 ? 0 : randomized;
}

KeyResult sendInputKey(const KeyRequest& request, int holdMilliseconds, InputMode mode)
{
    KeyResult result;
    result.mode = mode;

    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(request.virtualKey);
    SetLastError(ERROR_SUCCESS);
    const bool downOk = SendInput(1, &input, sizeof(input)) == 1;
    const DWORD downError = downOk ? ERROR_SUCCESS : GetLastError();

    if (holdMilliseconds > 0)
        QThread::msleep(static_cast<unsigned long>(holdMilliseconds));

    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SetLastError(ERROR_SUCCESS);
    const bool upOk = SendInput(1, &input, sizeof(input)) == 1;
    const DWORD upError = upOk ? ERROR_SUCCESS : GetLastError();

    result.success = downOk && upOk;
    result.errorCode = downOk ? upError : downError;
    result.detail = QStringLiteral("hold=%1ms, down=%2, up=%3")
        .arg(holdMilliseconds).arg(downOk).arg(upOk);
    return result;
}
}

SharedMessageKeyExecutor::SharedMessageKeyExecutor(SharedInputWorker& worker,
    TimeConfigManager& timeConfig)
    : inputWorker(worker), times(timeConfig)
{
}

KeyResult SharedMessageKeyExecutor::execute(const KeyRequest& request)
{
    const int hold = releaseMilliseconds(times);
    const SharedKeyResult shared = inputWorker.executeKey(request.virtualKey, hold);
    KeyResult result;
    result.success = shared.success;
    result.mode = InputMode::SharedMessage;
    result.errorCode = shared.errorCode;
    result.detail = QStringLiteral("hold=%1ms, state=%2, down=%3, up=%4")
        .arg(hold).arg(shared.keyboardStateSet).arg(shared.downSent).arg(shared.upSent);
    return result;
}

AutoWindowKeyExecutor::AutoWindowKeyExecutor(GameWindowManager& windowManager,
    TimeConfigManager& timeConfig)
    : windows(windowManager), times(timeConfig)
{
}

KeyResult AutoWindowKeyExecutor::execute(const KeyRequest& request)
{
    if (!windows.hasFocus(request.window))
        windows.activate(request.window);
    return sendInputKey(request, releaseMilliseconds(times), InputMode::KeyboardAutoWindow);
}

ManualWindowKeyExecutor::ManualWindowKeyExecutor(TimeConfigManager& timeConfig)
    : times(timeConfig)
{
}

KeyResult ManualWindowKeyExecutor::execute(const KeyRequest& request)
{
    return sendInputKey(request, releaseMilliseconds(times), InputMode::KeyboardManualWindow);
}

KeyExecutionService::KeyExecutionService(InputModeManager& modeManager,
    GameWindowManager& windowManager, TimeConfigManager& timeConfig, QObject* parent)
    : QObject(parent), modes(modeManager), sharedWorker(),
      sharedExecutor(sharedWorker, timeConfig),
      autoExecutor(windowManager, timeConfig), manualExecutor(timeConfig)
{
    connect(&sharedWorker, &SharedInputWorker::debugMessage,
        this, &KeyExecutionService::debugMessage);
    connect(&modes, &InputModeManager::modeChanged,
        this, [this](InputMode) { synchronizeSharedInput(); }, Qt::DirectConnection);
}

KeyExecutionService::~KeyExecutionService()
{
    disconnect(&modes, nullptr, this, nullptr);
    std::lock_guard<std::mutex> locker(resourceMutex);
    sharedWorker.stopForWindow();
    attachedWindow = nullptr;
}

void KeyExecutionService::setTargetWindow(HWND window)
{
    std::lock_guard<std::mutex> locker(resourceMutex);
    targetWindow = window;
    synchronizeSharedInputLocked();
}

void KeyExecutionService::synchronizeSharedInput()
{
    std::lock_guard<std::mutex> locker(resourceMutex);
    synchronizeSharedInputLocked();
}

KeyResult KeyExecutionService::executeKey(const KeyRequest& request)
{
    // 模式资源切换与完整按键互斥，避免共享按键执行到一半被Detach。
    std::lock_guard<std::mutex> locker(resourceMutex);
    synchronizeSharedInputLocked();
    const InputMode mode = modes.currentMode();
    switch (mode)
    {
    case InputMode::SharedMessage:
        if (attachedWindow != request.window)
        {
            KeyResult result;
            result.mode = mode;
            result.errorCode = ERROR_INVALID_STATE;
            result.detail = QStringLiteral("共享输入尚未Attach到当前窗口");
            return result;
        }
        return sharedExecutor.execute(request);
    case InputMode::KeyboardAutoWindow:
        return autoExecutor.execute(request);
    case InputMode::KeyboardManualWindow:
        return manualExecutor.execute(request);
    }

    KeyResult result;
    result.mode = mode;
    result.errorCode = ERROR_INVALID_PARAMETER;
    result.detail = QStringLiteral("未注册的按键方式");
    return result;
}

void KeyExecutionService::synchronizeSharedInputLocked()
{
    const bool needsSharedInput = modes.currentMode() == InputMode::SharedMessage &&
        targetWindow && IsWindow(targetWindow) != FALSE;

    if (attachedWindow && (!needsSharedInput || attachedWindow != targetWindow))
    {
        sharedWorker.stopForWindow();
        attachedWindow = nullptr;
    }

    if (needsSharedInput && !attachedWindow)
    {
        if (sharedWorker.startForWindow(targetWindow))
            attachedWindow = targetWindow;
    }
}
