QT += core network
QT -= gui
CONFIG += c++11

TARGET = fv2db
VERSION = 1.0.0.0

CONFIG += console
CONFIG -= app_bundle

DEFINES += APP_DESIGNER=\\\"Alex.A.Taranov\\\" \
           APP_NAME=\\\"$${TARGET}\\\"         \
           APP_VERSION=\\\"$${VERSION}\\\"

TEMPLATE = app

SOURCES += main.cpp \
    qvideocapture.cpp \
    qvideolocker.cpp \
    qslackclient.cpp \
    qslackimageposter.cpp \
    qfacerecognizer.cpp \
    qidentificationtaskposter.cpp

HEADERS += \
    qvideocapture.h \
    qvideolocker.h \
    qslackclient.h \
    qslackimageposter.h \
    qfacerecognizer.h \
    qidentificationtaskposter.h

include( $${PWD}/../../Sharedfiles/opencv.pri )
include( $${PWD}/../../Sharedfiles/opencl.pri )
include( $${PWD}/../../Sharedfiles/openmp.pri )
include( $${PWD}/../../Sharedfiles/dlib.pri )
include( $${PWD}/../../Src/multyfacetracker/facetracker.pri )




