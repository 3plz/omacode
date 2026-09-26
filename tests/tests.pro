QT += core gui quick testlib
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_omawrite

INCLUDEPATH += ../src
SOURCES += \
    tst_omawrite.cpp \
    ../src/backend.cpp \
    ../src/linenumbergutter.cpp \
    ../src/lspclient.cpp \
    ../src/markdownhighlighter.cpp \
    ../src/terminal.cpp
HEADERS += \
    ../src/backend.h \
    ../src/linenumbergutter.h \
    ../src/lspclient.h \
    ../src/markdownhighlighter.h \
    ../src/terminal.h

QT += widgets printsupport quickcontrols2 quickdialogs2 dbus
