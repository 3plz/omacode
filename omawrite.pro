QT += core gui widgets printsupport qml quick quickcontrols2 quickdialogs2 dbus

CONFIG += c++17 release
TARGET = omawrite
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/linenumbergutter.h \
    src/lspclient.h \
    src/markdownhighlighter.h \
    src/systemtheme.h \
    src/terminal.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/linenumbergutter.cpp \
    src/lspclient.cpp \
    src/markdownhighlighter.cpp \
    src/systemtheme.cpp \
    src/terminal.cpp

RESOURCES += src/resources.qrc
