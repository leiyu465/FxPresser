#include "sharedinputworker.h"

#include <QMutexLocker>

namespace
{
constexpr UINT SharedInputSendKeyMessage = WM_APP + 0x141;
constexpr UINT SharedInputStopMessage = WM_APP + 0x142;
}

SharedInputWorker::SharedInputWorker(QObject* parent)
    : QThread(parent)
{
}

SharedInputWorker::~SharedInputWorker()
{
    stopForWindow();
}

bool SharedInputWorker::startForWindow(HWND window, int startupTimeoutMilliseconds)
{
    if (isRunning())
        stopForWindow();

    QMutexLocker locker(&mutex);
    targetWindow = window;
    workerThreadId = 0;
    stopRequested = false;
    attached = false;
    startupComplete = false;
    commandPending = false;
    commandComplete = false;
    start();

    if (!startupComplete)
        startupFinished.wait(&mutex, startupTimeoutMilliseconds);
    return startupComplete && attached;
}

void SharedInputWorker::stopForWindow()
{
    DWORD threadId = 0;
    {
        QMutexLocker locker(&mutex);
        if (!isRunning())
            return;
        stopRequested = true;
        threadId = workerThreadId;
    }

    if (threadId != 0)
        PostThreadMessageW(threadId, SharedInputStopMessage, 0, 0);
    wait();
}

SharedKeyResult SharedInputWorker::executeKey(UINT virtualKey, int holdMilliseconds)
{
    QMutexLocker locker(&mutex);
    SharedKeyResult failure;
    failure.errorCode = ERROR_INVALID_STATE;

    if (!isRunning() || !attached || stopRequested || commandPending || workerThreadId == 0)
        return failure;

    commandPending = true;
    commandComplete = false;
    if (!PostThreadMessageW(workerThreadId, SharedInputSendKeyMessage,
        static_cast<WPARAM>(virtualKey), static_cast<LPARAM>(holdMilliseconds)))
    {
        commandPending = false;
        failure.errorCode = GetLastError();
        return failure;
    }

    while (!commandComplete && isRunning())
        commandFinished.wait(&mutex);

    return commandComplete ? commandResult : failure;
}

void SharedInputWorker::run()
{
    MSG message = {};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    HWND window;
    const DWORD currentWorkerThreadId = GetCurrentThreadId();
    {
        QMutexLocker locker(&mutex);
        window = targetWindow;
        workerThreadId = currentWorkerThreadId;
    }

    DWORD processId = 0;
    const DWORD gameThreadId = GetWindowThreadProcessId(window, &processId);
    const bool attachResult = gameThreadId != 0 && gameThreadId != currentWorkerThreadId &&
        AttachThreadInput(currentWorkerThreadId, gameThreadId, TRUE) != FALSE;
    const DWORD attachError = attachResult ? ERROR_SUCCESS : GetLastError();

    {
        QMutexLocker locker(&mutex);
        attached = attachResult;
        startupComplete = true;
        startupFinished.wakeAll();
    }
    emit debugMessage(QStringLiteral("共享输入Attach：pid=%1, gameThread=%2, workerThread=%3, ok=%4, error=%5")
        .arg(processId).arg(gameThreadId).arg(currentWorkerThreadId)
        .arg(attachResult).arg(attachError), !attachResult);

    if (!attachResult)
        return;

    bool running = true;
    while (running && GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        switch (message.message)
        {
        case SharedInputSendKeyMessage:
        {
            KeyCommand command;
            command.virtualKey = static_cast<UINT>(message.wParam);
            command.holdMilliseconds = static_cast<int>(message.lParam);
            SharedKeyResult result = performKey(command);
            {
                QMutexLocker locker(&mutex);
                commandResult = result;
                commandPending = false;
                commandComplete = true;
                commandFinished.wakeAll();
            }
            emit debugMessage(QStringLiteral("共享消息：vk=0x%1, hold=%2ms, state=%3, down=%4, up=%5, error=%6")
                .arg(command.virtualKey, 0, 16).arg(command.holdMilliseconds)
                .arg(result.keyboardStateSet).arg(result.downSent).arg(result.upSent)
                .arg(result.errorCode), !result.success);
            break;
        }
        case SharedInputStopMessage:
            running = false;
            break;
        default:
            TranslateMessage(&message);
            DispatchMessageW(&message);
            break;
        }
    }

    const bool detachResult = AttachThreadInput(currentWorkerThreadId, gameThreadId, FALSE) != FALSE;
    {
        QMutexLocker locker(&mutex);
        attached = false;
        workerThreadId = 0;
        commandPending = false;
        commandFinished.wakeAll();
    }
    emit debugMessage(QStringLiteral("共享输入Detach：ok=%1").arg(detachResult), !detachResult);
}

SharedKeyResult SharedInputWorker::performKey(const KeyCommand& command)
{
    SharedKeyResult result;
    DWORD stateError = ERROR_SUCCESS;
    result.keyboardStateSet = setKeyDownState(command.virtualKey, &stateError);

    DWORD downError = ERROR_SUCCESS;
    result.downSent = sendKeyMessage(command, false, &downError);

    if (command.holdMilliseconds > 0)
        QThread::msleep(static_cast<unsigned long>(command.holdMilliseconds));

    DWORD upError = ERROR_SUCCESS;
    // 已确认的流程：UP前不再次调用GetKeyboardState/SetKeyboardState，也不恢复目标键。
    result.upSent = sendKeyMessage(command, true, &upError);
    result.success = result.keyboardStateSet && result.downSent && result.upSent;
    result.errorCode = !result.keyboardStateSet ? stateError
        : (!result.downSent ? downError : (!result.upSent ? upError : ERROR_SUCCESS));
    return result;
}

bool SharedInputWorker::setKeyDownState(UINT virtualKey, DWORD* errorCode)
{
    BYTE states[256] = {};
    SetLastError(ERROR_SUCCESS);
    if (!GetKeyboardState(states))
    {
        *errorCode = GetLastError();
        return false;
    }

    states[virtualKey & 0xff] |= 0x80;
    const bool result = SetKeyboardState(states) != FALSE;
    *errorCode = result ? ERROR_SUCCESS : GetLastError();
    return result;
}

bool SharedInputWorker::sendKeyMessage(const KeyCommand& command, bool keyUp, DWORD* errorCode)
{
    const UINT scanCode = MapVirtualKeyW(command.virtualKey, MAPVK_VK_TO_VSC);
    const LPARAM downParam = static_cast<LPARAM>(1ULL | (static_cast<ULONGLONG>(scanCode) << 16));
    const LPARAM parameter = keyUp
        ? static_cast<LPARAM>(static_cast<ULONGLONG>(downParam) | (1ULL << 30) | (1ULL << 31))
        : downParam;
    const bool systemKey = command.virtualKey == VK_F10;
    const UINT message = systemKey
        ? (keyUp ? WM_SYSKEYUP : WM_SYSKEYDOWN)
        : (keyUp ? WM_KEYUP : WM_KEYDOWN);

    DWORD_PTR messageResult = 0;
    SetLastError(ERROR_SUCCESS);
    const bool sent = SendMessageTimeoutA(targetWindow, message, command.virtualKey, parameter,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &messageResult) != 0;
    *errorCode = sent ? ERROR_SUCCESS : GetLastError();
    return sent;
}
