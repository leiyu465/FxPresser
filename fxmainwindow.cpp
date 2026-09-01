#include "fxmainwindow.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QCryptographicHash>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QTextEdit>
#include <QTextCursor>
#include <QToolButton>
//角色名取样区域
static const QRect playerNameRect{ 80,22,90,14 };

class CharacterBoxDelegate : public QStyledItemDelegate
{
public:
    CharacterBoxDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        auto o = option;
        initStyleOption(&o, index);
        o.decorationSize.setWidth(o.rect.width());
        auto style = o.widget ? o.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &o, painter, o.widget);
    }
};

FxMainWindow::FxMainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUI();

    connect(&pressTimer, &QTimer::timeout, this, &FxMainWindow::pressProc);

    QDir dir = QCoreApplication::applicationDirPath();
    dir.mkdir(QStringLiteral("config"));

    //读取参数
    loadConfig();

    writeLog(QStringLiteral("程序启动，按键发送方式：%1").arg(currentSendMethodName()));

    //扫描游戏窗口
    scanGameWindows();

    //首次自动选择游戏窗口
    autoSelectAndRenameGameWindow(currentHash);

    pressTimer.setTimerType(Qt::PreciseTimer);
    pressTimer.start(50);
}

FxMainWindow::~FxMainWindow()
{
    pressTimer.stop();

    writeLog(QStringLiteral("程序退出，保存配置"));
    autoWriteConfig();
}

void FxMainWindow::autoSelectAndRenameGameWindow(const QByteArray& hash)
{
    int index = -1;

    if (!gameWindows.isEmpty())
    {
        if (!hash.isEmpty())
        {
            for (int hash_index = 0; hash_index < playerNameImages.size(); ++hash_index)
            {
                if (playerNameHashes[hash_index] == hash)
                {
                    index = hash_index;
                    break;
                }
            }
        }
    }

    combo_windows->setCurrentIndex(index);

    //找到窗口之后自动更改窗口标题
    if (index != -1)
    {
        changeWindowTitle();
    }
}

void FxMainWindow::pressProc()
{
    if (!check_global_switch->isChecked())
    {
        return;
    }

    int window_index = combo_windows->currentIndex();

    if (window_index == -1)
    {
        return;
    }

    if (currentDefaultKey != -1 && !defaultKeyTriggered && key_checks[currentDefaultKey]->isChecked())
    {
        tryPressKey(gameWindows[window_index], currentDefaultKey, true);
        defaultKeyTriggered = true;
    }

    //每轮从上次成功按键的下一个位置开始，并且最多触发一个按键。
    //旧逻辑固定从F1开始扫描，前面的按键会不断刷新全局时间戳，
    //使后面的按键在部分间隔组合下永久没有触发机会。
    for (int offset = 0; offset < 10; ++offset)
    {
        const int key_index = (nextKeyIndex + offset) % 10;
        if (key_index == currentDefaultKey || !key_checks[key_index]->isChecked())
        {
            continue;
        }

        if (tryPressKey(gameWindows[window_index], key_index, false))
        {
            nextKeyIndex = (key_index + 1) % 10;
            break;
        }
    }
}

void FxMainWindow::resetTimeStamp(int index)
{
    lastPressedTimePoint[index] = std::chrono::steady_clock::now();
}

void FxMainWindow::resetAllTimeStamps()
{
    //为了实现点击全局开关时自动触发一次，此处将每个按键的上次时间设为0
    lastPressedTimePoint.fill(std::chrono::steady_clock::time_point());
    lastAnyPressedTimePoint = std::chrono::steady_clock::time_point();
    nextKeyIndex = 0;
}

void FxMainWindow::scanGameWindows()
{
    wchar_t c_string[512];

    int found = 0, invalid = 0;

    gameWindows.clear();
    playerNameImages.clear();
    playerNameHashes.clear();
    combo_windows->clear();
    check_global_switch->setChecked(false);

    combo_windows->blockSignals(true);

    HWND hWindow = FindWindowW(L"QQSwordWinClass", nullptr);

    while (hWindow != nullptr)
    {
        wchar_t title[512] = {};
        wchar_t className[256] = {};
        GetWindowTextW(hWindow, title, 512);
        GetClassNameW(hWindow, className, 256);

        DWORD pid;
        GetWindowThreadProcessId(hWindow, &pid);
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        DWORD pathLength = 512;
        const bool gotProcessPath = hProcess &&
            QueryFullProcessImageNameW(hProcess, 0, c_string, &pathLength) != FALSE;
        if (hProcess)
            CloseHandle(hProcess);

        const bool currentProcessMatch = gotProcessPath &&
            QString::fromWCharArray(c_string, pathLength).endsWith(QStringLiteral("\\qqffo.exe"), Qt::CaseInsensitive);

        if (currentProcessMatch)
        {
            QImage playerNameImage = getGamePicture(hWindow, playerNameRect);

            if (!playerNameImage.isNull())
            {
                ++found;
                gameWindows.push_back(hWindow);
                playerNameHashes.push_back(imageHash(playerNameImage));
                playerNameImages.push_back(playerNameImage);
                combo_windows->addItem(QIcon(QPixmap::fromImage(playerNameImage)), nullptr);

                writeLog(QStringLiteral("扫描命中：handle=0x%1, pid=%2, class=%3, title=%4")
                    .arg(reinterpret_cast<quintptr>(hWindow), 0, 16).arg(pid)
                    .arg(QString::fromWCharArray(className), QString::fromWCharArray(title)));
            }
            else
            {
                ++invalid;
                writeLog(QStringLiteral("扫描排除不可截图窗口：handle=0x%1, pid=%2, minimized=%3, title=%4")
                    .arg(reinterpret_cast<quintptr>(hWindow), 0, 16).arg(pid)
                    .arg(IsIconic(hWindow) != FALSE).arg(QString::fromWCharArray(title)));
            }
        }

        hWindow = FindWindowExW(nullptr, hWindow, L"QQSwordWinClass", nullptr);
    }

    combo_windows->blockSignals(false);
    writeLog(QStringLiteral("扫描窗口完成：新版窗口类+进程方式，找到 %1 个窗口，%2 个窗口无法截图")
        .arg(found).arg(invalid));
}

void FxMainWindow::changeWindowTitle()
{
    int window_index = combo_windows->currentIndex();

    if (window_index == -1)
    {
        return;
    }

    QString text = line_title->text();

    if (!text.isEmpty())
    {
        SetWindowTextW(gameWindows[window_index], text.toStdWString().c_str());
    }
}

bool FxMainWindow::tryPressKey(HWND window, int key_index, bool force)
{
    auto nowTimePoint = std::chrono::steady_clock::now();

    std::chrono::milliseconds differFromSelf = std::chrono::duration_cast<std::chrono::milliseconds>(nowTimePoint - lastPressedTimePoint[key_index]);
    std::chrono::milliseconds differFromAny = std::chrono::duration_cast<std::chrono::milliseconds>(nowTimePoint - lastAnyPressedTimePoint);
    std::chrono::milliseconds selfInterval(static_cast<long long>(key_intervals[key_index]->value() * 1000));
    std::chrono::milliseconds anyInterval(static_cast<long long>(spin_global_interval->value() * 1000));

    if (force || (differFromSelf >= selfInterval && differFromAny >= anyInterval))
    {
        lastPressedTimePoint[key_index] = nowTimePoint;
        lastAnyPressedTimePoint = nowTimePoint;

        bool sent = pressKey(window, VK_F1 + key_index);
        writeLog(QStringLiteral("触发 F%1：%2").arg(key_index + 1).arg(sent ? QStringLiteral("发送成功") : QStringLiteral("发送失败")));
        return true;
    }

    return false;
}

bool FxMainWindow::pressKey(HWND window, UINT code)
{
    if (!IsWindow(window))
    {
        writeLog(QStringLiteral("发送失败：窗口句柄已失效，handle=0x%1")
            .arg(reinterpret_cast<quintptr>(window), 0, 16));
        return false;
    }

    const int method = combo_send_method->currentData().toInt();
    if (method >= 8)
    {
        DWORD errorCode = ERROR_SUCCESS;
        const bool result = sendLegacyWindowKey(window, code, method, &errorCode);
        wchar_t title[512] = {};
        wchar_t className[256] = {};
        GetWindowTextW(window, title, 512);
        GetClassNameW(window, className, 256);
        writeLog(QStringLiteral("旧版窗口消息：method=%1, vk=0x%2, target=0x%3, class=%4, title=%5, foreground=%6, ok=%7, error=%8")
            .arg(currentSendMethodName()).arg(code, 0, 16)
            .arg(reinterpret_cast<quintptr>(window), 0, 16)
            .arg(QString::fromWCharArray(className), QString::fromWCharArray(title))
            .arg(GetForegroundWindow() == window).arg(result).arg(errorCode));
        return result;
    }

    const bool autoActivateWindow = method >= 4;
    if (autoActivateWindow && GetForegroundWindow() != window)
    {
        if (IsIconic(window))
            ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
    }

    if (GetForegroundWindow() != window)
    {
        writeLog(QStringLiteral("全局输入已取消：游戏窗口不在前台，handle=0x%1, method=%2")
            .arg(reinterpret_cast<quintptr>(window), 0, 16)
            .arg(currentSendMethodName()));
        return false;
    }

    const UINT scanCode = MapVirtualKeyW(code, MAPVK_VK_TO_VSC);
    const bool foreground = GetForegroundWindow() == window;
    DWORD errorCode = ERROR_SUCCESS;

    const bool downOk = sendGlobalKey(false, code, method, &errorCode);
    writeLog(QStringLiteral("按键按下：method=%1, vk=0x%2, scan=0x%3, window=0x%4, foreground=%5, ok=%6, error=%7")
        .arg(currentSendMethodName())
        .arg(code, 0, 16).arg(scanCode, 0, 16)
        .arg(reinterpret_cast<quintptr>(window), 0, 16)
        .arg(foreground).arg(downOk).arg(errorCode));

    const int holdMilliseconds = qRound(spin_key_hold_interval->value() * 1000.0);
    QTimer::singleShot(holdMilliseconds, this,
        [this, code, method]() {
            DWORD upError = ERROR_SUCCESS;
            const bool upOk = sendGlobalKey(true, code, method, &upError);
            writeLog(QStringLiteral("按键释放：method=%1, vk=0x%2, ok=%3, error=%4")
                .arg(currentSendMethodName())
                .arg(code, 0, 16).arg(upOk).arg(upError));
        });
    return downOk;
}

bool FxMainWindow::sendLegacyWindowKey(HWND window, UINT code, int method, DWORD* errorCode)
{
    HWND target = window;
#if 0
    // 已隐藏的实验实现保留在源码中，但不编入发布版，减少无用敏感 API 特征。
    if (method == 11 || method == 12)
    {
        HWND child = GetTopWindow(window);
        if (child)
            target = child;
    }
#else
    Q_UNUSED(method);
#endif

    SetLastError(ERROR_SUCCESS);
    bool result = true;
#if 0
    const LPARAM point = MAKELPARAM(50, 50);

    // 9/12 精确复现旧版的可选右键按下；10 尝试完整右键点击。
    if (method == 9 || method == 12)
        result = PostMessageA(target, WM_RBUTTONDOWN, MK_RBUTTON, point) != FALSE;
    else if (method == 10)
    {
        result = PostMessageA(target, WM_RBUTTONDOWN, MK_RBUTTON, point) != FALSE;
        result = (PostMessageA(target, WM_RBUTTONUP, 0, point) != FALSE) && result;
    }
#endif

    bool keyResult = false;
#if 0
    if (method == 13)
        keyResult = SendNotifyMessageA(target, WM_KEYUP, code, 0) != FALSE;
    else
#endif
        keyResult = PostMessageA(target, WM_KEYUP, code, 0) != FALSE;

    result = result && keyResult;
    if (!result)
        *errorCode = GetLastError();

    writeLog(QStringLiteral("旧版消息目标：top=0x%1, actual=0x%2, msg=WM_KEYUP, wParam=0x%3, lParam=0")
        .arg(reinterpret_cast<quintptr>(window), 0, 16)
        .arg(reinterpret_cast<quintptr>(target), 0, 16).arg(code, 0, 16));
    return result;
}

bool FxMainWindow::sendGlobalKey(bool keyUp, UINT code, int method, DWORD* errorCode)
{
    SetLastError(ERROR_SUCCESS);
    bool result = false;
    switch (method)
    {
    case 0: case 4:
    {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(code);
        input.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
        result = SendInput(1, &input, sizeof(input)) == 1;
        break;
    }
#if 0
    // 其他下拉选项对应的实现暂不编译，需要恢复选项时可重新启用。
    case 1: case 5:
    {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(code, MAPVK_VK_TO_VSC));
        input.ki.dwFlags = KEYEVENTF_SCANCODE | (keyUp ? KEYEVENTF_KEYUP : 0);
        result = SendInput(1, &input, sizeof(input)) == 1;
        break;
    }
    case 2: case 3: case 6: case 7:
    {
        const bool scanOnly = method == 3 || method == 7;
        const BYTE scanCode = static_cast<BYTE>(MapVirtualKeyW(static_cast<UINT>(code), MAPVK_VK_TO_VSC));
        const DWORD flags = keyUp ? KEYEVENTF_KEYUP : 0;
        keybd_event(scanOnly ? 0 : static_cast<BYTE>(code), scanCode, flags, 0);
        result = true;
        break;
    }
#endif
    default:
        *errorCode = ERROR_INVALID_PARAMETER;
        return false;
    }

    if (!result)
        *errorCode = GetLastError();
    return result;
}

QString FxMainWindow::currentSendMethodName() const
{
    return combo_send_method->currentText();
}

void FxMainWindow::writeLog(const QString& message)
{
    if (!logTextEdit)
        return;

    logTextEdit->moveCursor(QTextCursor::End);
    logTextEdit->insertPlainText(QStringLiteral("%1  %2\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")), message));
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

    auto dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("FxPresser 调试日志"));
    dialog->resize(900, 520);

    auto layout = new QVBoxLayout(dialog);
    auto text = new QTextEdit(dialog);
    text->setReadOnly(true);
    logTextEdit = text;
    layout->addWidget(text, 1);
    dialog->show();
    writeLog(QStringLiteral("调试窗口已打开；仅显示打开期间的日志，关闭后立即销毁。"));
    writeLog(QStringLiteral("当前设置：按键方式=%1，全局间隔=%2s，释放间隔=%3s")
        .arg(currentSendMethodName())
        .arg(spin_global_interval->value(), 0, 'f', 2)
        .arg(spin_key_hold_interval->value(), 0, 'f', 2));

    const int windowIndex = combo_windows->currentIndex();
    if (windowIndex >= 0 && windowIndex < gameWindows.size())
    {
        writeLog(QStringLiteral("当前窗口：handle=0x%1")
            .arg(reinterpret_cast<quintptr>(gameWindows[windowIndex]), 0, 16));
    }
    else
    {
        writeLog(QStringLiteral("当前窗口：未选择"));
    }

    for (int index = 0; index < 10; ++index)
    {
        if (key_checks[index]->isChecked())
        {
            writeLog(QStringLiteral("启用 F%1：间隔=%2s，缺省=%3")
                .arg(index + 1)
                .arg(key_intervals[index]->value(), 0, 'f', 1)
                .arg(index == currentDefaultKey ? QStringLiteral("是") : QStringLiteral("否")));
        }
    }
}

QImage FxMainWindow::getGamePicture(HWND window, QRect rect)
{
    std::vector<uchar> pixelBuffer;
    QImage result;

    BITMAPINFO b;

    if ((IsWindow(window) == FALSE) || (IsIconic(window) == TRUE))
        return QImage();

    b.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    b.bmiHeader.biWidth = rect.width();
    b.bmiHeader.biHeight = rect.height();
    b.bmiHeader.biPlanes = 1;
    b.bmiHeader.biBitCount = 3 * 8;
    b.bmiHeader.biCompression = BI_RGB;
    b.bmiHeader.biSizeImage = 0;
    b.bmiHeader.biXPelsPerMeter = 0;
    b.bmiHeader.biYPelsPerMeter = 0;
    b.bmiHeader.biClrUsed = 0;
    b.bmiHeader.biClrImportant = 0;
    b.bmiColors[0].rgbBlue = 8;
    b.bmiColors[0].rgbGreen = 8;
    b.bmiColors[0].rgbRed = 8;
    b.bmiColors[0].rgbReserved = 0;

    HDC dc = GetDC(window);
    HDC cdc = CreateCompatibleDC(dc);

    HBITMAP hBitmap = CreateCompatibleBitmap(dc, rect.width(), rect.height());
    SelectObject(cdc, hBitmap);

    BitBlt(cdc, 0, 0, rect.width(), rect.height(), dc, rect.left(), rect.top(), SRCCOPY);
    pixelBuffer.resize(rect.width() * rect.height() * 4);
    GetDIBits(cdc, hBitmap, 0, rect.height(), pixelBuffer.data(), &b, DIB_RGB_COLORS);
    DeleteObject(hBitmap);

    DeleteDC(cdc);
    ReleaseDC(window, dc);

    return QImage(pixelBuffer.data(), rect.width(), rect.height(), (rect.width() * 3 + 3) & (~3), QImage::Format_RGB888).rgbSwapped().mirrored();
}

QString FxMainWindow::getConfigPath()
{
    //exe目录/config/exe文件名.json
    auto dirp = QCoreApplication::applicationDirPath();
    auto exep = QCoreApplication::applicationFilePath();

    return (dirp + "/config/%1.json").arg(exep.mid(dirp.length() + 1, exep.length() - dirp.length() - 5));
}

SConfigData FxMainWindow::readConfig(const QString& filename)
{
    QFile file;
    QJsonDocument doc;
    QJsonObject root;

    file.setFileName(filename);
    if (!file.open(QIODevice::Text | QIODevice::ReadOnly))
    {
        return SConfigData();
    }

    doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull())
    {
        return SConfigData();
    }

    return jsonToConfig(doc.object());
}

void FxMainWindow::writeConfig(const QString& filename, const SConfigData& config)
{
    QFile file;
    QJsonObject root;
    QJsonDocument doc;

    file.setFileName(filename);
    if (!file.open(QIODevice::Text | QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }

    root = configToJson(config);
    doc.setObject(root);
    file.write(doc.toJson(QJsonDocument::Indented));
}

void FxMainWindow::loadConfig()
{
    applyConfigToUI(readConfig(getConfigPath()));
}

void FxMainWindow::autoWriteConfig()
{
    writeConfig(getConfigPath(), makeConfigFromUI());
}

SConfigData FxMainWindow::makeConfigFromUI()
{
    SConfigData result;

    for (int index = 0; index < 10; ++index)
    {
        result.fxSwitch[index] = key_checks[index]->isChecked();
        result.fxCD[index] = key_intervals[index]->value();
    }

    result.globalInterval = spin_global_interval->value();
    result.defaultKey = currentDefaultKey;
    result.sendMethod = combo_send_method->currentIndex();
    result.keyHoldInterval = spin_key_hold_interval->value();

    result.hash = currentHash;
    result.title = line_title->text();

    auto rect = geometry();

    result.x = rect.x();
    result.y = rect.y();

    return result;
}

void FxMainWindow::applyConfigToUI(const SConfigData& config)
{
    for (int index = 0; index < 10; ++index)
    {
        key_checks[index]->setChecked(config.fxSwitch[index]);
        key_intervals[index]->setValue(config.fxCD[index]);
    }

    spin_global_interval->setValue(config.globalInterval);
    combo_send_method->setCurrentIndex(qBound(0, config.sendMethod, combo_send_method->count() - 1));
    spin_key_hold_interval->setValue(config.keyHoldInterval);

    currentDefaultKey = config.defaultKey;
    for (int index = 0; index < 10; ++index)
    {
        key_defaults[index]->setChecked(index == config.defaultKey);
    }

    currentHash = config.hash;
    line_title->setText(config.title);

    auto rect = geometry();

    if (config.x != -1 && config.y != -1)
    {
        setGeometry(config.x, config.y, rect.width(), rect.height());
    }
}

void FxMainWindow::applyDefaultConfigToUI()
{
    applyConfigToUI(SConfigData());
}

QJsonObject FxMainWindow::configToJson(const SConfigData& config)
{
    QJsonObject result;
    QJsonArray pressArray;
    QJsonObject supplyObject;

    for (int index = 0; index < 10; index++)
    {
        QJsonObject keyObject;
        keyObject[QStringLiteral("Enabled")] = config.fxSwitch[index];
        keyObject[QStringLiteral("Interval")] = config.fxCD[index];
        pressArray.append(keyObject);
    }
    result[QStringLiteral("AutoPress")] = pressArray;

    result["Interval"] = config.globalInterval;
    result["DefaultKey"] = config.defaultKey;
    result["SendMethod"] = config.sendMethod;
    result["KeyHoldInterval"] = config.keyHoldInterval;

    result["X"] = config.x;
    result["Y"] = config.y;

    result["Title"] = config.title;
    result["Hash"] = QString::fromUtf8(config.hash);

    return result;
}

SConfigData FxMainWindow::jsonToConfig(QJsonObject json)
{
    SConfigData result;
    QJsonArray pressArray;
    QJsonObject supplyObject;

    pressArray = json.take(QStringLiteral("AutoPress")).toArray();

    if (pressArray.size() == 10)
    {
        for (int index = 0; index < 10; index++)
        {
            QJsonObject keyObject = pressArray[index].toObject();
            result.fxSwitch[index] = keyObject.take(QStringLiteral("Enabled")).toBool(false);
            result.fxCD[index] = keyObject.take(QStringLiteral("Interval")).toDouble(1.0);
        }
    }

    result.globalInterval = json.take("Interval").toDouble(0.8);
    result.defaultKey = json.take("DefaultKey").toInt(-1);
    result.sendMethod = json.take("SendMethod").toInt(2);
    result.keyHoldInterval = json.take("KeyHoldInterval").toDouble(0.1);

    result.x = json.take("X").toInt(-1);
    result.y = json.take("Y").toInt(-1);

    result.title = json.take("Title").toString("");
    result.hash = json.take("Hash").toString("").toUtf8();

    return result;
}

QByteArray FxMainWindow::imageHash(QImage image)
{
    if (image.isNull() || image.format() != QImage::Format_RGB888)
        return QByteArray();

    QByteArray imageBytes;
    QDataStream stream(&imageBytes, QIODevice::WriteOnly);

    stream << image;

    return QCryptographicHash::hash(imageBytes, QCryptographicHash::Md5).toBase64();
}

void FxMainWindow::setupUI()
{
    auto get_h_line = []() {
        auto line = new QFrame;

        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        line->setLineWidth(1);

        return line;
    };

    auto makeHelpButton = [this](const QString& title, const QString& description) {
        auto button = new QToolButton;
        button->setText(QStringLiteral("?"));
        button->setFixedSize(18, 18);
        button->setToolTip(QStringLiteral("点击查看计算逻辑"));
        connect(button, &QToolButton::clicked, this, [this, title, description]() {
            QMessageBox::information(this, title, description);
        });
        return button;
    };

    QFont switch_font;
    switch_font.setFamily(QStringLiteral("微软雅黑"));
    switch_font.setPointSize(20);
    switch_font.setBold(true);

    QStringList supply_keys;

    for (int index = 0; index < 10; ++index)
    {
        supply_keys << QString("F%1").arg(index + 1);
    }

    auto main_widget = new QWidget;
    auto vlayout_main = new QVBoxLayout;

    btn_scan = new QPushButton(QStringLiteral("扫描游戏窗口"));
    connect(btn_scan, &QPushButton::clicked, [this]()
        {
            scanGameWindows();

            if (!gameWindows.isEmpty())
                autoSelectAndRenameGameWindow(currentHash);
        });
    vlayout_main->addWidget(btn_scan);

    combo_windows = new QComboBox;
    combo_windows->setIconSize(playerNameRect.size());
    combo_windows->setItemDelegate(new CharacterBoxDelegate);
    connect(combo_windows, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [this](int index)
        {
            check_global_switch->setChecked(false);

            if (index != -1)
            {
                currentHash = playerNameHashes[index];
            }
        });
    vlayout_main->addWidget(combo_windows);

    line_title = new QLineEdit;
    auto hlayout_title = new QHBoxLayout;
    hlayout_title->addWidget(new QLabel(QStringLiteral("窗口标题")));
    hlayout_title->addWidget(line_title, 1);
    vlayout_main->addLayout(hlayout_title);

    btn_change_title = new QPushButton(QStringLiteral("修改窗口标题"));
    connect(btn_change_title, &QPushButton::clicked, this, &FxMainWindow::changeWindowTitle);
    vlayout_main->addWidget(btn_change_title);

    btn_switch_to_window = new QPushButton(QStringLiteral("切换到游戏窗口"));
    connect(btn_switch_to_window, &QPushButton::clicked, [this]()
        {
            int window_index = combo_windows->currentIndex();

            if (window_index == -1)
            {
                return;
            }

            SetForegroundWindow(gameWindows[window_index]);
        });
    vlayout_main->addWidget(btn_switch_to_window);

    auto hlayout_send_method = new QHBoxLayout;
    combo_send_method = new QComboBox;
    combo_send_method->addItem(QStringLiteral("键盘+自动窗口"), 4);
    combo_send_method->addItem(QStringLiteral("键盘+手动窗口"), 0);
    combo_send_method->addItem(QStringLiteral("按键消息"), 8);

    // 以下实验方式保留实现，仅从界面下拉框隐藏，后续需要时可直接恢复。
    // combo_send_method->addItem(QStringLiteral("SendInput / 扫描码 / 手动保持前台"), 1);
    // combo_send_method->addItem(QStringLiteral("keybd_event / VK / 手动保持前台"), 2);
    // combo_send_method->addItem(QStringLiteral("keybd_event / 扫描码 / 手动保持前台"), 3);
    // combo_send_method->addItem(QStringLiteral("SendInput / 扫描码 / 自动切换前台"), 5);
    // combo_send_method->addItem(QStringLiteral("keybd_event / VK / 自动切换前台"), 6);
    // combo_send_method->addItem(QStringLiteral("keybd_event / 扫描码 / 自动切换前台"), 7);
    // combo_send_method->addItem(QStringLiteral("旧版：右键按下 + KEYUP"), 9);
    // combo_send_method->addItem(QStringLiteral("完整右键点击 + KEYUP"), 10);
    // combo_send_method->addItem(QStringLiteral("首个子窗口 / 仅 KEYUP"), 11);
    // combo_send_method->addItem(QStringLiteral("首个子窗口 / 右键按下 + KEYUP"), 12);
    // combo_send_method->addItem(QStringLiteral("SendNotifyMessageA / 仅 KEYUP"), 13);
    combo_send_method->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo_send_method->setMinimumContentsLength(0);
    combo_send_method->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(combo_send_method, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        [this](int) {
            writeLog(QStringLiteral("切换按键发送方式：%1").arg(currentSendMethodName()));
        });
    hlayout_send_method->addWidget(new QLabel(QStringLiteral("按键方式")));
    hlayout_send_method->addWidget(combo_send_method, 1);
    vlayout_main->addLayout(hlayout_send_method);

    spin_key_hold_interval = new QDoubleSpinBox;
    spin_key_hold_interval->setSuffix(QStringLiteral(" s"));
    spin_key_hold_interval->setDecimals(2);
    spin_key_hold_interval->setMinimum(0.01);
    spin_key_hold_interval->setMaximum(5.0);
    spin_key_hold_interval->setSingleStep(0.01);
    spin_key_hold_interval->setValue(0.1);
    auto hlayout_key_hold = new QHBoxLayout;
    hlayout_key_hold->addWidget(new QLabel(QStringLiteral("释放间隔")));
    hlayout_key_hold->addWidget(makeHelpButton(
        QStringLiteral("释放间隔计算逻辑"),
        QStringLiteral("键盘方式会先发送按下，再等待该时间，最后发送释放。\n\n"
                       "计算：释放时间 = 按下时间 + 释放间隔。\n\n"
                       "“按键消息”方式只发送旧版的 KEYUP 消息，因此不使用此间隔。")));
    hlayout_key_hold->addWidget(spin_key_hold_interval);
    hlayout_key_hold->addStretch();
    vlayout_main->addLayout(hlayout_key_hold);

    vlayout_main->addWidget(get_h_line());

    check_global_switch = new QCheckBox(QStringLiteral("全局开关"));
    check_global_switch->setFont(switch_font);
    connect(check_global_switch, &QCheckBox::toggled, [this](bool checked)
        {
            if (checked)
            {
                defaultKeyTriggered = false;
                resetAllTimeStamps();

                const int windowIndex = combo_windows->currentIndex();
                if (windowIndex == -1)
                    writeLog(QStringLiteral("全局开关已开启，但尚未选择有效游戏窗口"));
                else
                    writeLog(QStringLiteral("全局开关已开启：handle=0x%1, method=%2")
                        .arg(reinterpret_cast<quintptr>(gameWindows[windowIndex]), 0, 16)
                        .arg(currentSendMethodName()));
            }
            else
                writeLog(QStringLiteral("全局开关已关闭"));
        });
    auto hlayout_switch = new QHBoxLayout;
    hlayout_switch->addStretch();
    hlayout_switch->addWidget(check_global_switch);
    hlayout_switch->addStretch();
    vlayout_main->addLayout(hlayout_switch);

    spin_global_interval = new QDoubleSpinBox;
    spin_global_interval->setSuffix(" s");
    spin_global_interval->setDecimals(2);
    spin_global_interval->setMinimum(0.1);
    spin_global_interval->setMaximum(365.0);
    spin_global_interval->setSingleStep(0.01);
    spin_global_interval->setValue(0.1);
    auto hlayout_press_interval = new QHBoxLayout;
    hlayout_press_interval->addStretch();
    hlayout_press_interval->addWidget(new QLabel(QStringLiteral("全局间隔")));
    hlayout_press_interval->addWidget(makeHelpButton(
        QStringLiteral("全局间隔计算逻辑"),
        QStringLiteral("表示任意两个按键触发之间的最短等待时间。\n\n"
                       "允许下一个按键的时间 = 上一个任意按键的触发时间 + 全局间隔。\n\n"
                       "如果多个按键同时到期，每次只触发一个；等待全局间隔后，再按公平轮询顺序选择下一个。")));
    hlayout_press_interval->addWidget(spin_global_interval);
    hlayout_press_interval->addStretch();
    vlayout_main->addLayout(hlayout_press_interval);
    vlayout_main->addWidget(get_h_line());

    auto gridlayout_keys = new QGridLayout;
    gridlayout_keys->addWidget(new QLabel(QStringLiteral("启用")), 0, 0);
    auto keyIntervalHeader = new QWidget;
    auto keyIntervalHeaderLayout = new QHBoxLayout(keyIntervalHeader);
    keyIntervalHeaderLayout->setContentsMargins(0, 0, 0, 0);
    keyIntervalHeaderLayout->setSpacing(2);
    keyIntervalHeaderLayout->addWidget(new QLabel(QStringLiteral("间隔")));
    keyIntervalHeaderLayout->addWidget(makeHelpButton(
        QStringLiteral("单键间隔计算逻辑"),
        QStringLiteral("表示同一个按键两次触发之间的最短时间。\n\n"
                       "按键可触发条件：单键间隔已到，并且全局间隔也已到。\n\n"
                       "启用多个按键时还需要排队，所以实际周期可能大于这里设置的时间。缺省技能只在开启全局开关时触发一次。")));
    gridlayout_keys->addWidget(keyIntervalHeader, 0, 1);
    gridlayout_keys->addWidget(new QLabel(QStringLiteral("缺省")), 0, 2);

    for (int index = 0; index < 10; ++index)
    {
        auto check_key = new QCheckBox(QString("F%1").arg(index + 1));
        auto spin_key_interval = new QDoubleSpinBox;
        auto check_default = new QCheckBox;

        spin_key_interval->setSuffix(" s");
        spin_key_interval->setDecimals(1);
        spin_key_interval->setMinimum(0.1);
        spin_key_interval->setMaximum(365.0);
        spin_key_interval->setSingleStep(0.1);
        spin_key_interval->setValue(1.0);
        key_checks[index] = check_key;
        key_intervals[index] = spin_key_interval;
        key_defaults[index] = check_default;

        connect(check_key, &QCheckBox::toggled,
            [this, index](bool checked) {
                key_intervals[index]->setEnabled(!checked);
                resetTimeStamp(index);
            });

        connect(check_default, &QCheckBox::toggled,
            [this, index](bool checked) {
                //模拟QButtonGroup互斥，并能够全部取消选择
                if (checked)
                {
                    currentDefaultKey = index;

                    for (int key_index = 0; key_index < 10; ++key_index)
                    {
                        if (key_index != index)
                            key_defaults[key_index]->setChecked(false);
                    }
                }
                else
                {
                    currentDefaultKey = -1;
                }
            });

        gridlayout_keys->addWidget(check_key, index + 1, 0);
        gridlayout_keys->addWidget(spin_key_interval, index + 1, 1);
        gridlayout_keys->addWidget(check_default, index + 1, 2);
    }

    vlayout_main->addLayout(gridlayout_keys);

    btn_show_log = new QPushButton(QStringLiteral("查看调试日志"));
    connect(btn_show_log, &QPushButton::clicked, this, &FxMainWindow::showLogWindow);
    vlayout_main->addWidget(btn_show_log);

    main_widget->setLayout(vlayout_main);
    this->setCentralWidget(main_widget);
    // 高度固定；宽度默认及最大为 200px，并允许用户向更窄方向手动调节。
    this->setMinimumWidth(160);
    this->setMaximumWidth(200);
    this->setFixedHeight(minimumSizeHint().height());
    this->resize(200, height());
}
