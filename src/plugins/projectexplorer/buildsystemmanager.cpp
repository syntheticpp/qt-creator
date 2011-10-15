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

#include "buildsystemmanager.h"

#include "buildsystem.h"

#include <coreplugin/icore.h>
#include <projectexplorer/persistentsettings.h>

#include <extensionsystem/pluginmanager.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QSettings>
#include <QtGui/QMainWindow>

static const char TOOLCHAIN_DATA_KEY[] = "BuildSystem.";
static const char TOOLCHAIN_COUNT_KEY[] = "BuildSystem.Count";
static const char TOOLCHAIN_FILE_VERSION_KEY[] = "Version";
static const char DEFAULT_DEBUGGER_COUNT_KEY[] = "DefaultDebugger.Count";
static const char DEFAULT_DEBUGGER_ABI_KEY[] = "DefaultDebugger.Abi.";
static const char DEFAULT_DEBUGGER_PATH_KEY[] = "DefaultDebugger.Path.";
static const char TOOLCHAIN_FILENAME[] = "/buildSystems.xml";

static QString settingsFileName()
{
    ExtensionSystem::PluginManager *pm = ExtensionSystem::PluginManager::instance();
    QFileInfo settingsLocation(pm->settings()->fileName());
    return settingsLocation.absolutePath() + QLatin1String(TOOLCHAIN_FILENAME);
}

namespace ProjectExplorer {

BuildSystemManager *BuildSystemManager::m_instance = 0;

namespace Internal {

// --------------------------------------------------------------------------
// BuildSystemManagerPrivate
// --------------------------------------------------------------------------

class BuildSystemManagerPrivate
{
public:
    QList<BuildSystem *> m_buildSystems;
    QMap<QString, QString> m_abiToDebugger;
};

} // namespace Internal

// --------------------------------------------------------------------------
// BuildSystemManager
// --------------------------------------------------------------------------

BuildSystemManager *BuildSystemManager::instance()
{
    Q_ASSERT(m_instance);
    return m_instance;
}

BuildSystemManager::BuildSystemManager(QObject *parent) :
    QObject(parent),
    m_d(new Internal::BuildSystemManagerPrivate)
{
    Q_ASSERT(!m_instance);
    m_instance = this;
    connect(Core::ICore::instance(), SIGNAL(saveSettingsRequested()),
            this, SLOT(saveBuildSystems()));
    connect(this, SIGNAL(buildSystemAdded(ProjectExplorer::BuildSystem*)),
            this, SIGNAL(buildSystemsChanged()));
    connect(this, SIGNAL(buildSystemRemoved(ProjectExplorer::BuildSystem*)),
            this, SIGNAL(buildSystemsChanged()));
    connect(this, SIGNAL(buildSystemUpdated(ProjectExplorer::BuildSystem*)),
            this, SIGNAL(buildSystemsChanged()));
}

void BuildSystemManager::restoreBuildSystems()
{
    // Restore SDK settings first
    restoreBuildSystems(Core::ICore::instance()->resourcePath()
                      + QLatin1String("/Nokia") + QLatin1String(TOOLCHAIN_FILENAME), true);

    // Then auto detect
    ExtensionSystem::PluginManager *pm = ExtensionSystem::PluginManager::instance();
    QList<BuildSystemFactory *> factories = pm->getObjects<BuildSystemFactory>();
    // Autodetect tool chains:
    foreach (BuildSystemFactory *f, factories) {
        QList<BuildSystem *> tcs = f->autoDetect();
        foreach (BuildSystem *tc, tcs)
            registerBuildSystem(tc);
    }

    // Then restore user settings
    restoreBuildSystems(settingsFileName(), false);
}

BuildSystemManager::~BuildSystemManager()
{
    // Deregister tool chains
    QList<BuildSystem *> copy = m_d->m_buildSystems;
    foreach (BuildSystem *tc, copy)
        deregisterBuildSystem(tc);

    delete m_d;
    m_instance = 0;
}

void BuildSystemManager::saveBuildSystems()
{
    PersistentSettingsWriter writer;
    writer.saveValue(QLatin1String(TOOLCHAIN_FILE_VERSION_KEY), 1);

    int count = 0;
    foreach (BuildSystem *tc, m_d->m_buildSystems) {
        if (!tc->isAutoDetected() && tc->isValid()) {
            QVariantMap tmp = tc->toMap();
            if (tmp.isEmpty())
                continue;
            writer.saveValue(QString::fromLatin1(TOOLCHAIN_DATA_KEY) + QString::number(count), tmp);
            ++count;
        }
    }
    writer.saveValue(QLatin1String(TOOLCHAIN_COUNT_KEY), count);
    writer.save(settingsFileName(), "QtCreatorBuildSystems", Core::ICore::instance()->mainWindow());

    // Do not save default debuggers! Those are set by the SDK!
}

void BuildSystemManager::restoreBuildSystems(const QString &fileName, bool autoDetected)
{
    PersistentSettingsReader reader;
    if (!reader.load(fileName))
        return;
    QVariantMap data = reader.restoreValues();

    // Check version:
    int version = data.value(QLatin1String(TOOLCHAIN_FILE_VERSION_KEY), 0).toInt();
    if (version < 1)
        return;

    // Read default debugger settings (if any)
    int count = data.value(QLatin1String(DEFAULT_DEBUGGER_COUNT_KEY)).toInt();
    for (int i = 0; i < count; ++i) {
        const QString abiKey = QString::fromLatin1(DEFAULT_DEBUGGER_ABI_KEY) + QString::number(i);
        if (!data.contains(abiKey))
            continue;
        const QString pathKey = QString::fromLatin1(DEFAULT_DEBUGGER_PATH_KEY) + QString::number(i);
        if (!data.contains(pathKey))
            continue;
        m_d->m_abiToDebugger.insert(data.value(abiKey).toString(), data.value(pathKey).toString());
    }

    ExtensionSystem::PluginManager *pm = ExtensionSystem::PluginManager::instance();
    QList<BuildSystemFactory *> factories = pm->getObjects<BuildSystemFactory>();

    count = data.value(QLatin1String(TOOLCHAIN_COUNT_KEY), 0).toInt();
    for (int i = 0; i < count; ++i) {
        const QString key = QString::fromLatin1(TOOLCHAIN_DATA_KEY) + QString::number(i);
        if (!data.contains(key))
            break;

        const QVariantMap tcMap = data.value(key).toMap();

        bool restored = false;
        foreach (BuildSystemFactory *f, factories) {
            if (f->canRestore(tcMap)) {
                if (BuildSystem *tc = f->restore(tcMap)) {
                    tc->setAutoDetected(autoDetected);

                    registerBuildSystem(tc);
                    restored = true;
                    break;
                }
            }
        }
        if (!restored)
            qWarning("Warning: Unable to restore manual tool chain '%s' stored in %s.",
                     qPrintable(BuildSystemFactory::idFromMap(tcMap)),
                     qPrintable(QDir::toNativeSeparators(fileName)));
    }
}

QList<BuildSystem *> BuildSystemManager::buildSystems() const
{
    return m_d->m_buildSystems;
}

QList<BuildSystem *> BuildSystemManager::findBuildSystems() const
{
    QList<BuildSystem *> result;
    /*
    foreach (BuildSystem *tc, m_d->m_buildSystems) {
        Abi targetAbi = tc->targetAbi();
        if (targetAbi.isCompatibleWith(abi))
            result.append(tc);
    }
    */
    return result;
}


BuildSystem *BuildSystemManager::findBuildSystem(const QString &id) const
{
    foreach (BuildSystem *tc, m_d->m_buildSystems) {
        if (tc->id() == id)
            return tc;
    }
    return 0;
}


void BuildSystemManager::notifyAboutUpdate(ProjectExplorer::BuildSystem *tc)
{
    if (!tc || !m_d->m_buildSystems.contains(tc))
        return;
    emit buildSystemUpdated(tc);
}

bool BuildSystemManager::registerBuildSystem(BuildSystem *tc)
{
    if (!tc || m_d->m_buildSystems.contains(tc))
        return true;
    foreach (BuildSystem *current, m_d->m_buildSystems) {
        if (*tc == *current)
            return false;
    }

    m_d->m_buildSystems.append(tc);
    emit buildSystemAdded(tc);
    return true;
}

void BuildSystemManager::deregisterBuildSystem(BuildSystem *tc)
{
    if (!tc || !m_d->m_buildSystems.contains(tc))
        return;
    m_d->m_buildSystems.removeOne(tc);
    emit buildSystemRemoved(tc);
    delete tc;
}

} // namespace ProjectExplorer
