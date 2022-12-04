#include "../programmable_build.h"

// TODO(khvorov) Compare current command with the command used last time before deciding not to recompile
// TODO(khvorov) Use preproccessed file checksum to decide if we need to recompile

typedef int32_t  i32;
typedef uint64_t u64;

typedef enum Compiler {
    Compiler_Gcc,
    Compiler_Clang,
    Compiler_Msvc,
} Compiler;

typedef struct ProjectInfo {
    prb_String rootDir;
    prb_String compileOutDir;
    Compiler   compiler;
    bool       release;
} ProjectInfo;

typedef struct StaticLibInfo {
    ProjectInfo*      project;
    prb_String        name;
    prb_String        downloadDir;
    prb_String        includeDir;
    prb_String        includeFlag;
    prb_String        objDir;
    prb_String        libFile;
    prb_String        compileFlags;
    prb_String*       sourcesRelToDownload;
    i32               sourcesCount;
    bool              notDownloaded;
    bool              cpp;
    prb_ProcessStatus compileStatus;
} StaticLibInfo;

typedef enum Lang {
    Lang_C,
    Lang_Cpp,
} Lang;

static StaticLibInfo
getStaticLibInfo(
    prb_Arena*   arena,
    ProjectInfo* project,
    prb_String   name,
    Lang         lang,
    prb_String   includeDirRelToDownload,
    prb_String   compileFlags,
    prb_String*  sourcesRelToDownload,
    i32          sourcesCount
) {
    StaticLibInfo result = {
        .project = project,
        .name = name,
        .cpp = lang == Lang_Cpp,
        .downloadDir = prb_pathJoin(arena, project->rootDir, name),
        .objDir = prb_pathJoin(arena, project->compileOutDir, name),
        .sourcesRelToDownload = sourcesRelToDownload,
        .sourcesCount = sourcesCount,
    };
    result.includeDir = prb_pathJoin(arena, result.downloadDir, includeDirRelToDownload);
    result.includeFlag = prb_fmt(arena, "-I%.*s", prb_LIT(result.includeDir));
    result.compileFlags = prb_fmt(arena, "%.*s %.*s", prb_LIT(compileFlags), prb_LIT(result.includeFlag));

#if prb_PLATFORM_WINDOWS
    prb_String libFilename = prb_fmt(arena, "%.*s.lib", prb_LIT(name));
#elif prb_PLATFORM_LINUX
    prb_String libFilename = prb_fmt(arena, "%.*s.a", prb_LIT(name));
#else
#error unimplemented
#endif

    result.libFile = prb_pathJoin(arena, project->compileOutDir, libFilename);
    result.notDownloaded = !prb_isDirectory(arena, result.downloadDir) || prb_directoryIsEmpty(arena, result.downloadDir);
    return result;
}

static prb_ProcessHandle
gitClone(prb_Arena* arena, StaticLibInfo lib, prb_String downloadUrl) {
    prb_TempMemory    temp = prb_beginTempMemory(arena);
    prb_ProcessHandle handle = {};
    if (lib.notDownloaded) {
        prb_String cmd = prb_fmt(arena, "git clone %.*s %.*s", prb_LIT(downloadUrl), prb_LIT(lib.downloadDir));
        prb_writelnToStdout(cmd);
        handle = prb_execCmd(arena, cmd, prb_ProcessFlag_DontWait, (prb_String) {});
    } else {
        prb_String name = prb_getLastEntryInPath(lib.downloadDir);
        prb_String msg = prb_fmt(arena, "skip git clone %.*s", prb_LIT(name));
        prb_writelnToStdout(msg);
        handle.status = prb_ProcessStatus_CompletedSuccess;
    }
    prb_endTempMemory(temp);
    return handle;
}

static prb_Status
gitReset(prb_Arena* arena, StaticLibInfo lib, prb_String commit) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Status     result = prb_Success;
    if (lib.notDownloaded) {
        prb_String cwd = prb_getWorkingDir(arena);
        prb_assert(prb_setWorkingDir(arena, lib.downloadDir) == prb_Success);
        prb_String cmd = prb_fmt(arena, "git checkout %.*s --", prb_LIT(commit));
        prb_writelnToStdout(cmd);
        prb_ProcessHandle handle = prb_execCmd(arena, cmd, 0, (prb_String) {});
        prb_assert(handle.status == prb_ProcessStatus_CompletedSuccess);
        prb_assert(prb_setWorkingDir(arena, cwd) == prb_Success);
    }
    prb_endTempMemory(temp);
    return result;
}

static bool
fileIsPreprocessed(prb_Arena* arena, prb_String name) {
    bool result = prb_strEndsWith(arena, name, prb_STR(".i"), prb_StringFindMode_Exact) || prb_strEndsWith(arena, name, prb_STR(".ii"), prb_StringFindMode_Exact);
    return result;
}

static prb_String
constructCompileCmd(prb_Arena* arena, ProjectInfo* project, prb_String flags, prb_String inputPath, prb_String outputPath, prb_String linkFlags) {
    prb_GrowingString cmd = prb_beginString(arena);

    switch (project->compiler) {
        case Compiler_Gcc: prb_addStringSegment(&cmd, "gcc"); break;
        case Compiler_Clang: prb_addStringSegment(&cmd, "clang"); break;
        case Compiler_Msvc: prb_addStringSegment(&cmd, "cl /nologo /diagnostics:column /FC"); break;
    }

    if (project->release) {
        switch (project->compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStringSegment(&cmd, " -Ofast"); break;
            case Compiler_Msvc: prb_addStringSegment(&cmd, " /O2"); break;
        }
    } else {
        switch (project->compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStringSegment(&cmd, " -g"); break;
            case Compiler_Msvc: prb_addStringSegment(&cmd, " /Zi"); break;
        }
    }

    bool inIsPreprocessed = fileIsPreprocessed(arena, inputPath);
    bool outIsPreprocess = fileIsPreprocessed(arena, outputPath);
    if (outIsPreprocess) {
        prb_assert(!inIsPreprocessed);
        switch (project->compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStringSegment(&cmd, " -E"); break;
            case Compiler_Msvc: prb_addStringSegment(&cmd, " /P /Fi%.*s", prb_LIT(outputPath)); break;
        }
    }
    if (inIsPreprocessed) {
        prb_assert(!outIsPreprocess);
        switch (project->compiler) {
            case Compiler_Gcc: prb_addStringSegment(&cmd, " -fpreprocessed"); break;
            case Compiler_Clang: break;
            case Compiler_Msvc: prb_addStringSegment(&cmd, " /Yc"); break;
        }
    }

    prb_addStringSegment(&cmd, " %.*s", prb_LIT(flags));
    bool isObj = prb_strEndsWith(arena, outputPath, prb_STR("obj"), prb_StringFindMode_Exact);
    if (isObj) {
        prb_addStringSegment(&cmd, " -c");
    }

#if prb_PLATFORM_WINDOWS
    if (compiler == Compiler_Msvc) {
        prb_String pdbPath = prb_replaceExt(outputPath, prb_STR("pdb"));
        prb_addStringSegment(&cmd, " /Fd%.s", pdbPath);
    }
#endif

    switch (project->compiler) {
        case Compiler_Gcc:
        case Compiler_Clang: prb_addStringSegment(&cmd, " %.*s -o %.*s", prb_LIT(inputPath), prb_LIT(outputPath)); break;
        case Compiler_Msvc: {
            prb_String objPath = isObj ? outputPath : prb_replaceExt(arena, outputPath, prb_STR("obj"));
            prb_addStringSegment(&cmd, " /Fo%.*s", prb_LIT(objPath));
            if (!isObj) {
                prb_addStringSegment(&cmd, " /Fe%.*s", prb_LIT(outputPath));
            }
        } break;
    }

    if (linkFlags.ptr && linkFlags.len > 0) {
        switch (project->compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStringSegment(&cmd, " %.*s", prb_LIT(linkFlags)); break;
            case Compiler_Msvc: prb_addStringSegment(&cmd, "-link -incremental:no %.*s", prb_LIT(linkFlags)); break;
        }
        prb_addStringSegment(&cmd, " %.*s", prb_LIT(linkFlags));
    }

    prb_String cmdStr = prb_endString(&cmd);
    if (!outIsPreprocess) {
        prb_writelnToStdout(cmdStr);
    }
    return cmdStr;
}

typedef struct StringFound {
    char* key;
    bool  value;
} StringFound;

static void
compileStaticLib(prb_Arena* arena, void* staticLibInfo) {
    prb_TimeStart compileStart = prb_timeStart();
    StaticLibInfo* lib = (StaticLibInfo*)staticLibInfo;
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_assert(lib->compileStatus == prb_ProcessStatus_NotLaunched);
    lib->compileStatus = prb_ProcessStatus_Launched;

    prb_assert(prb_createDirIfNotExists(arena, lib->objDir) == prb_Success);

    prb_String* inputPaths = 0;
    for (i32 srcIndex = 0; srcIndex < lib->sourcesCount; srcIndex++) {
        prb_String           srcRelToDownload = lib->sourcesRelToDownload[srcIndex];
        prb_PathFindIterator iter = prb_createPathFindIter((prb_PathFindSpec) {arena, lib->downloadDir, prb_PathFindMode_Glob, .glob.pattern = srcRelToDownload});
        while (prb_pathFindIterNext(&iter)) {
            arrput(inputPaths, iter.curPath);
        }
        prb_destroyPathFindIter(&iter);
    }
    prb_assert(arrlen(inputPaths) > 0);

    // NOTE(khvorov) Recompile everything whenever any .h file changes
    uint64_t latestHFileChange = 0;
    {
        prb_Multitime        multitime = prb_createMultitime();
        prb_PathFindSpec     spec = {arena, lib->downloadDir, prb_PathFindMode_Glob, .recursive = true, .glob.pattern = prb_STR("*.h")};
        prb_PathFindIterator iter = prb_createPathFindIter(spec);
        while (prb_pathFindIterNext(&iter) == prb_Success) {
            prb_multitimeAdd(&multitime, prb_getLastModified(arena, iter.curPath));
        }
        spec.glob.pattern = prb_STR("*.hh");
        iter = prb_createPathFindIter(spec);
        while (prb_pathFindIterNext(&iter) == prb_Success) {
            prb_multitimeAdd(&multitime, prb_getLastModified(arena, iter.curPath));
        }
        latestHFileChange = multitime.timeLatest;
    }

    // TODO(khvorov) Just remove everything that's not obj
    StringFound* existingPreprocess = 0;
    StringFound* existingObjs = 0;
    {
        prb_PathFindIterator iter = prb_createPathFindIter((prb_PathFindSpec) {arena, lib->objDir, prb_PathFindMode_AllEntriesInDir, .recursive = false});
        while (prb_pathFindIterNext(&iter)) {
            if (fileIsPreprocessed(arena, iter.curPath)) {
                shput(existingPreprocess, iter.curPath.ptr, false);
            } else if (prb_strEndsWith(arena, iter.curPath, prb_STR(".obj"), prb_StringFindMode_Exact)) {
                shput(existingObjs, iter.curPath.ptr, false);
            } else {
                prb_removeFileIfExists(arena, iter.curPath);
            }
        }
        prb_destroyPathFindIter(&iter);
    }

    // NOTE(khvorov) Preprocess
    prb_String         preprocessExt = lib->cpp ? prb_STR("ii") : prb_STR("i");
    prb_String*        outputPreprocess = 0;
    prb_ProcessHandle* processesPreprocess = 0;
    for (i32 inputPathIndex = 0; inputPathIndex < arrlen(inputPaths); inputPathIndex++) {
        prb_String inputFilepath = inputPaths[inputPathIndex];
        prb_String inputFilename = prb_getLastEntryInPath(inputFilepath);

        prb_String outputPreprocessFilename = prb_replaceExt(arena, inputFilename, preprocessExt);
        prb_String outputPreprocessFilepath = prb_pathJoin(arena, lib->objDir, outputPreprocessFilename);
        arrput(outputPreprocess, outputPreprocessFilepath);
        if (shgeti(existingPreprocess, (char*)outputPreprocessFilepath.ptr) != -1) {
            shput(existingPreprocess, (char*)outputPreprocessFilepath.ptr, true);
        }

        prb_String        preprocessCmd = constructCompileCmd(arena, lib->project, lib->compileFlags, inputFilepath, outputPreprocessFilepath, prb_STR(""));
        prb_ProcessHandle proc = prb_execCmd(arena, preprocessCmd, prb_ProcessFlag_DontWait, (prb_String) {});
        prb_assert(proc.status == prb_ProcessStatus_Launched);
        arrput(processesPreprocess, proc);
    }

    prb_Status preprocessStatus = prb_waitForProcesses(processesPreprocess, arrlen(processesPreprocess));
    if (preprocessStatus == prb_Success) {
        // NOTE(khvorov) Compile
        prb_String*        outputObjs = 0;
        prb_ProcessHandle* processesCompile = 0;
        for (i32 inputPathIndex = 0; inputPathIndex < arrlen(inputPaths); inputPathIndex++) {
            prb_String inputNotPreprocessedFilepath = inputPaths[inputPathIndex];
            // NOTE(khvorov) I found that giving the compiler preprocessed output generates less useful warnings
            // prb_String inputPreprocessedFilepath = outputPreprocess[inputPathIndex];
            prb_String inputNotPreprocessedFilename = prb_getLastEntryInPath(inputNotPreprocessedFilepath);

            prb_String outputObjFilename = prb_replaceExt(arena, inputNotPreprocessedFilename, prb_STR("obj"));
            prb_String outputObjFilepath = prb_pathJoin(arena, lib->objDir, outputObjFilename);
            arrput(outputObjs, outputObjFilepath);
            if (shgeti(existingObjs, (char*)outputObjFilepath.ptr) != -1) {
                shput(existingObjs, (char*)outputObjFilepath.ptr, true);
            }

            // NOTE(khvorov) Figure out if we should recompile this file
            prb_FileTimestamp outputLastMod = prb_getLastModified(arena, outputObjFilepath);
            bool              shouldRecompile = !outputLastMod.valid;  // NOTE(khvorov) Output exists

            // NOTE(khvorov) Output older than input
            if (!shouldRecompile) {
                prb_FileTimestamp sourceLastMod = prb_getLastModified(arena, inputNotPreprocessedFilepath);
                prb_assert(sourceLastMod.valid);
                shouldRecompile = sourceLastMod.timestamp > outputLastMod.timestamp;
            }

            // NOTE(khvorov) Output older than one of h files we found
            if (!shouldRecompile) {
                shouldRecompile = latestHFileChange > outputLastMod.timestamp;
            }

            if (shouldRecompile) {
                prb_String        cmd = constructCompileCmd(arena, lib->project, lib->compileFlags, inputNotPreprocessedFilepath, outputObjFilepath, prb_STR(""));
                prb_ProcessHandle process = prb_execCmd(arena, cmd, prb_ProcessFlag_DontWait, (prb_String) {});
                arrput(processesCompile, process);
            }
        }

        // NOTE(khvorov) Remove all objs that don't correspond to any inputs
        for (i32 existingObjIndex = 0; existingObjIndex < shlen(existingObjs); existingObjIndex++) {
            StringFound existingObj = existingObjs[existingObjIndex];
            if (!existingObj.value) {
                prb_assert(prb_removeFileIfExists(arena, prb_STR(existingObj.key)) == prb_Success);
            }
        }

        // NOTE(khvorov) Remove all preprocessed that don't correspond to any inputs
        for (i32 existingPreprocessIndex = 0; existingPreprocessIndex < shlen(existingPreprocess); existingPreprocessIndex++) {
            StringFound existingI = existingPreprocess[existingPreprocessIndex];
            if (!existingI.value) {
                prb_assert(prb_removeFileIfExists(arena, prb_STR(existingI.key)) == prb_Success);
            }
        }

        if (arrlen(processesCompile) == 0) {
            prb_writelnToStdout(prb_fmt(arena, "skip compile %.*s", prb_LIT(lib->name)));
        }

        prb_Status compileStatus = prb_waitForProcesses(processesCompile, arrlen(processesCompile));
        arrfree(processesCompile);
        if (compileStatus == prb_Success) {
            prb_String objsPathsString = prb_stringsJoin(arena, outputObjs, arrlen(outputObjs), prb_STR(" "));

            u64 sourceLastMod = 0;
            {
                prb_Multitime multitime = prb_createMultitime();
                for (i32 pathIndex = 0; pathIndex < arrlen(outputObjs); pathIndex++) {
                    prb_String        path = outputObjs[pathIndex];
                    prb_FileTimestamp lastMod = prb_getLastModified(arena, path);
                    prb_assert(lastMod.valid);
                    prb_multitimeAdd(&multitime, lastMod);
                }
                prb_assert(multitime.validAddedTimestampsCount > 0 && multitime.invalidAddedTimestampsCount == 0);
                sourceLastMod = multitime.timeLatest;
            }
            arrfree(outputObjs);

            prb_FileTimestamp outputLastMod = prb_getLastModified(arena, lib->libFile);
            prb_Status        libStatus = prb_Success;
            if (!outputLastMod.valid || (sourceLastMod > outputLastMod.timestamp)) {
#if prb_PLATFORM_WINDOWS
                prb_String libCmd = prb_fmt("lib /nologo -out:%.*s %.*s", libFile, objsPattern);
#elif prb_PLATFORM_LINUX
                prb_String libCmd = prb_fmt(arena, "ar rcs %.*s %.*s", prb_LIT(lib->libFile), prb_LIT(objsPathsString));
#endif
                prb_writelnToStdout(libCmd);
                prb_assert(prb_removeFileIfExists(arena, lib->libFile) == prb_Success);
                prb_ProcessHandle libHandle = prb_execCmd(arena, libCmd, 0, (prb_String) {});
                prb_assert(libHandle.status == prb_ProcessStatus_CompletedSuccess || libHandle.status == prb_ProcessStatus_CompletedFailed);
                libStatus = libHandle.status == prb_ProcessStatus_CompletedSuccess ? prb_Success : prb_Failure;
            } else {
                prb_String msg = prb_fmt(arena, "skip lib %.*s", prb_LIT(lib->name));
                prb_writelnToStdout(msg);
            }

            if (libStatus == prb_Success) {
                lib->compileStatus = prb_ProcessStatus_CompletedSuccess;
            }
        }
    }

    if (lib->compileStatus != prb_ProcessStatus_CompletedSuccess) {
        lib->compileStatus = prb_ProcessStatus_CompletedFailed;
    }
    arrfree(inputPaths);
    shfree(existingPreprocess);
    shfree(existingObjs);
    arrfree(outputPreprocess);

    prb_writelnToStdout(prb_fmt(arena, "compile %.*s: %.2fms", prb_LIT(lib->name), prb_getMsFrom(compileStart)));
    prb_endTempMemory(temp);
}

static void
compileAndRunBidiGenTab(prb_Arena* arena, ProjectInfo* project, prb_String src, prb_String flags, prb_String runArgs, prb_String outpath) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    if (!prb_isFile(arena, outpath)) {
#if prb_PLATFORM_WINDOWS
        prb_String exeFilename = prb_replaceExt(arena, stc, prb_STR("exe"));
#elif prb_PLATFORM_LINUX
        prb_String exeFilename = prb_replaceExt(arena, src, prb_STR("bin"));
#else
#error unimplemented
#endif
        prb_String        packtabPath = prb_pathJoin(arena, prb_getParentDir(arena, src), prb_STR("packtab.c"));
        prb_String        cmd = constructCompileCmd(arena, project, flags, prb_fmt(arena, "%.*s %.*s", prb_LIT(packtabPath), prb_LIT(src)), exeFilename, prb_STR(""));
        prb_ProcessHandle handle = prb_execCmd(arena, cmd, 0, (prb_String) {});
        prb_assert(handle.status == prb_ProcessStatus_CompletedSuccess);

        prb_String cmdRun = prb_fmt(arena, "%.*s %.*s", prb_LIT(exeFilename), prb_LIT(runArgs));
        prb_writelnToStdout(cmdRun);
        prb_ProcessHandle handleRun = prb_execCmd(arena, cmdRun, prb_ProcessFlag_RedirectStdout, outpath);
        prb_assert(handleRun.status == prb_ProcessStatus_CompletedSuccess);
    }
    prb_endTempMemory(temp);
}

prb_PUBLICDEF void
textfileReplace(prb_Arena* arena, prb_String path, prb_String pattern, prb_String replacement) {
    prb_ReadEntireFileResult content = prb_readEntireFile(arena, path);
    prb_assert(content.success);
    prb_StringFindSpec spec = {
        .str = (prb_String) {(const char*)content.content.data, content.content.len},
        .pattern = pattern,
        .mode = prb_StringFindMode_Exact,
        .direction = prb_StringDirection_FromStart,
    };
    prb_String newContent = prb_strReplace(arena, spec, replacement);
    prb_assert(prb_writeEntireFile(arena, path, newContent.ptr, newContent.len) == prb_Success);
}

int
main() {
    prb_TimeStart scriptStartTime = prb_timeStart();
    prb_Arena     arena_ = prb_createArenaFromVmem(1 * prb_GIGABYTE);
    prb_Arena*    arena = &arena_;
    ProjectInfo   project_ = {};
    ProjectInfo*  project = &project_;

    prb_String* cmdArgs = prb_getCmdArgs(arena);
    prb_assert(arrlen(cmdArgs) == 3);
    prb_String compilerStr = cmdArgs[1];
    prb_String buildTypeStr = cmdArgs[2];
    prb_assert(prb_streq(buildTypeStr, prb_STR("debug")) || prb_streq(buildTypeStr, prb_STR("release")));

    project->rootDir = prb_getParentDir(arena, prb_STR(__FILE__));
    project->release = prb_streq(buildTypeStr, prb_STR("release"));
    project->compileOutDir = prb_pathJoin(arena, project->rootDir, prb_fmt(arena, "build-%.*s-%.*s", prb_LIT(compilerStr), prb_LIT(buildTypeStr)));
    prb_assert(prb_createDirIfNotExists(arena, project->compileOutDir) == prb_Success);

#if prb_PLATFORM_WINDOWS
    prb_assert(prb_streq(compilerStr, prb_STR("msvc")) || prb_streq(compilerStr, prb_STR("clang")));
    project->compiler = prb_streq(compilerStr, prb_STR("msvc")) ? Compiler_Msvc : Compiler_Clang;
#elif prb_PLATFORM_LINUX
    prb_assert(prb_streq(compilerStr, prb_STR("gcc")) || prb_streq(compilerStr, prb_STR("clang")));
    project->compiler = prb_streq(compilerStr, prb_STR("gcc")) ? Compiler_Gcc : Compiler_Clang;
#else
#error unimlemented
#endif

    //
    // SECTION Setup
    //

    // NOTE(khvorov) Fribidi

    prb_String fribidiCompileSouces[] = {prb_STR("lib/*.c")};
    prb_String fribidiNoConfigFlag = prb_STR("-DDONT_HAVE_FRIBIDI_CONFIG_H -DDONT_HAVE_FRIBIDI_UNICODE_VERSION_H");

    StaticLibInfo fribidi = getStaticLibInfo(
        arena,
        project,
        prb_STR("fribidi"),
        Lang_C,
        prb_STR("lib"),
        prb_fmt(arena, "%.*s -Dfribidi_malloc=fribidiCustomMalloc -Dfribidi_free=fribidiCustomFree -DHAVE_STRING_H=1 -DHAVE_STRINGIZE=1", prb_LIT(fribidiNoConfigFlag)),
        fribidiCompileSouces,
        prb_arrayLength(fribidiCompileSouces)
    );

    // NOTE(khvorov) ICU

    prb_String icuCompileSources[] = {
        prb_STR("icu4c/source/common/uchar.cpp"),
        prb_STR("icu4c/source/common/utrie.cpp"),
        prb_STR("icu4c/source/common/utrie2.cpp"),
        // prb_STR("icu4c/source/common/cmemory.cpp"), // NOTE(khvorov) Replaced in example.c
        prb_STR("icu4c/source/common/utf_impl.cpp"),
        prb_STR("icu4c/source/common/normalizer2.cpp"),
        prb_STR("icu4c/source/common/normalizer2impl.cpp"),
        prb_STR("icu4c/source/common/uobject.cpp"),
        prb_STR("icu4c/source/common/edits.cpp"),
        prb_STR("icu4c/source/common/unistr.cpp"),
        prb_STR("icu4c/source/common/appendable.cpp"),
        prb_STR("icu4c/source/common/ustring.cpp"),
        prb_STR("icu4c/source/common/cstring.cpp"),
        prb_STR("icu4c/source/common/uinvchar.cpp"),
        prb_STR("icu4c/source/common/udataswp.cpp"),
        prb_STR("icu4c/source/common/putil.cpp"),
        prb_STR("icu4c/source/common/charstr.cpp"),
        prb_STR("icu4c/source/common/umutex.cpp"),
        prb_STR("icu4c/source/common/ucln_cmn.cpp"),
        prb_STR("icu4c/source/common/utrace.cpp"),
        prb_STR("icu4c/source/common/stringpiece.cpp"),
        prb_STR("icu4c/source/common/ustrtrns.cpp"),
        prb_STR("icu4c/source/common/util.cpp"),
        prb_STR("icu4c/source/common/patternprops.cpp"),
        prb_STR("icu4c/source/common/uniset.cpp"),
        prb_STR("icu4c/source/common/unifilt.cpp"),
        prb_STR("icu4c/source/common/unifunct.cpp"),
        prb_STR("icu4c/source/common/uvector.cpp"),
        prb_STR("icu4c/source/common/uarrsort.cpp"),
        prb_STR("icu4c/source/common/unisetspan.cpp"),
        prb_STR("icu4c/source/common/bmpset.cpp"),
        prb_STR("icu4c/source/common/ucptrie.cpp"),
        prb_STR("icu4c/source/common/bytesinkutil.cpp"),
        prb_STR("icu4c/source/common/bytestream.cpp"),
        prb_STR("icu4c/source/common/umutablecptrie.cpp"),
        prb_STR("icu4c/source/common/utrie_swap.cpp"),
        prb_STR("icu4c/source/common/ubidi_props.cpp"),
        prb_STR("icu4c/source/common/uprops.cpp"),
        prb_STR("icu4c/source/common/unistr_case.cpp"),
        prb_STR("icu4c/source/common/ustrcase.cpp"),
        prb_STR("icu4c/source/common/ucase.cpp"),
        prb_STR("icu4c/source/common/loadednormalizer2impl.cpp"),
        prb_STR("icu4c/source/common/uhash.cpp"),
        prb_STR("icu4c/source/common/udatamem.cpp"),
        prb_STR("icu4c/source/common/ucmndata.cpp"),
        prb_STR("icu4c/source/common/umapfile.cpp"),
        prb_STR("icu4c/source/common/udata.cpp"),
        prb_STR("icu4c/source/common/emojiprops.cpp"),
        prb_STR("icu4c/source/common/ucharstrieiterator.cpp"),
        prb_STR("icu4c/source/common/uvectr32.cpp"),
        prb_STR("icu4c/source/common/umath.cpp"),
        prb_STR("icu4c/source/common/ucharstrie.cpp"),
        prb_STR("icu4c/source/common/propname.cpp"),
        prb_STR("icu4c/source/common/bytestrie.cpp"),
        prb_STR("icu4c/source/stubdata/stubdata.cpp"),  // NOTE(khvorov) We won't need to access data here
    };

    StaticLibInfo icu = getStaticLibInfo(
        arena,
        project,
        prb_STR("icu"),
        Lang_Cpp,
        prb_STR("icu4c/source/common"),
        prb_STR("-DU_COMMON_IMPLEMENTATION=1 -DU_COMBINED_IMPLEMENTATION=1 -DU_STATIC_IMPLEMENTATION=1"),
        icuCompileSources,
        prb_arrayLength(icuCompileSources)
    );

    // NOTE(khvorov) Freetype

    prb_String freetypeCompileSources[] = {
        // Required
        //"src/base/ftsystem.c", // NOTE(khvorov) Memory routines for freetype are in the main program
        prb_STR("src/base/ftinit.c"),
        prb_STR("src/base/ftdebug.c"),
        prb_STR("src/base/ftbase.c"),

        // Recommended
        prb_STR("src/base/ftbbox.c"),
        prb_STR("src/base/ftglyph.c"),

        // Optional
        prb_STR("src/base/ftbdf.c"),
        prb_STR("src/base/ftbitmap.c"),
        prb_STR("src/base/ftcid.c"),
        prb_STR("src/base/ftfstype.c"),
        prb_STR("src/base/ftgasp.c"),
        prb_STR("src/base/ftgxval.c"),
        prb_STR("src/base/ftmm.c"),
        prb_STR("src/base/ftotval.c"),
        prb_STR("src/base/ftpatent.c"),
        prb_STR("src/base/ftpfr.c"),
        prb_STR("src/base/ftstroke.c"),
        prb_STR("src/base/ftsynth.c"),
        prb_STR("src/base/fttype1.c"),
        prb_STR("src/base/ftwinfnt.c"),

        // Font drivers
        prb_STR("src/bdf/bdf.c"),
        prb_STR("src/cff/cff.c"),
        prb_STR("src/cid/type1cid.c"),
        prb_STR("src/pcf/pcf.c"),
        prb_STR("src/pfr/pfr.c"),
        prb_STR("src/sfnt/sfnt.c"),
        prb_STR("src/truetype/truetype.c"),
        prb_STR("src/type1/type1.c"),
        prb_STR("src/type42/type42.c"),
        prb_STR("src/winfonts/winfnt.c"),

        // Rasterisers
        prb_STR("src/raster/raster.c"),
        prb_STR("src/sdf/sdf.c"),
        prb_STR("src/smooth/smooth.c"),
        prb_STR("src/svg/svg.c"),

        // Auxillary
        prb_STR("src/autofit/autofit.c"),
        prb_STR("src/cache/ftcache.c"),
        prb_STR("src/gzip/ftgzip.c"),
        prb_STR("src/lzw/ftlzw.c"),
        prb_STR("src/bzip2/ftbzip2.c"),
        prb_STR("src/gxvalid/gxvalid.c"),
        prb_STR("src/otvalid/otvalid.c"),
        prb_STR("src/psaux/psaux.c"),
        prb_STR("src/pshinter/pshinter.c"),
        prb_STR("src/psnames/psnames.c"),
    };

    StaticLibInfo freetype = getStaticLibInfo(
        arena,
        project,
        prb_STR("freetype"),
        Lang_C,
        prb_STR("include"),
        prb_fmt(arena, "-DFT2_BUILD_LIBRARY -DFT_CONFIG_OPTION_DISABLE_STREAM_SUPPORT -DFT_CONFIG_OPTION_USE_HARFBUZZ"),
        freetypeCompileSources,
        prb_arrayLength(freetypeCompileSources)
    );

    // NOTE(khvorov) Harfbuzz

    prb_String harfbuzzCompileSources[] = {
        prb_STR("src/hb-aat-layout.cc"),
        prb_STR("src/hb-aat-map.cc"),
        prb_STR("src/hb-blob.cc"),
        prb_STR("src/hb-buffer-serialize.cc"),
        prb_STR("src/hb-buffer-verify.cc"),
        prb_STR("src/hb-buffer.cc"),
        prb_STR("src/hb-common.cc"),
        prb_STR("src/hb-coretext.cc"),
        prb_STR("src/hb-directwrite.cc"),
        prb_STR("src/hb-draw.cc"),
        prb_STR("src/hb-face.cc"),
        prb_STR("src/hb-fallback-shape.cc"),
        prb_STR("src/hb-font.cc"),
        prb_STR("src/hb-ft.cc"),
        prb_STR("src/hb-gdi.cc"),
        prb_STR("src/hb-glib.cc"),
        prb_STR("src/hb-graphite2.cc"),
        prb_STR("src/hb-map.cc"),
        prb_STR("src/hb-number.cc"),
        prb_STR("src/hb-ot-cff1-table.cc"),
        prb_STR("src/hb-ot-cff2-table.cc"),
        prb_STR("src/hb-ot-color.cc"),
        prb_STR("src/hb-ot-face.cc"),
        prb_STR("src/hb-ot-font.cc"),
        prb_STR("src/hb-ot-layout.cc"),
        prb_STR("src/hb-ot-map.cc"),
        prb_STR("src/hb-ot-math.cc"),
        prb_STR("src/hb-ot-meta.cc"),
        prb_STR("src/hb-ot-metrics.cc"),
        prb_STR("src/hb-ot-name.cc"),
        prb_STR("src/hb-ot-shape-fallback.cc"),
        prb_STR("src/hb-ot-shape-normalize.cc"),
        prb_STR("src/hb-ot-shape.cc"),
        prb_STR("src/hb-ot-shaper-arabic.cc"),
        prb_STR("src/hb-ot-shaper-default.cc"),
        prb_STR("src/hb-ot-shaper-hangul.cc"),
        prb_STR("src/hb-ot-shaper-hebrew.cc"),
        prb_STR("src/hb-ot-shaper-indic-table.cc"),
        prb_STR("src/hb-ot-shaper-indic.cc"),
        prb_STR("src/hb-ot-shaper-khmer.cc"),
        prb_STR("src/hb-ot-shaper-myanmar.cc"),
        prb_STR("src/hb-ot-shaper-syllabic.cc"),
        prb_STR("src/hb-ot-shaper-thai.cc"),
        prb_STR("src/hb-ot-shaper-use.cc"),
        prb_STR("src/hb-ot-shaper-vowel-constraints.cc"),
        prb_STR("src/hb-ot-tag.cc"),
        prb_STR("src/hb-ot-var.cc"),
        prb_STR("src/hb-set.cc"),
        prb_STR("src/hb-shape-plan.cc"),
        prb_STR("src/hb-shape.cc"),
        prb_STR("src/hb-shaper.cc"),
        prb_STR("src/hb-static.cc"),
        prb_STR("src/hb-style.cc"),
        prb_STR("src/hb-ucd.cc"),
        prb_STR("src/hb-unicode.cc"),
        prb_STR("src/hb-uniscribe.cc"),
        prb_STR("src/hb-icu.cc"),
    };

    StaticLibInfo harfbuzz = getStaticLibInfo(
        arena,
        project,
        prb_STR("harfbuzz"),
        Lang_Cpp,
        prb_STR("src"),
        prb_fmt(arena, "%.*s %.*s -DHAVE_ICU=1 -DHAVE_FREETYPE=1 -DHB_CUSTOM_MALLOC=1", prb_LIT(icu.includeFlag), prb_LIT(freetype.includeFlag)),
        harfbuzzCompileSources,
        prb_arrayLength(harfbuzzCompileSources)
    );

    // NOTE(khvorov) Freetype and harfbuzz depend on each other
    freetype.compileFlags = prb_fmt(arena, "%.*s %.*s", prb_LIT(freetype.compileFlags), prb_LIT(harfbuzz.includeFlag));

    // NOTE(khvorov) SDL

    prb_String sdlCompileSources[] = {
        prb_STR("src/atomic/*.c"),
        prb_STR("src/thread/*.c"),
        prb_STR("src/thread/generic/*.c"),
        prb_STR("src/events/*.c"),
        prb_STR("src/file/*.c"),
        prb_STR("src/stdlib/*.c"),
        prb_STR("src/libm/*.c"),
        prb_STR("src/locale/*.c"),
        prb_STR("src/timer/*.c"),
        prb_STR("src/video/*.c"),
        prb_STR("src/video/dummy/*.c"),
        prb_STR("src/video/yuv2rgb/*.c"),
        prb_STR("src/render/*.c"),
        prb_STR("src/render/software/*.c"),
        prb_STR("src/cpuinfo/*.c"),
        prb_STR("src/*.c"),
        prb_STR("src/misc/*.c"),
#if prb_PLATFORM_WINDOWS
        prb_STR("src/core/windows/windows.c"),
        prb_STR("src/filesystem/windows/*.c"),
        prb_STR("src/timer/windows/*.c"),
        prb_STR("src/video/windows/*.c"),
        prb_STR("src/locale/windows/*.c"),
        prb_STR("src/main/windows/*.c"),
#elif prb_PLATFORM_LINUX
        prb_STR("src/timer/unix/*.c"),
        prb_STR("src/filesystem/unix/*.c"),
        prb_STR("src/loadso/dlopen/*.c"),
        prb_STR("src/video/x11/*.c"),
        prb_STR("src/core/unix/SDL_poll.c"),
        prb_STR("src/core/linux/SDL_threadprio.c"),
        prb_STR("src/misc/unix/*.c"),
#endif
    };

    prb_String sdlCompileFlags[] = {
        prb_STR("-DSDL_AUDIO_DISABLED=1"),
        prb_STR("-DSDL_HAPTIC_DISABLED=1"),
        prb_STR("-DSDL_HIDAPI_DISABLED=1"),
        prb_STR("-DSDL_SENSOR_DISABLED=1"),
        prb_STR("-DSDL_LOADSO_DISABLED=1"),
        prb_STR("-DSDL_THREADS_DISABLED=1"),
        prb_STR("-DSDL_TIMERS_DISABLED=1"),
        prb_STR("-DSDL_JOYSTICK_DISABLED=1"),
        prb_STR("-DSDL_VIDEO_RENDER_D3D=0"),
        prb_STR("-DSDL_VIDEO_RENDER_D3D11=0"),
        prb_STR("-DSDL_VIDEO_RENDER_D3D12=0"),
        prb_STR("-DSDL_VIDEO_RENDER_OGL=0"),
        prb_STR("-DSDL_VIDEO_RENDER_OGL_ES2=0"),
#if prb_PLATFORM_LINUX
        prb_STR("-Wno-deprecated-declarations"),
        prb_STR("-DHAVE_STRING_H=1"),
        prb_STR("-DHAVE_STDIO_H=1"),
        prb_STR("-DSDL_TIMER_UNIX=1"),  // NOTE(khvorov) We don't actually need the prb_STR("timers") subsystem to use this
        prb_STR("-DSDL_FILESYSTEM_UNIX=1"),
        prb_STR("-DSDL_VIDEO_DRIVER_X11=1"),
        prb_STR("-DSDL_VIDEO_DRIVER_X11_SUPPORTS_GENERIC_EVENTS=1"),
        prb_STR("-DNO_SHARED_MEMORY=1"),
        prb_STR("-DHAVE_NANOSLEEP=1"),
        prb_STR("-DHAVE_CLOCK_GETTIME=1"),
        prb_STR("-DCLOCK_MONOTONIC_RAW=1"),
#endif
    };

    StaticLibInfo sdl = getStaticLibInfo(
        arena,
        project,
        prb_STR("sdl"),
        Lang_C,
        prb_STR("include"),
        prb_stringsJoin(arena, sdlCompileFlags, prb_arrayLength(sdlCompileFlags), prb_STR(" ")),
        sdlCompileSources,
        prb_arrayLength(sdlCompileSources)
    );

    //
    // SECTION Download
    //

    prb_ProcessHandle* downloadHandles = 0;
    arrput(downloadHandles, gitClone(arena, fribidi, prb_STR("https://github.com/fribidi/fribidi")));
    arrput(downloadHandles, gitClone(arena, icu, prb_STR("https://github.com/unicode-org/icu")));
    arrput(downloadHandles, gitClone(arena, freetype, prb_STR("https://github.com/freetype/freetype")));
    arrput(downloadHandles, gitClone(arena, harfbuzz, prb_STR("https://github.com/harfbuzz/harfbuzz")));
    arrput(downloadHandles, gitClone(arena, sdl, prb_STR("https://github.com/libsdl-org/SDL")));
    prb_assert(prb_waitForProcesses(downloadHandles, arrlen(downloadHandles)) == prb_Success);

    // NOTE(khvorov) Latest commits at the time of writing to make sure the example keeps working
    prb_assert(gitReset(arena, fribidi, prb_STR("a6a4defff24aabf9195f462f9a7736f3d9e9c120")) == prb_Success);
    prb_assert(gitReset(arena, icu, prb_STR("3654e945b68d5042cbf6254dd559a7ba794a76b3")) == prb_Success);
    prb_assert(gitReset(arena, freetype, prb_STR("aca4ec5907e0bfb5bbeb01370257a121f3f47a0f")) == prb_Success);
    prb_assert(gitReset(arena, harfbuzz, prb_STR("a5d35fd80a26cb62c4c9030894f94c0785d183e7")) == prb_Success);
    prb_assert(gitReset(arena, sdl, prb_STR("bc5677db95f32294a1e2c20f1b4146df02309ac7")) == prb_Success);

    //
    // SECTION Pre-compilation stuff
    //

    // NOTE(khvorov) Generate fribidi tables
    {
        prb_String gentabDir = prb_pathJoin(arena, fribidi.downloadDir, prb_STR("gen.tab"));
        prb_String flags = prb_fmt(arena, "%.*s %.*s -DHAVE_STDLIB_H=1 -DHAVE_STRING_H -DHAVE_STRINGIZE", prb_LIT(fribidiNoConfigFlag), prb_LIT(fribidi.includeFlag));
        prb_String datadir = prb_pathJoin(arena, gentabDir, prb_STR("unidata"));
        prb_String unidat = prb_pathJoin(arena, datadir, prb_STR("UnicodeData.txt"));

        // NOTE(khvorov) This max-depth is also known as compression and is set to 2 in makefiles
        i32 maxDepth = 2;

        prb_String bracketsPath = prb_pathJoin(arena, datadir, prb_STR("BidiBrackets.txt"));
        compileAndRunBidiGenTab(
            arena,
            project,
            prb_pathJoin(arena, gentabDir, prb_STR("gen-brackets-tab.c")),
            flags,
            prb_fmt(arena, "%d %.*s %.*s", maxDepth, prb_LIT(bracketsPath), prb_LIT(unidat)),
            prb_pathJoin(arena, fribidi.includeDir, prb_STR("brackets.tab.i"))
        );

        compileAndRunBidiGenTab(
            arena,
            project,
            prb_pathJoin(arena, gentabDir, prb_STR("gen-arabic-shaping-tab.c")),
            flags,
            prb_fmt(arena, "%d %.*s", maxDepth, prb_LIT(unidat)),
            prb_pathJoin(arena, fribidi.includeDir, prb_STR("arabic-shaping.tab.i"))
        );

        prb_String shapePath = prb_pathJoin(arena, datadir, prb_STR("ArabicShaping.txt"));
        compileAndRunBidiGenTab(
            arena,
            project,
            prb_pathJoin(arena, gentabDir, prb_STR("gen-joining-type-tab.c")),
            flags,
            prb_fmt(arena, "%d %.*s %.*s", maxDepth, prb_LIT(unidat), prb_LIT(shapePath)),
            prb_pathJoin(arena, fribidi.includeDir, prb_STR("joining-type.tab.i"))
        );

        compileAndRunBidiGenTab(
            arena,
            project,
            prb_pathJoin(arena, gentabDir, prb_STR("gen-brackets-type-tab.c")),
            flags,
            prb_fmt(arena, "%d %.*s", maxDepth, prb_LIT(bracketsPath)),
            prb_pathJoin(arena, fribidi.includeDir, prb_STR("brackets-type.tab.i"))
        );

        prb_String mirrorPath = prb_pathJoin(arena, datadir, prb_STR("BidiMirroring.txt"));
        compileAndRunBidiGenTab(
            arena,
            project,
            prb_pathJoin(arena, gentabDir, prb_STR("gen-mirroring-tab.c")),
            flags,
            prb_fmt(arena, "%d %.*s", maxDepth, prb_LIT(mirrorPath)),
            prb_pathJoin(arena, fribidi.includeDir, prb_STR("mirroring.tab.i"))
        );

        compileAndRunBidiGenTab(
            arena,
            project,
            prb_pathJoin(arena, gentabDir, prb_STR("gen-bidi-type-tab.c")),
            flags,
            prb_fmt(arena, "%d %.*s", maxDepth, prb_LIT(unidat)),
            prb_pathJoin(arena, fribidi.includeDir, prb_STR("bidi-type.tab.i"))
        );
    }

    // NOTE(khvorov) Forward declarations for fribidi custom allocators
    if (fribidi.notDownloaded) {
        prb_String file = prb_pathJoin(arena, fribidi.downloadDir, prb_STR("lib/common.h"));
        textfileReplace(
            arena,
            file,
            prb_STR("#ifndef fribidi_malloc"),
            prb_STR("#include <stddef.h>\nvoid* fribidiCustomMalloc(size_t);\nvoid fribidiCustomFree(void*);\n#ifndef fribidi_malloc")
        );
    }

    // NOTE(khvorov) Fix SDL
    if (sdl.notDownloaded) {
        prb_String downloadDir = sdl.downloadDir;

        // NOTE(khvorov) Purge dynamic api because otherwise you have to compile a lot more of sdl
        prb_String dynapiPath = prb_pathJoin(arena, downloadDir, prb_STR("src/dynapi/SDL_dynapi.h"));
        textfileReplace(
            arena,
            dynapiPath,
            prb_STR("#define SDL_DYNAMIC_API 1"),
            prb_STR("#define SDL_DYNAMIC_API 0")
        );

        // NOTE(khvorov) This XMissingExtension function is in X11 extensions and SDL doesn't use it.
        // Saves us from having to -lXext for no reason
        prb_String x11sym = prb_pathJoin(arena, downloadDir, prb_STR("src/video/x11/SDL_x11sym.h"));
        textfileReplace(
            arena,
            x11sym,
            prb_STR("SDL_X11_SYM(int,XMissingExtension,(Display* a,_Xconst char* b),(a,b),return)"),
            prb_STR("//SDL_X11_SYM(int,XMissingExtension,(Display* a,_Xconst char* b),(a,b),return")
        );

        // NOTE(khvorov) SDL allocates the pixels in the X11 framebuffer using
        // SDL_malloc but then frees it using XDestroyImage which will call libc
        // free. So even SDL's own custom malloc won't work because libc free will
        // crash when trying to free a pointer allocated with something other than
        // libc malloc.
        prb_String x11FrameBuffer = prb_pathJoin(arena, downloadDir, prb_STR("src/video/x11/SDL_x11framebuffer.c"));
        textfileReplace(
            arena,
            x11FrameBuffer,
            prb_STR("XDestroyImage(data->ximage);"),
            prb_STR("SDL_free(data->ximage->data);data->ximage->data = 0;XDestroyImage(data->ximage);")
        );
    }

    //
    // SECTION Compile
    //

    // NOTE(khvorov) Force clean
    // prb_assert(prb_clearDirectory(arena, fribidi.objDir) == prb_Success);
    // prb_assert(prb_clearDirectory(arena, icu.objDir) == prb_Success);
    // prb_assert(prb_clearDirectory(arena, freetype.objDir) == prb_Success);
    // prb_assert(prb_clearDirectory(arena, harfbuzz.objDir) == prb_Success);
    // prb_assert(prb_clearDirectory(arena, sdl.objDir) == prb_Success);

    prb_Job* compileJobs = 0;
    arrput(compileJobs, prb_createJob(compileStaticLib, &fribidi, arena, 50 * prb_MEGABYTE));
    arrput(compileJobs, prb_createJob(compileStaticLib, &icu, arena, 50 * prb_MEGABYTE));
    arrput(compileJobs, prb_createJob(compileStaticLib, &freetype, arena, 50 * prb_MEGABYTE));
    arrput(compileJobs, prb_createJob(compileStaticLib, &harfbuzz, arena, 50 * prb_MEGABYTE));
    arrput(compileJobs, prb_createJob(compileStaticLib, &sdl, arena, 50 * prb_MEGABYTE));
    prb_execJobs(compileJobs, arrlen(compileJobs));

    //
    // SECTION Main program
    //

    prb_String mainFlags[] = {
        freetype.includeFlag,
        sdl.includeFlag,
        harfbuzz.includeFlag,
        icu.includeFlag,
        fribidi.includeFlag,
        fribidiNoConfigFlag,
        prb_STR("-Wall -Wextra -Werror"),
    };

    prb_String mainNotPreprocessedName = prb_STR("example.c");
    prb_String mainNotPreprocessedPath = prb_pathJoin(arena, project->rootDir, mainNotPreprocessedName);
    prb_String mainPreprocessedName = prb_replaceExt(arena, mainNotPreprocessedName, prb_STR("i"));
    prb_String mainPreprocessedPath = prb_pathJoin(arena, project->compileOutDir, mainPreprocessedName);
    prb_String mainObjPath = prb_replaceExt(arena, mainPreprocessedPath, prb_STR("obj"));

    prb_String mainFlagsStr = prb_stringsJoin(arena, mainFlags, prb_arrayLength(mainFlags), prb_STR(" "));

    prb_String mainCmdPreprocess = constructCompileCmd(arena, project, mainFlagsStr, mainNotPreprocessedPath, mainPreprocessedPath, prb_STR(""));
    prb_writelnToStdout(mainCmdPreprocess);

    prb_ProcessHandle mainHandlePre = prb_execCmd(arena, mainCmdPreprocess, 0, (prb_String) {});
    prb_assert(mainHandlePre.status == prb_ProcessStatus_CompletedSuccess);

    prb_String        mainCmdObj = constructCompileCmd(arena, project, mainFlagsStr, mainNotPreprocessedPath, mainObjPath, prb_STR(""));
    prb_ProcessHandle mainHandleObj = prb_execCmd(arena, mainCmdObj, 0, (prb_String) {});
    prb_assert(mainHandleObj.status == prb_ProcessStatus_CompletedSuccess);

    prb_String mainObjs[] = {mainObjPath, freetype.libFile, sdl.libFile, harfbuzz.libFile, icu.libFile, fribidi.libFile};
    prb_String mainObjsStr = prb_stringsJoin(arena, mainObjs, prb_arrayLength(mainObjs), prb_STR(" "));

#if prb_PLATFORM_WINDOWS
    prb_String mainOutPath = prb_replaceExt(mainPreprocessedPath, prb_STR("exe"));
    prb_String mainLinkFlags = prb_STR("-subsystem:windows User32.lib");
#elif prb_PLATFORM_LINUX
    prb_String mainOutPath = prb_replaceExt(arena, mainPreprocessedPath, prb_STR("bin"));
    prb_String mainLinkFlags = prb_STR("-lX11 -lm -lstdc++ -ldl -lfontconfig");
#endif

    prb_String        mainCmdExe = constructCompileCmd(arena, project, mainFlagsStr, mainObjsStr, mainOutPath, mainLinkFlags);
    prb_ProcessHandle mainHandleExe = prb_execCmd(arena, mainCmdExe, 0, (prb_String) {});
    prb_assert(mainHandleExe.status == prb_ProcessStatus_CompletedSuccess);

    {
        prb_String msg = prb_fmt(arena, "total: %.2fms", prb_getMsFrom(scriptStartTime));
        prb_writelnToStdout(msg);
    }

    return 0;
}
