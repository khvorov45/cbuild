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

int
main() {
    prb_TimeStart scriptStart = prb_timeStart();
    prb_Arena     arena_ = prb_createArenaFromVmem(1 * prb_GIGABYTE);
    prb_Arena*    arena = &arena_;
    prb_String    testsDir = prb_getParentDir(arena, prb_STR(__FILE__));
    prb_String    testsFile = prb_pathJoin(arena, testsDir, prb_STR("tests.c"));
    prb_String    precompFile = prb_pathJoin(arena, testsDir, prb_STR("precompile.c"));

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

    for (i32 precompIndex = 0; precompIndex < arrlen(precompCmds); precompIndex++) {
        CompileCmd precmd = precompCmds[precompIndex];
        CompileCmd cmd = constructCompileCmd(arena, testsFile, precmd.output, precmd.compiler, precmd.lang);
        prb_writelnToStdout(cmd.cmd);
        prb_ProcessHandle proc = prb_execCmd(arena, cmd.cmd, prb_ProcessFlag_DontWait, (prb_String) {});
        arrput(compileProcs, proc);
        arrput(compCmds, cmd);
    }

    prb_assert(prb_waitForProcesses(compileProcs, arrlen(compileProcs)) == prb_Success);

    for (i32 compCmdIndex = 0; compCmdIndex < arrlen(compCmds); compCmdIndex++) {
        CompileCmd compCmd = compCmds[compCmdIndex];
        prb_writelnToStdout(compCmd.output);
        prb_ProcessHandle proc = prb_execCmd(arena, compCmd.output, 0, (prb_String) {});
        prb_assert(proc.status == prb_ProcessStatus_CompletedSuccess);
    }

    // TODO(khvorov) Run example compilation probably
    // TODO(khvorov) Run static analysis
    // TODO(khvorov) Change directories and run again (tests filepath handling)

    prb_writelnToStdout(prb_fmt(arena, "test run took %.2fms", prb_getMsFrom(scriptStart)));
    return 0;
}