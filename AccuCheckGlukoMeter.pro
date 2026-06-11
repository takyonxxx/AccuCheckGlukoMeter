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
    mainwindow.cpp

HEADERS += \
    blescanner.h \
    bleclient.h \
    mainwindow.h

# --- Optional: keep shadow-build outputs tidy -------------------------------
# Uncomment if you want the binary in a fixed folder next to the .pro instead
# of deep inside the shadow-build tree.
# DESTDIR = $$PWD/bin
# OBJECTS_DIR = $$PWD/build/obj
# MOC_DIR     = $$PWD/build/moc
# RCC_DIR     = $$PWD/build/rcc
# UI_DIR      = $$PWD/build/ui
