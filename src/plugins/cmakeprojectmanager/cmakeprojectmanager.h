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

#ifndef CMAKEPROJECTMANAGER_H
#define CMAKEPROJECTMANAGER_H

#include <projectexplorer/iprojectmanager.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectnodes.h>
#include <coreplugin/icontext.h>

#include <utils/environment.h>
#include <utils/pathchooser.h>

#include <QFuture>
#include <QStringList>
#include <QDir>
#include <QAction>

QT_FORWARD_DECLARE_CLASS(QProcess)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QFormLayout)

namespace Utils {
class QtcProcess;
}

namespace CMakeProjectManager {
namespace Internal {

class CMakeSettingsPage;

class CMakeManager : public ProjectExplorer::IProjectManager
{
    Q_OBJECT
public:
    CMakeManager(CMakeSettingsPage *cmakeSettingsPage);

    virtual ProjectExplorer::Project *openProject(const QString &fileName, QString *errorString);
    virtual QString mimeType() const;

    QString cmakeExecutable() const;
    bool isCMakeExecutableValid() const;

    void setCMakeExecutable(const QString &executable);

    QString ninjaExecutable() const;

    void createXmlFile(Utils::QtcProcess *process,
                       const QString &arguments,
                       const QString &sourceDirectory,
                       const QDir &buildDirectory,
                       const Utils::Environment &env,
                       const QString &generator);
    bool hasCodeBlocksMsvcGenerator() const;
    bool hasCodeBlocksNinjaGenerator() const;
    static QString findCbpFile(const QDir &);

    static QString findDumperLibrary(const Utils::Environment &env);
private slots:
    void updateContextMenu(ProjectExplorer::Project *project, ProjectExplorer::Node *node);
    void runCMake();
    void runCMakeContextMenu();
private:
    void runCMake(ProjectExplorer::Project *project);
    static QString qtVersionForQMake(const QString &qmakePath);
    static QPair<QString, QString> findQtDir(const Utils::Environment &env);
    CMakeSettingsPage *m_settingsPage;
    QAction *m_runCMakeAction;
    QAction *m_runCMakeActionContextMenu;
    ProjectExplorer::Project *m_contextProject;
};

struct CMakeValidator
{
    enum STATE { VALID, INVALID, RUNNING };
    STATE state;
    QProcess *process;
    bool hasCodeBlocksMsvcGenerator;
    bool hasCodeBlocksNinjaGenerator;
    QString version;
    QString executable;

    CMakeValidator() : state(INVALID), process(0), hasCodeBlocksMsvcGenerator(false), hasCodeBlocksNinjaGenerator(false) {}
};

class CMakeSettingsPage : public Core::IOptionsPage
{
    Q_OBJECT

public:
    CMakeSettingsPage();
    ~CMakeSettingsPage();

    QWidget *createPage(QWidget *parent);
    void apply();
    void finish();

    QString cmakeExecutable() const;
    void setCMakeExecutable(const QString &executable);
    bool isCMakeExecutableValid() const;
    bool hasCodeBlocksMsvcGenerator() const;
    bool hasCodeBlocksNinjaGenerator() const;

    QString ninjaExecutable() const;

private slots:
    void userCmakeFinished();
    void pathCmakeFinished();

private:
    enum BuildCommand { CMake, Ninja };
    void cmakeFinished(CMakeValidator *cmakeValidator) const;
    void saveSettings(BuildCommand cmd) const;
    QString findExecutable(BuildCommand cmd) const;
    void startProcess(CMakeValidator *cmakeValidator);
    void updateInfo(BuildCommand cmd, CMakeValidator *validator);
    void createExecutableChooser(QFormLayout *formLayout, Utils::PathChooser *&pathchooser,
                                 const QString &name, const QString &path);

    Utils::PathChooser *m_pathchooser;
    mutable CMakeValidator m_userCmake;
    mutable CMakeValidator m_pathCmake;

    Utils::PathChooser *m_pathchooserNinja;
    mutable CMakeValidator m_userNinja;
    mutable CMakeValidator m_pathNinja;

    void initValidator(BuildCommand cmd, CMakeValidator *user, CMakeValidator *path);
};

} // namespace Internal
} // namespace CMakeProjectManager

#endif // CMAKEPROJECTMANAGER_H
