#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "automationcontroller.h"
#include "gamewindowmanager.h"
#include "globalhotkeymanager.h"
#include "inputmodemanager.h"
#include "runtimeconfig.h"

#include <QByteArray>
#include <QImage>
#include <QMainWindow>
#include <QPointer>
#include <QVector>
#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class QToolButton;

struct SConfigData
{
    std::array<bool, 10> keyEnabled;
    std::array<double, 10> keyIntervals;
    double globalInterval = 0.1;
    double releaseInterval = 0.027;
    bool gameTopmost = false;
    bool automaticMode = true;
    InputMode manualMode = InputMode::SharedMessage;
    QKeySequence globalShortcut = QKeySequence(QStringLiteral("Ctrl+Alt+F12"));
    QString title;
    QByteArray hash;
    int x = -1;
    int y = -1;

    SConfigData()
    {
        keyEnabled.fill(false);
        keyIntervals.fill(1.0);
    }
};

class FxMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit FxMainWindow(QWidget* parent = nullptr);
    ~FxMainWindow() override;

private:
    void setupUI();
    void connectRuntime();

    void scanGameWindows();
    void autoSelectAndRenameGameWindow(const QByteArray& hash);
    void changeWindowTitle();
    void selectWindow(int index);
    void toggleAutomation();
    void stopAutomation();

    void updateModeUI();
    void updatePinUI();
    void applyGameTopmost(bool enabled);
    void clearGameTopmost();
    void showShortcutDialog();

    void writeLog(const QString& message, bool failed = false);
    void showLogWindow();

    static QByteArray imageHash(QImage image);
    static QImage getGamePicture(HWND window, QRect rect);
    static QIcon pinIcon(bool active);

    QString getConfigPath() const;
    SConfigData readConfig(const QString& filename) const;
    void writeConfig(const QString& filename, const SConfigData& config) const;
    SConfigData makeConfigFromUI() const;
    void applyConfigToUI(const SConfigData& config);
    static QJsonObject configToJson(const SConfigData& config);
    static SConfigData jsonToConfig(QJsonObject json);

    QPushButton* btnScan = nullptr;
    QComboBox* comboWindows = nullptr;
    QLineEdit* lineTitle = nullptr;
    QPushButton* btnChangeTitle = nullptr;
    QPushButton* btnSwitchToWindow = nullptr;
    QToolButton* btnGameTopmost = nullptr;
    QCheckBox* checkAutomaticMode = nullptr;
    QComboBox* comboInputMode = nullptr;
    QLabel* labelAutomaticMode = nullptr;
    QStackedWidget* modeStack = nullptr;
    QDoubleSpinBox* spinReleaseInterval = nullptr;
    QPushButton* btnStartStop = nullptr;
    QDoubleSpinBox* spinGlobalInterval = nullptr;
    std::array<QCheckBox*, 10> keyChecks;
    std::array<QDoubleSpinBox*, 10> keyIntervals;
    QPushButton* btnShortcut = nullptr;
    QPushButton* btnShowLog = nullptr;
    QPointer<QTextEdit> logTextEdit;

    QVector<HWND> gameWindows;
    QVector<QImage> playerNameImages;
    QVector<QByteArray> playerNameHashes;
    QByteArray currentHash;
    HWND topmostWindow = nullptr;

    GameWindowManager windowManager;
    TimeConfigManager timeConfig;
    KeyConfigManager keyConfig;
    InputModeManager modeManager;
    AutomationController automation;
    GlobalHotkeyManager hotkeyManager;
};

#endif // MAINWINDOW_H
