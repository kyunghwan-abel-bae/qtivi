/****************************************************************************
**
** Copyright (C) 2019 Luxoft Sweden AB
** Copyright (C) 2018 Pelagicore AG
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtIvi module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL-QTAS$
** Commercial License Usage
** Licensees holding valid commercial Qt Automotive Suite licenses may use
** this file in accordance with the commercial license agreement provided
** with the Software or, alternatively, in accordance with the terms
** contained in a written agreement between you and The Qt Company.  For
** licensing terms and conditions see https://www.qt.io/terms-conditions.
** For further information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
** SPDX-License-Identifier: LGPL-3.0
**
****************************************************************************/

#include "qtivicoremodule.h"
#include "qivipendingreply.h"
#include "qiviabstractfeature.h"
#include "qiviabstractzonedfeature.h"
#include "qiviservicemanager.h"
#include "qiviserviceobject.h"
#include "qivipagingmodel.h"
#include "qivisearchandbrowsemodel.h"

#include <QQmlEngine>

/*! \internal */
QObject* qtivicoremodule_singletontype_provider(QQmlEngine*, QJSEngine*)
{
    return new QtIviCoreModule();
}

QObject* serviceManagerSingelton(QQmlEngine *, QJSEngine *)
{
    auto manager = QIviServiceManager::instance();
    QQmlEngine::setObjectOwnership(manager, QQmlEngine::CppOwnership);
    return manager;
}

/*!
    \class QtIviCoreModule
    \inmodule QtIviCore

    \brief The QtIviCoreModule class holds enums which are used by multiple classes of QtIviCore
    and provides convenience functions to register types to QML
*/

/*!
    \qmltype QtIvi
    \instantiates QtIviCoreModule
    \inqmlmodule QtIvi

    \brief The QtIvi singleton holds enums, which are used by multiple Items of the QtIviCore module.

    The following enums are exported from this object:

    \section3 ModelCapability
    \value NoExtras
           The backend does only support the minimum feature set and is stateful.
    \value SupportsGetSize
           The backend can return the final number of items for a specific request. This makes it possible to support the QIviSearchAndBrowseModel::DataChanged loading
           type.
    \value SupportsFiltering
           The backend supports filtering of the content. QIviSearchAndBrowseModelInterface::availableContentTypesChanged() and QIviSearchAndBrowseModelInterface::queryIdentifiersChanged() will be used as input for the
           \l {Qt IVI Query Language}.
    \value SupportsSorting
           The backend supports sorting of the content. QIviSearchAndBrowseModelInterface::availableContentTypesChanged() and QIviSearchAndBrowseModelInterface::queryIdentifiersChanged() will be used as input for the
           \l {Qt IVI Query Language}.
    \value SupportsAndConjunction
           The backend supports handling multiple filters at the same time and these filters can be combined by using the AND conjunction.
    \value SupportsOrConjunction
           The backend supports handling multiple filters at the same time and these filters can be combined by using the OR conjunction.
    \value SupportsStatelessNavigation
           The backend is stateless and supports handling multiple instances of a QIviSearchAndBrowseModel requesting different data at the same time.
           E.g. One request for artists, sorted by name and another request for tracks. The backend has to consider that both request come from models which are
           currently visible at the same time.
    \value SupportsInsert
           The backend supports inserting new items at a given position.
    \value SupportsMove
           The backend supports moving items within the model.
    \value SupportsRemove
           The backend supports removing items from the model.
*/
QtIviCoreModule::QtIviCoreModule(QObject *parent)
    : QObject(parent)
{
}


/*!
    \enum QtIviCoreModule::ModelCapability
    \value NoExtras
           The backend does only support the minimum feature set and is stateful.
    \value SupportsGetSize
           The backend can return the final number of items for a specific request. This makes it possible to support the QIviSearchAndBrowseModel::DataChanged loading
           type.
    \value SupportsFiltering
           The backend supports filtering of the content. QIviSearchAndBrowseModelInterface::availableContentTypesChanged() and QIviSearchAndBrowseModelInterface::queryIdentifiersChanged() will be used as input for the
           \l {Qt IVI Query Language}.
    \value SupportsSorting
           The backend supports sorting of the content. QIviSearchAndBrowseModelInterface::availableContentTypesChanged() and QIviSearchAndBrowseModelInterface::queryIdentifiersChanged() will be used as input for the
           \l {Qt IVI Query Language}.
    \value SupportsAndConjunction
           The backend supports handling multiple filters at the same time and these filters can be combined by using the AND conjunction.
    \value SupportsOrConjunction
           The backend supports handling multiple filters at the same time and these filters can be combined by using the OR conjunction.
    \value SupportsStatelessNavigation
           The backend is stateless and supports handling multiple instances of a QIviSearchAndBrowseModel requesting different data at the same time.
           E.g. One request for artists, sorted by name and another request for tracks. The backend has to consider that both request come from models which are
           currently visible at the same time.
    \value SupportsInsert
           The backend supports inserting new items at a given position.
    \value SupportsMove
           The backend supports moving items within the model.
    \value SupportsRemove
           The backend supports removing items from the model.
*/

/*!
    Registers all enums in this class in the Qt MetaType system
*/
void QtIviCoreModule::registerTypes()
{
    qRegisterMetaType<QIviServiceObject*>();
    qRegisterMetaType<QList<QIviServiceObject*>>("QList<QIviServiceObject*>");
    qRegisterMetaType<QtIviCoreModule::ModelCapabilities>();
    qIviRegisterPendingReplyType<QtIviCoreModule::ModelCapabilities>();
}

/*!
    Registers the QtIviCore classes to the QML System in the library imported from \a uri having
    the version number composed from \a majorVersion and \a minorVersion.
*/
void QtIviCoreModule::registerQmlTypes(const QString &uri, int majorVersion, int minorVersion)
{
    registerTypes();
    QByteArray u = uri.toLatin1();
    qmlRegisterSingletonType<QtIviCoreModule>(u, majorVersion, minorVersion,
                                        "QtIviCoreModule",
                                        qtivicoremodule_singletontype_provider);
    qmlRegisterUncreatableType<QIviAbstractFeature>(u, 1, 0, "AbstractFeature", QStringLiteral("AbstractFeature is not accessible directly"));
    qmlRegisterUncreatableType<QIviAbstractZonedFeature>(u, 1, 0, "AbstractZonedFeature", QStringLiteral("AbstractZonedFeature is not accessible directly"));
    qmlRegisterType<QIviPagingModel>(u, 1, 0, "PagingModel");
    qmlRegisterType<QIviSearchAndBrowseModel>(u, 1, 0, "SearchAndBrowseModel");
    qmlRegisterSingletonType<QIviServiceManager>(u, 1, 0, "ServiceManager", &serviceManagerSingelton);
}
