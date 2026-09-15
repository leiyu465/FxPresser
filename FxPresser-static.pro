QT += core gui widgets

CONFIG += release static
CONFIG -= debug debug_and_release

isEmpty(FX_VERSION) {
    FX_VERSION = 2.0
}
isEmpty(FX_BUILD_DATE) {
    FX_BUILD_DATE = local
}
TARGET = FxPresser-v$${FX_VERSION}-$${FX_BUILD_DATE}
TEMPLATE = app

SOURCES += \
    main.cpp \
    fxmainwindow.cpp \
    sharedinputworker.cpp

HEADERS += fxmainwindow.h sharedinputworker.h
RC_FILE = icon.rc

# 生成无需 Qt/MinGW 运行库 DLL 的 32 位 Windows 单文件程序。
QMAKE_LFLAGS_RELEASE += -static -static-libgcc -static-libstdc++ -s
