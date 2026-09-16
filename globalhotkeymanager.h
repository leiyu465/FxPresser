#ifndef GLOBALHOTKEYMANAGER_H
#define GLOBALHOTKEYMANAGER_H

#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QObject>

class GlobalHotkeyManager : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit GlobalHotkeyManager(QObject* parent = nullptr);
    ~GlobalHotkeyManager() override;

    bool setShortcut(const QKeySequence& shortcut, QString* errorMessage = nullptr);
    QKeySequence shortcut() const;

    bool nativeEventFilter(const QByteArray& eventType, void* message,
        long* result) override;

signals:
    void activated();

private:
    bool registerSequence(const QKeySequence& shortcut);
    static bool toNativeShortcut(const QKeySequence& shortcut,
        unsigned int* modifiers, unsigned int* virtualKey);

    static const int HotkeyId = 0x4658;
    QKeySequence registeredShortcut;
    bool registered = false;
};

#endif // GLOBALHOTKEYMANAGER_H
