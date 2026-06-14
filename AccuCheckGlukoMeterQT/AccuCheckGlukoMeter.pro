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
    glucosechart.cpp \
    healthstatusdialog.cpp \
    winpairing.cpp

HEADERS += \
    mainwindow.h \
    bleclient.h \
    blescanner.h \
    glucosechart.h \
    glucosereading.h \
    healthadvisor.h \
    healthstatusdialog.h \
    winpairing.h

# macOS needs a Bluetooth usage description in the app bundle.
macx {
    QMAKE_INFO_PLIST = Info.plist
}

# Windows: WinRT custom pairing (manual PIN). Requires the Windows SDK C++/WinRT
# headers. If the build can't find <winrt/...>, set WINSDK_VER to your installed
# SDK version (see: C:\Program Files (x86)\Windows Kits\10\Include).
win32 {
    LIBS += -lwindowsapp -lole32 -loleaut32 -lruntimeobject

    WINSDK_VER = 10.0.22621.0
    INCLUDEPATH += "C:/Program Files (x86)/Windows Kits/10/Include/$$WINSDK_VER/cppwinrt"
}

# Linux (BlueZ) and macOS handle pairing through the OS; winpairing.cpp compiles
# to a no-op there.
