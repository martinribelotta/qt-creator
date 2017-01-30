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

#include "googletest.h"

#include "testenvironment.h"

#include <pchcreator.h>
#include <stringcache.h>

#include <QFileInfo>

namespace {

using ClangBackEnd::IdPaths;
using ClangBackEnd::ProjectPartPch;
using ClangBackEnd::V2::ProjectPartContainer;
using Utils::SmallString;

using testing::_;
using testing::AllOf;
using testing::Contains;
using testing::ElementsAre;
using testing::EndsWith;
using testing::NiceMock;
using testing::Not;
using testing::UnorderedElementsAre;
using testing::Property;
using testing::Field;

class PchCreator: public ::testing::Test
{
protected:
    uint id(const Utils::SmallString &path);

protected:
    ClangBackEnd::StringCache<Utils::SmallString> filePathCache;
    SmallString main1Path = TESTDATA_DIR "/includecollector_main3.cpp";
    SmallString main2Path = TESTDATA_DIR "/includecollector_main2.cpp";
    SmallString header1Path = TESTDATA_DIR "/includecollector_header1.h";
    SmallString header2Path = TESTDATA_DIR "/includecollector_header2.h";
    ProjectPartContainer projectPart1{"project1",
                                      {"-I", TESTDATA_DIR, "-Wno-pragma-once-outside-header"},
                                      {header1Path.clone()},
                                      {main1Path.clone()}};
    ProjectPartContainer projectPart2{"project2",
                                      {"-I", TESTDATA_DIR, "-x", "c++-header", "-Wno-pragma-once-outside-header"},
                                      {header2Path.clone()},
                                      {main2Path.clone()}};
    TestEnvironment environment;
    ClangBackEnd::PchCreator creator{{projectPart1.clone(),projectPart2.clone()},
                                     environment,
                                     filePathCache};
};

using PchCreatorSlowTest = PchCreator;
using PchCreatorVerySlowTest = PchCreator;

TEST_F(PchCreator, CreateGlobalHeaderPaths)
{
    auto filePaths = creator.generateGlobalHeaderPaths();

    ASSERT_THAT(filePaths,
                UnorderedElementsAre(header1Path, header2Path));
}

TEST_F(PchCreator, CreateGlobalSourcePaths)
{
    auto filePaths = creator.generateGlobalSourcePaths();

    ASSERT_THAT(filePaths,
                UnorderedElementsAre(main1Path, main2Path));
}

TEST_F(PchCreator, CreateGlobalHeaderAndSourcePaths)
{
    auto filePaths = creator.generateGlobalHeaderAndSourcePaths();

    ASSERT_THAT(filePaths,
                UnorderedElementsAre(main1Path, main2Path, header1Path, header2Path));
}

TEST_F(PchCreator, CreateGlobalArguments)
{
    auto arguments = creator.generateGlobalArguments();

    ASSERT_THAT(arguments, ElementsAre("-I", TESTDATA_DIR, "-Wno-pragma-once-outside-header", "-I", TESTDATA_DIR, "-x" , "c++-header", "-Wno-pragma-once-outside-header"));
}

TEST_F(PchCreator, CreateGlobalCommandLine)
{
    auto arguments = creator.generateGlobalCommandLine();

    ASSERT_THAT(arguments, ElementsAre(environment.clangCompilerPath(), "-I", TESTDATA_DIR, "-Wno-pragma-once-outside-header", "-I", TESTDATA_DIR, "-x" , "c++-header", "-Wno-pragma-once-outside-header"));
}

TEST_F(PchCreator, CreateGlobalPchIncludes)
{
    auto includeIds = creator.generateGlobalPchIncludeIds();

    ASSERT_THAT(includeIds,
                UnorderedElementsAre(id("includecollector_external3.h"),
                                     id("includecollector_external1.h"),
                                     id("includecollector_external2.h")));
}

TEST_F(PchCreatorVerySlowTest, CreateGlobalPchFileContent)
{
    auto content = creator.generateGlobalPchHeaderFileContent();

    ASSERT_THAT(content, "#include <includecollector_external3.h>\n#include <includecollector_external1.h>\n#include <includecollector_external2.h>\n");
}

TEST_F(PchCreatorVerySlowTest, CreateGlobalPchHeaderFile)
{
    auto file = creator.generateGlobalPchHeaderFile();
    file->open(QIODevice::ReadOnly);

    auto content = file->readAll();

    ASSERT_THAT(content, "#include <includecollector_external3.h>\n#include <includecollector_external1.h>\n#include <includecollector_external2.h>\n");
}

TEST_F(PchCreator, ConvertToQStringList)
{
    auto arguments = creator.convertToQStringList({"-I", TESTDATA_DIR});

    ASSERT_THAT(arguments, ElementsAre(QString("-I"), QString(TESTDATA_DIR)));
}

TEST_F(PchCreator, CreateGlobalPchCompilerArguments)
{
    auto arguments = creator.generateGlobalPchCompilerArguments();

    ASSERT_THAT(arguments, ElementsAre("-x","c++-header", "-Xclang", "-emit-pch", "-o", EndsWith(".pch"), EndsWith(".h")));
}

TEST_F(PchCreator, CreateGlobalClangCompilerArguments)
{
    auto arguments = creator.generateGlobalClangCompilerArguments();

    ASSERT_THAT(arguments, AllOf(Contains("-Wno-pragma-once-outside-header"),
                                 Contains("-emit-pch"),
                                 Contains("-o"),
                                 Not(Contains(environment.clangCompilerPath()))));
}

TEST_F(PchCreatorVerySlowTest, CreateGlobalPch)
{
    creator.generateGlobalPch();

    ASSERT_TRUE(QFileInfo::exists(creator.generateGlobalPchFilePath()));
}

TEST_F(PchCreator, CreateProjectPartCommandLine)
{
    auto commandLine = creator.generateProjectPartCommandLine(projectPart1);

    ASSERT_THAT(commandLine, ElementsAre(environment.clangCompilerPath(), "-I", TESTDATA_DIR, "-Wno-pragma-once-outside-header"));
}

TEST_F(PchCreator, CreateProjectPartHeaderAndSources)
{
    auto includeIds = creator.generateProjectPartHeaderAndSourcePaths(projectPart1);

    ASSERT_THAT(includeIds, UnorderedElementsAre(main1Path, header1Path));
}

TEST_F(PchCreatorSlowTest, CreateProjectPartPchIncludes)
{
    auto includeIds = creator.generateProjectPartPchIncludes(projectPart1);

    ASSERT_THAT(includeIds, UnorderedElementsAre(id("includecollector_external1.h"),
                                                 id("includecollector_external2.h"),
                                                 id("includecollector_header2.h")));
}

TEST_F(PchCreatorSlowTest, CreateProjectPartPchFileContent)
{
    auto includes = creator.generateProjectPartPchIncludes(projectPart1);

    auto content = creator.generatePchIncludeFileContent(includes);

    ASSERT_THAT(content, "#include <includecollector_header2.h>\n#include <includecollector_external1.h>\n#include <includecollector_external2.h>\n");
}

TEST_F(PchCreatorSlowTest, CreateProjectPartPchIncludeFile)
{
    auto includeIds = creator.generateProjectPartPchIncludes(projectPart1);
    auto content = creator.generatePchIncludeFileContent(includeIds);
    auto pchIncludeFilePath = creator.generateProjectPathPchHeaderFilePath(projectPart1);
    auto file = creator.generatePchHeaderFile(pchIncludeFilePath, content);
    file->open(QIODevice::ReadOnly);

    auto fileContent = file->readAll();

    ASSERT_THAT(fileContent, "#include <includecollector_header2.h>\n#include <includecollector_external1.h>\n#include <includecollector_external2.h>\n");
}

TEST_F(PchCreator, CreateProjectPartPchCompilerArguments)
{
    auto arguments = creator.generateProjectPartPchCompilerArguments(projectPart1);

    ASSERT_THAT(arguments, AllOf(Contains("-x"),
                                 Contains("c++-header"),
//                                 Contains("-Xclang"),
//                                 Contains("-include-pch"),
//                                 Contains("-Xclang"),
//                                 Contains(EndsWith(".pch")),
                                 Contains("-Xclang"),
                                 Contains("-emit-pch"),
                                 Contains("-o"),
                                 Contains(EndsWith(".pch"))));
}

TEST_F(PchCreator, CreateProjectPartClangCompilerArguments)
{
    auto arguments = creator.generateProjectPartClangCompilerArguments(projectPart1);

    ASSERT_THAT(arguments, AllOf(Contains(TESTDATA_DIR),
                                 Contains("-emit-pch"),
                                 Contains("-o"),
                                 Not(Contains(environment.clangCompilerPath()))));
}

TEST_F(PchCreatorVerySlowTest, ProjectPartPchsExistsAfterCreation)
{
    creator.generateGlobalPch();

    creator.generateProjectPartPch(projectPart1);

    ASSERT_TRUE(QFileInfo::exists(creator.generateProjectPathPchHeaderFilePath(projectPart1)));
}

TEST_F(PchCreatorVerySlowTest, CreatePartPchs)
{
    creator.generateGlobalPch();

    auto projectPartPchAndIdPath = creator.generateProjectPartPch(projectPart1);

    ASSERT_THAT(projectPartPchAndIdPath.first.id(), projectPart1.projectPartId());
    ASSERT_THAT(projectPartPchAndIdPath.first.path(), creator.generateProjectPartPchFilePath(projectPart1));
    ASSERT_THAT(projectPartPchAndIdPath.second.id, projectPart1.projectPartId());
    ASSERT_THAT(projectPartPchAndIdPath.second.paths, UnorderedElementsAre(1, 2, 3));
}

TEST_F(PchCreatorVerySlowTest, ProjectPartPchsForCreatePchsForProjectParts)
{
    creator.generatePchs();

    ASSERT_THAT(creator.takeProjectPartPchs(),
                ElementsAre(Property(&ProjectPartPch::id, "project1"),
                            Property(&ProjectPartPch::id, "project2")));
}

TEST_F(PchCreatorVerySlowTest, IdPathsForCreatePchsForProjectParts)
{
    creator.generatePchs();

    ASSERT_THAT(creator.takeProjectsIncludes(),
                ElementsAre(AllOf(Field(&IdPaths::id, "project1"),
                                  Field(&IdPaths::paths, UnorderedElementsAre(id("includecollector_header2.h"),
                                                                              id("includecollector_external1.h"),
                                                                              id("includecollector_external2.h")))),
                            AllOf(Field(&IdPaths::id, "project2"),
                                  Field(&IdPaths::paths, UnorderedElementsAre(id("includecollector_external1.h"),
                                                                              id("includecollector_external3.h"),
                                                                              id("includecollector_header1.h"),
                                                                              id("../data/includecollector_external2.h"))))));
}

uint PchCreator::id(const Utils::SmallString &path)
{
    return filePathCache.stringId(path);
}
}
