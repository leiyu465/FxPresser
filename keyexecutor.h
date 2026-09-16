#ifndef KEYEXECUTOR_H
#define KEYEXECUTOR_H

#include "inputmodemanager.h"
#include "sharedinputworker.h"

#include <QObject>
#include <QString>
#include <mutex>
#include <windows.h>

class GameWindowManager;
class TimeConfigManager;

struct KeyRequest
{
    HWND window = nullptr;
    UINT virtualKey = 0;
};

struct KeyResult
{
    bool success = false;
    InputMode mode = InputMode::SharedMessage;
    DWORD errorCode = ERROR_SUCCESS;
    QString detail;
};

class IKeyExecutor
{
public:
    virtual ~IKeyExecutor() = default;
    virtual KeyResult execute(const KeyRequest& request) = 0;
};

class SharedMessageKeyExecutor : public IKeyExecutor
{
public:
    SharedMessageKeyExecutor(SharedInputWorker& worker, TimeConfigManager& timeConfig);
    KeyResult execute(const KeyRequest& request) override;

private:
    SharedInputWorker& inputWorker;
    TimeConfigManager& times;
};

class AutoWindowKeyExecutor : public IKeyExecutor
{
public:
    AutoWindowKeyExecutor(GameWindowManager& windowManager, TimeConfigManager& timeConfig);
    KeyResult execute(const KeyRequest& request) override;

private:
    GameWindowManager& windows;
    TimeConfigManager& times;
};

class ManualWindowKeyExecutor : public IKeyExecutor
{
public:
    explicit ManualWindowKeyExecutor(TimeConfigManager& timeConfig);
    KeyResult execute(const KeyRequest& request) override;

private:
    TimeConfigManager& times;
};

class KeyExecutionService : public QObject
{
    Q_OBJECT

public:
    KeyExecutionService(InputModeManager& modeManager,
        GameWindowManager& windowManager, TimeConfigManager& timeConfig,
        QObject* parent = nullptr);
    ~KeyExecutionService() override;

    void setTargetWindow(HWND window);
    KeyResult executeKey(const KeyRequest& request);

signals:
    void debugMessage(const QString& message, bool failed);

private:
    void synchronizeSharedInput();
    void synchronizeSharedInputLocked();

    InputModeManager& modes;
    SharedInputWorker sharedWorker;
    SharedMessageKeyExecutor sharedExecutor;
    AutoWindowKeyExecutor autoExecutor;
    ManualWindowKeyExecutor manualExecutor;
    std::mutex resourceMutex;
    HWND targetWindow = nullptr;
    HWND attachedWindow = nullptr;
};

#endif // KEYEXECUTOR_H
