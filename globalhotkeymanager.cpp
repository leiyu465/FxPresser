#include "globalhotkeymanager.h"

#include <QApplication>
#include <windows.h>

GlobalHotkeyManager::GlobalHotkeyManager(QObject* parent)
    : QObject(parent)
{
    qApp->installNativeEventFilter(this);
}

GlobalHotkeyManager::~GlobalHotkeyManager()
{
    if (registered)
        UnregisterHotKey(nullptr, HotkeyId);
    qApp->removeNativeEventFilter(this);
}

bool GlobalHotkeyManager::setShortcut(const QKeySequence& shortcut,
    QString* errorMessage)
{
    if (shortcut == registeredShortcut && registered)
        return true;

    const QKeySequence previous = registeredShortcut;
    const bool wasRegistered = registered;
    if (registered)
    {
        UnregisterHotKey(nullptr, HotkeyId);
        registered = false;
    }

    if (shortcut.isEmpty())
    {
        registeredShortcut = QKeySequence();
        return true;
    }

    if (registerSequence(shortcut))
    {
        registeredShortcut = shortcut;
        registered = true;
        return true;
    }

    if (wasRegistered && registerSequence(previous))
    {
        registeredShortcut = previous;
        registered = true;
    }
    if (errorMessage)
        *errorMessage = QStringLiteral("快捷键注册失败，可能已被其他程序占用。");
    return false;
}

QKeySequence GlobalHotkeyManager::shortcut() const
{
    return registeredShortcut;
}

bool GlobalHotkeyManager::nativeEventFilter(const QByteArray&, void* rawMessage,
    long*)
{
    MSG* message = static_cast<MSG*>(rawMessage);
    if (message && message->message == WM_HOTKEY &&
        static_cast<int>(message->wParam) == HotkeyId)
    {
        emit activated();
        return true;
    }
    return false;
}

bool GlobalHotkeyManager::registerSequence(const QKeySequence& shortcut)
{
    UINT modifiers = 0;
    UINT virtualKey = 0;
    return toNativeShortcut(shortcut, &modifiers, &virtualKey) &&
        RegisterHotKey(nullptr, HotkeyId, modifiers | MOD_NOREPEAT, virtualKey) != FALSE;
}

bool GlobalHotkeyManager::toNativeShortcut(const QKeySequence& shortcut,
    UINT* modifiers, UINT* virtualKey)
{
    if (shortcut.count() != 1)
        return false;

    const int combination = shortcut[0];
    if (combination & Qt::CTRL)
        *modifiers |= MOD_CONTROL;
    if (combination & Qt::ALT)
        *modifiers |= MOD_ALT;
    if (combination & Qt::SHIFT)
        *modifiers |= MOD_SHIFT;
    if (combination & Qt::META)
        *modifiers |= MOD_WIN;

    const int key = combination & ~Qt::KeyboardModifierMask;
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        *virtualKey = static_cast<UINT>('A' + key - Qt::Key_A);
    else if (key >= Qt::Key_0 && key <= Qt::Key_9)
        *virtualKey = static_cast<UINT>('0' + key - Qt::Key_0);
    else if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        *virtualKey = static_cast<UINT>(VK_F1 + key - Qt::Key_F1);
    else
        return false;
    return *modifiers != 0;
}
