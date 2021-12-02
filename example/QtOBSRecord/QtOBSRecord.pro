#-------------------------------------------------
#
# Project created by QtCreator 2020-05-28T11:24:26
#
#-------------------------------------------------

QT       += core gui winextras network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG   += c++11

TARGET = QtOBSRecord
TEMPLATE = app

INCLUDEPATH += $$PWD/obs-studio/libobs
INCLUDEPATH += $$PWD/obs-studio/dependencies2015/win32/include
LIBS += $$PWD/obs-studio/build/lib/obs.lib


SOURCES += main.cpp\
        dialog.cpp \
    obs-wrapper.cpp

HEADERS  += dialog.h \
    obs-wrapper.h

FORMS    += dialog.ui
