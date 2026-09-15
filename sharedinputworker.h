#ifndef SHAREDINPUTWORKER_H
#define SHAREDINPUTWORKER_H

#include <QMutex>
#include <QQueue>
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

    bool sendKey(const KeyCommand& command);
    bool setOneKeyState(UINT virtualKey, bool pressed);

    QMutex mutex;
    QWaitCondition commandAvailable;
    QWaitCondition startupFinished;
    QQueue<KeyCommand> commands;
    HWND targetWindow = nullptr;
    bool stopRequested = false;
    bool attached = false;
    bool startupComplete = false;
    bool busy = false;
};

#endif // SHAREDINPUTWORKER_H
