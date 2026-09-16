#include "fxmainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QDataStream>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
const QRect PlayerNameRect{80, 22, 90, 14};

class CharacterBoxDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QStyleOptionViewItem adjusted = option;
        initStyleOption(&adjusted, index);
        adjusted.decorationSize.setWidth(adjusted.rect.width());
        QStyle* style = adjusted.widget ? adjusted.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &adjusted, painter, adjusted.widget);
    }
};

QFrame* horizontalLine()
{
    QFrame* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}
}

FxMainWindow::FxMainWindow(QWidget* parent)
    : QMainWindow(parent), modeManager(windowManager, nullptr),
      automation(windowManager, modeManager, keyConfig, timeConfig, nullptr),
      hotkeyManager(nullptr)
{
    setupUI();
    connectRuntime();

    QDir(QCoreApplication::applicationDirPath()).mkdir(QStringLiteral("config"));
    const SConfigData config = readConfig(getConfigPath());
    applyConfigToUI(config);

    scanGameWindows();
    autoSelectAndRenameGameWindow(currentHash);
    updateModeUI();

    QString shortcutError;
    if (!hotkeyManager.setShortcut(config.globalShortcut, &shortcutError))
        writeLog(shortcutError, true);
}

FxMainWindow::~FxMainWindow()
{
    stopAutomation();
    clearGameTopmost();
    writeConfig(getConfigPath(), makeConfigFromUI());
}

void FxMainWindow::connectRuntime()
{
    connect(&modeManager, &InputModeManager::modeChanged,
        this, [this](InputMode mode) {
            updateModeUI();
            writeLog(QStringLiteral("自动选择方式：%1").arg(InputModeManager::displayName(mode)));
        });

    connect(&automation, &AutomationController::runningChanged,
        this, [this](bool running) {
            btnStartStop->setText(running ? QStringLiteral("停止") : QStringLiteral("开始"));
        });
    connect(&automation, &AutomationController::debugMessage,
        this, &FxMainWindow::writeLog);
    connect(&automation, &AutomationController::keyExecuted,
        this, [this](int keyNumber, InputMode mode, bool success,
            DWORD errorCode, const QString& detail) {
            writeLog(QStringLiteral("F%1 · %2 · %3 · error=%4 · %5")
                .arg(keyNumber).arg(InputModeManager::displayName(mode))
                .arg(success ? QStringLiteral("成功") : QStringLiteral("失败"))
                .arg(errorCode).arg(detail), !success);
        });

    connect(&hotkeyManager, &GlobalHotkeyManager::activated,
        this, &FxMainWindow::toggleAutomation);
}

void FxMainWindow::setupUI()
{
    QWidget* mainWidget = new QWidget;
    QVBoxLayout* mainLayout = new QVBoxLayout(mainWidget);
    auto makeHelpButton = [this](const QString& title, const QString& text) {
        QToolButton* button = new QToolButton;
        button->setText(QStringLiteral("?"));
        button->setFixedSize(18, 18);
        button->setToolTip(QStringLiteral("点击查看计算逻辑"));
        connect(button, &QToolButton::clicked, this, [this, title, text]() {
            QMessageBox::information(this, title, text);
        });
        return button;
    };

    QHBoxLayout* scanRow = new QHBoxLayout;
    btnScan = new QPushButton(QStringLiteral("扫描游戏窗口"));
    btnGameTopmost = new QToolButton;
    btnGameTopmost->setCheckable(true);
    btnGameTopmost->setFixedSize(28, 28);
    btnGameTopmost->setToolTip(QStringLiteral("游戏窗口置顶"));
    scanRow->addWidget(btnScan, 1);
    scanRow->addWidget(btnGameTopmost, 0, Qt::AlignRight | Qt::AlignTop);
    mainLayout->addLayout(scanRow);

    comboWindows = new QComboBox;
    comboWindows->setIconSize(PlayerNameRect.size());
    comboWindows->setItemDelegate(new CharacterBoxDelegate(comboWindows));
    mainLayout->addWidget(comboWindows);

    QHBoxLayout* titleRow = new QHBoxLayout;
    titleRow->addWidget(new QLabel(QStringLiteral("窗口标题")));
    lineTitle = new QLineEdit;
    titleRow->addWidget(lineTitle, 1);
    mainLayout->addLayout(titleRow);

    btnChangeTitle = new QPushButton(QStringLiteral("修改窗口标题"));
    btnSwitchToWindow = new QPushButton(QStringLiteral("切换到游戏窗口"));
    mainLayout->addWidget(btnChangeTitle);
    mainLayout->addWidget(btnSwitchToWindow);

    checkAutomaticMode = new QCheckBox(QStringLiteral("自动选择"));
    comboInputMode = new QComboBox;
    comboInputMode->addItem(QStringLiteral("共享消息"), static_cast<int>(InputMode::SharedMessage));
    comboInputMode->addItem(QStringLiteral("按键+自动窗口"), static_cast<int>(InputMode::KeyboardAutoWindow));
    comboInputMode->addItem(QStringLiteral("按键+手动窗口"), static_cast<int>(InputMode::KeyboardManualWindow));
    comboInputMode->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    labelAutomaticMode = new QLabel;
    labelAutomaticMode->setAlignment(Qt::AlignCenter);
    modeStack = new QStackedWidget;
    modeStack->addWidget(comboInputMode);
    modeStack->addWidget(labelAutomaticMode);
    QHBoxLayout* modeRow = new QHBoxLayout;
    modeRow->addWidget(checkAutomaticMode);
    modeRow->addWidget(modeStack, 1);
    mainLayout->addLayout(modeRow);

    spinReleaseInterval = new QDoubleSpinBox;
    spinReleaseInterval->setSuffix(QStringLiteral(" s"));
    spinReleaseInterval->setDecimals(3);
    spinReleaseInterval->setRange(-5.0, 5.0);
    spinReleaseInterval->setSingleStep(0.001);
    QHBoxLayout* releaseRow = new QHBoxLayout;
    releaseRow->addWidget(new QLabel(QStringLiteral("释放间隔")));
    releaseRow->addWidget(makeHelpButton(QStringLiteral("释放间隔"),
        QStringLiteral("完整按键会先发送DOWN，再等待释放间隔，最后发送UP。\n\n"
                       "实际值 = 当前设置值 + -2ms至+2ms随机范围；结果小于1ms时不延迟。")));
    releaseRow->addWidget(spinReleaseInterval);
    mainLayout->addLayout(releaseRow);

    mainLayout->addWidget(horizontalLine());

    btnStartStop = new QPushButton(QStringLiteral("开始"));
    QFont startFont = btnStartStop->font();
    startFont.setPointSize(16);
    startFont.setBold(true);
    btnStartStop->setFont(startFont);
    btnStartStop->setMinimumHeight(48);
    mainLayout->addWidget(btnStartStop);

    spinGlobalInterval = new QDoubleSpinBox;
    spinGlobalInterval->setSuffix(QStringLiteral(" s"));
    spinGlobalInterval->setDecimals(2);
    spinGlobalInterval->setRange(0.0, 365.0);
    spinGlobalInterval->setSingleStep(0.01);
    QHBoxLayout* globalRow = new QHBoxLayout;
    globalRow->addWidget(new QLabel(QStringLiteral("全局间隔")));
    globalRow->addWidget(makeHelpButton(QStringLiteral("全局间隔"),
        QStringLiteral("每执行完一个完整按键后，等待该时间，再继续检查下一个按键。\n\n"
                       "运行中修改后，下一次等待使用新值。")));
    globalRow->addWidget(spinGlobalInterval);
    mainLayout->addLayout(globalRow);

    mainLayout->addWidget(horizontalLine());
    QGridLayout* keyGrid = new QGridLayout;
    keyGrid->addWidget(new QLabel(QStringLiteral("启用")), 0, 0);
    QWidget* keyIntervalHeader = new QWidget;
    QHBoxLayout* keyIntervalHeaderLayout = new QHBoxLayout(keyIntervalHeader);
    keyIntervalHeaderLayout->setContentsMargins(0, 0, 0, 0);
    keyIntervalHeaderLayout->setSpacing(2);
    keyIntervalHeaderLayout->addWidget(new QLabel(QStringLiteral("间隔")));
    keyIntervalHeaderLayout->addWidget(makeHelpButton(QStringLiteral("单键间隔"),
        QStringLiteral("循环到该按键时，当前时间距离该键上次KEYDOWN调度时间达到此间隔才会执行。\n\n"
                       "未到期时直接跳过；运行中修改后立即参与下一次判断。")));
    keyGrid->addWidget(keyIntervalHeader, 0, 1);
    for (int index = 0; index < 10; ++index)
    {
        keyChecks[index] = new QCheckBox(QStringLiteral("F%1").arg(index + 1));
        keyIntervals[index] = new QDoubleSpinBox;
        keyIntervals[index]->setSuffix(QStringLiteral(" s"));
        keyIntervals[index]->setDecimals(1);
        keyIntervals[index]->setRange(0.0, 365.0);
        keyIntervals[index]->setSingleStep(0.1);
        keyGrid->addWidget(keyChecks[index], index + 1, 0);
        keyGrid->addWidget(keyIntervals[index], index + 1, 1);
    }
    mainLayout->addLayout(keyGrid);

    btnShortcut = new QPushButton(QStringLiteral("设置全局快捷键"));
    btnShowLog = new QPushButton(QStringLiteral("查看调试日志"));
    mainLayout->addWidget(btnShortcut);
    mainLayout->addWidget(btnShowLog);

    setCentralWidget(mainWidget);
    setMinimumWidth(160);
    setMaximumWidth(200);
    resize(200, sizeHint().height());

    connect(btnScan, &QPushButton::clicked, this, [this]() {
        scanGameWindows();
        if (!gameWindows.isEmpty())
            autoSelectAndRenameGameWindow(currentHash);
    });
    connect(comboWindows, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this, &FxMainWindow::selectWindow);
    connect(btnChangeTitle, &QPushButton::clicked, this, &FxMainWindow::changeWindowTitle);
    connect(btnSwitchToWindow, &QPushButton::clicked, this, [this]() {
        windowManager.activate(windowManager.currentWindow());
    });
    connect(btnGameTopmost, &QToolButton::toggled,
        this, &FxMainWindow::applyGameTopmost);
    connect(checkAutomaticMode, &QCheckBox::toggled, this, [this](bool checked) {
        modeManager.setAutomatic(checked);
        updateModeUI();
    });
    connect(comboInputMode, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this, [this](int) {
            modeManager.setManualMode(static_cast<InputMode>(comboInputMode->currentData().toInt()));
        });
    connect(spinReleaseInterval, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
        this, [this](double value) { timeConfig.setReleaseIntervalSeconds(value); });
    connect(spinGlobalInterval, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
        this, [this](double value) { timeConfig.setGlobalIntervalSeconds(value); });
    for (int index = 0; index < 10; ++index)
    {
        connect(keyChecks[index], &QCheckBox::toggled, this,
            [this, index](bool enabled) { keyConfig.setEnabled(index, enabled); });
        connect(keyIntervals[index], static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            this, [this, index](double value) { timeConfig.setKeyIntervalSeconds(index, value); });
    }
    connect(btnStartStop, &QPushButton::clicked, this, &FxMainWindow::toggleAutomation);
    connect(btnShortcut, &QPushButton::clicked, this, &FxMainWindow::showShortcutDialog);
    connect(btnShowLog, &QPushButton::clicked, this, &FxMainWindow::showLogWindow);
}

void FxMainWindow::scanGameWindows()
{
    stopAutomation();
    clearGameTopmost();
    automation.setTargetWindow(nullptr);
    windowManager.setCurrentWindow(nullptr);
    comboWindows->blockSignals(true);
    comboWindows->clear();
    gameWindows.clear();
    playerNameImages.clear();
    playerNameHashes.clear();

    int found = 0;
    int invalid = 0;
    wchar_t processPath[512] = {};
    HWND window = FindWindowW(L"QQSwordWinClass", nullptr);
    while (window)
    {
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        DWORD pathLength = 512;
        const bool gotPath = process &&
            QueryFullProcessImageNameW(process, 0, processPath, &pathLength) != FALSE;
        if (process)
            CloseHandle(process);

        const bool matches = gotPath && QString::fromWCharArray(processPath, pathLength)
            .endsWith(QStringLiteral("\\qqffo.exe"), Qt::CaseInsensitive);
        if (matches)
        {
            QImage image = getGamePicture(window, PlayerNameRect);
            if (!image.isNull())
            {
                gameWindows.push_back(window);
                playerNameImages.push_back(image);
                playerNameHashes.push_back(imageHash(image));
                comboWindows->addItem(QIcon(QPixmap::fromImage(image)), QString());
                ++found;
            }
            else
            {
                ++invalid;
            }
        }
        window = FindWindowExW(nullptr, window, L"QQSwordWinClass", nullptr);
    }
    comboWindows->blockSignals(false);
    writeLog(QStringLiteral("扫描完成：找到%1个窗口，排除%2个不可截图窗口").arg(found).arg(invalid));
}

void FxMainWindow::autoSelectAndRenameGameWindow(const QByteArray& hash)
{
    int selected = -1;
    if (!hash.isEmpty())
    {
        for (int index = 0; index < playerNameHashes.size(); ++index)
        {
            if (playerNameHashes[index] == hash)
            {
                selected = index;
                break;
            }
        }
    }
    if (selected < 0 && !gameWindows.isEmpty())
        selected = 0;
    comboWindows->setCurrentIndex(selected);
    selectWindow(selected);
    if (selected >= 0)
        changeWindowTitle();
}

void FxMainWindow::selectWindow(int index)
{
    stopAutomation();
    clearGameTopmost();
    if (index < 0 || index >= gameWindows.size())
    {
        automation.setTargetWindow(nullptr);
        windowManager.setCurrentWindow(nullptr);
        return;
    }
    windowManager.setCurrentWindow(gameWindows[index]);
    automation.setTargetWindow(gameWindows[index]);
    currentHash = playerNameHashes[index];
    if (btnGameTopmost->isChecked())
        applyGameTopmost(true);
}

void FxMainWindow::changeWindowTitle()
{
    HWND window = windowManager.currentWindow();
    if (window && !lineTitle->text().isEmpty())
        SetWindowTextW(window, lineTitle->text().toStdWString().c_str());
}

void FxMainWindow::toggleAutomation()
{
    btnStartStop->setEnabled(false);
    if (automation.isRunning())
    {
        automation.stop();
    }
    else if (!windowManager.isCurrentWindowValid())
    {
        QMessageBox::warning(this, QStringLiteral("尚未选择游戏窗口"),
            QStringLiteral("请先扫描并选择有效的游戏窗口。"));
    }
    else if (!automation.start())
    {
        QMessageBox::warning(this, QStringLiteral("启动失败"),
            QStringLiteral("无法连接游戏输入线程，请检查游戏与本程序的权限。"));
    }
    btnStartStop->setText(automation.isRunning() ? QStringLiteral("停止") : QStringLiteral("开始"));
    btnStartStop->setEnabled(true);
}

void FxMainWindow::stopAutomation()
{
    if (automation.isRunning())
        automation.stop();
    if (btnStartStop)
        btnStartStop->setText(QStringLiteral("开始"));
}

void FxMainWindow::updateModeUI()
{
    const bool automatic = checkAutomaticMode->isChecked();
    modeStack->setCurrentIndex(automatic ? 1 : 0);
    labelAutomaticMode->setText(InputModeManager::displayName(modeManager.currentMode()));
}

void FxMainWindow::updatePinUI()
{
    btnGameTopmost->setIcon(pinIcon(btnGameTopmost->isChecked()));
    btnGameTopmost->setIconSize(QSize(20, 20));
}

void FxMainWindow::applyGameTopmost(bool enabled)
{
    clearGameTopmost();
    if (enabled)
    {
        HWND window = windowManager.currentWindow();
        if (windowManager.setTopmost(window, true))
            topmostWindow = window;
    }
    updatePinUI();
}

void FxMainWindow::clearGameTopmost()
{
    if (topmostWindow)
        windowManager.setTopmost(topmostWindow, false);
    topmostWindow = nullptr;
}

void FxMainWindow::showShortcutDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("设置全局快捷键"));
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("点击输入框后按下新的组合键：")));
    QKeySequenceEdit* editor = new QKeySequenceEdit(hotkeyManager.shortcut());
    layout->addWidget(editor);
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;
    QString error;
    if (!hotkeyManager.setShortcut(editor->keySequence(), &error))
        QMessageBox::warning(this, QStringLiteral("快捷键设置失败"), error);
}

void FxMainWindow::writeLog(const QString& message, bool failed)
{
    if (!logTextEdit)
        return;
    QTextCharFormat format;
    format.setForeground(failed ? QColor(Qt::red) : logTextEdit->palette().text().color());
    QTextCursor cursor = logTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("%1  %2\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")), message), format);
    logTextEdit->setTextCursor(cursor);
    logTextEdit->ensureCursorVisible();
}

void FxMainWindow::showLogWindow()
{
    if (logTextEdit)
    {
        logTextEdit->window()->raise();
        logTextEdit->window()->activateWindow();
        return;
    }
    QDialog* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("FxPresser 调试日志"));
    dialog->resize(760, 480);
    QVBoxLayout* layout = new QVBoxLayout(dialog);
    QTextEdit* text = new QTextEdit(dialog);
    text->setReadOnly(true);
    logTextEdit = text;
    layout->addWidget(text);
    dialog->show();
    writeLog(QStringLiteral("调试窗口已打开；日志仅保存在此窗口中。"));
}

QIcon FxMainWindow::pinIcon(bool active)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor color = active ? QColor(218, 165, 32) : QColor(110, 110, 110);
    painter.setPen(QPen(color.darker(130), 1.5));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(7, 3, 10, 7), 2, 2);
    QPolygonF point;
    point << QPointF(9, 9) << QPointF(15, 9) << QPointF(13, 15)
          << QPointF(12, 21) << QPointF(11, 15);
    painter.drawPolygon(point);
    return QIcon(pixmap);
}

QImage FxMainWindow::getGamePicture(HWND window, QRect rect)
{
    if (!window || IsWindow(window) == FALSE || IsIconic(window))
        return QImage();

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = rect.width();
    bitmapInfo.bmiHeader.biHeight = rect.height();
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 24;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    HDC windowDc = GetDC(window);
    HDC memoryDc = CreateCompatibleDC(windowDc);
    HBITMAP bitmap = CreateCompatibleBitmap(windowDc, rect.width(), rect.height());
    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
    BitBlt(memoryDc, 0, 0, rect.width(), rect.height(), windowDc,
        rect.left(), rect.top(), SRCCOPY);

    const int stride = (rect.width() * 3 + 3) & ~3;
    QByteArray pixels(stride * rect.height(), 0);
    GetDIBits(memoryDc, bitmap, 0, rect.height(), pixels.data(), &bitmapInfo, DIB_RGB_COLORS);
    SelectObject(memoryDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(window, windowDc);

    return QImage(reinterpret_cast<const uchar*>(pixels.constData()), rect.width(),
        rect.height(), stride, QImage::Format_RGB888).rgbSwapped().mirrored().copy();
}

QByteArray FxMainWindow::imageHash(QImage image)
{
    if (image.isNull())
        return QByteArray();
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << image;
    return QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toBase64();
}

QString FxMainWindow::getConfigPath() const
{
    const QString directory = QCoreApplication::applicationDirPath();
    const QString executable = QFileInfo(QCoreApplication::applicationFilePath()).completeBaseName();
    return directory + QStringLiteral("/config/") + executable + QStringLiteral(".json");
}

SConfigData FxMainWindow::readConfig(const QString& filename) const
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return SConfigData();
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? jsonToConfig(document.object()) : SConfigData();
}

void FxMainWindow::writeConfig(const QString& filename, const SConfigData& config) const
{
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        file.write(QJsonDocument(configToJson(config)).toJson(QJsonDocument::Indented));
}

SConfigData FxMainWindow::makeConfigFromUI() const
{
    SConfigData config;
    for (int index = 0; index < 10; ++index)
    {
        config.keyEnabled[index] = keyChecks[index]->isChecked();
        config.keyIntervals[index] = keyIntervals[index]->value();
    }
    config.globalInterval = spinGlobalInterval->value();
    config.releaseInterval = spinReleaseInterval->value();
    config.gameTopmost = btnGameTopmost->isChecked();
    config.automaticMode = checkAutomaticMode->isChecked();
    config.manualMode = static_cast<InputMode>(comboInputMode->currentData().toInt());
    config.globalShortcut = hotkeyManager.shortcut();
    config.title = lineTitle->text();
    config.hash = currentHash;
    config.x = geometry().x();
    config.y = geometry().y();
    return config;
}

void FxMainWindow::applyConfigToUI(const SConfigData& config)
{
    for (int index = 0; index < 10; ++index)
    {
        keyChecks[index]->setChecked(config.keyEnabled[index]);
        keyIntervals[index]->setValue(config.keyIntervals[index]);
    }
    spinGlobalInterval->setValue(config.globalInterval);
    spinReleaseInterval->setValue(config.releaseInterval);
    const int modeIndex = comboInputMode->findData(static_cast<int>(config.manualMode));
    comboInputMode->setCurrentIndex(modeIndex < 0 ? 0 : modeIndex);
    checkAutomaticMode->setChecked(config.automaticMode);
    btnGameTopmost->setChecked(config.gameTopmost);
    lineTitle->setText(config.title);
    currentHash = config.hash;
    if (config.x >= 0 && config.y >= 0)
        move(config.x, config.y);
    updatePinUI();
}

QJsonObject FxMainWindow::configToJson(const SConfigData& config)
{
    QJsonObject result;
    QJsonArray keys;
    for (int index = 0; index < 10; ++index)
    {
        QJsonObject key;
        key[QStringLiteral("Enabled")] = config.keyEnabled[index];
        key[QStringLiteral("Interval")] = config.keyIntervals[index];
        keys.append(key);
    }
    result[QStringLiteral("AutoPress")] = keys;
    result[QStringLiteral("Interval")] = config.globalInterval;
    result[QStringLiteral("KeyHoldInterval")] = config.releaseInterval;
    result[QStringLiteral("AlwaysOnTop")] = config.gameTopmost;
    result[QStringLiteral("AutomaticMode")] = config.automaticMode;
    result[QStringLiteral("ManualMode")] = static_cast<int>(config.manualMode);
    result[QStringLiteral("GlobalShortcut")] = config.globalShortcut.toString(QKeySequence::PortableText);
    result[QStringLiteral("Title")] = config.title;
    result[QStringLiteral("Hash")] = QString::fromUtf8(config.hash);
    result[QStringLiteral("X")] = config.x;
    result[QStringLiteral("Y")] = config.y;
    return result;
}

SConfigData FxMainWindow::jsonToConfig(QJsonObject json)
{
    SConfigData result;
    const QJsonArray keys = json.value(QStringLiteral("AutoPress")).toArray();
    if (keys.size() == 10)
    {
        for (int index = 0; index < 10; ++index)
        {
            const QJsonObject key = keys[index].toObject();
            result.keyEnabled[index] = key.value(QStringLiteral("Enabled")).toBool(false);
            result.keyIntervals[index] = key.value(QStringLiteral("Interval")).toDouble(1.0);
        }
    }
    result.globalInterval = json.value(QStringLiteral("Interval")).toDouble(0.1);
    result.releaseInterval = json.value(QStringLiteral("KeyHoldInterval")).toDouble(0.027);
    result.gameTopmost = json.value(QStringLiteral("AlwaysOnTop")).toBool(false);
    result.automaticMode = json.value(QStringLiteral("AutomaticMode")).toBool(true);
    result.manualMode = static_cast<InputMode>(json.value(QStringLiteral("ManualMode")).toInt(0));
    result.globalShortcut = QKeySequence(json.value(QStringLiteral("GlobalShortcut"))
        .toString(QStringLiteral("Ctrl+Alt+F12")), QKeySequence::PortableText);
    result.title = json.value(QStringLiteral("Title")).toString();
    result.hash = json.value(QStringLiteral("Hash")).toString().toUtf8();
    result.x = json.value(QStringLiteral("X")).toInt(-1);
    result.y = json.value(QStringLiteral("Y")).toInt(-1);
    // 旧配置中的DefaultKey和SendMethod字段按确认方案直接忽略。
    return result;
}
