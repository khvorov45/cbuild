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

function prb_Status
execCmd(prb_Arena* arena, prb_Str cmd) {
    prb_writelnToStdout(arena, cmd);
    prb_Process proc = prb_createProcess(cmd, (prb_ProcessSpec) {});
    prb_Status  result = prb_launchProcesses(arena, &proc, 1, prb_Background_No);
    return result;
}

function void
printFile(prb_Arena* arena, prb_Str file) {
    prb_ReadEntireFileResult readRes = prb_readEntireFile(arena, file);
    prb_assert(readRes.success);
    prb_writelnToStdout(arena, prb_strFromBytes(readRes.content));
}

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
        prb_Str cmd = constructCompileCmd(arena, preSpec);
        prb_assert(execCmd(arena, cmd));

        compileSpec.flags = prb_fmt(arena, "-Dprb_NO_IMPLEMENTATION %.*s", prb_LIT(compileSpec.flags));
        compileSpec.optObj = preSpec.output;
    }

    {
        prb_Str cmd = constructCompileCmd(arena, compileSpec);
        prb_assert(execCmd(arena, cmd));
    }

    prb_ProcessSpec execSpec = {};
    if (!spec->doNotRedirect) {
        execSpec.redirectStderr = true;
        execSpec.redirectStdout = true;
        prb_Str outlog = prb_fmt(arena, "%.*s-%.*s.log", compileSpec.input.len - 2, compileSpec.input.ptr, prb_LIT(outputSuffix));
        execSpec.stdoutFilepath = outlog;
        execSpec.stderrFilepath = outlog;
    }

    prb_Str     cmd = prb_fmt(arena, "%.*s %.*s", prb_LIT(compileSpec.output), prb_LIT(outputSuffix));
    prb_Process proc = prb_createProcess(cmd, execSpec);
    if (prb_launchProcesses(arena, &proc, 1, prb_Background_No) == prb_Failure) {
        if (!spec->doNotRedirect) {
            printFile(arena, execSpec.stdoutFilepath);
        }
        prb_assert(!"test failed");
    }

    spec->generatedCompileSpec = compileSpec;
    spec->generatedLogPath = execSpec.stdoutFilepath;
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

    globalTestsDir = prb_getParentDir(arena, prb_STR(__FILE__));
    prb_Str rootDir = prb_getParentDir(arena, globalTestsDir);
    prb_Str exampleDir = prb_pathJoin(arena, rootDir, prb_STR("example"));

    if (!runningOnCi) {
        prb_CoreCountResult cores = prb_getCoreCount(arena);
        prb_assert(cores.success);
        prb_assert(prb_allowExecutionOnCores(arena, cores.cores - 1));
    }

    // NOTE(khvorov) Remove artifacts
    {
        prb_Str* entries = prb_getAllDirEntries(arena, globalTestsDir, prb_Recursive_No);
        for (i32 entryIndex = 0; entryIndex < arrlen(entries); entryIndex++) {
            prb_Str entry = entries[entryIndex];
            prb_Str entryName = prb_getLastEntryInPath(entry);
            if (!prb_strEndsWith(entry, prb_STR("run.bin"))) {
                bool remove = prb_strEndsWith(entry, prb_STR(".gcda"))
                    || prb_strEndsWith(entry, prb_STR(".gcno"))
                    || prb_strEndsWith(entry, prb_STR(".bin"))
                    || prb_strEndsWith(entry, prb_STR(".obj"))
                    || prb_strEndsWith(entry, prb_STR(".log"))
                    || prb_strEndsWith(entry, prb_STR(".supp"))
                    || prb_strStartsWith(entryName, prb_STR("coverage"));
                if (remove) {
                    prb_assert(prb_removePathIfExists(arena, entry));
                }
            }
        }
    }

    if (!runAllTests && !runningOnCi) {
        // NOTE(khvorov) Fast path to avoid waiting for the full suite
        TestJobSpec spec = {};
        spec.doNotRedirect = true;
        compileAndRunTests(arena, &spec);

        // NOTE(khvorov) Look at coverage for a function
        if (false) {
            prb_Str coverageRaw = prb_pathJoin(arena, globalTestsDir, prb_STR("coverage.profraw"));
            prb_assert(prb_setenv(arena, prb_STR("LLVM_PROFILE_FILE"), coverageRaw));
            TestJobSpec spec = {};
            spec.flags = prb_STR("-fprofile-instr-generate -fcoverage-mapping");
            spec.addOutputSuffix = prb_STR("coverage");
            compileAndRunTests(arena, &spec);
            prb_Str coverageIndexed = prb_replaceExt(arena, coverageRaw, prb_STR("profdata"));
            prb_assert(execCmd(arena, prb_fmt(arena, "llvm-profdata merge -sparse %.*s -o %.*s", prb_LIT(coverageRaw), prb_LIT(coverageIndexed))));
            prb_Str cmd = prb_fmt(arena, "llvm-cov show %.*s -instr-profile=%.*s -name=prb_randomF3201 -show-branches=percent", prb_LIT(spec.generatedCompileSpec.output), prb_LIT(coverageIndexed));
            prb_writelnToStdout(arena, cmd);
            prb_Process covShowProc = prb_createProcess(cmd, (prb_ProcessSpec) {});
            prb_assert(prb_launchProcesses(arena, &covShowProc, 1, prb_Background_No));
        }

    } else {
        // NOTE(khvorov) The full suite

        // NOTE(khvorov) Output compiler versions
        if (runningOnCi) {
            prb_assert(execCmd(arena, prb_STR("clang --version")));
#if prb_PLATFORM_WINDOWS
#error unimplemented
#elif prb_PLATFORM_LINUX
            prb_assert(execCmd(arena, prb_STR("gcc --version")));
#else
#error unimplemented
#endif
        }

        // NOTE(khvorov) Start static analysis.
        prb_Process staticAnalysisProc = {};
        prb_Str     staticAnalysisOutput = {};
        {
            prb_Str         mainFilePath = prb_pathJoin(arena, rootDir, prb_STR("cbuild.h"));
            prb_ProcessSpec spec = {};
            prb_Str         cmd = prb_fmt(arena, "clang-tidy %.*s", prb_LIT(mainFilePath));
            prb_writelnToStdout(arena, cmd);
            staticAnalysisOutput = prb_pathJoin(arena, globalTestsDir, prb_STR("static_analysis.log"));
            spec.redirectStdout = true;
            spec.redirectStderr = true;
            spec.stdoutFilepath = staticAnalysisOutput;
            spec.stderrFilepath = staticAnalysisOutput;
            staticAnalysisProc = prb_createProcess(cmd, spec);
            prb_assert(prb_launchProcesses(arena, &staticAnalysisProc, 1, prb_Background_Yes));
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
            prb_assert(execCmd(arena, prb_fmt(arena, "llvm-profdata merge -sparse %.*s -o %.*s", prb_LIT(coverageRaw), prb_LIT(coverageIndexed))));
            prb_Str coverageText = prb_replaceExt(arena, coverageRaw, prb_STR("txt"));
            prb_Str cmd = prb_fmt(arena, "llvm-cov show %.*s -instr-profile=%.*s", prb_LIT(spec.generatedCompileSpec.output), prb_LIT(coverageIndexed));
            prb_writelnToStdout(arena, cmd);
            prb_ProcessSpec execSpec = {};
            execSpec.redirectStdout = true;
            execSpec.redirectStderr = true;
            execSpec.stdoutFilepath = coverageText;
            execSpec.stderrFilepath = coverageText;
            prb_Process covShowProc = prb_createProcess(cmd, execSpec);
            prb_assert(prb_launchProcesses(arena, &covShowProc, 1, prb_Background_No));
        }

        // NOTE(khvorov) Check we can compile without stb ds short names
        {
            prb_Str mainFile = prb_pathJoin(arena, rootDir, prb_STR("cbuild.h"));
            prb_Str outfile = prb_pathJoin(arena, globalTestsDir, prb_STR("cbuild.gch"));
            prb_assert(execCmd(arena, prb_fmt(arena, "clang -Wall -Wextra -Werror -Wfatal-errors -Dprb_STBDS_NO_SHORT_NAMES %.*s -o %.*s", prb_LIT(mainFile), prb_LIT(outfile))));
            prb_removePathIfExists(arena, outfile);
        }

        prb_Job* jobs = 0;

        // NOTE(khvorov) Sanitizers
        {
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
            for (i32 compilerIndex = 0; compilerIndex < prb_arrayCount(compilers); compilerIndex++) {
                spec.compiler = compilers[compilerIndex];
                for (i32 langIndex = 0; langIndex < prb_arrayCount(langs); langIndex++) {
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
            // NOTE(khvorov) Compile the build program
            CompileSpec spec = {};
            {
                spec.input = prb_pathJoin(arena, exampleDir, prb_STR("build.c"));
                spec.output = prb_pathJoin(arena, exampleDir, prb_STR("build.bin"));
                prb_assert(execCmd(arena, constructCompileCmd(arena, spec)));
            }

            // NOTE(khvorov) Use the build program to compile the examples
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

            for (i32 compArgIndex = 0; compArgIndex < prb_arrayCount(compilerArgs); compArgIndex++) {
                prb_Str compilerArg = compilerArgs[compArgIndex];
                for (i32 buildModeArgIndex = 0; buildModeArgIndex < prb_arrayCount(buildModeArgs); buildModeArgIndex++) {
                    prb_Str buildModeArg = buildModeArgs[buildModeArgIndex];
                    prb_Str cmd = prb_fmt(arena, "%.*s %.*s %.*s", prb_LIT(spec.output), prb_LIT(compilerArg), prb_LIT(buildModeArg));
                    prb_assert(execCmd(arena, cmd));
                    // NOTE(khvorov) Compile again to make sure incremental compilation code executes
                    prb_assert(execCmd(arena, cmd));
                }
            }

            // NOTE(khvorov) Check that the build scripts work
            {
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

                prb_Str execBuildcmd = prb_fmt(arena, "%.*s %.*s %.*s", prb_LIT(buildScriptCmd), prb_LIT(compilerArgs[0]), prb_LIT(buildModeArgs[0]));
                prb_assert(execCmd(arena, execBuildcmd));

                prb_assert(prb_setWorkingDir(arena, exampleDir) == prb_Success);
                prb_assert(execCmd(arena, execBuildcmd));
                prb_assert(prb_setWorkingDir(arena, rootDir) == prb_Success);
            }
        }

        prb_assert(prb_launchJobs(jobs, arrlen(jobs), prb_Background_Yes));
        prb_assert(prb_waitForJobs(jobs, arrlen(jobs)));

        // NOTE(khvorov) Print result of static analysis
        prb_assert(prb_waitForProcesses(&staticAnalysisProc, 1));
        {
            prb_writelnToStdout(arena, prb_STR("static analysis out:"));
            printFile(arena, staticAnalysisOutput);
        }

        // NOTE(khvorov) Print sanitizer output
        {
            prb_Str* entries = prb_getAllDirEntries(arena, globalTestsDir, prb_Recursive_Yes);
            for (i32 entryIndex = 0; entryIndex < arrlen(entries); entryIndex++) {
                prb_Str entry = entries[entryIndex];
                if (prb_strEndsWith(entry, prb_STR(".log"))) {
                    prb_StrFindSpec strFindSpec = {};
                    strFindSpec.pattern = prb_STR("-san-");
                    strFindSpec.mode = prb_StrFindMode_Exact;
                    prb_StrFindResult findRes = prb_strFind(prb_getLastEntryInPath(entry), strFindSpec);
                    if (findRes.found) {
                        prb_writelnToStdout(arena, entry);
                        printFile(arena, entry);
                    }
                }
            }
        }

        // NOTE(khvorov) Launch the examples to make sure they work
        if (!runningOnCi) {
            prb_Str* allInExample = prb_getAllDirEntries(arena, exampleDir, prb_Recursive_No);
            for (i32 exampleEntryIndex = 0; exampleEntryIndex < arrlen(allInExample); exampleEntryIndex++) {
                prb_Str exampleEntry = allInExample[exampleEntryIndex];
                if (prb_strStartsWith(prb_getLastEntryInPath(exampleEntry), prb_STR("build-"))) {
                    prb_Str     buildExe = prb_pathJoin(arena, exampleEntry, prb_STR("example.bin"));
                    prb_Process proc = prb_createProcess(buildExe, (prb_ProcessSpec) {});
                    prb_assert(prb_launchProcesses(arena, &proc, 1, prb_Background_Yes));
                    prb_sleep(3000);
                    prb_assert(prb_killProcesses(&proc, 1));
                }
            }
        }
    }

    prb_writelnToStdout(arena, prb_fmt(arena, "%stest run took %.2fms%s", prb_colorEsc(prb_ColorID_Green).ptr, prb_getMsFrom(scriptStart), prb_colorEsc(prb_ColorID_Reset).ptr));
    return 0;
}
