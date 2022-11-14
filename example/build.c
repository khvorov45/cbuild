#define prb_IMPLEMENTATION
#include "../programmable_build.h"

typedef struct StaticLib {
    bool       success;
    prb_String libFile;
} StaticLib;

typedef enum DownloadStatus {
    DownloadStatus_Downloaded,
    DownloadStatus_Skipped,
    DownloadStatus_Failed,
} DownloadStatus;

typedef struct DownloadResult {
    DownloadStatus status;
    prb_String     downloadDir;
    prb_String     includeDir;
    prb_String     includeFlag;
} DownloadResult;

static DownloadResult
downloadRepo(prb_String rootDir, prb_String name, prb_String downloadUrl, prb_String includeDirRelToDownload) {
    prb_String     downloadDir = prb_pathJoin(rootDir, name);
    DownloadStatus downloadStatus = DownloadStatus_Failed;
    if (!prb_isDirectory(downloadDir) || prb_directoryIsEmpty(downloadDir)) {
        prb_String cmd = prb_fmtAndPrintln(
            "git clone --depth 1 %.*s %.*s",
            downloadUrl.len,
            downloadUrl.str,
            downloadDir.len,
            downloadDir.str
        );
        prb_ProcessHandle handle = prb_execCmd(cmd, 0, (prb_String) {});
        prb_assert(handle.completed);
        if (handle.completionStatus == prb_Success) {
            downloadStatus = DownloadStatus_Downloaded;
        }
    } else {
        prb_fmtAndPrintln("skip git clone %.*s", name.len, name.str);
        downloadStatus = DownloadStatus_Skipped;
    }
    prb_String     includeDir = prb_pathJoin(downloadDir, includeDirRelToDownload);
    DownloadResult result = {
        .status = downloadStatus,
        .downloadDir = downloadDir,
        .includeDir = includeDir,
        .includeFlag = prb_fmt("-I%.*s", includeDir.len, includeDir.str),
    };
    return result;
}

static StaticLib
compileStaticLib(
    prb_String     name,
    prb_String     compileOutDir,
    prb_String     compileCmdStart,
    DownloadResult download,
    prb_String*    compileSourcesRelToDownload,
    int32_t        compileSourcesRelToDownloadCount,
    prb_String*    extraCompileFlags,
    int32_t        extraCompileFlagsCount
) {
    prb_String objDir = prb_pathJoin(compileOutDir, name);
    prb_createDirIfNotExists(objDir);

    prb_String extraFlagsStr = prb_stringsJoin(extraCompileFlags, extraCompileFlagsCount, prb_STR(" "));
    prb_String cmdStart = prb_fmt(
        "%.*s %.*s %.*s",
        compileCmdStart.len,
        compileCmdStart.str,
        download.includeFlag.len,
        download.includeFlag.str,
        extraFlagsStr.len,
        extraFlagsStr.str
    );

#if prb_PLATFORM_WINDOWS
    prb_String pdbPath = prb_pathJoin(compileOutDir, prb_fmt("%.*s.pdb", name));
    prb_String pdbOutputFlag = prb_fmt("/Fd%.*s", pdbPath);
    cmdStart = prb_fmt("%.*s %.*s", cmdStart, pdbOutputFlag);
#endif

    int32_t     compileSourcesCount = compileSourcesRelToDownloadCount;
    prb_String* compileSources = prb_allocArray(prb_String, compileSourcesCount);
    for (int32_t sourceIndex = 0; sourceIndex < compileSourcesCount; sourceIndex++) {
        compileSources[sourceIndex] = prb_pathJoin(download.downloadDir, compileSourcesRelToDownload[sourceIndex]);
    }

    int32_t      allInputMatchesCount = compileSourcesCount;
    prb_String** allInputMatches = 0;
    arrsetcap(allInputMatches, allInputMatchesCount);
    int32_t allInputFilepathsCount = 0;
    for (int32_t inputPatternIndex = 0; inputPatternIndex < compileSourcesCount; inputPatternIndex++) {
        prb_String           inputPattern = compileSources[inputPatternIndex];
        prb_PathFindIterator iter = prb_createPathFindIter((prb_PathFindSpec) {inputPattern, prb_PathFindMode_Glob, .recursive = false});
        prb_String*          inputMatches = 0;
        while (prb_pathFindIterNext(&iter)) {
            arrput(inputMatches, iter.curPath);
        }
        prb_destroyPathFindIter(&iter);
        prb_assert(arrlen(inputMatches) > 0);
        allInputMatches[inputPatternIndex] = inputMatches;
        allInputFilepathsCount += arrlen(inputMatches);
    }

    // NOTE(khvorov) Recompile everything whenever any .h file changes
    // TODO(khvorov) Probably just search the whole directory recursively for .h files
    prb_String hfilesInIncludePattern = prb_pathJoin(download.includeDir, prb_STR("*.h"));
    uint64_t   latestHFileChange = prb_getLatestLastModifiedFromPattern(hfilesInIncludePattern);
    for (int32_t inputMatchIndex = 0; inputMatchIndex < allInputMatchesCount; inputMatchIndex++) {
        prb_String* inputMatch = allInputMatches[inputMatchIndex];
        for (int32_t inputFilepathIndex = 0; inputFilepathIndex < arrlen(inputMatch); inputFilepathIndex++) {
            prb_String inputFilepath = inputMatch[inputFilepathIndex];
            prb_String inputDir = prb_getParentDir(inputFilepath);
            prb_String adjacentHFilesPattern = prb_pathJoin(inputDir, prb_STR("*.h"));
            latestHFileChange = prb_max(latestHFileChange, prb_getLatestLastModifiedFromPattern(adjacentHFilesPattern));
        }
    }

    prb_String*        allOutputFilepaths = prb_allocArray(prb_String, allInputFilepathsCount);
    prb_ProcessHandle* processes = prb_allocArray(prb_ProcessHandle, allInputFilepathsCount);
    int32_t            processCount = 0;
    int32_t            allOutputFilepathsCount = 0;
    for (int32_t inputMatchIndex = 0; inputMatchIndex < allInputMatchesCount; inputMatchIndex++) {
        prb_String* inputMatch = allInputMatches[inputMatchIndex];
        for (int32_t inputFilepathIndex = 0; inputFilepathIndex < arrlen(inputMatch); inputFilepathIndex++) {
            prb_String inputFilepath = inputMatch[inputFilepathIndex];
            prb_String inputFilename = prb_getLastEntryInPath(inputFilepath);
            prb_String outputFilename = prb_replaceExt(inputFilename, prb_STR("obj"));
            prb_String outputFilepath = prb_pathJoin(objDir, outputFilename);

            allOutputFilepaths[allOutputFilepathsCount++] = outputFilepath;

            uint64_t sourceLastMod = prb_getLatestLastModifiedFromPattern(inputFilepath);
            uint64_t outputLastMod = prb_getEarliestLastModifiedFromPattern(outputFilepath);

            if (sourceLastMod > outputLastMod || latestHFileChange > outputLastMod) {
#if prb_PLATFORM_WINDOWS
                prb_fmt("/Fo%.*s/", objDir);
#elif prb_PLATFORM_LINUX
                prb_String cmd = prb_fmtAndPrintln(
                    "%.*s -c -o %.*s %.*s",
                    cmdStart.len,
                    cmdStart.str,
                    outputFilepath.len,
                    outputFilepath.str,
                    inputFilepath.len,
                    inputFilepath.str
                );
#endif
                processes[processCount++] = prb_execCmd(cmd, prb_ProcessFlag_DontWait, (prb_String) {});
            }
        }
    }

    if (processCount == 0) {
        prb_fmtAndPrintln("skip compile %.*s", name.len, name.str);
    }

    StaticLib  result = {0};
    prb_Status compileStatus = prb_waitForProcesses(processes, processCount);
    if (compileStatus == prb_Success) {
#if prb_PLATFORM_WINDOWS
        prb_String staticLibFileExt = prb_STR("lib");
#elif prb_PLATFORM_LINUX
        prb_String staticLibFileExt = prb_STR("a");
#endif
        prb_String libFile = prb_pathJoin(compileOutDir, prb_fmt("%.*s.%.*s", prb_LIT(name), prb_LIT(staticLibFileExt)));

        prb_String objsPathsString = prb_stringsJoin(allOutputFilepaths, allOutputFilepathsCount, prb_STR(" "));

        uint64_t   sourceLastMod = prb_getLatestLastModifiedFromPatterns(allOutputFilepaths, allOutputFilepathsCount);
        uint64_t   outputLastMod = prb_getEarliestLastModifiedFromPattern(libFile);
        prb_Status libStatus = prb_Success;
        if (sourceLastMod > outputLastMod) {
#if prb_PLATFORM_WINDOWS
            prb_String libCmd = prb_fmtAndPrintln("lib /nologo -out:%.*s %.*s", libFile, objsPattern);
#elif prb_PLATFORM_LINUX
            prb_String libCmd = prb_fmtAndPrintln("ar rcs %.*s %.*s", prb_LIT(libFile), prb_LIT(objsPathsString));
#endif
            prb_removeFileIfExists(libFile);
            prb_ProcessHandle libHandle = prb_execCmd(libCmd, 0, (prb_String) {});
            prb_assert(libHandle.completed);
            libStatus = libHandle.completionStatus;
        } else {
            prb_fmtAndPrintln("skip lib %.*s", prb_LIT(name));
        }

        if (libStatus == prb_Success) {
            result = (StaticLib) {.success = true, .libFile = libFile};
        }
    }

    return result;
}

static void
compileAndRunBidiGenTab(prb_String src, prb_String compileCmdStart, prb_String runArgs, prb_String outpath) {
    if (!prb_isFile(outpath)) {
#if prb_PLATFORM_LINUX
        prb_String exeExt = prb_STR("bin");
#endif
        prb_String exeFilename = prb_replaceExt(src, exeExt);
#if prb_PLATFORM_LINUX
        prb_String compileCommandEnd = prb_fmt("-o %.*s", prb_LIT(exeFilename));
#endif

        prb_String        cmd = prb_fmtAndPrintln("%.*s %.*s %.*s", prb_LIT(compileCmdStart), prb_LIT(compileCommandEnd), prb_LIT(src));
        prb_ProcessHandle handle = prb_execCmd(cmd, 0, (prb_String) {});
        prb_assert(handle.completed);
        if (handle.completionStatus != prb_Success) {
            prb_terminate(1);
        }

        prb_String        cmdRun = prb_fmtAndPrintln("%.*s %.*s", prb_LIT(exeFilename), prb_LIT(runArgs));
        prb_ProcessHandle handleRun = prb_execCmd(cmdRun, prb_ProcessFlag_RedirectStdout, outpath);
        prb_assert(handleRun.completed);
        if (handleRun.completionStatus != prb_Success) {
            prb_terminate(1);
        }
    }
}

prb_PUBLICDEF void
textfileReplace(prb_String path, prb_String pattern, prb_String replacement) {
    prb_Bytes          content = prb_readEntireFile(path);
    prb_StringFindSpec spec = {
        .str = (prb_String) {(const char*)content.data, content.len},
        .pattern = pattern,
        .mode = prb_StringFindMode_Exact,
        .direction = prb_StringDirection_FromStart,
    };
    prb_String newContent = prb_strReplace(spec, replacement);
    prb_writeEntireFile(path, newContent.str, newContent.len);
}

int
main() {
    // TODO(khvorov) Argument parsing
    // TODO(khvorov) Release build
    // TODO(khvorov) Clone a specific commit probably
    prb_TimeStart scriptStartTime = prb_timeStart();
    prb_init(1 * prb_GIGABYTE);

    prb_String rootDir = prb_getParentDir(prb_STR(__FILE__));

    prb_String compileOutDir = prb_pathJoin(rootDir, prb_STR("build-debug"));
    prb_createDirIfNotExists(compileOutDir);

#if prb_PLATFORM_WINDOWS
    prb_String compileCmdStart = prb_STR("cl /nologo /diagnostics:column /FC /Zi");
#elif prb_PLATFORM_LINUX
    prb_String compileCmdStart = prb_STR("gcc -g");
#else
#error unimlemented
#endif

    //
    // SECTION Fribidi
    //

    prb_String     fribidiName = prb_STR("fribidi");
    DownloadResult fribidiDownload = downloadRepo(rootDir, fribidiName, prb_STR("https://github.com/fribidi/fribidi"), prb_STR("lib"));
    if (fribidiDownload.status == DownloadStatus_Failed) {
        return 1;
    }

    prb_String fribidiNoConfigFlag = prb_STR("-DDONT_HAVE_FRIBIDI_CONFIG_H -DDONT_HAVE_FRIBIDI_UNICODE_VERSION_H");

    // NOTE(khvorov) Generate fribidi tables
    {
        prb_String gentabDir = prb_pathJoin(fribidiDownload.downloadDir, prb_STR("gen.tab"));
        prb_String packtabPath = prb_pathJoin(gentabDir, prb_STR("packtab.c"));
        prb_String cmd = prb_fmt(
            "%.*s %.*s %.*s -DHAVE_STDLIB_H=1 -DHAVE_STRING_H -DHAVE_STRINGIZE %.*s",
            prb_LIT(compileCmdStart),
            prb_LIT(fribidiNoConfigFlag),
            prb_LIT(fribidiDownload.includeFlag),
            prb_LIT(packtabPath)
        );
        prb_String datadir = prb_pathJoin(gentabDir, prb_STR("unidata"));
        prb_String unidat = prb_pathJoin(datadir, prb_STR("UnicodeData.txt"));

        // NOTE(khvorov) This max-depth is also known as compression and is set to 2 in makefiles
        int32_t maxDepth = 2;

        prb_String bracketsPath = prb_pathJoin(datadir, prb_STR("BidiBrackets.txt"));
        compileAndRunBidiGenTab(
            prb_pathJoin(gentabDir, prb_STR("gen-brackets-tab.c")),
            cmd,
            prb_fmt("%d %.*s %.*s", maxDepth, prb_LIT(bracketsPath), prb_LIT(unidat)),
            prb_pathJoin(fribidiDownload.includeDir, prb_STR("brackets.tab.i"))
        );

        compileAndRunBidiGenTab(
            prb_pathJoin(gentabDir, prb_STR("gen-arabic-shaping-tab.c")),
            cmd,
            prb_fmt("%d %.*s", maxDepth, prb_LIT(unidat)),
            prb_pathJoin(fribidiDownload.includeDir, prb_STR("arabic-shaping.tab.i"))
        );

        prb_String shapePath = prb_pathJoin(datadir, prb_STR("ArabicShaping.txt"));
        compileAndRunBidiGenTab(
            prb_pathJoin(gentabDir, prb_STR("gen-joining-type-tab.c")),
            cmd,
            prb_fmt("%d %.*s %.*s", maxDepth, prb_LIT(unidat), prb_LIT(shapePath)),
            prb_pathJoin(fribidiDownload.includeDir, prb_STR("joining-type.tab.i"))
        );

        compileAndRunBidiGenTab(
            prb_pathJoin(gentabDir, prb_STR("gen-brackets-type-tab.c")),
            cmd,
            prb_fmt("%d %.*s", maxDepth, prb_LIT(bracketsPath)),
            prb_pathJoin(fribidiDownload.includeDir, prb_STR("brackets-type.tab.i"))
        );

        prb_String mirrorPath = prb_pathJoin(datadir, prb_STR("BidiMirroring.txt"));
        compileAndRunBidiGenTab(
            prb_pathJoin(gentabDir, prb_STR("gen-mirroring-tab.c")),
            cmd,
            prb_fmt("%d %.*s", maxDepth, prb_LIT(mirrorPath)),
            prb_pathJoin(fribidiDownload.includeDir, prb_STR("mirroring.tab.i"))
        );

        compileAndRunBidiGenTab(
            prb_pathJoin(gentabDir, prb_STR("gen-bidi-type-tab.c")),
            cmd,
            prb_fmt("%d %.*s", maxDepth, prb_LIT(unidat)),
            prb_pathJoin(fribidiDownload.includeDir, prb_STR("bidi-type.tab.i"))
        );
    }

    prb_String fribidiCompileSources[] = {prb_STR("lib/*.c")};

    prb_String fribidiCompileFlags[] = {
        fribidiNoConfigFlag,
        // TODO(khvorov) Custom allocators for fribidi
        prb_STR("-DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1"),
        prb_STR("-DHAVE_STRINGIZE=1"),
    };

    // prb_clearDirectory(prb_pathJoin(compileOutDir, fribidiName));
    StaticLib fribidi = compileStaticLib(
        fribidiName,
        compileOutDir,
        compileCmdStart,
        fribidiDownload,
        fribidiCompileSources,
        prb_arrayLength(fribidiCompileSources),
        fribidiCompileFlags,
        prb_arrayLength(fribidiCompileFlags)
    );
    if (!fribidi.success) {
        return 1;
    }

    //
    // SECTION ICU
    //

    // TODO(khvorov) Custom allocation for ICU
    prb_String     icuName = prb_STR("icu");
    DownloadResult icuDownload =
        downloadRepo(rootDir, icuName, prb_STR("https://github.com/unicode-org/icu"), prb_STR("icu4c/source/common"));

    if (icuDownload.status == DownloadStatus_Failed) {
        return 1;
    }

    prb_String icuCompileSources[] = {
        prb_STR("icu4c/source/common/uchar.cpp"),
        prb_STR("icu4c/source/common/utrie.cpp"),
        prb_STR("icu4c/source/common/utrie2.cpp"),
        prb_STR("icu4c/source/common/cmemory.cpp"),
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

    prb_String icuFlags[] = {
        prb_STR("-DU_COMMON_IMPLEMENTATION=1"),
        prb_STR("-DU_COMBINED_IMPLEMENTATION=1"),
        prb_STR("-DU_STATIC_IMPLEMENTATION=1"),
    };

    // prb_clearDirectory(prb_pathJoin(compileOutDir, icuName));
    StaticLib icu = compileStaticLib(
        icuName,
        compileOutDir,
        compileCmdStart,
        icuDownload,
        icuCompileSources,
        prb_arrayLength(icuCompileSources),
        icuFlags,
        prb_arrayLength(icuFlags)
    );

    if (!icu.success) {
        return 1;
    }

    //
    // SECTION Freetype and harfbuzz (they depend on each other)
    //

    prb_String     freetypeName = prb_STR("freetype");
    DownloadResult freetypeDownload =
        downloadRepo(rootDir, freetypeName, prb_STR("https://github.com/freetype/freetype"), prb_STR("include"));
    if (freetypeDownload.status == DownloadStatus_Failed) {
        return 1;
    }

    prb_String     harfbuzzName = prb_STR("harfbuzz");
    DownloadResult harfbuzzDownload =
        downloadRepo(rootDir, harfbuzzName, prb_STR("https://github.com/harfbuzz/harfbuzz"), prb_STR("src"));
    if (harfbuzzDownload.status == DownloadStatus_Failed) {
        return 1;
    }

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

    prb_String freetypeCompileFlags[] = {
        harfbuzzDownload.includeFlag,
        prb_STR("-DFT2_BUILD_LIBRARY"),
        prb_STR("-DFT_CONFIG_OPTION_DISABLE_STREAM_SUPPORT"),
        prb_STR("-DFT_CONFIG_OPTION_USE_HARFBUZZ"),
    };

    // prb_clearDirectory(prb_pathJoin(compileOutDir, freetypeName));
    StaticLib freetype = compileStaticLib(
        freetypeName,
        compileOutDir,
        compileCmdStart,
        freetypeDownload,
        freetypeCompileSources,
        prb_arrayLength(freetypeCompileSources),
        freetypeCompileFlags,
        prb_arrayLength(freetypeCompileFlags)
    );

    if (!freetype.success) {
        return 1;
    };

    prb_String harfbuzzCompileSources[] = {
        prb_STR("src/harfbuzz.cc"),
        prb_STR("src/hb-icu.cc"),
    };

    prb_String harfbuzzCompileFlags[] = {
        icuDownload.includeFlag,
        freetypeDownload.includeFlag,
        prb_STR("-DHAVE_ICU=1"),
        prb_STR("-DHAVE_FREETYPE=1"),
        prb_STR("-DHB_CUSTOM_MALLOC=1"),
    };

    // prb_clearDirectory(prb_pathJoin(compileOutDir, harfbuzzName));
    StaticLib harfbuzz = compileStaticLib(
        harfbuzzName,
        compileOutDir,
        compileCmdStart,
        harfbuzzDownload,
        harfbuzzCompileSources,
        prb_arrayLength(harfbuzzCompileSources),
        harfbuzzCompileFlags,
        prb_arrayLength(harfbuzzCompileFlags)
    );

    if (!harfbuzz.success) {
        return 1;
    };

    //
    // SECTION SDL
    //

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

    prb_String     sdlName = prb_STR("sdl");
    DownloadResult sdlDownload = downloadRepo(rootDir, sdlName, prb_STR("https://github.com/libsdl-org/SDL"), prb_STR("include"));
    if (sdlDownload.status == DownloadStatus_Failed) {
        return 1;
    }

    if (sdlDownload.status == DownloadStatus_Downloaded) {
        prb_String downloadDir = sdlDownload.downloadDir;

        // NOTE(khvorov) Purge dynamic api because otherwise you have to compile a lot more of sdl
        prb_String dynapiPath = prb_pathJoin(downloadDir, prb_STR("src/dynapi/SDL_dynapi.h"));
        textfileReplace(dynapiPath, prb_STR("#define SDL_DYNAMIC_API 1"), prb_STR("#define SDL_DYNAMIC_API 0"));

        // NOTE(khvorov) This XMissingExtension function is in X11 extensions and SDL doesn't use it.
        // Saves us from having to -lXext for no reason
        prb_String x11sym = prb_pathJoin(downloadDir, prb_STR("src/video/x11/SDL_x11sym.h"));
        textfileReplace(
            x11sym,
            prb_STR("SDL_X11_SYM(int,XMissingExtension,(Display* a,_Xconst char* b),(a,b),return)"),
            prb_STR("//SDL_X11_SYM(int,XMissingExtension,(Display* a,_Xconst char* b),(a,b),return")
        );

        // NOTE(khvorov) SDL allocates the pixels in the X11 framebuffer using
        // SDL_malloc but then frees it using XDestroyImage which will call libc
        // free. So even SDL's own custom malloc won't work because libc free will
        // crash when trying to free a pointer allocated with something other than
        // libc malloc.
        prb_String x11FrameBuffer = prb_pathJoin(downloadDir, prb_STR("src/video/x11/SDL_x11framebuffer.c"));
        textfileReplace(
            x11FrameBuffer,
            prb_STR("XDestroyImage(data->ximage);"),
            prb_STR("SDL_free(data->ximage->data);data->ximage->data = 0;XDestroyImage(data->ximage);")
        );
    }

    // prb_clearDirectory(prb_pathJoin(compileOutDir, sdlName));
    StaticLib sdl = compileStaticLib(
        sdlName,
        compileOutDir,
        compileCmdStart,
        sdlDownload,
        sdlCompileSources,
        prb_arrayLength(sdlCompileSources),
        sdlCompileFlags,
        prb_arrayLength(sdlCompileFlags)
    );

    if (!sdl.success) {
        return 1;
    }

    //
    // SECTION Main program
    //

    prb_String mainFlags[] = {
        freetypeDownload.includeFlag,
        sdlDownload.includeFlag,
        harfbuzzDownload.includeFlag,
        icuDownload.includeFlag,
        fribidiDownload.includeFlag,
        fribidiNoConfigFlag,        
        prb_STR("-Wall -Wextra -Wno-unused-function"),
#if prb_PLATFORM_WINDOWS
        prb_STR("-Zi"),
        prb_stringJoin2("-Fo"), prb_pathJoin(compileOutDir, prb_STR("example.obj")))),
        prb_stringJoin2("-Fe"), prb_pathJoin(compileOutDir, prb_STR("example.exe")))),
        prb_stringJoin2("-Fd"), prb_pathJoin(compileOutDir, prb_STR("example.pdb")))),
#elif prb_PLATFORM_LINUX
        prb_fmt("-o %.*s/example.bin", prb_LIT(compileOutDir)),
#endif
    };

    prb_String mainFiles[] = {
        prb_pathJoin(rootDir, prb_STR("example.c")),
        freetype.libFile,
        sdl.libFile,
        harfbuzz.libFile,
        icu.libFile,
        fribidi.libFile,
    };

#if prb_PLATFORM_WINDOWS
    prb_String mainLinkFlags =
        prb_STR(" -link -incremental:no -subsystem:windows ")
            prb_STR("User32.lib ");
#elif prb_PLATFORM_LINUX
    // TODO(khvorov) Get rid of -lm and -ldl
    prb_String mainLinkFlags = prb_STR("-lX11 -lm -lstdc++ -ldl -lfontconfig");
#endif

    prb_String mainFlagsStr = prb_stringsJoin(mainFlags, prb_arrayLength(mainFlags), prb_STR(" "));
    prb_String mainFilesStr = prb_stringsJoin(mainFiles, prb_arrayLength(mainFiles), prb_STR(" "));
    prb_String mainCmd = prb_fmtAndPrintln(
        "%.*s %.*s %.*s %.*s",
        prb_LIT(compileCmdStart),
        prb_LIT(mainFlagsStr),
        prb_LIT(mainFilesStr),
        prb_LIT(mainLinkFlags)
    );

    prb_ProcessHandle mainHandle = prb_execCmd(mainCmd, 0, (prb_String) {});
    prb_assert(mainHandle.completed);

    if (mainHandle.completionStatus == prb_Success) {
        prb_fmtAndPrintln("total: %.2fms", prb_getMsFrom(scriptStartTime));
    } else {
        return 1;
    }

    return 0;
}
