#include "automationcontroller.h"
#include "gamewindowmanager.h"
#include "inputmodemanager.h"
#include "keyscheduler.h"
#include "runtimeconfig.h"

AutomationController::AutomationController(GameWindowManager& windowManager,
    InputModeManager& modeManager, KeyConfigManager& keyConfig,
    TimeConfigManager& timeConfig, QObject* parent)
    : QObject(parent), windows(windowManager), keys(keyConfig), times(timeConfig),
      execution(modeManager, windowManager, timeConfig, nullptr)
{
    connect(&execution, &KeyExecutionService::debugMessage,
        this, &AutomationController::debugMessage);
}

AutomationController::~AutomationController()
{
    stop();
}

bool AutomationController::start()
{
    if (scheduler)
        return true;

    HWND window = windows.currentWindow();
    if (!window || IsWindow(window) == FALSE)
        return false;

    // 已确认：无论当前模式是什么，全局开关开启时都先Attach。
    if (!execution.startSession(window))
    {
        execution.stopSession();
        return false;
    }

    scheduler = new KeyScheduler(window, keys, times, execution, this);
    connect(scheduler, &KeyScheduler::keyExecuted,
        this, &AutomationController::keyExecuted);
    scheduler->start();
    emit runningChanged(true);
    emit debugMessage(QStringLiteral("自动按键调度已启动"), false);
    return true;
}

void AutomationController::stop()
{
    if (!scheduler)
    {
        execution.stopSession();
        return;
    }

    scheduler->requestStop();
    scheduler->wait();
    delete scheduler;
    scheduler = nullptr;
    execution.stopSession();
    emit runningChanged(false);
    emit debugMessage(QStringLiteral("自动按键调度已停止"), false);
}

bool AutomationController::isRunning() const
{
    return scheduler != nullptr;
}
