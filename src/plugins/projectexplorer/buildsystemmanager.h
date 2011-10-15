/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2011 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (info@qt.nokia.com)
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
** If you have questions regarding the use of this file, please contact
** Nokia at info@qt.nokia.com.
**
**************************************************************************/

#ifndef BUILD_SYSTEM_MANAGER_H
#define BUILD_SYSTEM_MANAGER_H

#include "projectexplorer_export.h"

#include "buildsystem.h"

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>

namespace ProjectExplorer {
class ProjectExplorerPlugin;
class BuildSystem;
class BuildSystemFactory;

namespace Internal {
class BuildSystemManagerPrivate;
}

// --------------------------------------------------------------------------
// BuildSystemManager
// --------------------------------------------------------------------------

class PROJECTEXPLORER_EXPORT BuildSystemManager : public QObject
{
    Q_OBJECT

public:
    static BuildSystemManager *instance();
    ~BuildSystemManager();

    QList<BuildSystem *> buildSystems() const;
    QList<BuildSystem *> findBuildSystems() const;
    BuildSystem *findBuildSystem(const QString &id) const;


public slots:
    bool registerBuildSystem(ProjectExplorer::BuildSystem *tc);
    void deregisterBuildSystem(ProjectExplorer::BuildSystem *tc);

    void saveBuildSystems();

signals:
    void buildSystemAdded(ProjectExplorer::BuildSystem *);
    // Tool chain is still valid when this call happens!
    void buildSystemRemoved(ProjectExplorer::BuildSystem *);
    // Tool chain was updated.
    void buildSystemUpdated(ProjectExplorer::BuildSystem *);
    // Something changed:
    void buildSystemsChanged();

private:
    explicit BuildSystemManager(QObject *parent = 0);

    // Make sure the this is only called after all
    // Tool chain Factories are registered!
    void restoreBuildSystems();
    void restoreBuildSystems(const QString &fileName, bool autoDetected = false);

    void notifyAboutUpdate(ProjectExplorer::BuildSystem *);


    Internal::BuildSystemManagerPrivate *const m_d;

    static BuildSystemManager *m_instance;

    friend class ProjectExplorerPlugin;
    friend class BuildSystem;
};

} // namespace ProjectExplorer

#endif // BUILD_SYSTEM_MANAGER_H
