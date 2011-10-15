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

#include "buildsystem.h"

#include "buildsystemmanager.h"
#include "projectexplorer.h"
#include "projectexplorersettings.h"

#include <extensionsystem/pluginmanager.h>
#include <utils/environment.h>



#include <QtCore/QCoreApplication>

static const char ID_KEY[] = "ProjectExplorer.BuildSystem.Id";
static const char DISPLAY_NAME_KEY[] = "ProjectExplorer.BuildSystem.DisplayName";

namespace ProjectExplorer {
namespace Internal {

// --------------------------------------------------------------------------
// BuildSystemPrivate
// --------------------------------------------------------------------------

class BuildSystemPrivate
{
public:
    BuildSystemPrivate(const QString &id, bool autodetect) : 
        m_id(id),
        m_autodetect(autodetect),
        m_make_command(0)
    { 
        Q_ASSERT(!id.isEmpty());
    }

    ~BuildSystemPrivate()
    { 
        // fits this into th QtCreator coding rules?
        delete m_make_command;
    }

    QString m_id;
    bool m_autodetect;
    mutable QString m_displayName;
    BuildCommand* m_make_command;
};

} // namespace Internal


// --------------------------------------------------------------------------
// BuildCommand
// --------------------------------------------------------------------------

BuildCommand::BuildCommand() :
    m_useNinja(ProjectExplorerPlugin::instance()->projectExplorerSettings().useNinja)
{
}

BuildCommand::~BuildCommand()
{
}


bool BuildCommand::useNinja() const
{
    return m_useNinja;
}


void BuildCommand::setUseNinja(bool val)
{
    m_useNinja = val;
}

QString BuildCommand::executableName() const
{
    if (useNinja()) {
#if defined(Q_OS_WIN)
        return QLatin1String("ninja.exe");
#else
        return QLatin1String("ninja");
#endif
    }
    return concreteExecutableName();
}



// --------------------------------------------------------------------------
// OneMakeCommand
// --------------------------------------------------------------------------

OneBuildCommand::OneBuildCommand(const QString& executableName) : 
    m_executableName(executableName)
{
}

BuildCommand* OneBuildCommand::clone() const
{
    BuildCommand* mc = new OneBuildCommand(m_executableName);
    mc->setUseNinja(useNinja());
    return mc;
}


QString OneBuildCommand::concreteExecutableName() const
{
    return m_executableName;
}




// --------------------------------------------------------------------------
// BuildSystem
// --------------------------------------------------------------------------


/*!
    \class ProjectExplorer::BuildSystem
    \brief Representation of a BuildSystem.
    \sa ProjectExplorer::BuildSystemManager
*/

// --------------------------------------------------------------------------

BuildSystem::BuildSystem(const QString &id, bool autodetect) :
    m_d(new Internal::BuildSystemPrivate(id, autodetect))
{ }

BuildSystem::BuildSystem(const BuildSystem &other) :
    m_d(new Internal::BuildSystemPrivate(other.id(), false))
{
    // leave the autodetection bit at false.
    m_d->m_displayName = QCoreApplication::translate("ProjectExplorer::BuildSystem", "Clone of %1")
            .arg(other.displayName());

    setBuildCommand(other.cloneBuildCommand());
}

BuildSystem::~BuildSystem()
{
    delete m_d;
}


void BuildSystem::setBuildCommand(BuildCommand* mc)
{
    Q_ASSERT(mc);
    m_d->m_make_command = mc;
}

BuildCommand* BuildSystem::cloneBuildCommand() const
{
    Q_ASSERT(m_d->m_make_command);
    return m_d->m_make_command->clone();
}

QString BuildSystem::displayName() const
{
    if (m_d->m_displayName.isEmpty())
        return typeName();
    return m_d->m_displayName;
}

void BuildSystem::setDisplayName(const QString &name)
{
    if (m_d->m_displayName == name)
        return;

    m_d->m_displayName = name;
    buildSystemUpdated();
}

bool BuildSystem::isAutoDetected() const
{
    return m_d->m_autodetect;
}

QString BuildSystem::id() const
{
    return m_d->m_id;
}

/*!
    \brief Returns a list of target ids that this tool chain is restricted to.

    An empty list is shows that the toolchain is compatible with all targets.
*/

QStringList BuildSystem::restrictedToTargets() const
{
    return QStringList();
}

bool BuildSystem::canClone() const
{
    return true;
}

QString BuildSystem::defaultMakeTarget() const
{
    return QString();
}

bool BuildSystem::operator == (const BuildSystem &tc) const
{
    if (this == &tc)
        return true;

    return id() == tc.id();
}

/*!
    \brief Used by the toolchainmanager to save user-generated tool chains.

    Make sure to call this method when deriving!
*/

QVariantMap BuildSystem::toMap() const
{
    QVariantMap result;
    if (isAutoDetected())
        return result;

    result.insert(QLatin1String(ID_KEY), id());
    result.insert(QLatin1String(DISPLAY_NAME_KEY), displayName());

    return result;
}

void BuildSystem::setId(const QString &id)
{
    Q_ASSERT(!id.isEmpty());
    if (m_d->m_id == id)
        return;

    m_d->m_id = id;
    buildSystemUpdated();
}

void BuildSystem::buildSystemUpdated()
{
    BuildSystemManager::instance()->notifyAboutUpdate(this);
}

void BuildSystem::setAutoDetected(bool autodetect)
{
    if (m_d->m_autodetect == autodetect)
        return;
    m_d->m_autodetect = autodetect;
    buildSystemUpdated();
}

/*!
    \brief Used by the toolchainmanager to load user-generated tool chains.

    Make sure to call this method when deriving!
*/

bool BuildSystem::fromMap(const QVariantMap &data)
{
    Q_ASSERT(!isAutoDetected());
    // do not read the id: That is already set anyway.
    m_d->m_displayName = data.value(QLatin1String(DISPLAY_NAME_KEY)).toString();

    return true;
}

/*!
    \class ProjectExplorer::BuildSystemFactory
    \brief Creates toolchains from settings or autodetects them.
*/

/*!
    \fn QString ProjectExplorer::BuildSystemFactory::displayName() const = 0
    \brief Name used to display the name of the tool chain that will be created.
*/

/*!
    \fn bool ProjectExplorer::BuildSystemFactory::canRestore(const QVariantMap &data)
    \brief Used by the BuildSystemManager to restore user-generated tool chains.
*/

QList<BuildSystem *> BuildSystemFactory::autoDetect()
{
    return QList<BuildSystem *>();
}

bool BuildSystemFactory::canCreate()
{
    return false;
}

BuildSystem *BuildSystemFactory::create()
{
    return 0;
}

bool BuildSystemFactory::canRestore(const QVariantMap &)
{
    return false;
}

BuildSystem *BuildSystemFactory::restore(const QVariantMap &)
{
    return 0;
}

QString BuildSystemFactory::idFromMap(const QVariantMap &data)
{
    return data.value(QLatin1String(ID_KEY)).toString();
}

} // namespace ProjectExplorer
