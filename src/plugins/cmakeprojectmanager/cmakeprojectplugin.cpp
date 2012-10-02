/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: http://www.qt-project.org/
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
**
**************************************************************************/

#include "cmakeprojectplugin.h"
#include "cmakeprojectmanager.h"
#include "cmakebuildconfiguration.h"
#include "cmakerunconfiguration.h"
#include "cmakeeditorfactory.h"
#include "makestep.h"
#include "cmakeprojectconstants.h"
#include "cmakelocatorfilter.h"
#include "cmakefilecompletionassist.h"

#include <coreplugin/icore.h>
#include <coreplugin/mimedatabase.h>
#include <texteditor/texteditoractionhandler.h>

#include <QtPlugin>
#include <QDebug>


using namespace CMakeProjectManager::Internal;

CMakeProjectPlugin::CMakeProjectPlugin()
{
}

CMakeProjectPlugin::~CMakeProjectPlugin()
{
}

bool CMakeProjectPlugin::initialize(const QStringList & /*arguments*/, QString *errorMessage)
{
    if (!Core::ICore::mimeDatabase()->addMimeTypes(QLatin1String(":cmakeproject/CMakeProject.mimetypes.xml"), errorMessage))
        return false;
    CMakeSettingsPage *cmp = new CMakeSettingsPage();
    addAutoReleasedObject(cmp);
    CMakeManager *manager = new CMakeManager(cmp);
    addAutoReleasedObject(manager);
    addAutoReleasedObject(new MakeStepFactory);
    addAutoReleasedObject(new CMakeRunConfigurationFactory);
    addAutoReleasedObject(new CMakeBuildConfigurationFactory);

    addAutoReleasedObject(new CMakeEditorFactory(manager));
    addAutoReleasedObject(new CMakeLocatorFilter);
    addAutoReleasedObject(new CMakeFileCompletionAssistProvider(cmp));
    return true;
}

void CMakeProjectPlugin::extensionsInitialized()
{
}

Q_EXPORT_PLUGIN(CMakeProjectPlugin)
