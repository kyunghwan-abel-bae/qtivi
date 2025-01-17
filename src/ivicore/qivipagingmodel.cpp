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

#include "qivipagingmodel.h"
#include "qivipagingmodel_p.h"

#include "qivipagingmodelinterface.h"
#include "qiviqmlconversion_helper.h"

#include <QDebug>
#include <QMetaObject>

QT_BEGIN_NAMESPACE

QIviPagingModelPrivate::QIviPagingModelPrivate(const QString &interface, QIviPagingModel *model)
    : QIviAbstractFeatureListModelPrivate(interface, model)
    , q_ptr(model)
    , m_capabilities(QtIviCoreModule::NoExtras)
    , m_chunkSize(30)
    , m_moreAvailable(false)
    , m_identifier(QUuid::createUuid())
    , m_fetchMoreThreshold(10)
    , m_fetchedDataCount(0)
    , m_loadingType(QIviPagingModel::FetchMore)
{
    QtIviCoreModule::registerTypes();
    qRegisterMetaType<QIviPagingModel::LoadingType>();
    qRegisterMetaType<QIviStandardItem>();
    qRegisterMetaType<QIviStandardItem>("QIviSearchAndBrowseModelItem");
}

QIviPagingModelPrivate::~QIviPagingModelPrivate()
{
}

void QIviPagingModelPrivate::initialize()
{
    QIviAbstractFeatureListModelPrivate::initialize();

    Q_Q(QIviPagingModel);
    q->setDiscoveryMode(QIviAbstractFeature::NoAutoDiscovery);

    QObject::connect(q, &QAbstractListModel::rowsInserted,
                     q, &QIviPagingModel::countChanged);
    QObject::connect(q, &QAbstractListModel::rowsRemoved,
                     q, &QIviPagingModel::countChanged);
    QObject::connect(q, &QAbstractListModel::modelReset,
                     q, &QIviPagingModel::countChanged);
    QObjectPrivate::connect(q, &QIviPagingModel::fetchMoreThresholdReached,
                            this, &QIviPagingModelPrivate::onFetchMoreThresholdReached);
}

void QIviPagingModelPrivate::onInitializationDone()
{
    Q_Q(QIviPagingModel);

    if (q->isInitialized())
        return;

    //Register this instance with the backend. The backend can initialize the internal structure now
    backend()->registerInstance(m_identifier);

    resetModel();
}

void QIviPagingModelPrivate::onCapabilitiesChanged(const QUuid &identifier, QtIviCoreModule::ModelCapabilities capabilities)
{
    if (!identifier.isNull() && identifier != m_identifier)
        return;

    if (m_capabilities == capabilities)
        return;

    Q_Q(QIviPagingModel);
    m_capabilities = capabilities;
    emit q->capabilitiesChanged(capabilities);
}

void QIviPagingModelPrivate::onDataFetched(const QUuid &identifier, const QList<QVariant> &items, int start, bool moreAvailable)
{
    if (!identifier.isNull() && (!items.count() || identifier != m_identifier))
        return;

    Q_ASSERT(items.count() <= m_chunkSize);
    Q_ASSERT((start + items.count() - 1) / m_chunkSize == start / m_chunkSize);

    Q_Q(QIviPagingModel);
    m_moreAvailable = moreAvailable;

    if (m_loadingType == QIviPagingModel::FetchMore) {
        q->beginInsertRows(QModelIndex(), m_itemList.count(), m_itemList.count() + items.count() -1);
        m_itemList += items;
        m_fetchedDataCount = m_itemList.count();
        q->endInsertRows();
    } else {
        const int newSize = start + items.count();
        if (m_itemList.count() <  newSize || m_availableChunks.count() < newSize / m_chunkSize) {
            qWarning() << "countChanged signal needs to be emitted before the dataFetched signal";
            return;
        }

        m_fetchedDataCount = start + items.count();

        for (int i = 0; i < items.count(); i++)
            m_itemList.replace(start + i, items.at(i));

        m_availableChunks.setBit(start / m_chunkSize);

        emit q->dataChanged(q->index(start), q->index(start + items.count() -1));
    }
}

void QIviPagingModelPrivate::onCountChanged(const QUuid &identifier, int new_length)
{
    if (!identifier.isNull() && (identifier != m_identifier || m_loadingType != QIviPagingModel::DataChanged || m_itemList.count() == new_length))
        return;

    Q_Q(QIviPagingModel);
    q->beginInsertRows(QModelIndex(), m_itemList.count(), m_itemList.count() + new_length -1);
    for (int i = 0; i < new_length; i++)
        m_itemList.append(QVariant());
    q->endInsertRows();

    m_availableChunks.resize(new_length / m_chunkSize + 1);
}

void QIviPagingModelPrivate::onDataChanged(const QUuid &identifier, const QList<QVariant> &data, int start, int count)
{
    if (!identifier.isNull() && identifier != m_identifier)
        return;

    if (start < 0 || start > m_itemList.count()) {
        qWarning("provided start argument is out of range");
        return;
    }

    if (count < 0 || count > m_itemList.count() - start) {
        qWarning("provided count argument is out of range");
        return;
    }

    Q_Q(QIviPagingModel);

    //delta > 0 insert rows
    //delta < 0 remove rows
    int delta = data.count() - count;
    //find data overlap for updates
    int updateCount = qMin(data.count(), count);
    int updateCountEnd = updateCount > 0 ? updateCount + 1 : 0;
    //range which is either added or removed
    int insertRemoveStart = start + updateCountEnd;
    int insertRemoveCount = qMax(data.count(), count) - updateCount;

    if (updateCount > 0) {
        for (int i = start, j=0; j < updateCount; i++, j++)
            m_itemList.replace(i, data.at(j));
        emit q->dataChanged(q->index(start), q->index(start + updateCount -1));
    }

    if (delta < 0) { //Remove
        q->beginRemoveRows(QModelIndex(), insertRemoveStart, insertRemoveStart + insertRemoveCount -1);
        for (int i = insertRemoveStart; i < insertRemoveStart + insertRemoveCount; i++)
            m_itemList.removeAt(i);
        q->endRemoveRows();
    } else if (delta > 0) { //Insert
        q->beginInsertRows(QModelIndex(), insertRemoveStart, insertRemoveStart + insertRemoveCount -1);
        for (int i = insertRemoveStart, j = updateCountEnd; i < insertRemoveStart + insertRemoveCount; i++, j++)
            m_itemList.insert(i, data.at(j));
        q->endInsertRows();
    }
}

void QIviPagingModelPrivate::onFetchMoreThresholdReached()
{
    Q_Q(QIviPagingModel);
    q->fetchMore(QModelIndex());
}

void QIviPagingModelPrivate::resetModel()
{
    Q_Q(QIviPagingModel);

    q->beginResetModel();
    m_itemList.clear();
    m_availableChunks.clear();
    m_fetchedDataCount = 0;
    //Setting this to true to let fetchMore do one first fetchcall.
    m_moreAvailable = true;
    q->endResetModel();

    q->fetchMore(QModelIndex());
}

void QIviPagingModelPrivate::fetchData(int startIndex)
{
    if (!backend())
        return;

    m_moreAvailable = false;
    const int start = startIndex >= 0 ? startIndex : m_fetchedDataCount;
    const int chunkIndex = start / m_chunkSize;
    if (chunkIndex < m_availableChunks.size())
        m_availableChunks.setBit(chunkIndex);
    backend()->fetchData(m_identifier, start, m_chunkSize);
}

void QIviPagingModelPrivate::clearToDefaults()
{
    Q_Q(QIviPagingModel);

    m_chunkSize = 30;
    emit q->chunkSizeChanged(m_chunkSize);
    m_moreAvailable = false;
    m_identifier = QUuid::createUuid();
    m_fetchMoreThreshold = 10;
    emit q->fetchMoreThresholdChanged(m_fetchMoreThreshold);
    m_fetchedDataCount = 0;
    m_loadingType = QIviPagingModel::FetchMore;
    emit q->loadingTypeChanged(m_loadingType);
    m_capabilities = QtIviCoreModule::NoExtras;
    emit q->capabilitiesChanged(m_capabilities);
    m_itemList.clear();

    resetModel();
}

const QIviStandardItem *QIviPagingModelPrivate::itemAt(int i) const
{
    const QVariant &var = m_itemList.at(i);
    if (!var.isValid())
        return nullptr;

    return qtivi_gadgetFromVariant<QIviStandardItem>(q_ptr, var);
}

QIviPagingModelInterface *QIviPagingModelPrivate::backend() const
{
    return QIviAbstractFeatureListModelPrivate::backend<QIviPagingModelInterface*>();
}

/*!
    \class QIviPagingModel
    \inmodule QtIviCore
    \brief The QIviPagingModel is a generic model to load its data using the "Paging" aproach.

    The QIviPagingModel should be used directly or as a base class whenever a lot of data needs to be
    presented in a ListView.

    The model only fetches the data it really needs and can it can be configured how this can be done using
    the loadingType property.

    The backend filling the model with data needs to implement the QIviPagingModelInterface class.

    \section1 Setting it up
    The QIviPagingModel is using QtIviCore's \l {Dynamic Backend System} and is derived from QIviAbstractFeatureListModel.
    Other than most "QtIvi Feature classes", the QIviPagingModel doesn't automatically connect to available backends.

    The easiest approach to set it up, is to connect to the same backend used by another feature. E.g. for connecting to the
    media backend, use the instance from the mediaplayer feature:
    \code
        QIviMediaPlayer *player = new QIviMediaPlayer();
        player->startAutoDiscovery();
        QIviPagingModel *model = new QIviPagingModel();
        model->setServiceObject(player->serviceObject());
    \endcode

    \section1 Loading Types

    Multiple loading types are supported, as the QIviPagingModel is made to work with asynchronous requests to
    fetch its data. The FetchMore loading type is the default and is using the \l{QAbstractItemModel::}{canFetchMore()}/\l{QAbstractItemModel::}{fetchMore()} functions of
    QAbstractItemModel to fetch new data once the view hits the end of the currently available data. As fetching can take
    some time, there is the fetchMoreThreshold property which controls how much in advance a new fetch should be started.

    The other loading type is DataChanged. In contrast to FetchMore, the complete model is pre-populated with empty rows
    and the actual data for a specific row is fetched the first time the data() function is called. Once the data is available,
    the dataChanged() signal will be triggered for this row and the view will start to render the new data.

    Please see the documentation of \l{QIviPagingModel::}{LoadingType} for more details on how the modes work and
    when they are suitable to use.

    See the \l{Models} section for more information about all models in QtIvi.
*/

/*!
    \enum QIviPagingModel::LoadingType
    \value FetchMore
           This is the default and can be used if you don't know the final size of the list (e.g. a infinite list).
           The list will detect that it is near the end (fetchMoreThreshold) and then fetch the next chunk of data using canFetchMore and fetchMore.
           The drawback of this method is that you can't display a dynamic scroll-bar indicator which is resized depending on the content of the list,
           because the final size of the data is not known.
           The other problem could be fast scrolling, as the data might not arrive in-time and scrolling stops. This can be tweaked by the fetchMoreThreshold property.

    \value DataChanged
           For this loading type you need to know how many items are in the list, as dummy items are created and the user can already start scrolling even though the data is not yet ready to be displayed.
           Similar to FetchMore, the data is also loaded in chunks. You can safely use a scroll indicator here.
           The delegate needs to support this approach, as it doesn't have content when it's first created.
*/

/*!
    \enum QIviPagingModel::Roles
    \value NameRole
          The name of the item. E.g. The name of a contact in a addressbook, or the artist-name in a list of artists.
    \value TypeRole
          The type of the item. E.g. "artist", "track", "contact".
    \value ItemRole
          The item itself. This provides access to the properties which are type specific. E.g. the address of a contact.
    \omitvalue LastRole
*/

/*!
    \qmltype PagingModel
    \instantiates QIviPagingModel
    \inqmlmodule QtIvi
    \inherits AbstractFeatureListModel
    \brief The PagingModel is a generic model to load its data using the "Paging" aproach.

    The PagingModel should be used directly or as a base class whenever a lot of data needs to be
    presented in a ListView.

    The model only fetches the data it really needs and can it can be configured how this can be done using
    the loadingType property.

    All rows in the model need to be subclassed from StandardItem.

    The following roles are available in this model:

    \table
    \header
        \li Role name
        \li Type
        \li Description
    \row
        \li \c name
        \li string
        \li The name of the item. E.g. The name of a contact in a addressbook, or the artist-name in a list of artists.
    \row
        \li \c type
        \li string
        \li The type of the item. E.g. "artist", "track", "contact".
    \row
        \li \c item
        \li object
        \li The item itself. This provides access to the properties which are type specific. E.g. the address of a contact.
    \endtable

    \section1 Setting it up
    The PagingModel is using QtIviCore's \l {Dynamic Backend System} and is derived from QIviAbstractFeatureListModel.
    Other than most "QtIvi Feature classes", the PagingModel doesn't automatically connect to available backends.

    The easiest approach to set it up, is to connect to the same backend used by another feature. E.g. for connecting to the
    media backend, use the instance from the mediaplayer feature:
    \qml
        Item {
            MediaPlayer {
                id: player
            }

            PagingModel {
                serviceObject: player.serviceObject
            }
        }
    \endqml

    \section1 Loading Types

    Multiple loading types are supported, as the PagingModel is made to work with asynchronous requests to
    fetch its data. The FetchMore loading type is the default and is using the \l{QAbstractItemModel::}{canFetchMore()}/\l{QAbstractItemModel::}{fetchMore()} functions of
    QAbstractItemModel to fetch new data once the view hits the end of the currently available data. As fetching can take
    some time, there is the fetchMoreThreshold property which controls how much in advance a new fetch should be started.

    The other loading type is DataChanged. In contrast to FetchMore, the complete model is pre-populated with empty rows
    and the actual data for a specific row is fetched the first time the data() function is called. Once the data is available,
    the dataChanged() signal will be triggered for this row and the view will start to render the new data.

    Please see the documentation of loadingType for more details on how the modes work and
    when they are suitable to use.

    See the \l{Models} section for more information about all models in QtIvi.
*/

/*!
    Constructs a QIviPagingModel.

    The \a parent argument is passed on to the \l QIviAbstractFeatureListModel base class.
*/
QIviPagingModel::QIviPagingModel(QObject *parent)
    : QIviAbstractFeatureListModel(*new QIviPagingModelPrivate(QStringLiteral(QIviPagingModel_iid), this), parent)
{
}

/*!
    \qmlproperty enumeration PagingModel::capabilities
    \brief Holds the capabilities of the backend.

    The capabilities controls what the backend supports.
    It can be a combination of the following values:

    \value NoExtras
           The backend does only support the minimum feature set and is stateful.
    \value SupportsGetSize
           The backend can return the final number of items for a specific request. This makes it possible to support the QIviPagingModel::DataChanged loading
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
    \property QIviPagingModel::capabilities
    \brief Holds the capabilities of the backend for the current content of the model.

    The capabilities controls what the current contentType supports. e.g. filtering or sorting.
*/

QtIviCoreModule::ModelCapabilities QIviPagingModel::capabilities() const
{
    Q_D(const QIviPagingModel);
    return d->m_capabilities;
}

/*!
    \qmlproperty int PagingModel::chunkSize
    \brief Holds the number of rows which are requested from the backend interface.

    This property can be used to fine tune the loading performance.

    Bigger chunks means less calls to the backend and to a potential IPC underneath, but more data
    to be transferred and probably longer waiting time until the request was finished.
*/

/*!
    \property QIviPagingModel::chunkSize
    \brief Holds the number of rows which are requested from the backend interface.

    This property can be used to fine tune the loading performance.

    Bigger chunks means less calls to the backend and to a potential IPC underneath, but more data
    to be transferred and probably longer waiting time until the request was finished.
*/
int QIviPagingModel::chunkSize() const
{
    Q_D(const QIviPagingModel);
    return d->m_chunkSize;
}

void QIviPagingModel::setChunkSize(int chunkSize)
{
    Q_D(QIviPagingModel);
    if (d->m_chunkSize == chunkSize)
        return;

    d->m_chunkSize = chunkSize;
    emit chunkSizeChanged(chunkSize);
}

/*!
    \qmlproperty int PagingModel::fetchMoreThreshold
    \brief Holds the row delta to the end before the next chunk is loaded

    This property can be used to fine tune the loading performance. When the
    threshold is reached the next chunk of rows are requested from the backend. How many rows are fetched
    can be defined by using the chunkSize property.

    The threshold defines the number of rows before the cached rows ends.

    \note This property is only used when loadingType is set to FetchMore.
*/

/*!
    \property QIviPagingModel::fetchMoreThreshold
    \brief Holds the row delta to the end before the next chunk is loaded

    This property can be used to fine tune the loading performance. When the
    threshold is reached the next chunk of rows are requested from the backend. How many rows are fetched
    can be defined by using the chunkSize property.

    The threshold defines the number of rows before the cached rows ends.

    \note This property is only used when loadingType is set to FetchMore.
*/
int QIviPagingModel::fetchMoreThreshold() const
{
    Q_D(const QIviPagingModel);
    return d->m_fetchMoreThreshold;
}

void QIviPagingModel::setFetchMoreThreshold(int fetchMoreThreshold)
{
    Q_D(QIviPagingModel);
    if (d->m_fetchMoreThreshold == fetchMoreThreshold)
        return;

    d->m_fetchMoreThreshold = fetchMoreThreshold;
    emit fetchMoreThresholdChanged(fetchMoreThreshold);
}

/*!
    \qmlproperty enumeration PagingModel::loadingType
    \brief Holds the currently used loading type used for loading the data.

    It can be one of the following values:
    \target FetchMore
    \value FetchMore
           This is the default and can be used if you don't know the final size of the list (e.g. a infinite list).
           The list will detect that it is near the end (fetchMoreThreshold) and then fetch the next chunk of data using canFetchMore and fetchMore.
           The drawback of this method is that you can't display a dynamic scroll-bar indicator which is resized depending on the content of the list,
           because the final size of the data is not known.
           The other problem could be fast scrolling, as the data might not arrive in-time and scrolling stops. This can be tweaked by the fetchMoreThreshold property.

    \target DataChanged
    \value DataChanged
           For this loading type you need to know how many items are in the list, as dummy items are created and the user can already start scrolling even though the data is not yet ready to be displayed.
           Similar to FetchMore, the data is also loaded in chunks. You can safely use a scroll indicator here.
           The delegate needs to support this approach, as it doesn't have content when it's first created.

    \note When changing this property the content will be reset.
*/

/*!
    \property QIviPagingModel::loadingType
    \brief Holds the currently used loading type used for loading the data.

    \note When changing this property the content will be reset.
*/
QIviPagingModel::LoadingType QIviPagingModel::loadingType() const
{
    Q_D(const QIviPagingModel);
    return d->m_loadingType;
}

void QIviPagingModel::setLoadingType(QIviPagingModel::LoadingType loadingType)
{
    Q_D(QIviPagingModel);
    if (d->m_loadingType == loadingType)
        return;

    if (loadingType == QIviPagingModel::DataChanged && !d->m_capabilities.testFlag(QtIviCoreModule::SupportsGetSize)) {
        qtivi_qmlOrCppWarning(this, "The backend doesn't support the DataChanged loading type. This call will have no effect");
        return;
    }

    d->m_loadingType = loadingType;
    emit loadingTypeChanged(loadingType);

    d->resetModel();
}

/*!
    \qmlproperty int PagingModel::count
    \brief Holds the current number of rows in this model.
*/
/*!
    \property QIviPagingModel::count
    \brief Holds the current number of rows in this model.
*/
int QIviPagingModel::rowCount(const QModelIndex &parent) const
{
    Q_D(const QIviPagingModel);
    if (parent.isValid())
        return 0;

    return d->m_itemList.count();
}

/*!
    \reimp
*/
QVariant QIviPagingModel::data(const QModelIndex &index, int role) const
{
    Q_D(const QIviPagingModel);
    Q_UNUSED(role)
    if (!index.isValid())
        return QVariant();

    int row = index.row();

    if (row >= d->m_itemList.count() || row < 0)
        return QVariant();

    const int chunkIndex = row / d->m_chunkSize;
    if (d->m_loadingType == DataChanged && !d->m_availableChunks.at(chunkIndex)) {
        //qWarning() << "Cache miss: Fetching Data for index " << row << "and following";
        const_cast<QIviPagingModelPrivate*>(d)->fetchData(chunkIndex * d->m_chunkSize);
        return QVariant();
    }

    if (row >= d->m_fetchedDataCount - d->m_fetchMoreThreshold && canFetchMore(QModelIndex()))
        emit fetchMoreThresholdReached();

    const QIviStandardItem *item = d->itemAt(row);
    if (!item) {
        //qWarning() << "Cache miss: Waiting for fetched Data";
        return QVariant();
    }

    switch (role) {
    case NameRole: return item->name();
    case TypeRole: return item->type();
    case ItemRole: return d->m_itemList.at(row);
    }

    return QVariant();
}

/*!
    \fn T QIviPagingModel::at(int i) const

    Returns the item at index \a i converted to the template type T.
*/
/*!
    \qmlmethod object PagingModel::get(i)

    Returns the item at index \a i.
*/
/*!
    Returns the item at index \a i as QVariant.

    This function is intended to be used from QML. For C++
    please use the at() instead.
*/
QVariant QIviPagingModel::get(int i) const
{
    return data(index(i,0), ItemRole);
}

/*!
    \qmlmethod PagingModel::reload()

    Resets the model and starts fetching the content again.
*/
/*!
    Resets the model and starts fetching the content again.
*/
void QIviPagingModel::reload()
{
    Q_D(QIviPagingModel);
    d->resetModel();
}

/*!
    \reimp
*/
bool QIviPagingModel::canFetchMore(const QModelIndex &parent) const
{
    Q_D(const QIviPagingModel);
    if (parent.isValid())
        return false;

    return d->m_moreAvailable;
}

/*!
    \reimp
*/
void QIviPagingModel::fetchMore(const QModelIndex &parent)
{
    Q_D(QIviPagingModel);
    if (parent.isValid())
        return;

    if (!d->backend() || !d->m_moreAvailable)
        return;

    d->m_moreAvailable = false;
    d->fetchData(-1);
}

/*!
    \reimp
*/
QHash<int, QByteArray> QIviPagingModel::roleNames() const
{
    static QHash<int, QByteArray> roles;
    if (roles.isEmpty()) {
        roles[NameRole] = "name";
        roles[TypeRole] = "type";
        roles[ItemRole] = "item";
    }
    return roles;
}

/*!
    \internal
*/
QIviPagingModel::QIviPagingModel(QIviServiceObject *serviceObject, QObject *parent)
    : QIviPagingModel(parent)
{
    setServiceObject(serviceObject);
}

/*!
    \internal
*/
QIviPagingModel::QIviPagingModel(QIviPagingModelPrivate &dd, QObject *parent)
    : QIviAbstractFeatureListModel(dd, parent)
{
}

/*!
    \reimp
*/
void QIviPagingModel::connectToServiceObject(QIviServiceObject *serviceObject)
{
    Q_D(QIviPagingModel);

    QIviPagingModelInterface *backend = d->backend();
    if (!backend)
        return;

    QObjectPrivate::connect(backend, &QIviPagingModelInterface::initializationDone,
                            d, &QIviPagingModelPrivate::onInitializationDone);
    QObjectPrivate::connect(backend, &QIviPagingModelInterface::supportedCapabilitiesChanged,
                            d, &QIviPagingModelPrivate::onCapabilitiesChanged);
    QObjectPrivate::connect(backend, &QIviPagingModelInterface::dataFetched,
                            d, &QIviPagingModelPrivate::onDataFetched);
    QObjectPrivate::connect(backend, &QIviPagingModelInterface::countChanged,
                            d, &QIviPagingModelPrivate::onCountChanged);
    QObjectPrivate::connect(backend, &QIviPagingModelInterface::dataChanged,
                            d, &QIviPagingModelPrivate::onDataChanged);

    QIviAbstractFeatureListModel::connectToServiceObject(serviceObject);
}

/*!
    \reimp
*/
void QIviPagingModel::disconnectFromServiceObject(QIviServiceObject *serviceObject)
{
    Q_D(QIviPagingModel);

    auto backend = d->backend();

    if (backend)
        backend->unregisterInstance(d->m_identifier);

    QIviAbstractFeatureListModel::disconnectFromServiceObject(serviceObject);
}

/*!
    \reimp
*/
void QIviPagingModel::clearServiceObject()
{
    Q_D(QIviPagingModel);
    d->clearToDefaults();
}

/*!
    \fn void QIviPagingModel::fetchMoreThresholdReached() const

    This signal is emitted whenever the fetchMoreThreshold is reached and new data is requested from the backend.
*/

/*!
    \qmlsignal PagingModel::fetchMoreThresholdReached()

    This signal is emitted whenever the fetchMoreThreshold is reached and new data is requested from the backend.
*/

QT_END_NAMESPACE

#include "moc_qivipagingmodel.cpp"
