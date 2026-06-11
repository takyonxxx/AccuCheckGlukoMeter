QT += core gui widgets bluetooth

CONFIG += c++17

TEMPLATE = app
TARGET = AccuCheckGlukoMeter

# Show a console window alongside the GUI during development so qDebug
# output is visible. Remove this line for a clean GUI-only release build.
CONFIG += console

SOURCES += \
    main.cpp \
    blescanner.cpp \
    bleclient.cpp \
    winpairing.cpp \
    mainwindow.cpp

HEADERS += \
    blescanner.h \
    bleclient.h \
    winpairing.h \
    mainwindow.h

# --- C++/WinRT (for native PIN pairing on Windows) --------------------------
# Adjust the SDK version below to one installed on your machine. Find it under
#   C:\Program Files (x86)\Windows Kits\10\Include\
win32 {
    INCLUDEPATH += "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/cppwinrt"
    # C++/WinRT base needs these OS import libs in a classic desktop app.
    LIBS += -lole32 -loleaut32 -lruntimeobject
}

# --- Optional: keep shadow-build outputs tidy -------------------------------
# Uncomment if you want the binary in a fixed folder next to the .pro instead
# of deep inside the shadow-build tree.
# DESTDIR = $$PWD/bin
# OBJECTS_DIR = $$PWD/build/obj
# MOC_DIR     = $$PWD/build/moc
# RCC_DIR     = $$PWD/build/rcc
# UI_DIR      = $$PWD/build/ui
