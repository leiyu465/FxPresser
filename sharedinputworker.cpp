#include "sharedinputworker.h"

#include <QMutexLocker>

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
    stopRequested = false;
    attached = false;
    startupComplete = false;
    busy = false;
    commands.clear();
    start();

    if (!startupComplete)
        startupFinished.wait(&mutex, startupTimeoutMilliseconds);
    return startupComplete && attached;
}

void SharedInputWorker::stopForWindow()
{
    {
        QMutexLocker locker(&mutex);
        if (!isRunning())
            return;
        stopRequested = true;
        commands.clear();
        commandAvailable.wakeAll();
    }
    wait();
}

bool SharedInputWorker::enqueueKey(UINT virtualKey, int holdMilliseconds)
{
    QMutexLocker locker(&mutex);
    if (!isRunning() || !attached || stopRequested || busy || !commands.isEmpty())
        return false;

    commands.enqueue({ virtualKey, holdMilliseconds });
    commandAvailable.wakeOne();
    return true;
}

void SharedInputWorker::run()
{
    // AttachThreadInput要求两端线程都有USER消息队列。
    MSG message = {};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    HWND window;
    {
        QMutexLocker locker(&mutex);
        window = targetWindow;
    }

    const DWORD toolThreadId = GetCurrentThreadId();
    DWORD processId = 0;
    const DWORD gameThreadId = GetWindowThreadProcessId(window, &processId);
    const bool attachResult = gameThreadId != 0 && gameThreadId != toolThreadId &&
        AttachThreadInput(toolThreadId, gameThreadId, TRUE) != FALSE;
    const DWORD attachError = attachResult ? ERROR_SUCCESS : GetLastError();

    {
        QMutexLocker locker(&mutex);
        attached = attachResult;
        startupComplete = true;
        startupFinished.wakeAll();
    }

    emit debugMessage(QStringLiteral("共享输入线程：pid=%1, gameThread=%2, workerThread=%3, attached=%4, error=%5")
        .arg(processId).arg(gameThreadId).arg(toolThreadId).arg(attachResult).arg(attachError));

    if (!attachResult)
        return;

    for (;;)
    {
        KeyCommand command = {};
        {
            QMutexLocker locker(&mutex);
            while (commands.isEmpty() && !stopRequested)
                commandAvailable.wait(&mutex);
            if (stopRequested)
                break;
            command = commands.dequeue();
            busy = true;
        }

        sendKey(command);

        {
            QMutexLocker locker(&mutex);
            busy = false;
            if (stopRequested)
                break;
        }
    }

    const bool detachResult = AttachThreadInput(toolThreadId, gameThreadId, FALSE) != FALSE;
    {
        QMutexLocker locker(&mutex);
        attached = false;
        busy = false;
        commands.clear();
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

bool SharedInputWorker::sendKey(const KeyCommand& command)
{
    const UINT scanCode = MapVirtualKeyW(command.virtualKey, MAPVK_VK_TO_VSC);
    const LPARAM downParam = static_cast<LPARAM>(1ULL | (static_cast<ULONGLONG>(scanCode) << 16));
    const LPARAM upParam = static_cast<LPARAM>(static_cast<ULONGLONG>(downParam) |
        (1ULL << 30) | (1ULL << 31));
    const bool systemKey = command.virtualKey == VK_F10;
    const UINT downMessage = systemKey ? WM_SYSKEYDOWN : WM_KEYDOWN;
    const UINT upMessage = systemKey ? WM_SYSKEYUP : WM_KEYUP;

    const bool downStateOk = setOneKeyState(command.virtualKey, true);
    DWORD_PTR downResult = 0;
    SetLastError(ERROR_SUCCESS);
    const bool downOk = SendMessageTimeoutA(targetWindow, downMessage, command.virtualKey, downParam,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &downResult) != 0;
    const DWORD downError = downOk ? ERROR_SUCCESS : GetLastError();

    {
        QMutexLocker locker(&mutex);
        if (!stopRequested)
            commandAvailable.wait(&mutex, static_cast<unsigned long>(command.holdMilliseconds));
    }

    const bool upStateOk = setOneKeyState(command.virtualKey, false);
    DWORD_PTR upResult = 0;
    SetLastError(ERROR_SUCCESS);
    const bool upOk = SendMessageTimeoutA(targetWindow, upMessage, command.virtualKey, upParam,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &upResult) != 0;
    const DWORD upError = upOk ? ERROR_SUCCESS : GetLastError();

    emit debugMessage(QStringLiteral("共享状态按键：vk=0x%1, scan=0x%2, hold=%3ms, downState=%4, down=%5/%6, upState=%7, up=%8/%9")
        .arg(command.virtualKey, 0, 16).arg(scanCode, 0, 16).arg(command.holdMilliseconds)
        .arg(downStateOk).arg(downOk).arg(downError)
        .arg(upStateOk).arg(upOk).arg(upError));
    return downStateOk && downOk && upStateOk && upOk;
}
