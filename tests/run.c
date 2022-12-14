#include "../programmable_build.h"

#define function static

typedef int32_t i32;

typedef enum Compiler {
    Compiler_Gcc,
    Compiler_Clang,
    Compiler_Msvc,
} Compiler;

typedef enum Lang {
    Lang_C,
    Lang_Cpp,
} Lang;

typedef struct CompileCmd {
    prb_String output;
    prb_String cmd;
    Compiler   compiler;
    Lang       lang;
} CompileCmd;

function CompileCmd
constructCompileCmd(prb_Arena* arena, prb_String input, prb_String extraInputs, Compiler compiler, Lang lang) {
    bool isPrecompile = prb_strEndsWith(input, prb_STR("precompile.c"));

    prb_String outputName = {};
    {
        prb_assert(prb_strEndsWith(input, prb_STR(".c")));
        prb_String inputNameNoExt = input;
        inputNameNoExt.len -= 2;
        prb_GrowingString gstr = prb_beginString(arena);
        prb_addStringSegment(&gstr, "%.*s", prb_LIT(inputNameNoExt));
        switch (compiler) {
            case Compiler_Gcc: prb_addStringSegment(&gstr, "-gcc"); break;
            case Compiler_Clang: prb_addStringSegment(&gstr, "-clang"); break;
            case Compiler_Msvc: prb_addStringSegment(&gstr, "-msvc"); break;
        }
        if (isPrecompile || extraInputs.len > 0) {
            prb_addStringSegment(&gstr, "-2tu");
        } else {
            prb_addStringSegment(&gstr, "-1tu");
        }
        switch (lang) {
            case Lang_C: prb_addStringSegment(&gstr, "-c"); break;
            case Lang_Cpp: prb_addStringSegment(&gstr, "-cpp"); break;
        }

        prb_String outputExt = prb_STR("obj");
        if (!isPrecompile) {
#if prb_PLATFORM_WINDOWS
            outputExt = prb_STR("exe");
#elif prb_PLATFORM_LINUX
            outputExt = prb_STR("bin");
#else
#error unimplemented
#endif
        }
        prb_addStringSegment(&gstr, ".%.*s", prb_LIT(outputExt));

        outputName = prb_endString(&gstr);
    }

    prb_String outputNamePdb = prb_replaceExt(arena, outputName, prb_STR("pdb"));

    prb_GrowingString cmd = prb_beginString(arena);

    switch (compiler) {
        case Compiler_Gcc: prb_addStringSegment(&cmd, "gcc"); break;
        case Compiler_Clang: prb_addStringSegment(&cmd, "clang"); break;
        case Compiler_Msvc: prb_addStringSegment(&cmd, "cl /nologo /diagnostics:column /FC"); break;
    }

    switch (compiler) {
        case Compiler_Gcc:
        case Compiler_Clang: prb_addStringSegment(&cmd, " -g"); break;
        case Compiler_Msvc: prb_addStringSegment(&cmd, " /Zi"); break;
    }

    prb_addStringSegment(&cmd, " -Wall");

    switch (compiler) {
        case Compiler_Gcc:
        case Compiler_Clang: prb_addStringSegment(&cmd, " -Wextra -Werror -Wfatal-errors"); break;
        case Compiler_Msvc: prb_addStringSegment(&cmd, " /WX"); break;
    }

    if (!isPrecompile && extraInputs.len == 0 && compiler == Compiler_Gcc && lang == Lang_C) {
        prb_addStringSegment(&cmd, " --coverage -fno-inline -fno-inline-small-functions -fno-default-inline");
    }

    if (extraInputs.len > 0) {
        prb_addStringSegment(&cmd, " -Dprb_NO_IMPLEMENTATION");
    }

    if (isPrecompile) {
        prb_addStringSegment(&cmd, " -c");
    }

    switch (lang) {
        case Lang_C:
            switch (compiler) {
                case Compiler_Gcc:
                case Compiler_Clang: prb_addStringSegment(&cmd, " -x c"); break;
                case Compiler_Msvc: prb_addStringSegment(&cmd, " /Tc"); break;
            }
            break;
        case Lang_Cpp:
            switch (compiler) {
                case Compiler_Gcc:
                case Compiler_Clang: prb_addStringSegment(&cmd, " -x c++"); break;
                case Compiler_Msvc: prb_addStringSegment(&cmd, " /Tp"); break;
            }
            break;
    }

    prb_addStringSegment(&cmd, " %.*s", prb_LIT(input));

    if (extraInputs.len > 0) {
        switch (compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStringSegment(&cmd, " -x none %.*s", prb_LIT(extraInputs)); break;
            case Compiler_Msvc: prb_addStringSegment(&cmd, " %.*s", prb_LIT(extraInputs)); break;
        }
    }

    switch (compiler) {
        case Compiler_Gcc:
        case Compiler_Clang: prb_addStringSegment(&cmd, " -o %.*s", prb_LIT(outputName)); break;
        case Compiler_Msvc: {
            prb_addStringSegment(&cmd, " /Fd%.*s", prb_LIT(outputNamePdb));
            if (isPrecompile) {
                prb_addStringSegment(&cmd, " /Fo%.*s", prb_LIT(outputName));
            } else {
                prb_addStringSegment(&cmd, " /Fe%.*s", prb_LIT(outputName));
            }
        } break;
    }

    if (!isPrecompile) {
        switch (compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStringSegment(&cmd, " -lpthread"); break;
            case Compiler_Msvc: break;
        }
    }

    prb_String cmdStr = prb_endString(&cmd);
    CompileCmd result = {.cmd = cmdStr, .output = outputName, .compiler = compiler, .lang = lang};
    return result;
}

function void
compileAndRunTests(prb_Arena* arena, prb_String testsDir) {
    prb_String testsFile = prb_pathJoin(arena, testsDir, prb_STR("tests.c"));
    prb_String precompFile = prb_pathJoin(arena, testsDir, prb_STR("precompile.c"));

    {
        prb_String       patsToRemove[] = {prb_STR("*.gcda"), prb_STR("*.gcno"), prb_STR("*.bin"), prb_STR("*.obj")};
        prb_PathFindSpec spec = {.arena = arena, .dir = testsDir, .mode = prb_PathFindMode_Glob};
        for (i32 patIndex = 0; patIndex < prb_arrayLength(patsToRemove); patIndex++) {
            prb_String pat = patsToRemove[patIndex];
            spec.pattern = pat;
            prb_PathFindIterator iter = prb_createPathFindIter(spec);
            while (prb_pathFindIterNext(&iter) == prb_Success) {
                prb_removeFileIfExists(arena, iter.curPath);
            }
            prb_destroyPathFindIter(&iter);
        }
    }

    Compiler compilers[] = {
        Compiler_Clang,
#if prb_PLATFORM_WINDOWS
        Compiler_Msvc
#elif prb_PLATFORM_LINUX
        Compiler_Gcc
#else
#error unimplemented
#endif
    };
    Lang langs[] = {
        Lang_C,
        Lang_Cpp,
    };
    prb_String sources[] = {
        testsFile,
        precompFile,
    };

    // NOTE(khvorov) Start test compiling
    prb_ProcessHandle* compileProcs = 0;
    prb_ProcessHandle* precompileProcs = 0;
    CompileCmd*        precompCmds = 0;
    CompileCmd*        compCmds = 0;
    for (i32 compilerIndex = 0; compilerIndex < prb_arrayLength(compilers); compilerIndex++) {
        Compiler compiler = compilers[compilerIndex];
        for (i32 langIndex = 0; langIndex < prb_arrayLength(langs); langIndex++) {
            Lang lang = langs[langIndex];
            for (i32 srcIndex = 0; srcIndex < prb_arrayLength(sources); srcIndex++) {
                prb_String source = sources[srcIndex];
                CompileCmd cmd = constructCompileCmd(arena, source, prb_STR(""), compiler, lang);
                prb_writelnToStdout(cmd.cmd);
                prb_ProcessHandle proc = prb_execCmd(arena, cmd.cmd, prb_ProcessFlag_DontWait, (prb_String) {});
                prb_assert(proc.status >= prb_ProcessStatus_Launched);
                if (prb_strEndsWith(source, prb_STR("precompile.c"))) {
                    arrput(precompileProcs, proc);
                    arrput(precompCmds, cmd);
                } else {
                    arrput(compileProcs, proc);
                    arrput(compCmds, cmd);
                }
            }
        }
    }
    prb_assert(prb_waitForProcesses(precompileProcs, arrlen(compileProcs)) == prb_Success);

    // NOTE(khvorov) Finish test compiling (2 TU ones)
    for (i32 precompIndex = 0; precompIndex < arrlen(precompCmds); precompIndex++) {
        CompileCmd precmd = precompCmds[precompIndex];
        CompileCmd cmd = constructCompileCmd(arena, testsFile, precmd.output, precmd.compiler, precmd.lang);
        prb_writelnToStdout(cmd.cmd);
        prb_ProcessHandle proc = prb_execCmd(arena, cmd.cmd, prb_ProcessFlag_DontWait, (prb_String) {});
        arrput(compileProcs, proc);
        arrput(compCmds, cmd);
    }
    prb_assert(prb_waitForProcesses(compileProcs, arrlen(compileProcs)) == prb_Success);

    // NOTE(khvorov) Run all the test executalbes. Probalby overkill but tests finish fast so do it anyway.
    for (i32 compCmdIndex = 0; compCmdIndex < arrlen(compCmds); compCmdIndex++) {
        CompileCmd compCmd = compCmds[compCmdIndex];
        prb_writelnToStdout(compCmd.output);
        prb_ProcessHandle proc = prb_execCmd(arena, compCmd.output, 0, (prb_String) {});
        prb_assert(proc.status == prb_ProcessStatus_CompletedSuccess);
    }
}

int
main() {
    prb_TimeStart scriptStart = prb_timeStart();
    prb_Arena     arena_ = prb_createArenaFromVmem(1 * prb_GIGABYTE);
    prb_Arena*    arena = &arena_;

    prb_String* args = prb_getCmdArgs(arena);
    bool        noexamples = arrlen(args) >= 2 && prb_streq(args[1], prb_STR("noexamples"));

    prb_String testsDir = prb_getAbsolutePath(arena, prb_getParentDir(arena, prb_STR(__FILE__)));
    prb_String rootDir = prb_getParentDir(arena, testsDir);

    // TODO(khvorov) Run sanitizers for tests probably
    // TODO(khvorov) Organize output better

    // NOTE(khvorov) Static analysis
    prb_String mainFilePath = prb_pathJoin(arena, rootDir, prb_STR("programmable_build.h"));
    prb_String staticAnalysisCmd = prb_fmt(arena, "clang-tidy %.*s", prb_LIT(mainFilePath));
    prb_writelnToStdout(staticAnalysisCmd);
    prb_ProcessHandle staticAnalysisProc = prb_execCmd(arena, staticAnalysisCmd, prb_ProcessFlag_DontWait, (prb_String){});

    // NOTE(khvorov) Run tests from different example directories because I
    // found it tests filepath handling better

    prb_assert(prb_setWorkingDir(arena, rootDir) == prb_Success);
    compileAndRunTests(arena, testsDir);

    prb_assert(prb_setWorkingDir(arena, testsDir) == prb_Success);
    compileAndRunTests(arena, testsDir);

    prb_assert(prb_setWorkingDir(arena, rootDir) == prb_Success);

    // NOTE(khvorov) Compile all the examples in every supported way
    if (!noexamples) {
        prb_String exampleDir = prb_pathJoin(arena, rootDir, prb_STR("example"));
        prb_String exampleBuildProgramSrc = prb_pathJoin(arena, exampleDir, prb_STR("build.c"));
        CompileCmd exampleBuildProgramCompileCmd = constructCompileCmd(arena, exampleBuildProgramSrc, prb_STR(""), Compiler_Clang, Lang_C);
        prb_writelnToStdout(exampleBuildProgramCompileCmd.cmd);
        prb_ProcessHandle exampleBuildProgramCompileProc = prb_execCmd(arena, exampleBuildProgramCompileCmd.cmd, 0, (prb_String) {});
        prb_assert(exampleBuildProgramCompileProc.status == prb_ProcessStatus_CompletedSuccess);

        prb_String compilerArgs[] = {
            prb_STR("clang"),
#if prb_PLATFORM_WINDOWS
            prb_STR("msvc")
#elif prb_PLATFORM_LINUX
            prb_STR("gcc")
#else
#error unimplemented
#endif
        };

        prb_String buildModeArgs[] = {prb_STR("debug"), prb_STR("release")};

        for (i32 compArgIndex = 0; compArgIndex < prb_arrayLength(compilerArgs); compArgIndex++) {
            prb_String compilerArg = compilerArgs[compArgIndex];
            for (i32 buildModeArgIndex = 0; buildModeArgIndex < prb_arrayLength(buildModeArgs); buildModeArgIndex++) {
                prb_String buildModeArg = buildModeArgs[buildModeArgIndex];
                prb_String cmd = prb_fmt(arena, "%.*s %.*s %.*s", prb_LIT(exampleBuildProgramCompileCmd.output), prb_LIT(compilerArg), prb_LIT(buildModeArg));
                prb_writelnToStdout(cmd);
                prb_ProcessHandle proc = prb_execCmd(arena, cmd, 0, (prb_String) {});
                prb_assert(proc.status == prb_ProcessStatus_CompletedSuccess);
                // NOTE(khvorov) Compile again to make sure incremental compilation code executes
                proc = prb_execCmd(arena, cmd, 0, (prb_String) {});
                prb_assert(proc.status == prb_ProcessStatus_CompletedSuccess);
            }
        }

#if prb_PLATFORM_WINDOWS
        prb_String buildScriptName = prb_STR("build.bat");
#elif prb_PLATFORM_LINUX
        prb_String buildScriptName = prb_STR("build.sh");
#else
#error unimplemented
#endif

        prb_String buildScriptPath = prb_pathJoin(arena, exampleDir, buildScriptName);

#if prb_PLATFORM_WINDOWS
        prb_String buildScriptName = prb_STR("build.bat");
#elif prb_PLATFORM_LINUX
        prb_String buildScriptCmd = prb_fmt(arena, "sh %.*s", prb_LIT(buildScriptPath));
#else
#error unimplemented
#endif

        buildScriptCmd = prb_fmt(arena, "%.*s %.*s %.*s", prb_LIT(buildScriptCmd), prb_LIT(compilerArgs[0]), prb_LIT(buildModeArgs[0]));
        prb_writelnToStdout(buildScriptCmd);
        prb_ProcessHandle builsScriptExec = prb_execCmd(arena, buildScriptCmd, 0, (prb_String) {});
        prb_assert(builsScriptExec.status == prb_ProcessStatus_CompletedSuccess);

        prb_assert(prb_setWorkingDir(arena, exampleDir) == prb_Success);
        prb_writelnToStdout(buildScriptCmd);
        builsScriptExec = prb_execCmd(arena, buildScriptCmd, 0, (prb_String) {});
        prb_assert(builsScriptExec.status == prb_ProcessStatus_CompletedSuccess);

        prb_assert(prb_setWorkingDir(arena, rootDir) == prb_Success);
    }

    prb_assert(prb_waitForProcesses(&staticAnalysisProc, 1));

    prb_writelnToStdout(prb_fmt(arena, "test run took %.2fms", prb_getMsFrom(scriptStart)));
    return 0;
}
