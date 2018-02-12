/****************************************************************************
**
** Copyright (C) 2018 Pelagicore AG
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtIvi module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT-QTAS$
** Commercial License Usage
** Licensees holding valid commercial Qt Automotive Suite licenses may use
** this file in accordance with the commercial license agreement provided
** with the Software or, alternatively, in accordance with the terms
** contained in a written agreement between you and The Qt Company.  For
** licensing terms and conditions see https://www.qt.io/terms-conditions.
** For further information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/


#ifndef SIMPLESTATICPLUGIN_H
#define SIMPLESTATICPLUGIN_H

#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtIviCore/QIviServiceInterface>

class SimpleStaticPlugin : public QObject, public QIviServiceInterface
{
    Q_OBJECT
    Q_INTERFACES(QIviServiceInterface)
    Q_PLUGIN_METADATA(IID QIviServiceInterface_iid FILE "simple_plugin.json")
public:
    explicit SimpleStaticPlugin();
    ~SimpleStaticPlugin() {}

    QStringList interfaces() const {
        return QStringList() << "simple_plugin_static";
    }

    QIviFeatureInterface *interfaceInstance(const QString &interface) const {
        Q_UNUSED(interface)
        return 0;
    }

};

#endif // SIMPLESTATICPLUGIN_H