TEMPLATE = app
TARGET = server
QT += core network sql
CONFIG += c++11 console
CONFIG -= app_bundle
SOURCES += src/main.cpp \
           src/databasemanager.cpp \
           src/dataforwarder.cpp \
           src/roomhub.cpp
HEADERS += src/roomhub.h \
    src/clientctx.h \
    src/databasemanager.h \
    src/dataforwarder.h
include(../common/common.pri)
