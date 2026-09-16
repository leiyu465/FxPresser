#include "keyscheduler.h"
#include "runtimeconfig.h"

#include <QtMath>
#include <chrono>

KeyScheduler::KeyScheduler(HWND window, KeyConfigManager& keyConfig,
    TimeConfigManager& timeConfig, KeyExecutionService& executionService,
    QObject* parent)
    : QThread(parent), targetWindow(window), keys(keyConfig), times(timeConfig),
      execution(executionService), stopRequested(false)
{
}

KeyScheduler::~KeyScheduler()
{
    requestStop();
    wait();
}

void KeyScheduler::requestStop()
{
    stopRequested.store(true);
    waitCondition.notify_all();
}

void KeyScheduler::run()
{
    using Clock = std::chrono::steady_clock;
    std::array<Clock::time_point, 10> lastPressed;
    lastPressed.fill(Clock::time_point());
    int keyIndex = 0;
    int skippedInRound = 0;

    while (!stopRequested.load())
    {
        if (!keys.isEnabled(keyIndex))
        {
            keyIndex = (keyIndex + 1) % 10;
            if (++skippedInRound >= 10)
            {
                skippedInRound = 0;
                waitInterruptibly(10);
            }
            continue;
        }

        const Clock::time_point now = Clock::now();
        const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastPressed[keyIndex]).count();
        const long long required = qRound64(times.keyIntervalSeconds(keyIndex) * 1000.0);
        if (elapsed < required)
        {
            keyIndex = (keyIndex + 1) % 10;
            if (++skippedInRound >= 10)
            {
                skippedInRound = 0;
                waitInterruptibly(10);
            }
            continue;
        }

        // 已确认：在执行前记录调度时间，结果成功或失败都不回滚。
        lastPressed[keyIndex] = now;
        skippedInRound = 0;

        KeyRequest request;
        request.window = targetWindow;
        request.virtualKey = VK_F1 + keyIndex;
        const KeyResult result = execution.executeKey(request);
        emit keyExecuted(keyIndex + 1, result.mode, result.success,
            result.errorCode, result.detail);

        keyIndex = (keyIndex + 1) % 10;
        const int globalWait = qMax(0, qRound(times.globalIntervalSeconds() * 1000.0));
        waitInterruptibly(globalWait);
    }
}

bool KeyScheduler::waitInterruptibly(int milliseconds)
{
    if (milliseconds <= 0)
        return stopRequested.load();
    std::unique_lock<std::mutex> locker(waitMutex);
    waitCondition.wait_for(locker, std::chrono::milliseconds(milliseconds),
        [this]() { return stopRequested.load(); });
    return stopRequested.load();
}
