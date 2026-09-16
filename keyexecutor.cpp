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
}

KeyExecutionService::~KeyExecutionService()
{
    stopSession();
}

bool KeyExecutionService::startSession(HWND window)
{
    return sharedWorker.startForWindow(window);
}

void KeyExecutionService::stopSession()
{
    sharedWorker.stopForWindow();
}

KeyResult KeyExecutionService::executeKey(const KeyRequest& request)
{
    const InputMode mode = modes.currentMode();
    switch (mode)
    {
    case InputMode::SharedMessage:
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
