/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "winceqtversionfactory.h"
#include "winceqtversion.h"
#include <qtsupport/qtsupportconstants.h>
#include <qtsupport/profilereader.h>
#include <QFileInfo>

using namespace Qt4ProjectManager;
using namespace Qt4ProjectManager::Internal;

WinCeQtVersionFactory::WinCeQtVersionFactory(QObject *parent)
    : QtVersionFactory(parent)
{

}

WinCeQtVersionFactory::~WinCeQtVersionFactory()
{

}

bool WinCeQtVersionFactory::canRestore(const QString &type)
{
    return type == QLatin1String(QtSupport::Constants::WINCEQT);
}

QtSupport::BaseQtVersion *WinCeQtVersionFactory::restore(const QString &type, const QVariantMap &data)
{
    if (!canRestore(type))
        return 0;
    WinCeQtVersion *v = new WinCeQtVersion;
    v->fromMap(data);
    return v;
}

int WinCeQtVersionFactory::priority() const
{
    return 50;
}

QtSupport::BaseQtVersion *WinCeQtVersionFactory::create(const Utils::FileName &qmakePath, ProFileEvaluator *evaluator, bool isAutoDetected, const QString &autoDetectionSource)
{
    QFileInfo fi = qmakePath.toFileInfo();
    if (!fi.exists() || !fi.isExecutable() || !fi.isFile())
        return 0;

    QString ce_sdk = evaluator->values(QLatin1String("CE_SDK")).join(QLatin1String(" "));
    QString ce_arch = evaluator->value(QLatin1String("CE_ARCH"));

    if (!ce_sdk.isEmpty() && !ce_arch.isEmpty())
        return new WinCeQtVersion(qmakePath, ce_arch, isAutoDetected, autoDetectionSource);

    return 0;
}
