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

#include "searchandbrowsebackend.h"
#include "logging.h"

#include <QtConcurrent/QtConcurrent>

#include <QFuture>
#include <QSqlError>
#include <QSqlQuery>
#include <QtDebug>

static const QString artistLiteral = QStringLiteral("artist");
static const QString albumLiteral = QStringLiteral("album");
static const QString trackLiteral = QStringLiteral("track");

QDataStream &operator<<(QDataStream &stream, const SearchAndBrowseItem &obj)
{
    stream << obj.name();
    stream << obj.type();
    stream << obj.url();
    stream << QVariant(obj.data());
    return stream;
}

QDataStream &operator>>(QDataStream &stream, SearchAndBrowseItem &obj)
{
    QString name;
    QString type;
    QUrl url;
    QVariant data;
    stream >> name;
    stream >> type;
    stream >> url;
    stream >> data;
    obj.setName(name);
    obj.setType(type);
    obj.setUrl(url);
    obj.setData(data.toMap());
    return stream;
}

SearchAndBrowseBackend::SearchAndBrowseBackend(const QSqlDatabase &database, QObject *parent)
    : QIviSearchAndBrowseModelInterface(parent)
    , m_threadPool(new QThreadPool(this))
{
    m_threadPool->setMaxThreadCount(1);

    qRegisterMetaType<SearchAndBrowseItem>();
    qRegisterMetaTypeStreamOperators<SearchAndBrowseItem>();
    qRegisterMetaType<QIviAudioTrackItem>();
    qRegisterMetaTypeStreamOperators<QIviAudioTrackItem>();

    m_db = database;
    m_db.open();

    m_contentTypes << artistLiteral;
    m_contentTypes << albumLiteral;
    m_contentTypes << trackLiteral;
}

QStringList SearchAndBrowseBackend::availableContentTypes() const
{
    return m_contentTypes;
}

void SearchAndBrowseBackend::initialize()
{
    emit availableContentTypesChanged(m_contentTypes);
    emit initializationDone();
}

void SearchAndBrowseBackend::registerInstance(const QUuid &identifier)
{
    m_state.insert(identifier, {});
}

void SearchAndBrowseBackend::unregisterInstance(const QUuid &identifier)
{
    m_state.remove(identifier);
}

void SearchAndBrowseBackend::setContentType(const QUuid &identifier, const QString &contentType)
{
    auto &state = m_state[identifier];
    state.contentType = contentType;

    QStringList types = state.contentType.split('/');
    QString current_type = types.last();
    bool canGoBack = types.count() >= 2;

    if (!m_contentTypes.contains(current_type)) {
        emit errorChanged(QIviAbstractFeature::InvalidOperation, QStringLiteral("The provided content type is not supported"));
        return;
    }

    QSet<QString> identifiers;
    if (current_type == artistLiteral || current_type == albumLiteral)
        identifiers = identifiersFromItem<SearchAndBrowseItem>();
    else
        identifiers = identifiersFromItem<QIviAudioTrackItem>();
    emit queryIdentifiersChanged(identifier, identifiers);
    emit canGoBackChanged(identifier, canGoBack);
    emit contentTypeChanged(identifier, contentType);
}

void SearchAndBrowseBackend::setupFilter(const QUuid &identifier, QIviAbstractQueryTerm *term, const QList<QIviOrderTerm> &orderTerms)
{
    auto &state = m_state[identifier];
    state.queryTerm = term;
    state.orderTerms = orderTerms;
}

void SearchAndBrowseBackend::fetchData(const QUuid &identifier, int start, int count)
{
    emit supportedCapabilitiesChanged(identifier, QtIviCoreModule::ModelCapabilities(
                                          QtIviCoreModule::SupportsFiltering |
                                          QtIviCoreModule::SupportsSorting |
                                          QtIviCoreModule::SupportsAndConjunction |
                                          QtIviCoreModule::SupportsOrConjunction |
                                          QtIviCoreModule::SupportsStatelessNavigation |
                                          QtIviCoreModule::SupportsGetSize
                                          ));

    if (!m_state.contains(identifier)) {
        qCCritical(media) << "INTERNAL ERROR: No state available for this uuid";
        return;
    }
    auto state = m_state[identifier];

    qCDebug(media) << "FETCH" << identifier << state.contentType << start << count;

    //Determine the current type and which items got selected previously to define the base filter.
    QStringList where_clauses;
    QStringList types = state.contentType.split('/');
    for (const QString &filter_type : types) {
        QStringList parts = filter_type.split('?');
        if (parts.count() != 2)
            continue;

        QString filter = QString::fromUtf8(QByteArray::fromBase64(parts.at(1).toUtf8(), QByteArray::Base64UrlEncoding));
        where_clauses.append(QStringLiteral("%1 = \"%2\"").arg(mapIdentifiers(parts.at(0), QStringLiteral("name")), filter));
    }
    QString current_type = types.last();

    QString order;
    if (!state.orderTerms.isEmpty())
        order = QStringLiteral("ORDER BY %1").arg(createSortOrder(current_type, state.orderTerms));

    QString columns;
    QString groupBy;
    if (current_type == artistLiteral) {
        columns = QStringLiteral("artistName, coverArtUrl");
        groupBy = QStringLiteral("artistName");
    } else if (current_type == albumLiteral) {
        columns = QStringLiteral("artistName, albumName, coverArtUrl");
        groupBy = QStringLiteral("artistName, albumName");
    } else {
        columns = QStringLiteral("artistName, albumName, trackName, genre, number, file, id, coverArtUrl");
    }

    QString filterClause = createWhereClause(current_type, state.queryTerm);
    if (!filterClause.isEmpty())
        where_clauses.append(filterClause);

    QString whereClause = where_clauses.join(QStringLiteral(" AND "));

    QString countQuery = QStringLiteral("SELECT count() FROM (SELECT %1 FROM track %2 %3)")
            .arg(columns,
                 whereClause.isEmpty() ? QString() : QStringLiteral("WHERE ") + whereClause,
                 groupBy.isEmpty() ? QString() : QStringLiteral("GROUP BY ") + groupBy);

    QtConcurrent::run(m_threadPool, [this, countQuery, identifier]() {
        QSqlQuery query(m_db);
        if (query.exec(countQuery)) {
            while (query.next()) {
                emit countChanged(identifier, query.value(0).toInt());
            }
        } else {
            sqlError(this, query.lastQuery(), query.lastError().text());
        }
    });

    QString queryString = QStringLiteral("SELECT %1 FROM track %2 %3 %4 LIMIT %5, %6")
            .arg(columns,
            whereClause.isEmpty() ? QString() : QStringLiteral("WHERE ") + whereClause,
            order,
            groupBy.isEmpty() ? QString() : QStringLiteral("GROUP BY ") + groupBy,
            QString::number(start),
            QString::number(count));

    QtConcurrent::run(m_threadPool,
                      this,
                      &SearchAndBrowseBackend::search,
                      identifier,
                      queryString,
                      current_type,
                      start,
                      count);
}

void SearchAndBrowseBackend::search(const QUuid &identifier, const QString &queryString, const QString &type, int start, int count)
{
    QVariantList list;
    QSqlQuery query(m_db);

    if (query.exec(queryString)) {
        while (query.next()) {
            QString artist = query.value(0).toString();
            QString album = query.value(1).toString();

            if (type == trackLiteral) {
                QIviAudioTrackItem item;
                item.setId(query.value(6).toString());
                item.setTitle(query.value(2).toString());
                item.setArtist(artist);
                item.setAlbum(album);
                item.setUrl(QUrl::fromLocalFile(query.value(5).toString()));
                item.setCoverArtUrl(QUrl::fromLocalFile(query.value(7).toString()));
                list.append(QVariant::fromValue(item));
            } else {
                SearchAndBrowseItem item;
                item.setType(type);
                if (type == artistLiteral) {
                    item.setName(artist);
                    item.setData(QVariantMap{{"coverArtUrl", QUrl::fromLocalFile(query.value(1).toString())}});
                } else if (type == albumLiteral) {
                    item.setName(album);
                    item.setData(QVariantMap{{"artist", artist},
                                             {"coverArtUrl", QUrl::fromLocalFile(query.value(2).toString())}
                                             });
                }
                list.append(QVariant::fromValue(item));
            }

//            if (type == "artist") {
//                DiskArtistItem* artistItem = new DiskArtistItem();
//                artistItem->m_artist = artist;
//                list.append(artistItem);
//            } else if (type == "album") {
//                DiskAlbumItem* albumItem = new DiskAlbumItem();
//                albumItem->m_album = album;
//                albumItem->m_artist = artist;
//                list.append(albumItem);
//            } else if (type == "track") {
//                DiskTrackItem* trackItem = new DiskTrackItem();
//                trackItem->m_artist = artist;
//                trackItem->m_album = album;
//                trackItem->m_track = query.value(2).toString();
//                trackItem->m_genres.append(query.value(3).toString());
//                trackItem->m_number = query.value(4).toUInt();
//                trackItem->m_url = QUrl::fromLocalFile(query.value(5).toString()).toString();
//                list.append(trackItem);
//            }
        }
    } else {
        qCWarning(media) << query.lastError().text();
    }

    emit dataFetched(identifier, list, start, list.count() >= count);

    auto &state = m_state[identifier];
    for (int i=0; i < list.count(); i++) {
        if (start + i >= state.items.count())
            state.items.append(list.at(i));
        else
            state.items.replace(start + i, list.at(i));
    }

    if (type == artistLiteral || type == albumLiteral)
        emit canGoForwardChanged(identifier, QVector<bool>(list.count(), true), start);
}

QString SearchAndBrowseBackend::createSortOrder(const QString &type, const QList<QIviOrderTerm> &orderTerms)
{
    QStringList order;
    int i = 0;
    for (const QIviOrderTerm & term : orderTerms) {
        if (i)
            order.append(QStringLiteral(","));

        order.append(mapIdentifiers(type, term.propertyName()));
        if (term.isAscending())
            order.append(QStringLiteral("ASC"));
        else
            order.append(QStringLiteral("DESC"));

        i++;
    }

    return order.join(' ');
}

QString SearchAndBrowseBackend::mapIdentifiers(const QString &type, const QString &identifer)
{
    if (identifer == QLatin1String("name")) {
        if (type == artistLiteral)
            return QStringLiteral("artistName");
        else if (type == albumLiteral)
            return QStringLiteral("albumName");
        else if (type == trackLiteral)
            return QStringLiteral("trackName");
    }

    return identifer;
}

QString SearchAndBrowseBackend::createWhereClause(const QString &type, QIviAbstractQueryTerm *term)
{
    if (!term)
        return QString();

    switch (term->type()) {
    case QIviAbstractQueryTerm::ScopeTerm: {
        auto *scope = static_cast<QIviScopeTerm*>(term);
        return QStringLiteral("%1 (%2)").arg(scope->isNegated() ? QStringLiteral("NOT") : QString(), createWhereClause(type, scope->term()));
    }
    case QIviAbstractQueryTerm::ConjunctionTerm: {
        auto *conjunctionTerm = static_cast<QIviConjunctionTerm*>(term);
        QLatin1String conjunction = QLatin1String("AND");
        if (conjunctionTerm->conjunction() == QIviConjunctionTerm::Or)
            conjunction = QLatin1String("OR");

        QString string;
        const auto terms = conjunctionTerm->terms();
        for (QIviAbstractQueryTerm *term : terms) {
            string += createWhereClause(type, term) + QLatin1Char(' ') + conjunction + QLatin1Char(' ');
        }
        if (!string.isEmpty())
            string.chop(2 + conjunction.size()); // chop off trailing " AND " or " OR "
        return string;
    }
    case QIviAbstractQueryTerm::FilterTerm: {
        auto *filter = static_cast<QIviFilterTerm*>(term);
        QString operatorString;
        bool negated = filter->isNegated();
        QString value;
        if (filter->value().type() == QVariant::String)
            value = QStringLiteral("'%1'").arg(filter->value().toString().replace('*', '%'));
        else
            value = filter->value().toString();

        switch (filter->operatorType()){
            case QIviFilterTerm::Equals: operatorString = QStringLiteral("="); break;
            case QIviFilterTerm::EqualsCaseInsensitive: operatorString = QStringLiteral("LIKE"); break;
            case QIviFilterTerm::Unequals: operatorString = QStringLiteral("="); negated = !negated; break;
            case QIviFilterTerm::GreaterThan: operatorString = QStringLiteral(">"); break;
            case QIviFilterTerm::GreaterEquals: operatorString = QStringLiteral(">="); break;
            case QIviFilterTerm::LowerThan: operatorString = QStringLiteral("<"); break;
            case QIviFilterTerm::LowerEquals: operatorString = QStringLiteral("<="); break;
        }

        QStringList clause;
        if (negated)
            clause.append(QStringLiteral("NOT"));
        clause.append(mapIdentifiers(type, filter->propertyName()));
        clause.append(operatorString);
        clause.append(value);

        return clause.join(QStringLiteral(" "));
    }
    }

    return QString();
}

QIviPendingReply<QString> SearchAndBrowseBackend::goBack(const QUuid &identifier)
{
    auto &state = m_state[identifier];
    QStringList types = state.contentType.split('/');

    if (types.count() < 2)
        return QIviPendingReply<QString>::createFailedReply();

    types.removeLast();
    types.replace(types.count() - 1, types.at(types.count() - 1).split('?').at(0));

    return QIviPendingReply<QString>(types.join('/'));
}

QIviPendingReply<QString> SearchAndBrowseBackend::goForward(const QUuid &identifier, int index)
{
    auto &state = m_state[identifier];

    const QIviStandardItem *i = qtivi_gadgetFromVariant<QIviStandardItem>(this, state.items.value(index, QVariant()));
    if (!i)
        return QIviPendingReply<QString>::createFailedReply();

    QString itemId = i->id();
    QStringList types = state.contentType.split('/');

    QString current_type = types.last();
    QString new_type = state.contentType + QStringLiteral("?%1").arg(QLatin1String(itemId.toUtf8().toBase64(QByteArray::Base64UrlEncoding)));

    if (current_type == artistLiteral)
        new_type += QLatin1String("/album");
    else if (current_type == albumLiteral)
        new_type += QLatin1String("/track");
    else
        return QIviPendingReply<QString>::createFailedReply();

    return QIviPendingReply<QString>(new_type);
}

QIviPendingReply<void> SearchAndBrowseBackend::insert(const QUuid &identifier, int index, const QVariant &item)
{
    Q_UNUSED(identifier)
    Q_UNUSED(index)
    Q_UNUSED(item)

    return QIviPendingReply<void>::createFailedReply();
}

QIviPendingReply<void> SearchAndBrowseBackend::remove(const QUuid &identifier, int index)
{
    Q_UNUSED(identifier)
    Q_UNUSED(index)

    return QIviPendingReply<void>::createFailedReply();
}

QIviPendingReply<void> SearchAndBrowseBackend::move(const QUuid &identifier, int currentIndex, int newIndex)
{
    Q_UNUSED(identifier)
    Q_UNUSED(currentIndex)
    Q_UNUSED(newIndex)

    return QIviPendingReply<void>::createFailedReply();
}

QIviPendingReply<int> SearchAndBrowseBackend::indexOf(const QUuid &identifier, const QVariant &item)
{
    Q_UNUSED(identifier)
    Q_UNUSED(item)

    return QIviPendingReply<int>::createFailedReply();
}
