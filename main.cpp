#include "fxmainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QFont fnt(QStringLiteral("微软雅黑"),10);
    QApplication a(argc, argv);
    QApplication::setFont(fnt);
    FxMainWindow w;

    w.show();
    return a.exec();
}
