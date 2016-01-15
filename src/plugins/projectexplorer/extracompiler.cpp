/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "extracompiler.h"
#include "buildmanager.h"
#include "session.h"
#include "target.h"
#include "buildconfiguration.h"
#include "kitinformation.h"

#include <utils/qtcassert.h>
#include <coreplugin/idocument.h>
#include <coreplugin/editormanager/editormanager.h>

#include <QDateTime>
#include <QTimer>

namespace ProjectExplorer {

Q_GLOBAL_STATIC(QList<ExtraCompilerFactory *>, factories);

class ExtraCompilerPrivate
{
public:
    const Project *project;
    Utils::FileName source;
    Utils::FileNameList targets;
    QVector<QString> contents;
    QDateTime compileTime;
    Core::IEditor *lastEditor = 0;
    QMetaObject::Connection activeBuildConfigConnection;
    QMetaObject::Connection activeEnvironmentConnection;
    bool dirty = false;

    QTimer timer;
};

ExtraCompiler::ExtraCompiler(const Project *project, const Utils::FileName &source,
                             const Utils::FileNameList &targets, QObject *parent) :
    QObject(parent), d(new ExtraCompilerPrivate)
{
    d->project = project;
    d->source = source;
    d->targets = targets;
    d->contents.resize(targets.size());
    d->timer.setSingleShot(true);

    connect(d->project, &Project::activeTargetChanged, this, &ExtraCompiler::onActiveTargetChanged);
    onActiveTargetChanged();

    connect(&d->timer, &QTimer::timeout, this, [this](){
        if (d->dirty && d->lastEditor) {
            run(d->lastEditor->document()->contents());
            d->dirty = false;
        }
    });

    connect(BuildManager::instance(), &BuildManager::buildStateChanged,
            this, &ExtraCompiler::onTargetsBuilt);

    connect(SessionManager::instance(), &SessionManager::projectRemoved,
            this, [this](Project *project) {
        if (project == d->project)
            deleteLater();
    });

    Core::EditorManager *editorManager = Core::EditorManager::instance();
    connect(editorManager, &Core::EditorManager::currentEditorChanged,
            this, &ExtraCompiler::onEditorChanged);
    connect(editorManager, &Core::EditorManager::editorAboutToClose,
            this, &ExtraCompiler::onEditorAboutToClose);

    // Use existing target files, where possible. Otherwise run the compiler.
    QDateTime sourceTime = d->source.toFileInfo().lastModified();
    foreach (const Utils::FileName &target, targets) {
        QFileInfo targetFileInfo(target.toFileInfo());
        if (!targetFileInfo.exists()) {
            d->dirty = true;
            continue;
        }

        QDateTime lastModified = targetFileInfo.lastModified();
        if (lastModified < sourceTime)
            d->dirty = true;

        if (!d->compileTime.isValid() || d->compileTime > lastModified)
            d->compileTime = lastModified;

        QFile file(target.toString());
        if (file.open(QFile::ReadOnly | QFile::Text))
            setContent(target, QTextStream(&file).readAll());
    }

    if (d->dirty) {
        // Run in the next event loop, as run() is not available yet in the ctor.
        QTimer::singleShot(0, this, [this](){
            QFile file(d->source.toString());
            if (file.open(QFile::ReadOnly | QFile::Text))
                run(file.readAll());
            d->dirty = false;
        });
    }
}

ExtraCompiler::~ExtraCompiler()
{
    delete d;
}

const Project *ExtraCompiler::project() const
{
    return d->project;
}

Utils::FileName ExtraCompiler::source() const
{
    return d->source;
}

QString ExtraCompiler::content(const Utils::FileName &file) const
{
    for (int i = 0; i < d->targets.length(); ++i) {
        if (d->targets[i] == file)
            return d->contents[i];
    }
    return QString();
}

Utils::FileNameList ExtraCompiler::targets() const
{
    return d->targets;
}

void ExtraCompiler::setCompileTime(const QDateTime &time)
{
    d->compileTime = time;
}

QDateTime ExtraCompiler::compileTime() const
{
    return d->compileTime;
}

void ExtraCompiler::onTargetsBuilt(Project *project)
{
    if (project != d->project || BuildManager::isBuilding(project))
        return;

    // This is mostly a fall back for the cases when the generator couldn't be run.
    // It pays special attention to the case where a source file was newly created
    const QDateTime sourceTime = d->source.toFileInfo().lastModified();
    if (d->compileTime.isValid() && d->compileTime >= sourceTime)
        return;

    foreach (const Utils::FileName &target, d->targets) {
        QFileInfo fi(target.toFileInfo());
        QDateTime generateTime = fi.exists() ? fi.lastModified() : QDateTime();
        if (generateTime.isValid() && (generateTime > sourceTime)) {
            if (d->compileTime >= generateTime)
                continue;

            QFile file(target.toString());
            if (file.open(QFile::ReadOnly | QFile::Text)) {
                QTextStream stream(&file);
                d->compileTime = generateTime;
                setContent(target, stream.readAll());
            }
        }
    }
}

void ExtraCompiler::onEditorChanged(Core::IEditor *editor)
{
    // Handle old editor
    if (d->lastEditor) {
        Core::IDocument *doc = d->lastEditor->document();
        disconnect(doc, &Core::IDocument::contentsChanged,
                   this, &ExtraCompiler::setDirty);

        if (d->dirty) {
            run(doc->contents());
            d->dirty = false;
        }
    }

    if (editor && editor->document()->filePath() == d->source) {
        d->lastEditor = editor;

        // Handle new editor
        connect(d->lastEditor->document(), &Core::IDocument::contentsChanged,
                this, &ExtraCompiler::setDirty);
    } else {
        d->lastEditor = 0;
    }
}

void ExtraCompiler::setDirty()
{
    d->dirty = true;
    d->timer.start(1000);
}

void ExtraCompiler::onEditorAboutToClose(Core::IEditor *editor)
{
    if (d->lastEditor != editor)
        return;

    // Oh no our editor is going to be closed
    // get the content first
    Core::IDocument *doc = d->lastEditor->document();
    disconnect(doc, &Core::IDocument::contentsChanged,
               this, &ExtraCompiler::setDirty);
    if (d->dirty) {
        run(doc->contents());
        d->dirty = false;
    }
    d->lastEditor = 0;
}

void ExtraCompiler::onActiveTargetChanged()
{
    disconnect(d->activeBuildConfigConnection);
    if (Target *target = d->project->activeTarget()) {
        d->activeBuildConfigConnection = connect(
                target, &Target::activeBuildConfigurationChanged,
                this, &ExtraCompiler::onActiveBuildConfigurationChanged);
        onActiveBuildConfigurationChanged();
    } else {
        disconnect(d->activeEnvironmentConnection);
        setDirty();
    }
}

void ExtraCompiler::onActiveBuildConfigurationChanged()
{
    disconnect(d->activeEnvironmentConnection);
    Target *target = d->project->activeTarget();
    QTC_ASSERT(target, return);
    if (BuildConfiguration *bc = target->activeBuildConfiguration()) {
        d->activeEnvironmentConnection = connect(
                    bc, &BuildConfiguration::environmentChanged,
                    this, &ExtraCompiler::setDirty);
    } else {
        d->activeEnvironmentConnection = connect(KitManager::instance(), &KitManager::kitUpdated,
                                                 this, [this](Kit *kit) {
            Target *target = d->project->activeTarget();
            QTC_ASSERT(target, return);
            if (kit == target->kit())
                setDirty();
        });
    }
    setDirty();
}

Utils::Environment ExtraCompiler::buildEnvironment() const
{
    if (Target *target = project()->activeTarget()) {
        if (BuildConfiguration *bc = target->activeBuildConfiguration()) {
            return bc->environment();
        } else {
            QList<Utils::EnvironmentItem> changes =
                    EnvironmentKitInformation::environmentChanges(target->kit());
            Utils::Environment env = Utils::Environment::systemEnvironment();
            env.modify(changes);
            return env;
        }
    }

    return Utils::Environment::systemEnvironment();
}

void ExtraCompiler::setContent(const Utils::FileName &file, const QString &contents)
{
    for (int i = 0; i < d->targets.length(); ++i) {
        if (d->targets[i] == file) {
            if (d->contents[i] != contents) {
                d->contents[i] = contents;
                emit contentsChanged(file);
            }
            return;
        }
    }
}

ExtraCompilerFactory::ExtraCompilerFactory(QObject *parent) : QObject(parent)
{
}

void ExtraCompilerFactory::registerExtraCompilerFactory(ExtraCompilerFactory *factory)
{
    factories()->append(factory);
}

QList<ExtraCompilerFactory *> ExtraCompilerFactory::extraCompilerFactories()
{
    return *factories();
}

} // namespace ProjectExplorer
