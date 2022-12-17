#include "../cbuild.h"

#define function static
#define global_variable static

typedef int32_t  i32;
typedef uint32_t u32;

typedef enum Compiler {
    Compiler_Clang,
    Compiler_Gcc,
    Compiler_Msvc,
} Compiler;

typedef enum Lang {
    Lang_C,
    Lang_Cpp,
} Lang;

typedef struct CompileSpec {
    Compiler compiler;
    Lang     lang;
    prb_Str  flags;
    prb_Str  input;
    prb_Str  optObj;
    prb_Str  output;
} CompileSpec;

global_variable prb_Str globalTestsDir = {};

function prb_Str
constructCompileCmd(prb_Arena* arena, CompileSpec spec) {
    prb_Str outputNamePdb = prb_replaceExt(arena, spec.output, prb_STR("pdb"));

    prb_GrowingStr cmd = prb_beginStr(arena);

    switch (spec.compiler) {
        case Compiler_Gcc: prb_addStrSegment(&cmd, "gcc"); break;
        case Compiler_Clang: prb_addStrSegment(&cmd, "clang"); break;
        case Compiler_Msvc: prb_addStrSegment(&cmd, "cl /nologo /diagnostics:column /FC"); break;
    }

    switch (spec.compiler) {
        case Compiler_Gcc:
        case Compiler_Clang: prb_addStrSegment(&cmd, " -g"); break;
        case Compiler_Msvc: prb_addStrSegment(&cmd, " /Zi"); break;
    }

    prb_addStrSegment(&cmd, " -Wall");

    switch (spec.compiler) {
        case Compiler_Gcc:
        case Compiler_Clang: prb_addStrSegment(&cmd, " -Wextra -Werror -Wfatal-errors"); break;
        case Compiler_Msvc: prb_addStrSegment(&cmd, " /WX"); break;
    }

    prb_addStrSegment(&cmd, " %.*s", prb_LIT(spec.flags));

    bool outIsObj = prb_strEndsWith(spec.output, prb_STR(".obj"));
    if (outIsObj) {
        prb_addStrSegment(&cmd, " -c");
    }

    switch (spec.lang) {
        case Lang_C:
            switch (spec.compiler) {
                case Compiler_Gcc:
                case Compiler_Clang: prb_addStrSegment(&cmd, " -x c"); break;
                case Compiler_Msvc: prb_addStrSegment(&cmd, " /Tc"); break;
            }
            break;
        case Lang_Cpp:
            switch (spec.compiler) {
                case Compiler_Gcc:
                case Compiler_Clang: prb_addStrSegment(&cmd, " -x c++"); break;
                case Compiler_Msvc: prb_addStrSegment(&cmd, " /Tp"); break;
            }
            break;
    }

    prb_addStrSegment(&cmd, " %.*s", prb_LIT(spec.input));

    if (spec.optObj.len > 0) {
        switch (spec.compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStrSegment(&cmd, " -x none %.*s", prb_LIT(spec.optObj)); break;
            case Compiler_Msvc: prb_addStrSegment(&cmd, " %.*s", prb_LIT(spec.optObj)); break;
        }
    }

    switch (spec.compiler) {
        case Compiler_Gcc:
        case Compiler_Clang: prb_addStrSegment(&cmd, " -o %.*s", prb_LIT(spec.output)); break;
        case Compiler_Msvc: {
            prb_addStrSegment(&cmd, " /Fd%.*s", prb_LIT(outputNamePdb));
            if (outIsObj) {
                prb_addStrSegment(&cmd, " /Fo%.*s", prb_LIT(spec.output));
            } else {
                prb_addStrSegment(&cmd, " /Fe%.*s", prb_LIT(spec.output));
            }
        } break;
    }

    if (!outIsObj) {
        switch (spec.compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStrSegment(&cmd, " -lpthread"); break;
            case Compiler_Msvc: break;
        }
    }

    prb_Str cmdStr = prb_endStr(&cmd);
    return cmdStr;
}

typedef struct TestJobSpec {
    Compiler    compiler;
    Lang        lang;
    prb_Str     flags;
    bool        twotu;
    bool        doNotRedirect;
    prb_Str     addOutputSuffix;
    CompileSpec generatedCompileSpec;
    prb_Str     generatedLogPath;
} TestJobSpec;

function void
compileAndRunTests(prb_Arena* arena, void* data) {
    TestJobSpec  spec_ = {};
    TestJobSpec* spec = &spec_;
    if (data) {
        spec = (TestJobSpec*)data;
    }

    prb_Str outputSuffix = {};
    {
        prb_GrowingStr gstr = prb_beginStr(arena);
        switch (spec->compiler) {
            case Compiler_Gcc: prb_addStrSegment(&gstr, "gcc"); break;
            case Compiler_Clang: prb_addStrSegment(&gstr, "clang"); break;
            case Compiler_Msvc: prb_addStrSegment(&gstr, "msvc"); break;
        }
        if (spec->twotu) {
            prb_addStrSegment(&gstr, "-2tu");
        } else {
            prb_addStrSegment(&gstr, "-1tu");
        }
        switch (spec->lang) {
            case Lang_C: prb_addStrSegment(&gstr, "-c"); break;
            case Lang_Cpp: prb_addStrSegment(&gstr, "-cpp"); break;
        }

        if (spec->addOutputSuffix.len > 0) {
            prb_addStrSegment(&gstr, "-%.*s", prb_LIT(spec->addOutputSuffix));
        }

        outputSuffix = prb_endStr(&gstr);
    }

#if prb_PLATFORM_WINDOWS
    prb_Str outputExt = prb_STR("exe");
#elif prb_PLATFORM_LINUX
    prb_Str outputExt = prb_STR("bin");
#else
#error unimplemented
#endif

    CompileSpec compileSpec = {};
    compileSpec.compiler = spec->compiler;
    compileSpec.lang = spec->lang;
    compileSpec.flags = spec->flags;
    compileSpec.input = prb_pathJoin(arena, globalTestsDir, prb_STR("tests.c"));
    compileSpec.output = prb_fmt(arena, "%.*s-%.*s.%*s", compileSpec.input.len - 2, compileSpec.input.ptr, prb_LIT(outputSuffix), prb_LIT(outputExt));

    if (spec->twotu) {
        CompileSpec preSpec = compileSpec;
        preSpec.input = prb_pathJoin(arena, globalTestsDir, prb_STR("precompile.c"));
        preSpec.output = prb_fmt(arena, "%.*s-%.*s.obj", preSpec.input.len - 2, preSpec.input.ptr, prb_LIT(outputSuffix));
        prb_Str precmd = constructCompileCmd(arena, preSpec);
        prb_writelnToStdout(arena, precmd);
        prb_assert(prb_execCmd(arena, precmd, 0, (prb_Str) {}).status == prb_ProcessStatus_CompletedSuccess);

        compileSpec.flags = prb_fmt(arena, "-Dprb_NO_IMPLEMENTATION %.*s", prb_LIT(compileSpec.flags));
        compileSpec.optObj = preSpec.output;
    }

    prb_Str cmd = constructCompileCmd(arena, compileSpec);
    prb_writelnToStdout(arena, cmd);
    prb_assert(prb_execCmd(arena, cmd, 0, (prb_Str) {}).status == prb_ProcessStatus_CompletedSuccess);
    prb_writelnToStdout(arena, compileSpec.output);

    prb_Str outlog = prb_fmt(arena, "%.*s-%.*s.log", compileSpec.input.len - 2, compileSpec.input.ptr, prb_LIT(outputSuffix));
    u32     procFlags = prb_ProcessFlag_RedirectStderr | prb_ProcessFlag_RedirectStdout;
    if (spec->doNotRedirect) {
        outlog = (prb_Str) {};
        procFlags = 0;
    }
    prb_ProcessHandle proc = prb_execCmd(arena, prb_fmt(arena, "%.*s %.*s", prb_LIT(compileSpec.output), prb_LIT(outputSuffix)), procFlags, outlog);
    if (proc.status != prb_ProcessStatus_CompletedSuccess) {
        if (!spec->doNotRedirect) {
            prb_ReadEntireFileResult readRes = prb_readEntireFile(arena, outlog);
            prb_assert(readRes.success);
            prb_writelnToStdout(arena, prb_strFromBytes(readRes.content));
        }
        prb_assert(!"test failed");
    }

    spec->generatedCompileSpec = compileSpec;
    spec->generatedLogPath = outlog;
}

function prb_Job
createTestJob(prb_Arena* arena, TestJobSpec spec) {
    TestJobSpec* specAllocated = prb_arenaAllocStruct(arena, TestJobSpec);
    *specAllocated = spec;
    prb_Job job = prb_createJob(compileAndRunTests, specAllocated, arena, 10 * prb_MEGABYTE);
    return job;
}

int
main() {
    prb_TimeStart scriptStart = prb_timeStart();
    prb_Arena     arena_ = prb_createArenaFromVmem(1 * prb_GIGABYTE);
    prb_Arena*    arena = &arena_;

    prb_Str* args = prb_getCmdArgs(arena);
    bool     runAllTests = arrlen(args) >= 2 && prb_streq(args[1], prb_STR("all"));
    bool     runningOnCi = arrlen(args) >= 2 && prb_streq(args[1], prb_STR("ci"));

    globalTestsDir = prb_getAbsolutePath(arena, prb_getParentDir(arena, prb_STR(__FILE__)));
    prb_Str rootDir = prb_getParentDir(arena, globalTestsDir);

    // NOTE(khvorov) Remove artifacts
    {
        prb_Str patsToRemove[] = {
            prb_STR("*.gcda"),
            prb_STR("*.gcno"),
            prb_STR("*.bin"),
            prb_STR("*.obj"),
            prb_STR("*.log"),
            prb_STR("*.supp"),
            prb_STR("coverage*"),
        };
        prb_PathFindSpec spec = {.arena = arena, .dir = globalTestsDir, .mode = prb_PathFindMode_Glob};
        for (i32 patIndex = 0; patIndex < prb_arrayLength(patsToRemove); patIndex++) {
            prb_Str pat = patsToRemove[patIndex];
            spec.pattern = pat;
            prb_PathFindIter iter = prb_createPathFindIter(spec);
            while (prb_pathFindIterNext(&iter) == prb_Success) {
                if (!prb_strEndsWith(iter.curPath, prb_STR("run.bin"))) {
                    prb_removeFileIfExists(arena, iter.curPath);
                }
            }
            prb_destroyPathFindIter(&iter);
        }
    }

    if (!runAllTests && !runningOnCi) {
        // NOTE(khvorov) Fast path to avoid waiting for the full suite
        TestJobSpec spec = {};
        spec.doNotRedirect = true;
        compileAndRunTests(arena, &spec);
    } else {
        // NOTE(khvorov) The full suite

        // NOTE(khvorov) Output compiler versions
        if (runningOnCi) {
            prb_assert(prb_execCmd(arena, prb_STR("clang --version"), 0, (prb_Str) {}).status == prb_ProcessStatus_CompletedSuccess);
#if prb_PLATFORM_WINDOWS
#error unimplemented
#elif prb_PLATFORM_LINUX
            prb_assert(prb_execCmd(arena, prb_STR("gcc --version"), 0, (prb_Str) {}).status == prb_ProcessStatus_CompletedSuccess);
#else
#error unimplemented
#endif
        }

        // NOTE(khvorov) Start static analysis.
        prb_ProcessHandle staticAnalysisProc = {};
        prb_Str           staticAnalysisOutput = {};
        {
            prb_Str mainFilePath = prb_pathJoin(arena, rootDir, prb_STR("cbuild.h"));
            prb_Str staticAnalysisCmd = prb_fmt(arena, "clang-tidy %.*s", prb_LIT(mainFilePath));
            prb_writelnToStdout(arena, staticAnalysisCmd);
            staticAnalysisOutput = prb_pathJoin(arena, globalTestsDir, prb_STR("static_analysis.log"));
            staticAnalysisProc = prb_execCmd(arena, staticAnalysisCmd, prb_ProcessFlag_DontWait | prb_ProcessFlag_RedirectStderr | prb_ProcessFlag_RedirectStdout, staticAnalysisOutput);
            prb_assert(staticAnalysisProc.status == prb_ProcessStatus_Launched);
        }

        // NOTE(khvorov) Run tests from different example directories because I
        // found it tests filepath handling better. These have to complete
        // because working directory is part of the global state of the whole
        // process.
        {
            TestJobSpec spec = {};
            prb_assert(prb_setWorkingDir(arena, rootDir) == prb_Success);
            spec.addOutputSuffix = prb_STR("rootdir");
            compileAndRunTests(arena, &spec);
            prb_assert(prb_setWorkingDir(arena, globalTestsDir) == prb_Success);
            spec.addOutputSuffix = prb_STR("testsdir");
            compileAndRunTests(arena, &spec);
            prb_assert(prb_setWorkingDir(arena, rootDir) == prb_Success);
        }

        // NOTE(khvorov) Coverage
        {
            prb_Str coverageRaw = prb_pathJoin(arena, globalTestsDir, prb_STR("coverage.profraw"));
            prb_assert(prb_setenv(arena, prb_STR("LLVM_PROFILE_FILE"), coverageRaw));
            TestJobSpec spec = {};
            spec.flags = prb_STR("-fprofile-instr-generate -fcoverage-mapping");
            spec.addOutputSuffix = prb_STR("coverage");
            compileAndRunTests(arena, &spec);
            prb_Str coverageIndexed = prb_replaceExt(arena, coverageRaw, prb_STR("profdata"));
            prb_Str mergeCmd = prb_fmt(arena, "llvm-profdata merge -sparse %.*s -o %.*s", prb_LIT(coverageRaw), prb_LIT(coverageIndexed));
            prb_writelnToStdout(arena, mergeCmd);
            prb_assert(prb_execCmd(arena, mergeCmd, 0, (prb_Str) {}).status == prb_ProcessStatus_CompletedSuccess);
            prb_Str coverageText = prb_replaceExt(arena, coverageRaw, prb_STR("txt"));
            prb_Str showCmd = prb_fmt(arena, "llvm-cov show %.*s -instr-profile=%.*s", prb_LIT(spec.generatedCompileSpec.output), prb_LIT(coverageIndexed));
            prb_writelnToStdout(arena, showCmd);
            prb_assert(prb_execCmd(arena, showCmd, prb_ProcessFlag_RedirectStderr | prb_ProcessFlag_RedirectStdout, coverageText).status == prb_ProcessStatus_CompletedSuccess);
        }

        prb_Job* jobs = 0;

        // TODO(khvorov) Check if can run on ci now
        // NOTE(khvorov) Sanitizers (don't run on ci because stuff is executed in a weird way there and they don't work)
        if (!runningOnCi) {
            {
                prb_Str ubSuppress = prb_STR("alignment:prb_stbsp_vsprintfcb");
                prb_Str ubsanFilepath = prb_pathJoin(arena, globalTestsDir, prb_STR("ubsan.supp"));
                prb_assert(prb_writeEntireFile(arena, ubsanFilepath, ubSuppress.ptr, ubSuppress.len));
                prb_assert(prb_setenv(arena, prb_STR("UBSAN_OPTIONS"), prb_fmt(arena, "suppressions=%.*s", prb_LIT(ubsanFilepath))));
            }

            TestJobSpec spec = {};

            spec.flags = prb_STR("-fsanitize=address -fno-omit-frame-pointer");
            spec.addOutputSuffix = prb_STR("san-address");
            arrput(jobs, createTestJob(arena, spec));

            spec.flags = prb_STR("-fsanitize=thread");
            spec.addOutputSuffix = prb_STR("san-thread");
            arrput(jobs, createTestJob(arena, spec));

            spec.flags = prb_STR("-fsanitize=memory -fno-omit-frame-pointer");
            spec.addOutputSuffix = prb_STR("san-memory");
            arrput(jobs, createTestJob(arena, spec));

            spec.flags = prb_STR("-fsanitize=undefined");
            spec.addOutputSuffix = prb_STR("san-ub");
            arrput(jobs, createTestJob(arena, spec));
        }

        // NOTE(khvorov) Run tests for all combinations of compiler/language
        {
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

            TestJobSpec spec = {};
            for (i32 compilerIndex = 0; compilerIndex < prb_arrayLength(compilers); compilerIndex++) {
                spec.compiler = compilers[compilerIndex];
                for (i32 langIndex = 0; langIndex < prb_arrayLength(langs); langIndex++) {
                    spec.lang = langs[langIndex];
                    arrput(jobs, createTestJob(arena, spec));
                }
            }
        }

        // NOTE(khvorov) Two translation units
        {
            TestJobSpec spec = {};
            spec.twotu = true;
            arrput(jobs, createTestJob(arena, spec));
        }

        // NOTE(khvorov) Compile all the examples in every supported way
        {
            CompileSpec spec = {};
            prb_Str     exampleDir = prb_pathJoin(arena, rootDir, prb_STR("example"));
            spec.input = prb_pathJoin(arena, exampleDir, prb_STR("build.c"));
            spec.output = prb_pathJoin(arena, exampleDir, prb_STR("build.bin"));
            prb_Str exampleBuildProgramCompileCmd = constructCompileCmd(arena, spec);
            prb_writelnToStdout(arena, exampleBuildProgramCompileCmd);
            prb_ProcessHandle exampleBuildProgramCompileProc = prb_execCmd(arena, exampleBuildProgramCompileCmd, 0, (prb_Str) {});
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
                    prb_Str cmd = prb_fmt(arena, "%.*s %.*s %.*s", prb_LIT(spec.output), prb_LIT(compilerArg), prb_LIT(buildModeArg));
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

        prb_assert(prb_execJobs(jobs, arrlen(jobs), prb_ThreadMode_Multi));

        // NOTE(khvorov) Print result of static analysis
        prb_assert(prb_waitForProcesses(&staticAnalysisProc, 1));
        {
            prb_ReadEntireFileResult staticAnalysisOutRead = prb_readEntireFile(arena, staticAnalysisOutput);
            prb_assert(staticAnalysisOutRead.success);
            prb_writelnToStdout(arena, prb_STR("static analysis out:"));
            prb_writelnToStdout(arena, prb_strFromBytes(staticAnalysisOutRead.content));
        }

        // NOTE(khvorov) Print sanitizer output
        {
            prb_PathFindSpec spec = {};
            spec.arena = arena;
            spec.dir = globalTestsDir;
            spec.pattern = prb_STR("*-san-*.log");
            spec.mode = prb_PathFindMode_Glob;
            prb_PathFindIter iter = prb_createPathFindIter(spec);
            while (prb_pathFindIterNext(&iter)) {
                prb_ReadEntireFileResult staticAnalysisOutRead = prb_readEntireFile(arena, iter.curPath);
                prb_assert(staticAnalysisOutRead.success);
                prb_writelnToStdout(arena, iter.curPath);
                prb_writelnToStdout(arena, prb_strFromBytes(staticAnalysisOutRead.content));
            }
        }
    }

    prb_writelnToStdout(arena, prb_fmt(arena, "%stest run took %.2fms%s", prb_colorEsc(prb_ColorID_Green).ptr, prb_getMsFrom(scriptStart), prb_colorEsc(prb_ColorID_Reset).ptr));
    return 0;
}
