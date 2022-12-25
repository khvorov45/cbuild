#include "../cbuild.h"

#define function

typedef int32_t  i32;
typedef uint64_t u64;

typedef enum Compiler {
    Compiler_Gcc,
    Compiler_Clang,
    Compiler_Msvc,
} Compiler;

typedef struct ObjInfo {
    prb_Str  compileCmd;
    uint64_t preprocessedHash;
} ObjInfo;

typedef struct CompileLogEntry {
    char*   key;
    ObjInfo value;
} CompileLogEntry;

typedef struct ProjectInfo {
    CompileLogEntry* prevCompileLog;
    CompileLogEntry* thisCompileLog;
    prb_Str          rootDir;
    prb_Str          compileOutDir;
    Compiler         compiler;
    bool             release;
} ProjectInfo;

typedef struct StaticLibInfo {
    ProjectInfo*      project;
    prb_Str           name;
    prb_Str           downloadDir;
    prb_Str           includeDir;
    prb_Str           includeFlag;
    prb_Str           objDir;
    prb_Str           libFile;
    prb_Str           compileFlags;
    prb_Str*          sourcesRelToDownload;
    i32               sourcesCount;
    bool              notDownloaded;
    bool              cpp;
    prb_ProcessStatus compileStatus;
} StaticLibInfo;

typedef enum Lang {
    Lang_C,
    Lang_Cpp,
} Lang;

function StaticLibInfo
getStaticLibInfo(
    prb_Arena*   arena,
    ProjectInfo* project,
    prb_Str      name,
    Lang         lang,
    prb_Str      includeDirRelToDownload,
    prb_Str      compileFlags,
    prb_Str*     sourcesRelToDownload,
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
    prb_Str libFilename = prb_fmt(arena, "%.*s.lib", prb_LIT(name));
#elif prb_PLATFORM_LINUX
    prb_Str libFilename = prb_fmt(arena, "%.*s.a", prb_LIT(name));
#else
#error unimplemented
#endif

    result.libFile = prb_pathJoin(arena, project->compileOutDir, libFilename);
    result.notDownloaded = !prb_isDir(arena, result.downloadDir) || prb_dirIsEmpty(arena, result.downloadDir);
    return result;
}

function prb_Process
gitClone(prb_Arena* arena, StaticLibInfo lib, prb_Str downloadUrl) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Process    handle = {};
    if (lib.notDownloaded) {
        prb_Str cmd = prb_fmt(arena, "git clone %.*s %.*s", prb_LIT(downloadUrl), prb_LIT(lib.downloadDir));
        prb_writelnToStdout(arena, cmd);
        handle = prb_createProcess(cmd, (prb_ProcessSpec) {});
        // NOTE(khvorov) Launch here since we are putting cmd in temp memory
        prb_assert(prb_launchProcesses(arena, &handle, 1, prb_Background_Yes));
    } else {
        prb_Str name = prb_getLastEntryInPath(lib.downloadDir);
        prb_Str msg = prb_fmt(arena, "skip git clone %.*s", prb_LIT(name));
        prb_writelnToStdout(arena, msg);
        handle.status = prb_ProcessStatus_CompletedSuccess;
    }
    prb_endTempMemory(temp);
    return handle;
}

function prb_Status
execCmd(prb_Arena* arena, prb_Str cmd) {
    prb_writelnToStdout(arena, cmd);
    prb_Process proc = prb_createProcess(cmd, (prb_ProcessSpec) {});
    prb_Status status = prb_launchProcesses(arena, &proc, 1, prb_Background_No);
    return status;
}

function prb_Status
gitReset(prb_Arena* arena, StaticLibInfo lib, prb_Str commit) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Status     result = prb_Success;
    if (lib.notDownloaded) {
        prb_Str cwd = prb_getWorkingDir(arena);
        prb_assert(prb_setWorkingDir(arena, lib.downloadDir) == prb_Success);
        prb_assert(execCmd(arena, prb_fmt(arena, "git checkout %.*s --", prb_LIT(commit))));
        prb_assert(prb_setWorkingDir(arena, cwd) == prb_Success);
    }
    prb_endTempMemory(temp);
    return result;
}

function bool
fileIsPreprocessed(prb_Str name) {
    bool result = prb_strEndsWith(name, prb_STR(".i")) || prb_strEndsWith(name, prb_STR(".ii"));
    return result;
}

function prb_Str
constructCompileCmd(prb_Arena* arena, ProjectInfo* project, prb_Str flags, prb_Str inputPath, prb_Str outputPath, prb_Str linkFlags) {
    prb_GrowingStr cmd = prb_beginStr(arena);

    switch (project->compiler) {
        case Compiler_Gcc: prb_addStrSegment(&cmd, "gcc"); break;
        case Compiler_Clang: prb_addStrSegment(&cmd, "clang"); break;
        case Compiler_Msvc: prb_addStrSegment(&cmd, "cl /nologo /diagnostics:column /FC"); break;
    }

    if (project->release) {
        switch (project->compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStrSegment(&cmd, " -Ofast"); break;
            case Compiler_Msvc: prb_addStrSegment(&cmd, " /O2"); break;
        }
    } else {
        switch (project->compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStrSegment(&cmd, " -g"); break;
            case Compiler_Msvc: prb_addStrSegment(&cmd, " /Zi"); break;
        }
    }

    bool inIsPreprocessed = fileIsPreprocessed(inputPath);
    bool outIsPreprocess = fileIsPreprocessed(outputPath);
    if (outIsPreprocess) {
        prb_assert(!inIsPreprocessed);
        switch (project->compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStrSegment(&cmd, " -E"); break;
            case Compiler_Msvc: prb_addStrSegment(&cmd, " /P /Fi%.*s", prb_LIT(outputPath)); break;
        }
    }
    if (inIsPreprocessed) {
        prb_assert(!outIsPreprocess);
        switch (project->compiler) {
            case Compiler_Gcc: prb_addStrSegment(&cmd, " -fpreprocessed"); break;
            case Compiler_Clang: break;
            case Compiler_Msvc: prb_addStrSegment(&cmd, " /Yc"); break;
        }
    }

    prb_addStrSegment(&cmd, " %.*s", prb_LIT(flags));
    bool isObj = prb_strEndsWith(outputPath, prb_STR("obj"));
    if (isObj) {
        prb_addStrSegment(&cmd, " -c");
    }

#if prb_PLATFORM_WINDOWS
    if (compiler == Compiler_Msvc) {
        prb_Str pdbPath = prb_replaceExt(outputPath, prb_STR("pdb"));
        prb_addStrSegment(&cmd, " /Fd%.s", pdbPath);
    }
#endif

    switch (project->compiler) {
        case Compiler_Gcc:
        case Compiler_Clang: prb_addStrSegment(&cmd, " %.*s -o %.*s", prb_LIT(inputPath), prb_LIT(outputPath)); break;
        case Compiler_Msvc: {
            prb_Str objPath = isObj ? outputPath : prb_replaceExt(arena, outputPath, prb_STR("obj"));
            prb_addStrSegment(&cmd, " /Fo%.*s", prb_LIT(objPath));
            if (!isObj) {
                prb_addStrSegment(&cmd, " /Fe%.*s", prb_LIT(outputPath));
            }
        } break;
    }

    if (linkFlags.ptr && linkFlags.len > 0) {
        switch (project->compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStrSegment(&cmd, " %.*s", prb_LIT(linkFlags)); break;
            case Compiler_Msvc: prb_addStrSegment(&cmd, "-link -incremental:no %.*s", prb_LIT(linkFlags)); break;
        }
        prb_addStrSegment(&cmd, " %.*s", prb_LIT(linkFlags));
    }

    prb_Str cmdStr = prb_endStr(&cmd);
    return cmdStr;
}

typedef struct StringFound {
    char* key;
    bool  value;
} StringFound;

function prb_Str
strMallocCopy(prb_Str str) {
    prb_Str copy = {};
    copy.len = str.len;
    copy.ptr = (const char*)prb_malloc(str.len + 1);
    prb_memcpy((char*)copy.ptr, str.ptr, str.len);
    ((char*)copy.ptr)[copy.len] = '\0';
    return copy;
}

function void
compileStaticLib(prb_Arena* arena, void* staticLibInfo) {
    prb_TimeStart  compileStart = prb_timeStart();
    StaticLibInfo* lib = (StaticLibInfo*)staticLibInfo;
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_assert(lib->compileStatus == prb_ProcessStatus_NotLaunched);
    lib->compileStatus = prb_ProcessStatus_Launched;

    prb_assert(prb_createDirIfNotExists(arena, lib->objDir) == prb_Success);

    prb_Str* inputPaths = 0;
    for (i32 srcIndex = 0; srcIndex < lib->sourcesCount; srcIndex++) {
        prb_Str srcRelToDownload = lib->sourcesRelToDownload[srcIndex];
        if (prb_strEndsWith(srcRelToDownload, prb_STR("/*.c"))) {
            prb_Str relevantDir = prb_pathJoin(arena, lib->downloadDir, srcRelToDownload);
            relevantDir.len -= 4;
            prb_Str* entries = prb_getAllDirEntries(arena, relevantDir, prb_Recursive_No);
            bool     atleastone = false;
            for (i32 entryIndex = 0; entryIndex < arrlen(entries); entryIndex++) {
                prb_Str entry = entries[entryIndex];
                if (prb_strEndsWith(entry, prb_STR(".c"))) {
                    arrput(inputPaths, entry);
                    atleastone = true;
                }
            }
            prb_assert(atleastone);
        } else {
            prb_Str path = prb_pathJoin(arena, lib->downloadDir, srcRelToDownload);
            prb_assert(prb_isFile(arena, path));
            arrput(inputPaths, path);
        }
    }
    prb_assert(arrlen(inputPaths) > 0);

    StringFound* existingObjs = 0;
    {
        prb_Str* entries = prb_getAllDirEntries(arena, lib->objDir, prb_Recursive_No);
        for (i32 entryIndex = 0; entryIndex < arrlen(entries); entryIndex++) {
            prb_Str entry = entries[entryIndex];
            if (prb_strEndsWith(entry, prb_STR(".obj"))) {
                shput(existingObjs, entry.ptr, false);
            } else {
                prb_removeFileIfExists(arena, entry);
            }
        }
    }

    // NOTE(khvorov) Preprocess
    prb_Str      preprocessExt = lib->cpp ? prb_STR("ii") : prb_STR("i");
    prb_Str*     outputPreprocess = 0;
    prb_Process* processesPreprocess = 0;
    for (i32 inputPathIndex = 0; inputPathIndex < arrlen(inputPaths); inputPathIndex++) {
        prb_Str inputFilepath = inputPaths[inputPathIndex];
        prb_Str inputFilename = prb_getLastEntryInPath(inputFilepath);

        prb_Str outputPreprocessFilename = prb_replaceExt(arena, inputFilename, preprocessExt);
        prb_Str outputPreprocessFilepath = prb_pathJoin(arena, lib->objDir, outputPreprocessFilename);
        arrput(outputPreprocess, outputPreprocessFilepath);

        prb_Str     cmd = constructCompileCmd(arena, lib->project, lib->compileFlags, inputFilepath, outputPreprocessFilepath, prb_STR(""));
        prb_Process proc = prb_createProcess(cmd, (prb_ProcessSpec) {});
        arrput(processesPreprocess, proc);
    }

    prb_assert(prb_launchProcesses(arena, processesPreprocess, arrlen(processesPreprocess), prb_Background_Yes));
    prb_Status preprocessStatus = prb_waitForProcesses(processesPreprocess, arrlen(processesPreprocess));

    // NOTE(khvorov) Compile
    if (preprocessStatus == prb_Success) {
        prb_Str*     outputObjs = 0;
        prb_Process* processesCompile = 0;
        for (i32 inputPathIndex = 0; inputPathIndex < arrlen(inputPaths); inputPathIndex++) {
            prb_Str inputNotPreprocessedFilepath = inputPaths[inputPathIndex];
            prb_Str inputNotPreprocessedFilename = prb_getLastEntryInPath(inputNotPreprocessedFilepath);

            prb_Str outputObjFilename = prb_replaceExt(arena, inputNotPreprocessedFilename, prb_STR("obj"));
            prb_Str outputObjFilepath = prb_pathJoin(arena, lib->objDir, outputObjFilename);
            arrput(outputObjs, outputObjFilepath);
            if (shgeti(existingObjs, (char*)outputObjFilepath.ptr) != -1) {
                shput(existingObjs, (char*)outputObjFilepath.ptr, true);
            }

            // NOTE(khvorov) Using not preprocessed input.
            // I found that giving the compiler preprocessed file generates less useful warnings.
            prb_Str compileCmd = constructCompileCmd(arena, lib->project, lib->compileFlags, inputNotPreprocessedFilepath, outputObjFilepath, prb_STR(""));

            // NOTE(khvorov) Figure out if we should recompile this file
            bool         shouldRecompile = true;
            prb_FileHash preprocessedHash = prb_getFileHash(arena, outputPreprocess[inputPathIndex]);
            prb_assert(preprocessedHash.valid);
            if (lib->project->prevCompileLog != 0 && prb_isFile(arena, outputObjFilepath)) {
                int32_t logEntryIndex = shgeti(lib->project->prevCompileLog, outputObjFilepath.ptr);
                if (logEntryIndex != -1) {
                    ObjInfo info = lib->project->prevCompileLog[logEntryIndex].value;
                    if (preprocessedHash.hash == info.preprocessedHash) {
                        if (prb_streq(compileCmd, info.compileCmd)) {
                            shouldRecompile = false;
                        }
                    }
                }
            }

            if (shouldRecompile) {
                prb_writelnToStdout(arena, compileCmd);
                prb_Process process = prb_createProcess(compileCmd, (prb_ProcessSpec) {});
                arrput(processesCompile, process);
            }

            // NOTE(khvorov) Update compile log
            {
                prb_Str outputObjFilepathCopy = strMallocCopy(outputObjFilepath);
                prb_Str compileCmdCopy = strMallocCopy(compileCmd);
                ObjInfo thisObjInfo = {compileCmdCopy, preprocessedHash.hash};
                shput(lib->project->thisCompileLog, outputObjFilepathCopy.ptr, thisObjInfo);
            }
        }

        // NOTE(khvorov) Remove all objs that don't correspond to any inputs
        for (i32 existingObjIndex = 0; existingObjIndex < shlen(existingObjs); existingObjIndex++) {
            StringFound existingObj = existingObjs[existingObjIndex];
            if (!existingObj.value) {
                prb_assert(prb_removeFileIfExists(arena, prb_STR(existingObj.key)) == prb_Success);
            }
        }

        if (arrlen(processesCompile) == 0) {
            prb_writelnToStdout(arena, prb_fmt(arena, "skip compile %.*s", prb_LIT(lib->name)));
        }

        prb_assert(prb_launchProcesses(arena, processesCompile, arrlen(processesCompile), prb_Background_Yes));
        prb_Status compileStatus = prb_waitForProcesses(processesCompile, arrlen(processesCompile));
        arrfree(processesCompile);

        if (compileStatus == prb_Success) {
            prb_Str objsPathsString = prb_stringsJoin(arena, outputObjs, arrlen(outputObjs), prb_STR(" "));

            u64 sourceLastMod = 0;
            {
                prb_Multitime multitime = prb_createMultitime();
                for (i32 pathIndex = 0; pathIndex < arrlen(outputObjs); pathIndex++) {
                    prb_Str           path = outputObjs[pathIndex];
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
                prb_Str libCmd = prb_fmt("lib /nologo -out:%.*s %.*s", libFile, objsPattern);
#elif prb_PLATFORM_LINUX
                prb_Str libCmd = prb_fmt(arena, "ar rcs %.*s %.*s", prb_LIT(lib->libFile), prb_LIT(objsPathsString));
#endif
                prb_assert(prb_removeFileIfExists(arena, lib->libFile) == prb_Success);
                libStatus = execCmd(arena, libCmd);
            } else {
                prb_Str msg = prb_fmt(arena, "skip lib %.*s", prb_LIT(lib->name));
                prb_writelnToStdout(arena, msg);
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
    shfree(existingObjs);
    arrfree(outputPreprocess);

    prb_writelnToStdout(arena, prb_fmt(arena, "%.*s compile step: %.2fms", prb_LIT(lib->name), prb_getMsFrom(compileStart)));
    prb_endTempMemory(temp);
}

function void
compileAndRunBidiGenTab(prb_Arena* arena, ProjectInfo* project, prb_Str src, prb_Str flags, prb_Str runArgs, prb_Str outpath) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    if (!prb_isFile(arena, outpath)) {
#if prb_PLATFORM_WINDOWS
        prb_Str exeFilename = prb_replaceExt(arena, stc, prb_STR("exe"));
#elif prb_PLATFORM_LINUX
        prb_Str exeFilename = prb_replaceExt(arena, src, prb_STR("bin"));
#else
#error unimplemented
#endif
        prb_Str packtabPath = prb_pathJoin(arena, prb_getParentDir(arena, src), prb_STR("packtab.c"));
        prb_Str cmd = constructCompileCmd(arena, project, flags, prb_fmt(arena, "%.*s %.*s", prb_LIT(packtabPath), prb_LIT(src)), exeFilename, prb_STR(""));
        prb_assert(execCmd(arena, cmd));

        prb_Str cmdRun = prb_fmt(arena, "%.*s %.*s", prb_LIT(exeFilename), prb_LIT(runArgs));
        prb_writelnToStdout(arena, cmdRun);
        prb_ProcessSpec specRun = {};
        specRun.redirectStdout = true;
        specRun.stdoutFilepath = outpath;
        prb_Process handleRun = prb_createProcess(cmdRun, specRun);
        prb_assert(prb_launchProcesses(arena, &handleRun, 1, prb_Background_No));
    }
    prb_endTempMemory(temp);
}

function void
textfileReplace(prb_Arena* arena, prb_Str path, prb_Str pattern, prb_Str replacement) {
    prb_ReadEntireFileResult content = prb_readEntireFile(arena, path);
    prb_assert(content.success);
    prb_StrFindSpec spec = {
        .pattern = pattern,
        .mode = prb_StrFindMode_Exact,
        .direction = prb_StrDirection_FromStart,
    };
    prb_Str newContent = prb_strReplace(arena, prb_strFromBytes(content.content), spec, replacement);
    prb_assert(prb_writeEntireFile(arena, path, newContent.ptr, newContent.len) == prb_Success);
}

typedef struct GetStrInQuotesResult {
    bool    success;
    prb_Str inquotes;
    prb_Str past;
} GetStrInQuotesResult;

function GetStrInQuotesResult
getStrInQuotes(prb_Str str) {
    GetStrInQuotesResult result = {};
    prb_StrFindSpec      quoteFindSpec = {.pattern = prb_STR("\"")};
    prb_StrScanner       scanner = prb_createStrScanner(str);
    if (prb_strScannerMove(&scanner, quoteFindSpec, prb_StrScannerSide_AfterMatch)) {
        if (prb_strScannerMove(&scanner, quoteFindSpec, prb_StrScannerSide_AfterMatch)) {
            result.success = true;
            result.inquotes = scanner.betweenLastMatches;
            result.past = scanner.afterMatch;
        }
    }
    return result;
}

typedef struct String3 {
    bool    success;
    prb_Str strings[3];
} String3;

function String3
get3StrInQuotes(prb_Str str) {
    String3 result = {.success = true};
    for (i32 index = 0; index < prb_arrayCount(result.strings) && result.success; index++) {
        GetStrInQuotesResult get1 = getStrInQuotes(str);
        if (get1.success) {
            result.strings[index] = get1.inquotes;
            str = get1.past;
        } else {
            result.success = false;
        }
    }
    return result;
}

typedef enum LogColumn {
    LogColumn_ObjPath,
    LogColumn_CompileCmd,
    LogColumn_PreprocessedHash,
    LogColumn_Count,
} LogColumn;

typedef struct ParseLogResult {
    CompileLogEntry* log;
    bool             success;
} ParseLogResult;

function ParseLogResult
parseLog(prb_Arena* arena, prb_Str str, prb_Str* columnNames) {
    prb_unused(str);
    ParseLogResult  result = {};
    prb_StrScanner  lineIter = prb_createStrScanner(str);
    prb_StrFindSpec lineBreakSpec = {.mode = prb_StrFindMode_LineBreak};
    if (prb_strScannerMove(&lineIter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success) {
        String3 headers = get3StrInQuotes(lineIter.betweenLastMatches);
        if (headers.success) {
            bool expectedHeaders = prb_streq(headers.strings[LogColumn_ObjPath], columnNames[LogColumn_ObjPath])
                && prb_streq(headers.strings[LogColumn_CompileCmd], columnNames[LogColumn_CompileCmd])
                && prb_streq(headers.strings[LogColumn_PreprocessedHash], columnNames[LogColumn_PreprocessedHash]);
            if (expectedHeaders) {
                result.success = true;
                while (prb_strScannerMove(&lineIter, lineBreakSpec, prb_StrScannerSide_AfterMatch) && result.success) {
                    String3 row = get3StrInQuotes(lineIter.betweenLastMatches);
                    if (row.success) {
                        prb_ParsedNumber hashResult = prb_parseNumber(row.strings[LogColumn_PreprocessedHash]);
                        if (hashResult.kind == prb_ParsedNumberKind_U64) {
                            ObjInfo info = {row.strings[LogColumn_CompileCmd], hashResult.parsedU64};
                            shput(result.log, prb_strGetNullTerminated(arena, row.strings[LogColumn_ObjPath]), info);
                        }
                    } else {
                        result.success = false;
                    }
                }
            }
        }
    }
    return result;
}

function void
addLogRow(prb_GrowingStr* gstr, prb_Str* strings) {
    for (i32 colIndex = 0; colIndex < LogColumn_Count; colIndex++) {
        prb_addStrSegment(gstr, "\"%.*s\"", prb_LIT(strings[colIndex]));
        if (colIndex == LogColumn_Count - 1) {
            prb_addStrSegment(gstr, "\n");
        } else {
            prb_addStrSegment(gstr, ",");
        }
    }
}

function void
writeLog(prb_Arena* arena, CompileLogEntry* log, prb_Str path, prb_Str* columnNames) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Arena      numberFmtArena = prb_createArenaFromArena(arena, 100);
    prb_GrowingStr gstr = prb_beginStr(arena);
    addLogRow(&gstr, columnNames);
    for (i32 entryIndex = 0; entryIndex < shlen(log); entryIndex++) {
        CompileLogEntry entry = log[entryIndex];
        prb_TempMemory  tempNumber = prb_beginTempMemory(&numberFmtArena);

        prb_Str strings[] = {
            [LogColumn_ObjPath] = prb_STR(entry.key),
            [LogColumn_CompileCmd] = entry.value.compileCmd,
            [LogColumn_PreprocessedHash] = prb_fmt(&numberFmtArena, "0x%lX", entry.value.preprocessedHash),
        };
        addLogRow(&gstr, strings);
        prb_endTempMemory(tempNumber);
    }
    prb_Str str = prb_endStr(&gstr);
    prb_assert(prb_writeEntireFile(arena, path, str.ptr, str.len) == prb_Success);
    prb_endTempMemory(temp);
}

int
main() {
    prb_TimeStart scriptStartTime = prb_timeStart();
    prb_Arena     arena_ = prb_createArenaFromVmem(1 * prb_GIGABYTE);
    prb_Arena*    arena = &arena_;
    ProjectInfo   project_ = {};
    ProjectInfo*  project = &project_;

    prb_Str* cmdArgs = prb_getCmdArgs(arena);
    prb_assert(arrlen(cmdArgs) == 3);
    prb_Str compilerStr = cmdArgs[1];
    prb_Str buildTypeStr = cmdArgs[2];
    prb_assert(prb_streq(buildTypeStr, prb_STR("debug")) || prb_streq(buildTypeStr, prb_STR("release")));

    project->rootDir = prb_getParentDir(arena, prb_STR(__FILE__));
    project->release = prb_streq(buildTypeStr, prb_STR("release"));
    project->compileOutDir = prb_pathJoin(arena, project->rootDir, prb_fmt(arena, "build-%.*s-%.*s", prb_LIT(compilerStr), prb_LIT(buildTypeStr)));
    prb_assert(prb_createDirIfNotExists(arena, project->compileOutDir) == prb_Success);

    // NOTE(khvorov) Log file from previous compilation

    prb_Str logColumnNames[] = {
        [LogColumn_ObjPath] = prb_STR("objPath"),
        [LogColumn_CompileCmd] = prb_STR("compileCmd"),
        [LogColumn_PreprocessedHash] = prb_STR("preprocessedHash"),
    };
    prb_Str buildLogPath = prb_pathJoin(arena, project->compileOutDir, prb_STR("log.csv"));
    {
        prb_ReadEntireFileResult prevLogRead = prb_readEntireFile(arena, buildLogPath);
        if (prevLogRead.success) {
            prb_Str        prevLog = (prb_Str) {(const char*)prevLogRead.content.data, prevLogRead.content.len};
            ParseLogResult prevLogParsed = parseLog(arena, prevLog, logColumnNames);
            if (prevLogParsed.success) {
                project->prevCompileLog = prevLogParsed.log;
            }
        }
    }

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

    prb_Str fribidiCompileSouces[] = {prb_STR("lib/*.c")};
    prb_Str fribidiNoConfigFlag = prb_STR("-DDONT_HAVE_FRIBIDI_CONFIG_H -DDONT_HAVE_FRIBIDI_UNICODE_VERSION_H");

    StaticLibInfo fribidi = getStaticLibInfo(
        arena,
        project,
        prb_STR("fribidi"),
        Lang_C,
        prb_STR("lib"),
        prb_fmt(arena, "%.*s -Dfribidi_malloc=fribidiCustomMalloc -Dfribidi_free=fribidiCustomFree -DHAVE_STRING_H=1 -DHAVE_STRINGIZE=1", prb_LIT(fribidiNoConfigFlag)),
        fribidiCompileSouces,
        prb_arrayCount(fribidiCompileSouces)
    );

    // NOTE(khvorov) ICU

    prb_Str icuCompileSources[] = {
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
        prb_arrayCount(icuCompileSources)
    );

    // NOTE(khvorov) Freetype

    prb_Str freetypeCompileSources[] = {
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
        prb_arrayCount(freetypeCompileSources)
    );

    // NOTE(khvorov) Harfbuzz

    prb_Str harfbuzzCompileSources[] = {
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
        prb_arrayCount(harfbuzzCompileSources)
    );

    // NOTE(khvorov) Freetype and harfbuzz depend on each other
    freetype.compileFlags = prb_fmt(arena, "%.*s %.*s", prb_LIT(freetype.compileFlags), prb_LIT(harfbuzz.includeFlag));

    // NOTE(khvorov) SDL

    prb_Str sdlCompileSources[] = {
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

    prb_Str sdlCompileFlags[] = {
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
        prb_stringsJoin(arena, sdlCompileFlags, prb_arrayCount(sdlCompileFlags), prb_STR(" ")),
        sdlCompileSources,
        prb_arrayCount(sdlCompileSources)
    );

    //
    // SECTION Download
    //

    prb_Process* downloadHandles = 0;
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
        prb_Str gentabDir = prb_pathJoin(arena, fribidi.downloadDir, prb_STR("gen.tab"));
        prb_Str flags = prb_fmt(arena, "%.*s %.*s -DHAVE_STDLIB_H=1 -DHAVE_STRING_H -DHAVE_STRINGIZE", prb_LIT(fribidiNoConfigFlag), prb_LIT(fribidi.includeFlag));
        prb_Str datadir = prb_pathJoin(arena, gentabDir, prb_STR("unidata"));
        prb_Str unidat = prb_pathJoin(arena, datadir, prb_STR("UnicodeData.txt"));

        // NOTE(khvorov) This max-depth is also known as compression and is set to 2 in makefiles
        i32 maxDepth = 2;

        prb_Str bracketsPath = prb_pathJoin(arena, datadir, prb_STR("BidiBrackets.txt"));
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

        prb_Str shapePath = prb_pathJoin(arena, datadir, prb_STR("ArabicShaping.txt"));
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

        prb_Str mirrorPath = prb_pathJoin(arena, datadir, prb_STR("BidiMirroring.txt"));
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
        prb_Str file = prb_pathJoin(arena, fribidi.downloadDir, prb_STR("lib/common.h"));
        textfileReplace(
            arena,
            file,
            prb_STR("#ifndef fribidi_malloc"),
            prb_STR("#include <stddef.h>\nvoid* fribidiCustomMalloc(size_t);\nvoid fribidiCustomFree(void*);\n#ifndef fribidi_malloc")
        );
    }

    // NOTE(khvorov) Fix SDL
    if (sdl.notDownloaded) {
        prb_Str downloadDir = sdl.downloadDir;

        // NOTE(khvorov) Purge dynamic api because otherwise you have to compile a lot more of sdl
        prb_Str dynapiPath = prb_pathJoin(arena, downloadDir, prb_STR("src/dynapi/SDL_dynapi.h"));
        textfileReplace(
            arena,
            dynapiPath,
            prb_STR("#define SDL_DYNAMIC_API 1"),
            prb_STR("#define SDL_DYNAMIC_API 0")
        );

        // NOTE(khvorov) This XMissingExtension function is in X11 extensions and SDL doesn't use it.
        // Saves us from having to -lXext for no reason
        prb_Str x11sym = prb_pathJoin(arena, downloadDir, prb_STR("src/video/x11/SDL_x11sym.h"));
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
        prb_Str x11FrameBuffer = prb_pathJoin(arena, downloadDir, prb_STR("src/video/x11/SDL_x11framebuffer.c"));
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

    prb_TimeStart compileStart = prb_timeStart();

    // NOTE(khvorov) Force clean
    // prb_assert(prb_clearDir(arena, fribidi.objDir) == prb_Success);
    // prb_assert(prb_clearDir(arena, icu.objDir) == prb_Success);
    // prb_assert(prb_clearDir(arena, freetype.objDir) == prb_Success);
    // prb_assert(prb_clearDir(arena, harfbuzz.objDir) == prb_Success);
    // prb_assert(prb_clearDir(arena, sdl.objDir) == prb_Success);

    prb_Job* compileJobs = 0;
    arrput(compileJobs, prb_createJob(compileStaticLib, &fribidi, arena, 50 * prb_MEGABYTE));
    arrput(compileJobs, prb_createJob(compileStaticLib, &icu, arena, 50 * prb_MEGABYTE));
    arrput(compileJobs, prb_createJob(compileStaticLib, &freetype, arena, 50 * prb_MEGABYTE));
    arrput(compileJobs, prb_createJob(compileStaticLib, &harfbuzz, arena, 50 * prb_MEGABYTE));
    arrput(compileJobs, prb_createJob(compileStaticLib, &sdl, arena, 50 * prb_MEGABYTE));
    {
        prb_Background threadMode = prb_Background_Yes;
        // NOTE(khvorov) Buggy debuggers can't always handle threads
        if (prb_debuggerPresent(arena)) {
            threadMode = prb_Background_No;
        }
        prb_assert(prb_launchJobs(compileJobs, arrlen(compileJobs), threadMode));
        prb_assert(prb_waitForJobs(compileJobs, arrlen(compileJobs)));
    }

    prb_assert(fribidi.compileStatus == prb_ProcessStatus_CompletedSuccess);
    prb_assert(icu.compileStatus == prb_ProcessStatus_CompletedSuccess);
    prb_assert(freetype.compileStatus == prb_ProcessStatus_CompletedSuccess);
    prb_assert(harfbuzz.compileStatus == prb_ProcessStatus_CompletedSuccess);
    prb_assert(sdl.compileStatus == prb_ProcessStatus_CompletedSuccess);

    prb_writelnToStdout(arena, prb_fmt(arena, "total deps compile: %.2fms", prb_getMsFrom(compileStart)));

    //
    // SECTION Main program
    //

    prb_Str mainFlags[] = {
        freetype.includeFlag,
        sdl.includeFlag,
        harfbuzz.includeFlag,
        icu.includeFlag,
        fribidi.includeFlag,
        fribidiNoConfigFlag,
        prb_STR("-Wall -Wextra -Werror"),
    };

    prb_Str mainNotPreprocessedName = prb_STR("example.c");
    prb_Str mainNotPreprocessedPath = prb_pathJoin(arena, project->rootDir, mainNotPreprocessedName);
    prb_Str mainPreprocessedName = prb_replaceExt(arena, mainNotPreprocessedName, prb_STR("i"));
    prb_Str mainPreprocessedPath = prb_pathJoin(arena, project->compileOutDir, mainPreprocessedName);
    prb_Str mainObjPath = prb_replaceExt(arena, mainPreprocessedPath, prb_STR("obj"));

    prb_Str mainFlagsStr = prb_stringsJoin(arena, mainFlags, prb_arrayCount(mainFlags), prb_STR(" "));

    prb_Str mainCmdPreprocess = constructCompileCmd(arena, project, mainFlagsStr, mainNotPreprocessedPath, mainPreprocessedPath, prb_STR(""));
    prb_writelnToStdout(arena, mainCmdPreprocess);
    prb_Process mainHandlePre = prb_createProcess(mainCmdPreprocess, (prb_ProcessSpec) {});
    prb_assert(prb_launchProcesses(arena, &mainHandlePre, 1, prb_Background_Yes));

    prb_Str mainCmdObj = constructCompileCmd(arena, project, mainFlagsStr, mainNotPreprocessedPath, mainObjPath, prb_STR(""));
    prb_assert(execCmd(arena, mainCmdObj));

    prb_Str mainObjs[] = {mainObjPath, freetype.libFile, sdl.libFile, harfbuzz.libFile, icu.libFile, fribidi.libFile};
    prb_Str mainObjsStr = prb_stringsJoin(arena, mainObjs, prb_arrayCount(mainObjs), prb_STR(" "));

#if prb_PLATFORM_WINDOWS
    prb_Str mainOutPath = prb_replaceExt(mainPreprocessedPath, prb_STR("exe"));
    prb_Str mainLinkFlags = prb_STR("-subsystem:windows User32.lib");
#elif prb_PLATFORM_LINUX
    prb_Str mainOutPath = prb_replaceExt(arena, mainPreprocessedPath, prb_STR("bin"));
    prb_Str mainLinkFlags = prb_STR("-lX11 -lm -lstdc++ -ldl -lfontconfig");
#endif

    prb_Str mainCmdExe = constructCompileCmd(arena, project, mainFlagsStr, mainObjsStr, mainOutPath, mainLinkFlags);
    prb_assert(execCmd(arena, mainCmdExe));

    prb_assert(prb_waitForProcesses(&mainHandlePre, 1) == prb_Success);

    writeLog(arena, project->thisCompileLog, buildLogPath, logColumnNames);
    prb_writelnToStdout(arena, prb_fmt(arena, "total: %.2fms", prb_getMsFrom(scriptStartTime)));
    return 0;
}
