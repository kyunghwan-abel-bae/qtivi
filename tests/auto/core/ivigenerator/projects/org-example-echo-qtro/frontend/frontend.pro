TEMPLATE=lib
TARGET= $$qtLibraryTarget(echo_frontend)
DESTDIR = ../

CONFIG += ivigenerator

QT += ivicore ivicore-private qml quick

DEFINES += QT_BUILD_ECHO_LIB

macos: QMAKE_SONAME_PREFIX = @rpath

QFACE_SOURCES = ../../../org.example.echo.qtro.qface