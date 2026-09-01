QT += core gui widgets

CONFIG += release static
CONFIG -= debug debug_and_release

TARGET = FxPresser
TEMPLATE = app

SOURCES += \
    main.cpp \
    fxmainwindow.cpp

HEADERS += fxmainwindow.h
RC_FILE = icon.rc

# 生成无需 Qt/MinGW 运行库 DLL 的 32 位 Windows 单文件程序。
QMAKE_LFLAGS_RELEASE += -static -static-libgcc -static-libstdc++ -s
