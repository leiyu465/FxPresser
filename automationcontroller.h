#ifndef AUTOMATIONCONTROLLER_H
#define AUTOMATIONCONTROLLER_H

#include "keyexecutor.h"

#include <QObject>

class GameWindowManager;
class InputModeManager;
class KeyConfigManager;
class KeyScheduler;
class TimeConfigManager;

class AutomationController : public QObject
{
    Q_OBJECT

public:
    AutomationController(GameWindowManager& windowManager,
        InputModeManager& modeManager, KeyConfigManager& keyConfig,
        TimeConfigManager& timeConfig, QObject* parent = nullptr);
    ~AutomationController() override;

    bool start();
    void stop();
    bool isRunning() const;
    void setTargetWindow(HWND window);

signals:
    void runningChanged(bool running);
    void keyExecuted(int keyNumber, InputMode mode, bool success,
        DWORD errorCode, const QString& detail);
    void debugMessage(const QString& message, bool failed);

private:
    GameWindowManager& windows;
    KeyConfigManager& keys;
    TimeConfigManager& times;
    KeyExecutionService execution;
    KeyScheduler* scheduler = nullptr;
};

#endif // AUTOMATIONCONTROLLER_H
