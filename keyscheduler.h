#ifndef KEYSCHEDULER_H
#define KEYSCHEDULER_H

#include "keyexecutor.h"

#include <QThread>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>

class KeyConfigManager;
class TimeConfigManager;

class KeyScheduler : public QThread
{
    Q_OBJECT

public:
    KeyScheduler(HWND window, KeyConfigManager& keyConfig,
        TimeConfigManager& timeConfig, KeyExecutionService& executionService,
        QObject* parent = nullptr);
    ~KeyScheduler() override;

    void requestStop();

signals:
    void keyExecuted(int keyNumber, InputMode mode, bool success,
        DWORD errorCode, const QString& detail);

protected:
    void run() override;

private:
    bool waitInterruptibly(int milliseconds);

    HWND targetWindow;
    KeyConfigManager& keys;
    TimeConfigManager& times;
    KeyExecutionService& execution;
    std::atomic<bool> stopRequested;
    std::mutex waitMutex;
    std::condition_variable waitCondition;
};

#endif // KEYSCHEDULER_H
