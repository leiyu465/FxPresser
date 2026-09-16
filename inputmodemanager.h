#ifndef INPUTMODEMANAGER_H
#define INPUTMODEMANAGER_H

#include <QObject>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

class GameWindowManager;

enum class InputMode
{
    SharedMessage = 0,
    KeyboardAutoWindow = 1,
    KeyboardManualWindow = 2
};

Q_DECLARE_METATYPE(InputMode)

class InputModeManager : public QObject
{
    Q_OBJECT

public:
    explicit InputModeManager(GameWindowManager& windowManager, QObject* parent = nullptr);
    ~InputModeManager() override;

    bool isAutomatic() const;
    InputMode manualMode() const;
    InputMode currentMode() const;

    void setAutomatic(bool enabled);
    void setManualMode(InputMode mode);

    static QString displayName(InputMode mode);

signals:
    void modeChanged(InputMode mode);
    void automaticChanged(bool enabled);

private:
    void startMonitor();
    void stopMonitor();
    void monitorLoop();
    void updateDetectedMode();
    void storeCurrentMode(InputMode mode);

    GameWindowManager& windows;
    std::atomic<bool> automatic;
    std::atomic<int> selectedManualMode;
    std::atomic<int> effectiveMode;
    std::atomic<bool> stopRequested;
    std::thread monitorThread;
    std::mutex waitMutex;
    std::condition_variable waitCondition;
};

#endif // INPUTMODEMANAGER_H
