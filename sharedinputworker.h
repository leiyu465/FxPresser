#ifndef SHAREDINPUTWORKER_H
#define SHAREDINPUTWORKER_H

#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <QString>

#include <windows.h>

class SharedInputWorker : public QThread
{
    Q_OBJECT

public:
    explicit SharedInputWorker(QObject* parent = nullptr);
    ~SharedInputWorker() override;

    bool startForWindow(HWND window, int startupTimeoutMilliseconds = 500);
    void stopForWindow();
    bool enqueueKey(UINT virtualKey, int holdMilliseconds);

signals:
    void debugMessage(const QString& message);

protected:
    void run() override;

private:
    struct KeyCommand
    {
        UINT virtualKey;
        int holdMilliseconds;
    };

    bool setOneKeyState(UINT virtualKey, bool pressed);
    bool sendKeyMessage(const KeyCommand& command, bool keyUp, DWORD* errorCode);

    QMutex mutex;
    QWaitCondition startupFinished;
    HWND targetWindow = nullptr;
    DWORD workerThreadId = 0;
    bool stopRequested = false;
    bool attached = false;
    bool startupComplete = false;
    bool busy = false;
};

#endif // SHAREDINPUTWORKER_H
