QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    ctrlbar.cpp \
    displaywind.cpp \
    ff_ffplay.cpp \
    ff_ffplay_def.cpp \
    ffmsg_queue.cpp \
    ijkmediaplayer.cpp \
    imagescaler.cpp \
    main.cpp \
    mainwind.cpp \
    playlistwind.cpp \
    titlebar.cpp

HEADERS += \
    ctrlbar.h \
    displaywind.h \
    ff_ffplay.h \
    ff_ffplay_def.h \
    ffmsg.h \
    ffmsg_queue.h \
    ijkmediaplayer.h \
    imagescaler.h \
    mainwind.h \
    playlistwind.h \
    titlebar.h

FORMS += \
    ctrlbar.ui \
    displaywind.ui \
    mainwind.ui \
    playlistwind.ui \
    titlebar.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    icon.qrc

win32{
INCLUDEPATH += $$PWD/ffmpeg-4.2.1-win32-dev/include
INCLUDEPATH += $$PWD/SDL2/include

LIBS += $$PWD/ffmpeg-4.2.1-win32-dev/lib/avcodec.lib \
        $$PWD/ffmpeg-4.2.1-win32-dev/lib/avdevice.lib \
        $$PWD/ffmpeg-4.2.1-win32-dev/lib/avfilter.lib \
        $$PWD/ffmpeg-4.2.1-win32-dev/lib/avformat.lib \
        $$PWD/ffmpeg-4.2.1-win32-dev/lib/avutil.lib \
        $$PWD/ffmpeg-4.2.1-win32-dev/lib/postproc.lib \
        $$PWD/ffmpeg-4.2.1-win32-dev/lib/swresample.lib \
        $$PWD/ffmpeg-4.2.1-win32-dev/lib/swscale.lib \
        $$PWD/SDL2/lib/x86/SDL2.lib \
        "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.20348.0/um/x86/Ole32.Lib"
}
