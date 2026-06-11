QT += core gui widgets bluetooth charts
greaterThan(QT_MAJOR_VERSION, 5): QT += core

CONFIG += c++17
TEMPLATE = app
TARGET = AccuCheckGlukoMeter

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    bleclient.cpp \
    blescanner.cpp \
    glucosechart.cpp

HEADERS += \
    mainwindow.h \
    bleclient.h \
    blescanner.h \
    glucosechart.h \
    glucosereading.h

# macOS needs a Bluetooth usage description in the app bundle.
macx {
    QMAKE_INFO_PLIST = Info.plist
}

# Linux (BlueZ) and Windows (WinRT) backends are provided by Qt Bluetooth;
# no extra libraries are required.
