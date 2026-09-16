#ifndef SHAREDINPUTWORKER_H
#define SHAREDINPUTWORKER_H

#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <QString>

#include <windows.h>

struct SharedKeyResult
{
    bool success = false;
    DWORD errorCode = ERROR_SUCCESS;
    bool keyboardStateSet = false;
    bool downSent = false;
    bool upSent = false;
};

class SharedInputWorker : public QThread
{
    Q_OBJECT

public:
    explicit SharedInputWorker(QObject* parent = nullptr);
    ~SharedInputWorker() override;

    bool startForWindow(HWND window, int startupTimeoutMilliseconds = 500);
    void stopForWindow();
    SharedKeyResult executeKey(UINT virtualKey, int holdMilliseconds);

signals:
    void debugMessage(const QString& message, bool failed);

protected:
    void run() override;

private:
    struct KeyCommand
    {
        UINT virtualKey = 0;
        int holdMilliseconds = 0;
    };

    SharedKeyResult performKey(const KeyCommand& command);
    bool setKeyDownState(UINT virtualKey, DWORD* errorCode);
    bool sendKeyMessage(const KeyCommand& command, bool keyUp, DWORD* errorCode);

    QMutex mutex;
    QWaitCondition startupFinished;
    QWaitCondition commandFinished;
    HWND targetWindow = nullptr;
    DWORD workerThreadId = 0;
    bool stopRequested = false;
    bool attached = false;
    bool startupComplete = false;
    bool commandPending = false;
    bool commandComplete = false;
    SharedKeyResult commandResult;
};

#endif // SHAREDINPUTWORKER_H
