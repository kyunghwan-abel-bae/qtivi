TEMPLATE=lib
TARGET = $$qtLibraryTarget(example_ivi_addressbook)

QT_FOR_CONFIG += ivicore
!qtConfig(ivigenerator): error("No ivigenerator available")

LIBS += -L$$OUT_PWD/../ -l$$qtLibraryTarget(QtIviAdressBookExample)
DESTDIR = ../qtivi
CONFIG += warn_off
INCLUDEPATH += $$OUT_PWD/../frontend
QT += core ivicore
CONFIG += ivigenerator plugin

QFACE_FORMAT = backend_simulator
QFACE_SOURCES = ../example-ivi-addressbook.qface
PLUGIN_TYPE = qtivi
PLUGIN_CLASS_NAME = AddressBookPlugin

CONFIG += install_ok  # Do not cargo-cult this!
target.path = $$[QT_INSTALL_EXAMPLES]/ivicore/qface-ivi-addressbook/qtivi/
INSTALLS += target

#! [0]
RESOURCES += plugin_resource.qrc
#! [0]

QML_IMPORT_PATH = $$OUT_PWD/qml
