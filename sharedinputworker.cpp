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
    busy = false;
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

bool SharedInputWorker::enqueueKey(UINT virtualKey, int holdMilliseconds)
{
    QMutexLocker locker(&mutex);
    if (!isRunning() || !attached || stopRequested || busy || workerThreadId == 0)
        return false;

    busy = true;
    if (!PostThreadMessageW(workerThreadId, SharedInputSendKeyMessage,
        static_cast<WPARAM>(virtualKey), static_cast<LPARAM>(holdMilliseconds)))
    {
        busy = false;
        return false;
    }
    return true;
}

void SharedInputWorker::run()
{
    MSG message = {};
    // 强制创建线程消息队列，之后所有命令都由GetMessage循环处理。
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
    emit debugMessage(QStringLiteral("共享输入线程：pid=%1, gameThread=%2, workerThread=%3, attached=%4, error=%5")
        .arg(processId).arg(gameThreadId).arg(currentWorkerThreadId)
        .arg(attachResult).arg(attachError));

    if (!attachResult)
        return;

    KeyCommand pendingCommand = {};
    UINT_PTR releaseTimer = 0;
    bool keyIsDown = false;
    bool running = true;

    while (running && GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        switch (message.message)
        {
        case SharedInputSendKeyMessage:
        {
            pendingCommand.virtualKey = static_cast<UINT>(message.wParam);
            pendingCommand.holdMilliseconds = static_cast<int>(message.lParam);
            const bool stateOk = setOneKeyState(pendingCommand.virtualKey, true);
            DWORD downError = ERROR_SUCCESS;
            const bool downOk = sendKeyMessage(pendingCommand, false, &downError);
            keyIsDown = true;

            if (pendingCommand.holdMilliseconds < 1)
                PostThreadMessageW(currentWorkerThreadId, WM_TIMER, 0, 0);
            else
            {
                releaseTimer = SetTimer(nullptr, 0,
                    static_cast<UINT>(pendingCommand.holdMilliseconds), nullptr);
                if (releaseTimer == 0)
                    PostThreadMessageW(currentWorkerThreadId, WM_TIMER, 0, 0);
            }

            emit debugMessage(QStringLiteral("共享状态按下：vk=0x%1, hold=%2ms, state=%3, message=%4, error=%5")
                .arg(pendingCommand.virtualKey, 0, 16).arg(pendingCommand.holdMilliseconds)
                .arg(stateOk).arg(downOk).arg(downError));
            break;
        }
        case WM_TIMER:
            if (keyIsDown && (releaseTimer == 0 || message.wParam == releaseTimer))
            {
                if (releaseTimer != 0)
                    KillTimer(nullptr, releaseTimer);
                releaseTimer = 0;
                const bool stateOk = setOneKeyState(pendingCommand.virtualKey, false);
                DWORD upError = ERROR_SUCCESS;
                const bool upOk = sendKeyMessage(pendingCommand, true, &upError);
                keyIsDown = false;
                {
                    QMutexLocker locker(&mutex);
                    busy = false;
                }
                emit debugMessage(QStringLiteral("共享状态释放：vk=0x%1, state=%2, message=%3, error=%4")
                    .arg(pendingCommand.virtualKey, 0, 16).arg(stateOk).arg(upOk).arg(upError));
            }
            break;
        case SharedInputStopMessage:
            running = false;
            break;
        default:
            TranslateMessage(&message);
            DispatchMessageW(&message);
            break;
        }
    }

    // 即使停止发生在释放计时期间，也必须先释放模拟键。
    if (keyIsDown)
    {
        if (releaseTimer != 0)
            KillTimer(nullptr, releaseTimer);
        setOneKeyState(pendingCommand.virtualKey, false);
        DWORD ignoredError = ERROR_SUCCESS;
        sendKeyMessage(pendingCommand, true, &ignoredError);
    }

    const bool detachResult = AttachThreadInput(currentWorkerThreadId, gameThreadId, FALSE) != FALSE;
    {
        QMutexLocker locker(&mutex);
        attached = false;
        busy = false;
        workerThreadId = 0;
    }
    emit debugMessage(QStringLiteral("共享输入线程已停止：detached=%1").arg(detachResult));
}

bool SharedInputWorker::setOneKeyState(UINT virtualKey, bool pressed)
{
    BYTE states[256] = {};
    if (!GetKeyboardState(states))
        return false;

    if (pressed)
        states[virtualKey & 0xff] |= 0x80;
    else
        states[virtualKey & 0xff] &= 0x7f;
    return SetKeyboardState(states) != FALSE;
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

    DWORD_PTR result = 0;
    SetLastError(ERROR_SUCCESS);
    const bool sent = SendMessageTimeoutA(targetWindow, message, command.virtualKey, parameter,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &result) != 0;
    *errorCode = sent ? ERROR_SUCCESS : GetLastError();
    return sent;
}
