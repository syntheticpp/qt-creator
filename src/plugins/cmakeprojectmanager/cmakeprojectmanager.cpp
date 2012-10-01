/****************************************************************************
**
** Copyright (C) 2012 Digia Plc and/or its subsidiary(-ies).
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

#include "cmakeopenprojectwizard.h"
#include "cmakeprojectmanager.h"
#include "cmakeprojectconstants.h"
#include "cmakeproject.h"

#include <utils/synchronousprocess.h>
#include <utils/qtcprocess.h>

#include <coreplugin/icore.h>
#include <coreplugin/id.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/actionmanager/actioncontainer.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/target.h>
#include <utils/QtConcurrentTools>
#include <QtConcurrentRun>
#include <QCoreApplication>
#include <QSettings>
#include <QDateTime>
#include <QFormLayout>
#include <QBoxLayout>
#include <QDesktopServices>
#include <QApplication>
#include <QLabel>
#include <QGroupBox>
#include <QSpacerItem>

using namespace CMakeProjectManager::Internal;

CMakeManager::CMakeManager(CMakeSettingsPage *cmakeSettingsPage)
    : m_settingsPage(cmakeSettingsPage)
{
    ProjectExplorer::ProjectExplorerPlugin *projectExplorer = ProjectExplorer::ProjectExplorerPlugin::instance();
    connect(projectExplorer, SIGNAL(aboutToShowContextMenu(ProjectExplorer::Project*,ProjectExplorer::Node*)),
            this, SLOT(updateContextMenu(ProjectExplorer::Project*,ProjectExplorer::Node*)));

    connect(m_settingsPage, SIGNAL(cmakeExecutableChanged()), this, SIGNAL(cmakeExecutableChanged()));

    Core::ActionContainer *mbuild =
            Core::ActionManager::actionContainer(ProjectExplorer::Constants::M_BUILDPROJECT);
    Core::ActionContainer *mproject =
            Core::ActionManager::actionContainer(ProjectExplorer::Constants::M_PROJECTCONTEXT);
    Core::ActionContainer *msubproject =
            Core::ActionManager::actionContainer(ProjectExplorer::Constants::M_SUBPROJECTCONTEXT);

    const Core::Context projectContext(CMakeProjectManager::Constants::PROJECTCONTEXT);

    m_runCMakeAction = new QAction(QIcon(), tr("Run CMake"), this);
    Core::Command *command = Core::ActionManager::registerAction(m_runCMakeAction,
                                                                 Constants::RUNCMAKE, projectContext);
    command->setAttribute(Core::Command::CA_Hide);
    mbuild->addAction(command, ProjectExplorer::Constants::G_BUILD_DEPLOY);
    connect(m_runCMakeAction, SIGNAL(triggered()), this, SLOT(runCMake()));

    m_runCMakeActionContextMenu = new QAction(QIcon(), tr("Run CMake"), this);
    command = Core::ActionManager::registerAction(m_runCMakeActionContextMenu,
                                                  Constants::RUNCMAKECONTEXTMENU, projectContext);
    command->setAttribute(Core::Command::CA_Hide);
    mproject->addAction(command, ProjectExplorer::Constants::G_PROJECT_BUILD);
    msubproject->addAction(command, ProjectExplorer::Constants::G_PROJECT_BUILD);
    connect(m_runCMakeActionContextMenu, SIGNAL(triggered()), this, SLOT(runCMakeContextMenu()));

}

void CMakeManager::updateContextMenu(ProjectExplorer::Project *project, ProjectExplorer::Node *node)
{
    Q_UNUSED(node);
    m_contextProject = project;
}

void CMakeManager::runCMake()
{
    runCMake(ProjectExplorer::ProjectExplorerPlugin::currentProject());
}

void CMakeManager::runCMakeContextMenu()
{
    runCMake(m_contextProject);
}

void CMakeManager::runCMake(ProjectExplorer::Project *project)
{
    if (!project)
        return;
    CMakeProject *cmakeProject = qobject_cast<CMakeProject *>(project);
    if (!cmakeProject || !cmakeProject->activeTarget() || !cmakeProject->activeTarget()->activeBuildConfiguration())
        return;

    CMakeBuildConfiguration *bc
            = static_cast<CMakeBuildConfiguration *>(cmakeProject->activeTarget()->activeBuildConfiguration());

    CMakeOpenProjectWizard copw(this, CMakeOpenProjectWizard::WantToUpdate,
                                CMakeOpenProjectWizard::BuildInfo(bc));
    if (copw.exec() == QDialog::Accepted) {
        cmakeProject->parseCMakeLists();
        if (copw.useOutOfSourceProject())
            cmakeProject->setUseOutOfSourceProject(copw.buildDirectory());
    }
}

ProjectExplorer::Project *CMakeManager::openProject(const QString &fileName, QString *errorString)
{
    Q_UNUSED(errorString)
    // TODO check whether this project is already opened
    QString cmakeFile = CMakeProject::cmakeFileFromOutOfSourceProject(fileName);
    if (!cmakeFile.isEmpty()) {
        CMakeProject *project = new CMakeProject(this, cmakeFile);
        project->setUseOutOfSourceProject(QFileInfo(fileName).absoluteDir().absolutePath());
        return project;
    }
    return new CMakeProject(this, fileName);
}

QString CMakeManager::mimeType() const
{
    return Constants::CMAKEMIMETYPE;
}

QString CMakeManager::cmakeExecutable() const
{
    return m_settingsPage->cmakeExecutable();
}

QString CMakeManager::ninjaExecutable() const
{
    return m_settingsPage->ninjaExecutable();
}

bool CMakeManager::isCMakeExecutableValid() const
{
    return m_settingsPage->isCMakeExecutableValid();
}

void CMakeManager::setCMakeExecutable(const QString &executable)
{
    m_settingsPage->setCMakeExecutable(executable);
}

bool CMakeManager::hasCodeBlocksMsvcGenerator() const
{
    return m_settingsPage->hasCodeBlocksMsvcGenerator();
}

bool CMakeManager::hasCodeBlocksNinjaGenerator() const
{
    return m_settingsPage->hasCodeBlocksNinjaGenerator();
}

// need to refactor this out
// we probably want the process instead of this function
// cmakeproject then could even run the cmake process in the background, adding the files afterwards
// sounds like a plan
void CMakeManager::createXmlFile(Utils::QtcProcess *proc, const QString &arguments,
                                 const QString &sourceDirectory, const QDir &buildDirectory,
                                 const Utils::Environment &env, const QString &generator)
{
    // We create a cbp file, only if we didn't find a cbp file in the base directory
    // Yet that can still override cbp files in subdirectories
    // And we are creating tons of files in the source directories
    // All of that is not really nice.
    // The mid term plan is to move away from the CodeBlocks Generator and use our own
    // QtCreator generator, which actually can be very similar to the CodeBlock Generator
    QString buildDirectoryPath = buildDirectory.absolutePath();
    buildDirectory.mkpath(buildDirectoryPath);
    proc->setWorkingDirectory(buildDirectoryPath);
    proc->setEnvironment(env);

    const QString srcdir = buildDirectory.exists(QLatin1String("CMakeCache.txt")) ?
                QString(QLatin1Char('.')) : sourceDirectory;
    QString args;
    Utils::QtcProcess::addArg(&args, srcdir);
    Utils::QtcProcess::addArgs(&args, arguments);
    Utils::QtcProcess::addArg(&args, generator);
    proc->setCommand(cmakeExecutable(), args);
    proc->start();
}

QString CMakeManager::findCbpFile(const QDir &directory)
{
    // Find the cbp file
    //   the cbp file is named like the project() command in the CMakeList.txt file
    //   so this method below could find the wrong cbp file, if the user changes the project()
    //   2name
    QDateTime t;
    QString file;
    foreach (const QString &cbpFile , directory.entryList()) {
        if (cbpFile.endsWith(QLatin1String(".cbp"))) {
            QFileInfo fi(directory.path() + QLatin1Char('/') + cbpFile);
            if (t.isNull() || fi.lastModified() > t) {
                file = directory.path() + QLatin1Char('/') + cbpFile;
                t = fi.lastModified();
            }
        }
    }
    return file;
}

// This code is duplicated from qtversionmanager
QString CMakeManager::qtVersionForQMake(const QString &qmakePath)
{
    QProcess qmake;
    qmake.start(qmakePath, QStringList(QLatin1String("--version")));
    if (!qmake.waitForStarted()) {
        qWarning("Cannot start '%s': %s", qPrintable(qmakePath), qPrintable(qmake.errorString()));
        return QString();
    }
    if (!qmake.waitForFinished())      {
        Utils::SynchronousProcess::stopProcess(qmake);
        qWarning("Timeout running '%s'.", qPrintable(qmakePath));
        return QString();
    }
    QString output = qmake.readAllStandardOutput();
    QRegExp regexp(QLatin1String("(QMake version|Qmake version:)[\\s]*([\\d.]*)"));
    regexp.indexIn(output);
    if (regexp.cap(2).startsWith(QLatin1String("2."))) {
        QRegExp regexp2(QLatin1String("Using Qt version[\\s]*([\\d\\.]*)"));
        regexp2.indexIn(output);
        return regexp2.cap(1);
    }
    return QString();
}

/////
// CMakeSettingsPage
////


CMakeSettingsPage::CMakeSettingsPage()
    :  m_pathchooser(0), m_pathchooserNinja(0)
{
    setId(QLatin1String("Z.CMake"));
    setDisplayName(tr("CMake"));
    setCategory(QLatin1String(ProjectExplorer::Constants::PROJECTEXPLORER_SETTINGS_CATEGORY));
    setDisplayCategory(QCoreApplication::translate("ProjectExplorer",
       ProjectExplorer::Constants::PROJECTEXPLORER_SETTINGS_TR_CATEGORY));
    setCategoryIcon(QLatin1String(ProjectExplorer::Constants::PROJECTEXPLORER_SETTINGS_CATEGORY_ICON));

    initValidator(CMake, &m_cmakeValidatorForUser, &m_cmakeValidatorForSystem);
    initValidator(Ninja, &m_ninjaValidatorForUser, &m_ninjaValidatorForSystem);

    connect(&m_cmakeValidatorForUser, SIGNAL(executableChanged()), this, SIGNAL(cmakeExecutableChanged()));
    connect(&m_cmakeValidatorForSystem, SIGNAL(executableChanged()), this, SIGNAL(cmakeExecutableChanged()));
}

void CMakeSettingsPage::initValidator(BuildCommand cmd, CMakeValidator *user, CMakeValidator *path)
{
    QSettings *settings = Core::ICore::settings();
    settings->beginGroup(QLatin1String("CMakeSettings"));
    QString key = QLatin1String(cmd == CMake ? "cmakeExecutable" : "ninjaExecutable");
    updateInfo(cmd, user, settings->value(key).toString());
    settings->endGroup();
    updateInfo(cmd, path, findExecutable(cmd, QStringList() << QCoreApplication::applicationDirPath()));
}

bool CMakeSettingsPage::isCMakeExecutableValid() const
{
    if (m_cmakeValidatorForUser.isValid())
        return true;

    return m_cmakeValidatorForSystem.isValid();
}

CMakeSettingsPage::~CMakeSettingsPage()
{
    m_cmakeValidatorForUser.cancel();
    m_cmakeValidatorForSystem.cancel();
}

QString CMakeSettingsPage::findExecutable(BuildCommand cmd, const QStringList &dirs) const
{
    Utils::Environment env = Utils::Environment::systemEnvironment();
    return env.searchInPath(QLatin1String(cmd == CMake ? "cmake" : "ninja"), dirs);
}

QWidget *CMakeSettingsPage::createPage(QWidget *parent)
{
    QWidget *outerWidget = new QWidget(parent);
    QFormLayout *formLayout = new QFormLayout(outerWidget);
    createExecutableChooser(formLayout, m_pathchooser, QLatin1String("  cmake "), m_cmakeValidatorForUser.executable());
    createExecutableChooser(formLayout, m_pathchooserNinja, QLatin1String("  ninja "), m_ninjaValidatorForUser.executable());
    return outerWidget;
}

void CMakeSettingsPage::createExecutableChooser(QFormLayout *formLayout, Utils::PathChooser *&pathchooser,
                                                const QString &name, const QString& path)
{
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    pathchooser = new Utils::PathChooser;
    pathchooser->setExpectedKind(Utils::PathChooser::ExistingCommand);
    formLayout->addRow(tr("Executable:") + name, pathchooser);
    formLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Ignored, QSizePolicy::MinimumExpanding));
    pathchooser->setPath(path);
}

void CMakeSettingsPage::updateInfo(BuildCommand cmd, CMakeValidator *validator, const QString &executable)
{
    saveSettings(cmd);
    if (cmd == Ninja) {
        validator->setExecutablePlain(executable);
    } else {
        validator->setExecutable(executable);
    }
}

void CMakeSettingsPage::saveSettings(BuildCommand cmd) const
{
    QSettings *settings = Core::ICore::settings();
    settings->beginGroup(QLatin1String("CMakeSettings"));
    if (cmd == CMake)
        settings->setValue(QLatin1String("cmakeExecutable"), m_cmakeValidatorForUser.executable());
    else
        settings->setValue(QLatin1String("ninjaExecutable"), m_ninjaValidatorForUser.executable());
    settings->endGroup();
}

void CMakeSettingsPage::apply()
{
    if (!m_pathchooser || !m_pathchooserNinja) // page was never shown
        return;
    if (m_cmakeValidatorForUser.executable() == m_pathchooser->path() &&
        m_ninjaValidatorForUser.executable() == m_pathchooserNinja->path())
        return;
    updateInfo(CMake, &m_cmakeValidatorForUser, m_pathchooser->path());
    updateInfo(Ninja, &m_ninjaValidatorForUser, m_pathchooserNinja->path());
}

void CMakeSettingsPage::finish()
{

}

QString CMakeSettingsPage::cmakeExecutable() const
{
    if (!isCMakeExecutableValid())
        return QString();

    if (m_cmakeValidatorForUser.isValid())
        return m_cmakeValidatorForUser.executable();
    if (m_cmakeValidatorForSystem.isValid())
        return m_cmakeValidatorForSystem.executable();
    return QString();
}

QString CMakeSettingsPage::ninjaExecutable() const
{
    if (!m_ninjaValidatorForUser.executable().isEmpty()) // TODO
        return m_ninjaValidatorForUser.executable();
    else
        return m_ninjaValidatorForSystem.executable();
}

void CMakeSettingsPage::setCMakeExecutable(const QString &executable)
{
    if (m_cmakeValidatorForUser.executable() == executable)
        return;
    updateInfo(CMake, &m_cmakeValidatorForUser, executable);
}

bool CMakeSettingsPage::hasCodeBlocksMsvcGenerator() const
{
    if (m_cmakeValidatorForUser.isValid())
        return m_cmakeValidatorForUser.hasCodeBlocksMsvcGenerator();
    if (m_cmakeValidatorForSystem.isValid())
        return m_cmakeValidatorForSystem.hasCodeBlocksMsvcGenerator();
    return false;
}

bool CMakeSettingsPage::hasCodeBlocksNinjaGenerator() const
{
    if (m_cmakeValidatorForUser.isValid())
        return m_cmakeValidatorForUser.hasCodeBlocksNinjaGenerator();
    if (m_cmakeValidatorForSystem.isValid())
        return m_cmakeValidatorForSystem.hasCodeBlocksNinjaGenerator();
    return false;
}

TextEditor::Keywords CMakeSettingsPage::keywords()
{
    if (m_cmakeValidatorForUser.isValid())
        return m_cmakeValidatorForUser.keywords();

    if (m_cmakeValidatorForSystem.isValid())
        return m_cmakeValidatorForSystem.keywords();

    return TextEditor::Keywords(QStringList(), QStringList(), QMap<QString, QStringList>());
}
