#include "../cbuild.h"

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
    prb_Str  output;
    prb_Str  cmd;
    Compiler compiler;
    Lang     lang;
} CompileCmd;

function CompileCmd
constructCompileCmd(prb_Arena* arena, prb_Str input, prb_Str extraInputs, Compiler compiler, Lang lang, prb_Str extraFlags) {
    bool isPrecompile = prb_strEndsWith(input, prb_STR("precompile.c"));

    prb_Str outputName = {};
    {
        prb_assert(prb_strEndsWith(input, prb_STR(".c")));
        prb_Str inputNameNoExt = input;
        inputNameNoExt.len -= 2;
        prb_GrowingStr gstr = prb_beginStr(arena);
        prb_addStrSegment(&gstr, "%.*s", prb_LIT(inputNameNoExt));
        switch (compiler) {
            case Compiler_Gcc: prb_addStrSegment(&gstr, "-gcc"); break;
            case Compiler_Clang: prb_addStrSegment(&gstr, "-clang"); break;
            case Compiler_Msvc: prb_addStrSegment(&gstr, "-msvc"); break;
        }
        if (isPrecompile || extraInputs.len > 0) {
            prb_addStrSegment(&gstr, "-2tu");
        } else {
            prb_addStrSegment(&gstr, "-1tu");
        }
        switch (lang) {
            case Lang_C: prb_addStrSegment(&gstr, "-c"); break;
            case Lang_Cpp: prb_addStrSegment(&gstr, "-cpp"); break;
        }

        prb_Str outputExt = prb_STR("obj");
        if (!isPrecompile) {
#if prb_PLATFORM_WINDOWS
            outputExt = prb_STR("exe");
#elif prb_PLATFORM_LINUX
            outputExt = prb_STR("bin");
#else
#error unimplemented
#endif
        }
        prb_addStrSegment(&gstr, ".%.*s", prb_LIT(outputExt));

        outputName = prb_endStr(&gstr);
    }

    prb_Str outputNamePdb = prb_replaceExt(arena, outputName, prb_STR("pdb"));

    prb_GrowingStr cmd = prb_beginStr(arena);

    switch (compiler) {
        case Compiler_Gcc: prb_addStrSegment(&cmd, "gcc"); break;
        case Compiler_Clang: prb_addStrSegment(&cmd, "clang"); break;
        case Compiler_Msvc: prb_addStrSegment(&cmd, "cl /nologo /diagnostics:column /FC"); break;
    }

    switch (compiler) {
        case Compiler_Gcc:
        case Compiler_Clang: prb_addStrSegment(&cmd, " -g"); break;
        case Compiler_Msvc: prb_addStrSegment(&cmd, " /Zi"); break;
    }

    prb_addStrSegment(&cmd, " -Wall");

    switch (compiler) {
        case Compiler_Gcc:
        case Compiler_Clang: prb_addStrSegment(&cmd, " -Wextra -Werror -Wfatal-errors"); break;
        case Compiler_Msvc: prb_addStrSegment(&cmd, " /WX"); break;
    }

    prb_addStrSegment(&cmd, " %.*s", prb_LIT(extraFlags));

    if (extraInputs.len > 0) {
        prb_addStrSegment(&cmd, " -Dprb_NO_IMPLEMENTATION");
    }

    if (isPrecompile) {
        prb_addStrSegment(&cmd, " -c");
    }

    switch (lang) {
        case Lang_C:
            switch (compiler) {
                case Compiler_Gcc:
                case Compiler_Clang: prb_addStrSegment(&cmd, " -x c"); break;
                case Compiler_Msvc: prb_addStrSegment(&cmd, " /Tc"); break;
            }
            break;
        case Lang_Cpp:
            switch (compiler) {
                case Compiler_Gcc:
                case Compiler_Clang: prb_addStrSegment(&cmd, " -x c++"); break;
                case Compiler_Msvc: prb_addStrSegment(&cmd, " /Tp"); break;
            }
            break;
    }

    prb_addStrSegment(&cmd, " %.*s", prb_LIT(input));

    if (extraInputs.len > 0) {
        switch (compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStrSegment(&cmd, " -x none %.*s", prb_LIT(extraInputs)); break;
            case Compiler_Msvc: prb_addStrSegment(&cmd, " %.*s", prb_LIT(extraInputs)); break;
        }
    }

    switch (compiler) {
        case Compiler_Gcc:
        case Compiler_Clang: prb_addStrSegment(&cmd, " -o %.*s", prb_LIT(outputName)); break;
        case Compiler_Msvc: {
            prb_addStrSegment(&cmd, " /Fd%.*s", prb_LIT(outputNamePdb));
            if (isPrecompile) {
                prb_addStrSegment(&cmd, " /Fo%.*s", prb_LIT(outputName));
            } else {
                prb_addStrSegment(&cmd, " /Fe%.*s", prb_LIT(outputName));
            }
        } break;
    }

    if (!isPrecompile) {
        switch (compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStrSegment(&cmd, " -lpthread"); break;
            case Compiler_Msvc: break;
        }
    }

    prb_Str    cmdStr = prb_endStr(&cmd);
    CompileCmd result = {.cmd = cmdStr, .output = outputName, .compiler = compiler, .lang = lang};
    return result;
}

function void
compileAndRunTestsWithSan(prb_Arena* arena, prb_Str testsDir, prb_Str testsFile, prb_Str flags, prb_Str outname) {
    CompileCmd cmd = constructCompileCmd(arena, testsFile, prb_STR(""), Compiler_Clang, Lang_C, flags);
    prb_writelnToStdout(arena, cmd.cmd);
    prb_assert(prb_execCmd(arena, cmd.cmd, 0, (prb_Str) {}).status == prb_ProcessStatus_CompletedSuccess);
    prb_writelnToStdout(arena, cmd.output);
    prb_Str outpath = prb_pathJoin(arena, testsDir, outname);
    prb_assert(prb_execCmd(arena, cmd.output, prb_ProcessFlag_RedirectStdout | prb_ProcessFlag_RedirectStderr, outpath).status == prb_ProcessStatus_CompletedSuccess);
}

function void
compileAndRunTests(prb_Arena* arena, prb_Str testsDir, prb_Str testsFile) {
    prb_Str precompFile = prb_pathJoin(arena, testsDir, prb_STR("precompile.c"));

    {
        prb_Str          patsToRemove[] = {prb_STR("*.gcda"), prb_STR("*.gcno"), prb_STR("*.bin"), prb_STR("*.obj")};
        prb_PathFindSpec spec = {.arena = arena, .dir = testsDir, .mode = prb_PathFindMode_Glob};
        for (i32 patIndex = 0; patIndex < prb_arrayLength(patsToRemove); patIndex++) {
            prb_Str pat = patsToRemove[patIndex];
            spec.pattern = pat;
            prb_PathFindIter iter = prb_createPathFindIter(spec);
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
    prb_Str sources[] = {
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
                prb_Str    source = sources[srcIndex];
                CompileCmd cmd = constructCompileCmd(arena, source, prb_STR(""), compiler, lang, prb_STR(""));
                prb_writelnToStdout(arena, cmd.cmd);
                prb_ProcessHandle proc = prb_execCmd(arena, cmd.cmd, prb_ProcessFlag_DontWait, (prb_Str) {});
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
        CompileCmd cmd = constructCompileCmd(arena, testsFile, precmd.output, precmd.compiler, precmd.lang, prb_STR(""));
        prb_writelnToStdout(arena, cmd.cmd);
        prb_ProcessHandle proc = prb_execCmd(arena, cmd.cmd, prb_ProcessFlag_DontWait, (prb_Str) {});
        arrput(compileProcs, proc);
        arrput(compCmds, cmd);
    }
    prb_assert(prb_waitForProcesses(compileProcs, arrlen(compileProcs)) == prb_Success);

    // NOTE(khvorov) Run all the test executalbes. Probalby overkill but tests finish fast so do it anyway.
    for (i32 compCmdIndex = 0; compCmdIndex < arrlen(compCmds); compCmdIndex++) {
        CompileCmd compCmd = compCmds[compCmdIndex];
        prb_writelnToStdout(arena, compCmd.output);
        prb_Str           cmdOutPath = prb_replaceExt(arena, compCmd.output, prb_STR("log"));
        prb_ProcessHandle proc = prb_execCmd(arena, compCmd.output, prb_ProcessFlag_RedirectStderr | prb_ProcessFlag_RedirectStdout, cmdOutPath);
        prb_assert(proc.status == prb_ProcessStatus_CompletedSuccess);
    }
}

int
main() {
    prb_TimeStart scriptStart = prb_timeStart();
    prb_Arena     arena_ = prb_createArenaFromVmem(1 * prb_GIGABYTE);
    prb_Arena*    arena = &arena_;

    prb_Str* args = prb_getCmdArgs(arena);
    bool     noexamples = arrlen(args) >= 2 && prb_streq(args[1], prb_STR("noexamples"));

    prb_Str testsDir = prb_getAbsolutePath(arena, prb_getParentDir(arena, prb_STR(__FILE__)));
    prb_Str rootDir = prb_getParentDir(arena, testsDir);

    prb_Str testsFile = prb_pathJoin(arena, testsDir, prb_STR("tests.c"));

    // NOTE(khvorov) Set up sanitizers
    prb_Str lsanFilepath = {};
    {
        prb_Str leakSuppress = prb_STR("leak:regcomp");
        lsanFilepath = prb_pathJoin(arena, testsDir, prb_STR("lsan.supp"));
        prb_assert(prb_writeEntireFile(arena, lsanFilepath, leakSuppress.ptr, leakSuppress.len));
        prb_assert(prb_setenv(arena, prb_STR("LSAN_OPTIONS"), prb_fmt(arena, "suppressions=%.*s", prb_LIT(lsanFilepath))));
    }

    // NOTE(khvorov) Static analysis
    prb_Str mainFilePath = prb_pathJoin(arena, rootDir, prb_STR("cbuild.h"));
    prb_Str staticAnalysisCmd = prb_fmt(arena, "clang-tidy %.*s", prb_LIT(mainFilePath));
    prb_writelnToStdout(arena, staticAnalysisCmd);
    prb_Str           staticAnalysisOutput = prb_pathJoin(arena, testsDir, prb_STR("static_analysis_out"));
    prb_ProcessHandle staticAnalysisProc = prb_execCmd(arena, staticAnalysisCmd, prb_ProcessFlag_DontWait | prb_ProcessFlag_RedirectStderr | prb_ProcessFlag_RedirectStdout, staticAnalysisOutput);
    prb_assert(staticAnalysisProc.status == prb_ProcessStatus_Launched);

    // NOTE(khvorov) Run tests from different example directories because I
    // found it tests filepath handling better
    prb_assert(prb_setWorkingDir(arena, rootDir) == prb_Success);
    compileAndRunTests(arena, testsDir, testsFile);
    prb_assert(prb_setWorkingDir(arena, testsDir) == prb_Success);
    compileAndRunTests(arena, testsDir, testsFile);
    prb_assert(prb_setWorkingDir(arena, rootDir) == prb_Success);

    // NOTE(khvorov) Coverage
    {
#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

        prb_Str gccCoverageFlags = prb_STR("--coverage -fno-inline -fno-inline-small-functions -fno-default-inline");
        CompileCmd cmd = constructCompileCmd(arena, testsFile, prb_STR(""), Compiler_Gcc, Lang_C, gccCoverageFlags);
        prb_writelnToStdout(arena, cmd.cmd);
        prb_assert(prb_execCmd(arena, cmd.cmd, 0, (prb_Str) {}).status == prb_ProcessStatus_CompletedSuccess);
        prb_writelnToStdout(arena, cmd.output);
        prb_Str outpath = prb_pathJoin(arena, testsDir, prb_STR("tests-coverage.log"));
        prb_assert(prb_execCmd(arena, cmd.output, prb_ProcessFlag_RedirectStdout | prb_ProcessFlag_RedirectStderr, outpath).status == prb_ProcessStatus_CompletedSuccess);

#else
#error unimplemented
#endif
    }

    // NOTE(khvorov) Sanitizers
    compileAndRunTestsWithSan(arena, testsDir, testsFile, prb_STR("-fsanitize=address -fno-omit-frame-pointer"), prb_STR("tests-address-sanitizer.log"));
    compileAndRunTestsWithSan(arena, testsDir, testsFile, prb_STR("-fsanitize=thread"), prb_STR("tests-thread-sanitizer.log"));
    compileAndRunTestsWithSan(arena, testsDir, testsFile, prb_STR("-fsanitize=memory -fno-omit-frame-pointer"), prb_STR("tests-memory-sanitizer.log"));

    // NOTE(khvorov) Print the one of the test results
    {
        prb_PathFindIter iter = prb_createPathFindIter((prb_PathFindSpec) {.arena = arena, .dir = testsDir, .mode = prb_PathFindMode_Glob, .pattern = prb_STR("*.log"), .recursive = false});
        while (prb_pathFindIterNext(&iter)) {
            // NOTE(khvorov) Print test output from the run with the sanitizers
            if (prb_streq(prb_getLastEntryInPath(iter.curPath), prb_STR("tests-address-sanitizer.log"))) {
                prb_writelnToStdout(arena, prb_STR("tests output:"));
                prb_ReadEntireFileResult readres = prb_readEntireFile(arena, iter.curPath);
                prb_assert(readres.success);
                prb_writelnToStdout(arena, prb_strFromBytes(readres.content));
            }
            prb_removeFileIfExists(arena, iter.curPath);
        }
    }

    // NOTE(khvorov) Compile all the examples in every supported way
    if (!noexamples) {
        prb_Str    exampleDir = prb_pathJoin(arena, rootDir, prb_STR("example"));
        prb_Str    exampleBuildProgramSrc = prb_pathJoin(arena, exampleDir, prb_STR("build.c"));
        CompileCmd exampleBuildProgramCompileCmd = constructCompileCmd(arena, exampleBuildProgramSrc, prb_STR(""), Compiler_Clang, Lang_C, prb_STR(""));
        prb_writelnToStdout(arena, exampleBuildProgramCompileCmd.cmd);
        prb_ProcessHandle exampleBuildProgramCompileProc = prb_execCmd(arena, exampleBuildProgramCompileCmd.cmd, 0, (prb_Str) {});
        prb_assert(exampleBuildProgramCompileProc.status == prb_ProcessStatus_CompletedSuccess);

        prb_Str compilerArgs[] = {
            prb_STR("clang"),
#if prb_PLATFORM_WINDOWS
            prb_STR("msvc")
#elif prb_PLATFORM_LINUX
            prb_STR("gcc")
#else
#error unimplemented
#endif
        };

        prb_Str buildModeArgs[] = {prb_STR("debug"), prb_STR("release")};

        for (i32 compArgIndex = 0; compArgIndex < prb_arrayLength(compilerArgs); compArgIndex++) {
            prb_Str compilerArg = compilerArgs[compArgIndex];
            for (i32 buildModeArgIndex = 0; buildModeArgIndex < prb_arrayLength(buildModeArgs); buildModeArgIndex++) {
                prb_Str buildModeArg = buildModeArgs[buildModeArgIndex];
                prb_Str cmd = prb_fmt(arena, "%.*s %.*s %.*s", prb_LIT(exampleBuildProgramCompileCmd.output), prb_LIT(compilerArg), prb_LIT(buildModeArg));
                prb_writelnToStdout(arena, cmd);
                prb_ProcessHandle proc = prb_execCmd(arena, cmd, 0, (prb_Str) {});
                prb_assert(proc.status == prb_ProcessStatus_CompletedSuccess);
                // NOTE(khvorov) Compile again to make sure incremental compilation code executes
                proc = prb_execCmd(arena, cmd, 0, (prb_Str) {});
                prb_assert(proc.status == prb_ProcessStatus_CompletedSuccess);
            }
        }

#if prb_PLATFORM_WINDOWS
        prb_Str buildScriptName = prb_STR("build.bat");
#elif prb_PLATFORM_LINUX
        prb_Str buildScriptName = prb_STR("build.sh");
#else
#error unimplemented
#endif

        prb_Str buildScriptPath = prb_pathJoin(arena, exampleDir, buildScriptName);

#if prb_PLATFORM_WINDOWS
        prb_Str buildScriptName = prb_STR("build.bat");
#elif prb_PLATFORM_LINUX
        prb_Str buildScriptCmd = prb_fmt(arena, "sh %.*s", prb_LIT(buildScriptPath));
#else
#error unimplemented
#endif

        buildScriptCmd = prb_fmt(arena, "%.*s %.*s %.*s", prb_LIT(buildScriptCmd), prb_LIT(compilerArgs[0]), prb_LIT(buildModeArgs[0]));
        prb_writelnToStdout(arena, buildScriptCmd);
        prb_ProcessHandle builsScriptExec = prb_execCmd(arena, buildScriptCmd, 0, (prb_Str) {});
        prb_assert(builsScriptExec.status == prb_ProcessStatus_CompletedSuccess);

        prb_assert(prb_setWorkingDir(arena, exampleDir) == prb_Success);
        prb_writelnToStdout(arena, buildScriptCmd);
        builsScriptExec = prb_execCmd(arena, buildScriptCmd, 0, (prb_Str) {});
        prb_assert(builsScriptExec.status == prb_ProcessStatus_CompletedSuccess);

        prb_assert(prb_setWorkingDir(arena, rootDir) == prb_Success);
    }

    // NOTE(khvorov) Print result of static analysis
    prb_assert(prb_waitForProcesses(&staticAnalysisProc, 1));
    {
        prb_ReadEntireFileResult staticAnalysisOutRead = prb_readEntireFile(arena, staticAnalysisOutput);
        prb_assert(staticAnalysisOutRead.success);
        prb_writelnToStdout(arena, prb_STR("static analysis out:"));
        prb_writelnToStdout(arena, prb_strFromBytes(staticAnalysisOutRead.content));
        prb_assert(prb_removeFileIfExists(arena, staticAnalysisOutput));
    }

    prb_removeFileIfExists(arena, lsanFilepath);

    prb_writelnToStdout(arena, prb_fmt(arena, "test run took %.2fms", prb_getMsFrom(scriptStart)));
    return 0;
}
