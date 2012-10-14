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

#include "cmakeproject.h"

#include "cmakebuildconfiguration.h"
#include "cmakeprojectconstants.h"
#include "cmakeprojectnodes.h"
#include "cmakerunconfiguration.h"
#include "makestep.h"
#include "cmakeopenprojectwizard.h"
#include "cmakebuildconfiguration.h"
#include "cmakeuicodemodelsupport.h"

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/headerpath.h>
#include <projectexplorer/buildenvironmentwidget.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/target.h>
#include <projectexplorer/deployconfiguration.h>
#include <qtsupport/customexecutablerunconfiguration.h>
#include <cpptools/ModelManagerInterface.h>
#include <extensionsystem/pluginmanager.h>
#include <utils/qtcassert.h>
#include <coreplugin/icore.h>
#include <coreplugin/infobar.h>
#include <coreplugin/editormanager/editormanager.h>

#include <QMap>
#include <QDebug>
#include <QDir>
#include <QDateTime>
#include <QProcess>
#include <QFormLayout>
#include <QInputDialog>
#include <QFileSystemWatcher>

using namespace CMakeProjectManager;
using namespace CMakeProjectManager::Internal;
using namespace ProjectExplorer;

// QtCreator CMake Generator wishlist:
// Which make targets we need to build to get all executables
// What is the make we need to call
// What is the actual compiler executable
// DEFINES

// Open Questions
// Who sets up the environment for cl.exe ? INCLUDEPATH and so on

// Test for form editor (loosely coupled)
static inline bool isFormWindowEditor(const QObject *o)
{
    return o && !qstrcmp(o->metaObject()->className(), "Designer::FormWindowEditor");
}

// Return contents of form editor (loosely coupled)
static inline QString formWindowEditorContents(const QObject *editor)
{
    const QVariant contentV = editor->property("contents");
    QTC_ASSERT(contentV.isValid(), return QString());
    return contentV.toString();
}

/*!
  \class CMakeProject
*/
CMakeProject::CMakeProject(CMakeManager *manager, const QString &fileName)
    : m_manager(manager),
      m_fileName(fileName),
      m_rootNode(new CMakeProjectNode(m_fileName)),
      m_lastEditor(0)
{
    setProjectContext(Core::Context(CMakeProjectManager::Constants::PROJECTCONTEXT));
    setProjectLanguage(Core::Context(ProjectExplorer::Constants::LANG_CXX));

    m_file = new CMakeFile(this, fileName);

    connect(this, SIGNAL(addedTarget(ProjectExplorer::Target*)),
            SLOT(targetAdded(ProjectExplorer::Target*)));
    connect(this, SIGNAL(buildTargetsChanged()),
            this, SLOT(updateRunConfigurations()));
}

CMakeProject::~CMakeProject()
{
    // Remove CodeModel support
    CPlusPlus::CppModelManagerInterface *modelManager
            = CPlusPlus::CppModelManagerInterface::instance();
    QMap<QString, CMakeUiCodeModelSupport *>::const_iterator it, end;
    it = m_uiCodeModelSupport.constBegin();
    end = m_uiCodeModelSupport.constEnd();
    for (; it!=end; ++it) {
        modelManager->removeEditorSupport(it.value());
        delete it.value();
    }

    m_codeModelFuture.cancel();
    delete m_rootNode;
}

void CMakeProject::fileChanged(const QString &fileName)
{
    Q_UNUSED(fileName)

    parseCMakeLists();
}

void CMakeProject::changeActiveBuildConfiguration(ProjectExplorer::BuildConfiguration *bc)
{
    if (!bc || bc->target() != activeTarget())
        return;

    CMakeBuildConfiguration *cmakebc = static_cast<CMakeBuildConfiguration *>(bc);

    // Pop up a dialog asking the user to rerun cmake
    QString cbpFile = CMakeManager::findCbpFile(QDir(bc->buildDirectory()));
    QFileInfo cbpFileFi(cbpFile);
    CMakeOpenProjectWizard::Mode mode = CMakeOpenProjectWizard::Nothing;
    if (!cbpFileFi.exists()) {
        mode = CMakeOpenProjectWizard::NeedToCreate;
    } else {
        foreach (const QString &file, m_watchedFiles) {
            if (QFileInfo(file).lastModified() > cbpFileFi.lastModified()) {
                mode = CMakeOpenProjectWizard::NeedToUpdate;
                break;
            }
        }
    }

    if (mode != CMakeOpenProjectWizard::Nothing) {
        CMakeOpenProjectWizard copw(m_manager, mode,
                                    CMakeOpenProjectWizard::BuildInfo(cmakebc));
        if (copw.exec() == QDialog::Accepted) {
            cmakebc->setUseNinja(copw.useNinja()); // NeedToCreate can change the Ninja setting
            if (copw.useOutOfSourceProject())
                setUseOutOfSourceProject(copw.buildDirectory());
        }
    }

    // reparse
    parseCMakeLists();
}

void CMakeProject::targetAdded(ProjectExplorer::Target *t)
{
    if (!t)
        return;

    connect(t, SIGNAL(activeBuildConfigurationChanged(ProjectExplorer::BuildConfiguration*)),
            SLOT(changeActiveBuildConfiguration(ProjectExplorer::BuildConfiguration*)));
}

void CMakeProject::changeBuildDirectory(CMakeBuildConfiguration *bc, const QString &newBuildDirectory)
{
    bc->setBuildDirectory(newBuildDirectory);
    parseCMakeLists();
}

QString CMakeProject::defaultBuildDirectory() const
{
    return projectDirectory() + QLatin1String("-build");
}


QString CMakeProject::outOfSourcePostfix()
{
    return QLatin1String(".qtcreator");
}

QString CMakeProject::outOfSourceProjectFileName(const QString &buildDir)
{
    QString name = QLatin1String("CMake");
    if (!m_projectName.isEmpty()) {
        name = m_projectName;
    } else if(!buildDir.isEmpty()) {
        CMakeCbpParser cbpparser;
        if (cbpparser.parseCbpFile(CMakeManager::findCbpFile(buildDir))) {
            name = cbpparser.projectName();
        }
    }
    return name + outOfSourcePostfix();
}

QString CMakeProject::cmakeFileFromOutOfSourceProject(const QString& filePath)
{
    if (!filePath.endsWith(outOfSourcePostfix()))
        return QString();
    QSettings ini(filePath, QSettings::IniFormat);
    return ini.value(QLatin1String("CMakeLists.txt")).toString();
}

void CMakeProject::setUseOutOfSourceProject(const QString &buildDir)
{
    if (m_outOfSourceProject != buildDir) {
        m_outOfSourceProject = buildDir;
        if (buildDir.isEmpty()) {
            setProperty(QByteArray("qtcUserFileName"), QVariant());
            setNamedSettings(QLatin1String("RecentName"), QVariant());
        } else {
            QString projectPath = buildDir + "/" + outOfSourceProjectFileName(buildDir);
            QSettings ini(projectPath, QSettings::IniFormat);
            ini.setValue(QLatin1String("CMakeLists.txt"), m_file->fileName());
            QString userFile = buildDir + "/CMakeLists.txt.user";
            setProperty(QByteArray("qtcUserFileName"), userFile);
            setNamedSettings(QLatin1String("RecentName"), QVariant(projectPath));
        }
    }
}

bool CMakeProject::parseCMakeLists()
{
    if (!activeTarget() ||
        !activeTarget()->activeBuildConfiguration()) {
        return false;
    }

    CMakeBuildConfiguration *activeBC = static_cast<CMakeBuildConfiguration *>(activeTarget()->activeBuildConfiguration());
    foreach (Core::IEditor *editor, Core::EditorManager::instance()->openedEditors())
        if (isProjectFile(editor->document()->fileName()))
            editor->document()->infoBar()->removeInfo(QLatin1String("CMakeEditor.RunCMake"));

    // Find cbp file
    QString cbpFile = CMakeManager::findCbpFile(activeBC->buildDirectory());

    if (cbpFile.isEmpty()) {
        emit buildTargetsChanged();
        return false;
    }

    // setFolderName
    m_rootNode->setDisplayName(QFileInfo(cbpFile).completeBaseName());
    CMakeCbpParser cbpparser;
    // Parsing
    //qDebug()<<"Parsing file "<<cbpFile;
    if (!cbpparser.parseCbpFile(cbpFile)) {
        // TODO report error
        emit buildTargetsChanged();
        return false;
    }

    foreach (const QString &file, m_watcher->files())
        if (file != cbpFile)
            m_watcher->removePath(file);

    // how can we ensure that it is completely written?
    m_watcher->addPath(cbpFile);

    m_projectName = cbpparser.projectName();
    m_rootNode->setDisplayName(cbpparser.projectName());

    //qDebug()<<"Building Tree";
    QList<ProjectExplorer::FileNode *> fileList = cbpparser.fileList();
    QSet<QString> projectFiles;
    if (cbpparser.hasCMakeFiles()) {
        fileList.append(cbpparser.cmakeFileList());
        foreach (const ProjectExplorer::FileNode *node, cbpparser.cmakeFileList())
            projectFiles.insert(node->path());
    } else {
        // Manually add the CMakeLists.txt file
        QString cmakeListTxt = projectDirectory() + "/CMakeLists.txt";
        bool generated = false;
        fileList.append(new ProjectExplorer::FileNode(cmakeListTxt, ProjectExplorer::ProjectFileType, generated));
        projectFiles.insert(cmakeListTxt);
    }

    m_watchedFiles = projectFiles;

    m_files.clear();
    foreach (ProjectExplorer::FileNode *fn, fileList)
        m_files.append(fn->path());
    m_files.sort();

    buildTree(m_rootNode, fileList);

    //qDebug()<<"Adding Targets";
    m_buildTargets = cbpparser.buildTargets();
//        qDebug()<<"Printing targets";
//        foreach (CMakeBuildTarget ct, m_buildTargets) {
//            qDebug()<<ct.title<<" with executable:"<<ct.executable;
//            qDebug()<<"WD:"<<ct.workingDirectory;
//            qDebug()<<ct.makeCommand<<ct.makeCleanCommand;
//            qDebug()<<"";
//        }


    // TOOD this code ain't very pretty ...
    m_uicCommand.clear();
    QFile cmakeCache(activeBC->buildDirectory() + "/CMakeCache.txt");
    cmakeCache.open(QIODevice::ReadOnly);
    while (!cmakeCache.atEnd()) {
        QString line = cmakeCache.readLine();
        if (line.startsWith("QT_UIC_EXECUTABLE")) {
            if (int pos = line.indexOf('=')) {
                m_uicCommand = line.mid(pos + 1).trimmed();
            }
            break;
        }
    }
    cmakeCache.close();

    createUiCodeModelSupport();

    Kit *k = activeTarget()->kit();
    ToolChain *tc = ProjectExplorer::ToolChainKitInformation::toolChain(k);
    if (!tc) {
        emit buildTargetsChanged();
        emit fileListChanged();
        return true;
    }

    QStringList allIncludePaths;
    // This explicitly adds -I. to the include paths
    allIncludePaths.append(projectDirectory());
    allIncludePaths.append(cbpparser.includeFiles());

    QByteArray allDefines;
    allDefines.append(tc->predefinedMacros(QStringList()));
    allDefines.append(cbpparser.defines());

    QStringList allFrameworkPaths;
    QList<ProjectExplorer::HeaderPath> allHeaderPaths;
    allHeaderPaths = tc->systemHeaderPaths(SysRootKitInformation::sysRoot(k));
    foreach (const ProjectExplorer::HeaderPath &headerPath, allHeaderPaths) {
        if (headerPath.kind() == ProjectExplorer::HeaderPath::FrameworkHeaderPath)
            allFrameworkPaths.append(headerPath.path());
        else
            allIncludePaths.append(headerPath.path());
    }

    CPlusPlus::CppModelManagerInterface *modelmanager =
            CPlusPlus::CppModelManagerInterface::instance();
    if (modelmanager) {
        CPlusPlus::CppModelManagerInterface::ProjectInfo pinfo = modelmanager->projectInfo(this);
        if (pinfo.includePaths() != allIncludePaths
                || pinfo.sourceFiles() != m_files
                || pinfo.defines() != allDefines
                || pinfo.frameworkPaths() != allFrameworkPaths)  {
            pinfo.clearProjectParts();
            CPlusPlus::CppModelManagerInterface::ProjectPart::Ptr part(
                        new CPlusPlus::CppModelManagerInterface::ProjectPart);
            part->includePaths = allIncludePaths;
            // TODO we only want C++ files, not all other stuff that might be in the project
            part->sourceFiles = m_files;
            part->defines = allDefines;
            part->frameworkPaths = allFrameworkPaths;
            part->language = CPlusPlus::CppModelManagerInterface::CXX;
            pinfo.appendProjectPart(part);
            modelmanager->updateProjectInfo(pinfo);
            m_codeModelFuture.cancel();
            m_codeModelFuture = modelmanager->updateSourceFiles(m_files);
        }
    }
    emit buildTargetsChanged();
    emit fileListChanged();

    return true;
}

bool CMakeProject::isProjectFile(const QString &fileName)
{
    return m_watchedFiles.contains(fileName);
}

QList<CMakeBuildTarget> CMakeProject::buildTargets() const
{
    return m_buildTargets;
}

QStringList CMakeProject::buildTargetTitles() const
{
    QStringList results;
    foreach (const CMakeBuildTarget &ct, m_buildTargets) {
        if (ct.executable.isEmpty())
            continue;
        if (ct.title.endsWith(QLatin1String("/fast")))
            continue;
        results << ct.title;
    }
    return results;
}

bool CMakeProject::hasBuildTarget(const QString &title) const
{
    foreach (const CMakeBuildTarget &ct, m_buildTargets) {
        if (ct.executable.isEmpty())
            continue;
        if (ct.title.endsWith(QLatin1String("/fast")))
            continue;
        if (ct.title == title)
            return true;
    }
    return false;
}

void CMakeProject::gatherFileNodes(ProjectExplorer::FolderNode *parent, QList<ProjectExplorer::FileNode *> &list)
{
    foreach (ProjectExplorer::FolderNode *folder, parent->subFolderNodes())
        gatherFileNodes(folder, list);
    foreach (ProjectExplorer::FileNode *file, parent->fileNodes())
        list.append(file);
}

bool sortNodesByPath(Node *a, Node *b)
{
    return a->path() < b->path();
}

void CMakeProject::buildTree(CMakeProjectNode *rootNode, QList<ProjectExplorer::FileNode *> newList)
{
    // Gather old list
    QList<ProjectExplorer::FileNode *> oldList;
    gatherFileNodes(rootNode, oldList);
    qSort(oldList.begin(), oldList.end(), sortNodesByPath);
    qSort(newList.begin(), newList.end(), sortNodesByPath);

    // generate added and deleted list
    QList<ProjectExplorer::FileNode *>::const_iterator oldIt  = oldList.constBegin();
    QList<ProjectExplorer::FileNode *>::const_iterator oldEnd = oldList.constEnd();
    QList<ProjectExplorer::FileNode *>::const_iterator newIt  = newList.constBegin();
    QList<ProjectExplorer::FileNode *>::const_iterator newEnd = newList.constEnd();

    QList<ProjectExplorer::FileNode *> added;
    QList<ProjectExplorer::FileNode *> deleted;

    while (oldIt != oldEnd && newIt != newEnd) {
        if ( (*oldIt)->path() == (*newIt)->path()) {
            delete *newIt;
            ++oldIt;
            ++newIt;
        } else if ((*oldIt)->path() < (*newIt)->path()) {
            deleted.append(*oldIt);
            ++oldIt;
        } else {
            added.append(*newIt);
            ++newIt;
        }
    }

    while (oldIt != oldEnd) {
        deleted.append(*oldIt);
        ++oldIt;
    }

    while (newIt != newEnd) {
        added.append(*newIt);
        ++newIt;
    }

    // add added nodes
    foreach (ProjectExplorer::FileNode *fn, added) {
//        qDebug()<<"added"<<fn->path();
        // Get relative path to rootNode
        QString parentDir = QFileInfo(fn->path()).absolutePath();
        ProjectExplorer::FolderNode *folder = findOrCreateFolder(rootNode, parentDir);
        rootNode->addFileNodes(QList<ProjectExplorer::FileNode *>()<< fn, folder);
    }

    // remove old file nodes and check whether folder nodes can be removed
    foreach (ProjectExplorer::FileNode *fn, deleted) {
        ProjectExplorer::FolderNode *parent = fn->parentFolderNode();
//        qDebug()<<"removed"<<fn->path();
        rootNode->removeFileNodes(QList<ProjectExplorer::FileNode *>() << fn, parent);
        // Check for empty parent
        while (parent->subFolderNodes().isEmpty() && parent->fileNodes().isEmpty()) {
            ProjectExplorer::FolderNode *grandparent = parent->parentFolderNode();
            rootNode->removeFolderNodes(QList<ProjectExplorer::FolderNode *>() << parent, grandparent);
            parent = grandparent;
            if (parent == rootNode)
                break;
        }
    }
}

ProjectExplorer::FolderNode *CMakeProject::findOrCreateFolder(CMakeProjectNode *rootNode, QString directory)
{
    QString relativePath = QDir(QFileInfo(rootNode->path()).path()).relativeFilePath(directory);
    QStringList parts = relativePath.split(QLatin1Char('/'), QString::SkipEmptyParts);
    ProjectExplorer::FolderNode *parent = rootNode;
    QString path = QFileInfo(rootNode->path()).path();
    foreach (const QString &part, parts) {
        path += QLatin1Char('/');
        path += part;
        // Find folder in subFolders
        bool found = false;
        foreach (ProjectExplorer::FolderNode *folder, parent->subFolderNodes()) {
            if (folder->path() == path) {
                // yeah found something :)
                parent = folder;
                found = true;
                break;
            }
        }
        if (!found) {
            // No FolderNode yet, so create it
            ProjectExplorer::FolderNode *tmp = new ProjectExplorer::FolderNode(path);
            tmp->setDisplayName(part);
            rootNode->addFolderNodes(QList<ProjectExplorer::FolderNode *>() << tmp, parent);
            parent = tmp;
        }
    }
    return parent;
}

QString CMakeProject::displayName() const
{
    return m_projectName;
}

Core::Id CMakeProject::id() const
{
    return Core::Id(Constants::CMAKEPROJECT_ID);
}

Core::IDocument *CMakeProject::document() const
{
    return m_file;
}

CMakeManager *CMakeProject::projectManager() const
{
    return m_manager;
}

QList<ProjectExplorer::BuildConfigWidget*> CMakeProject::subConfigWidgets()
{
    QList<ProjectExplorer::BuildConfigWidget*> list;
    list << new BuildEnvironmentWidget;
    return list;
}

ProjectExplorer::ProjectNode *CMakeProject::rootProjectNode() const
{
    return m_rootNode;
}


QStringList CMakeProject::files(FilesMode fileMode) const
{
    Q_UNUSED(fileMode)
    return m_files;
}

bool CMakeProject::fromMap(const QVariantMap &map)
{
    if (!Project::fromMap(map))
        return false;

    bool hasUserFile = activeTarget();
    if (!hasUserFile) {
        CMakeOpenProjectWizard copw(m_manager, projectDirectory(), Utils::Environment::systemEnvironment());
        if (copw.exec() != QDialog::Accepted)
            return false;
        if (copw.useOutOfSourceProject())
            setUseOutOfSourceProject(copw.buildDirectory());
        Kit *k = copw.kit();
        Target *t = new Target(this, k);
        CMakeBuildConfiguration *bc(new CMakeBuildConfiguration(t));
        bc->setDefaultDisplayName("all");
        bc->setUseNinja(copw.useNinja());
        bc->setBuildDirectory(copw.buildDirectory());
        ProjectExplorer::BuildStepList *buildSteps = bc->stepList(ProjectExplorer::Constants::BUILDSTEPS_BUILD);
        ProjectExplorer::BuildStepList *cleanSteps = bc->stepList(ProjectExplorer::Constants::BUILDSTEPS_CLEAN);

        // Now create a standard build configuration
        buildSteps->insertStep(0, new MakeStep(buildSteps));

        MakeStep *cleanMakeStep = new MakeStep(cleanSteps);
        cleanSteps->insertStep(0, cleanMakeStep);
        cleanMakeStep->setAdditionalArguments("clean");
        cleanMakeStep->setClean(true);

        t->addBuildConfiguration(bc);

        DeployConfigurationFactory *fac = ExtensionSystem::PluginManager::instance()->getObject<DeployConfigurationFactory>();
        ProjectExplorer::DeployConfiguration *dc = fac->create(t, ProjectExplorer::Constants::DEFAULT_DEPLOYCONFIGURATION_ID);
        t->addDeployConfiguration(dc);

        addTarget(t);
    } else {
        // We have a user file, but we could still be missing the cbp file
        // or simply run createXml with the saved settings
        QFileInfo sourceFileInfo(m_fileName);
        CMakeBuildConfiguration *activeBC = qobject_cast<CMakeBuildConfiguration *>(activeTarget()->activeBuildConfiguration());
        if (!activeBC)
            return false;
        QString cbpFile = CMakeManager::findCbpFile(QDir(activeBC->buildDirectory()));
        QFileInfo cbpFileFi(cbpFile);

        CMakeOpenProjectWizard::Mode mode = CMakeOpenProjectWizard::Nothing;
        if (!cbpFileFi.exists())
            mode = CMakeOpenProjectWizard::NeedToCreate;
        else if (cbpFileFi.lastModified() < sourceFileInfo.lastModified())
            mode = CMakeOpenProjectWizard::NeedToUpdate;

        if (mode != CMakeOpenProjectWizard::Nothing) {
            CMakeOpenProjectWizard copw(m_manager, mode,
                                        CMakeOpenProjectWizard::BuildInfo(activeBC));
            if (copw.exec() != QDialog::Accepted) {
                return false;
            } else {
                activeBC->setUseNinja(copw.useNinja());
                if (copw.useOutOfSourceProject())
                    setUseOutOfSourceProject(copw.buildDirectory());
            }
        }
    }

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, SIGNAL(fileChanged(QString)), this, SLOT(fileChanged(QString)));

    parseCMakeLists();

    if (!hasUserFile && hasBuildTarget("all")) {
        MakeStep *makeStep = qobject_cast<MakeStep *>(
                    activeTarget()->activeBuildConfiguration()->stepList(ProjectExplorer::Constants::BUILDSTEPS_BUILD)->at(0));
        Q_ASSERT(makeStep);
        makeStep->setBuildTarget("all", true);
    }

    connect(Core::EditorManager::instance(), SIGNAL(editorAboutToClose(Core::IEditor*)),
            this, SLOT(editorAboutToClose(Core::IEditor*)));

    connect(Core::EditorManager::instance(), SIGNAL(currentEditorChanged(Core::IEditor*)),
            this, SLOT(editorChanged(Core::IEditor*)));

    connect(ProjectExplorer::ProjectExplorerPlugin::instance()->buildManager(), SIGNAL(buildStateChanged(ProjectExplorer::Project*)),
            this, SLOT(buildStateChanged(ProjectExplorer::Project*)));

    return true;
}

bool CMakeProject::setupTarget(Target *t)
{
    CMakeBuildConfigurationFactory *factory
            = ExtensionSystem::PluginManager::instance()->getObject<CMakeBuildConfigurationFactory>();
    CMakeBuildConfiguration *bc = factory->create(t, Constants::CMAKE_BC_ID, QLatin1String("all"));
    if (!bc)
        return false;

    t->addBuildConfiguration(bc);

    DeployConfigurationFactory *fac = ExtensionSystem::PluginManager::instance()->getObject<DeployConfigurationFactory>();
    ProjectExplorer::DeployConfiguration *dc = fac->create(t, ProjectExplorer::Constants::DEFAULT_DEPLOYCONFIGURATION_ID);
    t->addDeployConfiguration(dc);
    return true;
}

CMakeBuildTarget CMakeProject::buildTargetForTitle(const QString &title)
{
    foreach (const CMakeBuildTarget &ct, m_buildTargets)
        if (ct.title == title)
            return ct;
    return CMakeBuildTarget();
}

QString CMakeProject::uicCommand() const
{
    return m_uicCommand;
}

QString CMakeProject::uiHeaderFile(const QString &uiFile)
{
    QDir srcDirRoot = QDir(projectDirectory());
    QString relativePath = srcDirRoot.relativeFilePath(uiFile);
    QDir buildDir = QDir(activeTarget()->activeBuildConfiguration()->buildDirectory());
    QString uiHeaderFilePath = buildDir.absoluteFilePath(relativePath);

    QFileInfo fi(uiHeaderFilePath);
    uiHeaderFilePath = fi.absolutePath();
    uiHeaderFilePath += QLatin1String("/ui_");
    uiHeaderFilePath += fi.completeBaseName();
    uiHeaderFilePath += QLatin1String(".h");
    return QDir::cleanPath(uiHeaderFilePath);
}

void CMakeProject::updateRunConfigurations()
{
    foreach (Target *t, targets())
        updateRunConfigurations(t);
}

// TODO Compare with updateDefaultRunConfigurations();
void CMakeProject::updateRunConfigurations(Target *t)
{
    // *Update* runconfigurations:
    QMultiMap<QString, CMakeRunConfiguration*> existingRunConfigurations;
    QList<ProjectExplorer::RunConfiguration *> toRemove;
    foreach (ProjectExplorer::RunConfiguration *rc, t->runConfigurations()) {
        if (CMakeRunConfiguration* cmakeRC = qobject_cast<CMakeRunConfiguration *>(rc))
            existingRunConfigurations.insert(cmakeRC->title(), cmakeRC);
        QtSupport::CustomExecutableRunConfiguration *ceRC =
                qobject_cast<QtSupport::CustomExecutableRunConfiguration *>(rc);
        if (ceRC && !ceRC->isConfigured())
            toRemove << rc;
    }

    foreach (const CMakeBuildTarget &ct, buildTargets()) {
        if (ct.library)
            continue;
        if (ct.executable.isEmpty())
            continue;
        if (ct.title.endsWith("/fast"))
            continue;
        QList<CMakeRunConfiguration *> list = existingRunConfigurations.values(ct.title);
        if (!list.isEmpty()) {
            // Already exists, so override the settings...
            foreach (CMakeRunConfiguration *rc, list) {
                rc->setExecutable(ct.executable);
                rc->setBaseWorkingDirectory(ct.workingDirectory);
                rc->setEnabled(true);
            }
            existingRunConfigurations.remove(ct.title);
        } else {
            // Does not exist yet
            Core::Id id = CMakeRunConfigurationFactory::idFromBuildTarget(ct.title);
            CMakeRunConfiguration *rc = new CMakeRunConfiguration(t, id, ct.executable,
                                                                  ct.workingDirectory, ct.title);
            t->addRunConfiguration(rc);
        }
    }
    QMultiMap<QString, CMakeRunConfiguration *>::const_iterator it =
            existingRunConfigurations.constBegin();
    for ( ; it != existingRunConfigurations.constEnd(); ++it) {
        CMakeRunConfiguration *rc = it.value();
        // The executables for those runconfigurations aren't build by the current buildconfiguration
        // We just set a disable flag and show that in the display name
        rc->setEnabled(false);
        // removeRunConfiguration(rc);
    }

    foreach (ProjectExplorer::RunConfiguration *rc, toRemove)
        t->removeRunConfiguration(rc);

    if (t->runConfigurations().isEmpty()) {
        // Oh no, no run configuration,
        // create a custom executable run configuration
        t->addRunConfiguration(new QtSupport::CustomExecutableRunConfiguration(t));
    }
}

void CMakeProject::createUiCodeModelSupport()
{
//    qDebug()<<"creatUiCodeModelSupport()";
    CPlusPlus::CppModelManagerInterface *modelManager
            = CPlusPlus::CppModelManagerInterface::instance();

    // First move all to
    QMap<QString, CMakeUiCodeModelSupport *> oldCodeModelSupport;
    oldCodeModelSupport = m_uiCodeModelSupport;
    m_uiCodeModelSupport.clear();

    // Find all ui files
    foreach (const QString &uiFile, m_files) {
        if (uiFile.endsWith(".ui")) {
            // UI file, not convert to
            QString uiHeaderFilePath = uiHeaderFile(uiFile);
            QMap<QString, CMakeUiCodeModelSupport *>::iterator it
                    = oldCodeModelSupport.find(uiFile);
            if (it != oldCodeModelSupport.end()) {
                //                qDebug()<<"updated old codemodelsupport";
                CMakeUiCodeModelSupport *cms = it.value();
                cms->setFileName(uiHeaderFilePath);
                m_uiCodeModelSupport.insert(it.key(), cms);
                oldCodeModelSupport.erase(it);
            } else {
                //                qDebug()<<"adding new codemodelsupport";
                CMakeUiCodeModelSupport *cms = new CMakeUiCodeModelSupport(modelManager, this, uiFile, uiHeaderFilePath);
                m_uiCodeModelSupport.insert(uiFile, cms);
                modelManager->addEditorSupport(cms);
            }
        }
    }

    // Remove old
    QMap<QString, CMakeUiCodeModelSupport *>::const_iterator it, end;
    end = oldCodeModelSupport.constEnd();
    for (it = oldCodeModelSupport.constBegin(); it!=end; ++it) {
        modelManager->removeEditorSupport(it.value());
        delete it.value();
    }
}

void CMakeProject::updateCodeModelSupportFromEditor(const QString &uiFileName,
                                                    const QString &contents)
{
    const QMap<QString, CMakeUiCodeModelSupport *>::const_iterator it =
            m_uiCodeModelSupport.constFind(uiFileName);
    if (it != m_uiCodeModelSupport.constEnd())
        it.value()->updateFromEditor(contents);
}

void CMakeProject::editorChanged(Core::IEditor *editor)
{
    // Handle old editor
    if (isFormWindowEditor(m_lastEditor)) {
        disconnect(m_lastEditor, SIGNAL(changed()), this, SLOT(uiEditorContentsChanged()));
        if (m_dirtyUic) {
            const QString contents =  formWindowEditorContents(m_lastEditor);
            updateCodeModelSupportFromEditor(m_lastEditor->document()->fileName(), contents);
            m_dirtyUic = false;
        }
    }

    m_lastEditor = editor;

    // Handle new editor
    if (isFormWindowEditor(editor))
        connect(editor, SIGNAL(changed()), this, SLOT(uiEditorContentsChanged()));
}

void CMakeProject::editorAboutToClose(Core::IEditor *editor)
{
    if (m_lastEditor == editor) {
        // Oh no our editor is going to be closed
        // get the content first
        if (isFormWindowEditor(m_lastEditor)) {
            disconnect(m_lastEditor, SIGNAL(changed()), this, SLOT(uiEditorContentsChanged()));
            if (m_dirtyUic) {
                const QString contents = formWindowEditorContents(m_lastEditor);
                updateCodeModelSupportFromEditor(m_lastEditor->document()->fileName(), contents);
                m_dirtyUic = false;
            }
        }
        m_lastEditor = 0;
    }
}

void CMakeProject::uiEditorContentsChanged()
{
    // cast sender, get filename
    if (!m_dirtyUic && isFormWindowEditor(sender()))
        m_dirtyUic = true;
}

void CMakeProject::buildStateChanged(ProjectExplorer::Project *project)
{
    if (project == this) {
        if (!ProjectExplorer::ProjectExplorerPlugin::instance()->buildManager()->isBuilding(this)) {
            QMap<QString, CMakeUiCodeModelSupport *>::const_iterator it, end;
            end = m_uiCodeModelSupport.constEnd();
            for (it = m_uiCodeModelSupport.constBegin(); it != end; ++it) {
                it.value()->updateFromBuild();
            }
        }
    }
}

// CMakeFile

CMakeFile::CMakeFile(CMakeProject *parent, QString fileName)
    : Core::IDocument(parent), m_project(parent), m_fileName(fileName)
{

}

bool CMakeFile::save(QString *errorString, const QString &fileName, bool autoSave)
{
    // Once we have an texteditor open for this file, we probably do
    // need to implement this, don't we.
    Q_UNUSED(errorString)
    Q_UNUSED(fileName)
    Q_UNUSED(autoSave)
    return false;
}

QString CMakeFile::fileName() const
{
    return m_fileName;
}

QString CMakeFile::defaultPath() const
{
    return QString();
}

QString CMakeFile::suggestedFileName() const
{
    return QString();
}

QString CMakeFile::mimeType() const
{
    return Constants::CMAKEMIMETYPE;
}


bool CMakeFile::isModified() const
{
    return false;
}

bool CMakeFile::isSaveAsAllowed() const
{
    return false;
}

void CMakeFile::rename(const QString &newName)
{
    Q_ASSERT(false);
    Q_UNUSED(newName);
    // Can't happen....
}

Core::IDocument::ReloadBehavior CMakeFile::reloadBehavior(ChangeTrigger state, ChangeType type) const
{
    Q_UNUSED(state)
    Q_UNUSED(type)
    return BehaviorSilent;
}

bool CMakeFile::reload(QString *errorString, ReloadFlag flag, ChangeType type)
{
    Q_UNUSED(errorString)
    Q_UNUSED(flag)
    Q_UNUSED(type)
    return true;
}

CMakeBuildSettingsWidget::CMakeBuildSettingsWidget() : m_buildConfiguration(0)
{
    QFormLayout *fl = new QFormLayout(this);
    fl->setContentsMargins(20, -1, 0, -1);
    fl->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    setLayout(fl);

    QPushButton *runCmakeButton = new QPushButton(tr("Run cmake"));
    connect(runCmakeButton, SIGNAL(clicked()),
            this, SLOT(runCMake()));
    fl->addRow(tr("Reconfigure project:"), runCmakeButton);

    m_pathLineEdit = new QLineEdit(this);
    m_pathLineEdit->setReadOnly(true);

    QHBoxLayout *hbox = new QHBoxLayout();
    hbox->addWidget(m_pathLineEdit);

    m_changeButton = new QPushButton(this);
    m_changeButton->setText(tr("&Change"));
    connect(m_changeButton, SIGNAL(clicked()), this, SLOT(openChangeBuildDirectoryDialog()));
    hbox->addWidget(m_changeButton);

    fl->addRow(tr("Build directory:"), hbox);
}

QString CMakeBuildSettingsWidget::displayName() const
{
    return "CMake";
}

void CMakeBuildSettingsWidget::init(BuildConfiguration *bc)
{
    m_buildConfiguration = static_cast<CMakeBuildConfiguration *>(bc);
    m_pathLineEdit->setText(m_buildConfiguration->buildDirectory());
    if (m_buildConfiguration->buildDirectory() == bc->target()->project()->projectDirectory())
        m_changeButton->setEnabled(false);
    else
        m_changeButton->setEnabled(true);
}

void CMakeBuildSettingsWidget::openChangeBuildDirectoryDialog()
{
    CMakeProject *project = static_cast<CMakeProject *>(m_buildConfiguration->target()->project());
    CMakeOpenProjectWizard copw(project->projectManager(), CMakeOpenProjectWizard::ChangeDirectory,
                                CMakeOpenProjectWizard::BuildInfo(m_buildConfiguration));
    if (copw.exec() == QDialog::Accepted) {
        project->changeBuildDirectory(m_buildConfiguration, copw.buildDirectory());
        m_buildConfiguration->setUseNinja(copw.useNinja());
        m_pathLineEdit->setText(m_buildConfiguration->buildDirectory());
        if (copw.useOutOfSourceProject())
            project->setUseOutOfSourceProject(copw.buildDirectory());
    }
}

void CMakeBuildSettingsWidget::runCMake()
{
    CMakeProject *project = static_cast<CMakeProject *>(m_buildConfiguration->target()->project());
    CMakeOpenProjectWizard copw(project->projectManager(),
                                CMakeOpenProjectWizard::WantToUpdate,
                                CMakeOpenProjectWizard::BuildInfo(m_buildConfiguration));
    if (copw.exec() == QDialog::Accepted) {
        project->parseCMakeLists();
        if (copw.useOutOfSourceProject())
            project->setUseOutOfSourceProject(copw.buildDirectory());
    }
}


/////
// CMakeCbpParser
////

bool CMakeCbpParser::parseCbpFile(const QString &fileName)
{
    QFile fi(fileName);
    if (fi.exists() && fi.open(QFile::ReadOnly)) {
        setDevice(&fi);

        while (!atEnd()) {
            readNext();
            if (name() == "CodeBlocks_project_file") {
                parseCodeBlocks_project_file();
            } else if (isStartElement()) {
                parseUnknownElement();
            }
        }
        fi.close();
        m_includeFiles.sort();
        m_includeFiles.removeDuplicates();
        return true;
    }
    return false;
}

void CMakeCbpParser::parseCodeBlocks_project_file()
{
    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            return;
        } else if (name() == "Project") {
            parseProject();
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

void CMakeCbpParser::parseProject()
{
    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            return;
        } else if (name() == "Option") {
            parseOption();
        } else if (name() == "Unit") {
            parseUnit();
        } else if (name() == "Build") {
            parseBuild();
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

void CMakeCbpParser::parseBuild()
{
    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            return;
        } else if (name() == "Target") {
            parseBuildTarget();
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

void CMakeCbpParser::parseBuildTarget()
{
    m_buildTargetType = false;
    m_buildTarget.clear();

    if (attributes().hasAttribute("title"))
        m_buildTarget.title = attributes().value("title").toString();
    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            if (m_buildTargetType || m_buildTarget.title == "all" || m_buildTarget.title == "install") {
                m_buildTargets.append(m_buildTarget);
            }
            return;
        } else if (name() == "Compiler") {
            parseCompiler();
        } else if (name() == "Option") {
            parseBuildTargetOption();
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

void CMakeCbpParser::parseBuildTargetOption()
{
    if (attributes().hasAttribute("output")) {
        m_buildTarget.executable = attributes().value("output").toString();
    } else if (attributes().hasAttribute("type") && (attributes().value("type") == "1" || attributes().value("type") == "0")) {
        m_buildTargetType = true;
    } else if (attributes().hasAttribute("type") && (attributes().value("type") == "3" || attributes().value("type") == "2")) {
        m_buildTargetType = true;
        m_buildTarget.library = true;
    } else if (attributes().hasAttribute("working_dir")) {
        m_buildTarget.workingDirectory = attributes().value("working_dir").toString();
    }
    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            return;
        } else if (name() == "MakeCommand") {
            parseMakeCommand();
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

QString CMakeCbpParser::projectName() const
{
    return m_projectName;
}

void CMakeCbpParser::parseOption()
{
    if (attributes().hasAttribute("title"))
        m_projectName = attributes().value("title").toString();

    if (attributes().hasAttribute("compiler"))
        m_compiler = attributes().value("compiler").toString();

    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            return;
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

void CMakeCbpParser::parseMakeCommand()
{
    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            return;
        } else if (name() == "Build") {
            parseBuildTargetBuild();
        } else if (name() == "Clean") {
            parseBuildTargetClean();
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

void CMakeCbpParser::parseBuildTargetBuild()
{
    if (attributes().hasAttribute("command"))
        m_buildTarget.makeCommand = attributes().value("command").toString();
    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            return;
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

void CMakeCbpParser::parseBuildTargetClean()
{
    if (attributes().hasAttribute("command"))
        m_buildTarget.makeCleanCommand = attributes().value("command").toString();
    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            return;
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

void CMakeCbpParser::parseCompiler()
{
    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            return;
        } else if (name() == "Add") {
            parseAdd();
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

void CMakeCbpParser::parseAdd()
{
    // CMake only supports <Add option=\> and <Add directory=\>
    const QXmlStreamAttributes addAttributes = attributes();

    const QString includeDirectory = addAttributes.value("directory").toString();
    // allow adding multiple times because order happens
    if (!includeDirectory.isEmpty()) {
        m_includeFiles.append(includeDirectory);
    }

    QString compilerOption = addAttributes.value("option").toString();
    // defining multiple times a macro to the same value makes no sense
    if (!compilerOption.isEmpty() && !m_compilerOptions.contains(compilerOption)) {
        m_compilerOptions.append(compilerOption);
        int macroNameIndex = compilerOption.indexOf("-D") + 2;
        if (macroNameIndex != 1) {
            int assignIndex = compilerOption.indexOf('=', macroNameIndex);
            if (assignIndex != -1) {
                compilerOption[assignIndex] = ' ';
            }
            m_defines.append("#define ");
            m_defines.append(compilerOption.mid(macroNameIndex).toAscii());
            m_defines.append('\n');
        }
    }

    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            return;
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

void CMakeCbpParser::parseUnit()
{
    //qDebug()<<stream.attributes().value("filename");
    QString fileName = attributes().value("filename").toString();
    m_parsingCmakeUnit = false;
    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            if (!fileName.endsWith(QLatin1String(".rule")) && !m_processedUnits.contains(fileName)) {
                // Now check whether we found a virtual element beneath
                if (m_parsingCmakeUnit) {
                    m_cmakeFileList.append( new ProjectExplorer::FileNode(fileName, ProjectExplorer::ProjectFileType, false));
                } else {
                    bool generated = false;
                    QString onlyFileName = QFileInfo(fileName).fileName();
                    if (   (onlyFileName.startsWith("moc_") && onlyFileName.endsWith(".cxx"))
                        || (onlyFileName.startsWith("ui_") && onlyFileName.endsWith(".h"))
                        || (onlyFileName.startsWith("qrc_") && onlyFileName.endsWith(".cxx")))
                        generated = true;

                    if (fileName.endsWith(QLatin1String(".qrc")))
                        m_fileList.append( new ProjectExplorer::FileNode(fileName, ProjectExplorer::ResourceType, generated));
                    else
                        m_fileList.append( new ProjectExplorer::FileNode(fileName, ProjectExplorer::SourceType, generated));
                }
                m_processedUnits.insert(fileName);
            }
            return;
        } else if (name() == "Option") {
            parseUnitOption();
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

void CMakeCbpParser::parseUnitOption()
{
    if (attributes().hasAttribute("virtualFolder"))
        m_parsingCmakeUnit = true;

    while (!atEnd()) {
        readNext();

        if (isEndElement())
            break;

        if (isStartElement())
            parseUnknownElement();
    }
}

void CMakeCbpParser::parseUnknownElement()
{
    Q_ASSERT(isStartElement());

    while (!atEnd()) {
        readNext();

        if (isEndElement())
            break;

        if (isStartElement())
            parseUnknownElement();
    }
}

QList<ProjectExplorer::FileNode *> CMakeCbpParser::fileList()
{
    return m_fileList;
}

QList<ProjectExplorer::FileNode *> CMakeCbpParser::cmakeFileList()
{
    return m_cmakeFileList;
}

bool CMakeCbpParser::hasCMakeFiles()
{
    return !m_cmakeFileList.isEmpty();
}

QStringList CMakeCbpParser::includeFiles()
{
    return m_includeFiles;
}

QByteArray CMakeCbpParser::defines() const
{
    return m_defines;
}

QList<CMakeBuildTarget> CMakeCbpParser::buildTargets()
{
    return m_buildTargets;
}

QString CMakeCbpParser::compilerName() const
{
    return m_compiler;
}

void CMakeBuildTarget::clear()
{
    executable.clear();
    makeCommand.clear();
    makeCleanCommand.clear();
    workingDirectory.clear();
    title.clear();
    library = false;
}

