#include "../programmable_build.h"

typedef struct ProjectInfo {
    prb_String rootDir;
    prb_String compileOutDir;
} ProjectInfo;

typedef struct StaticLibInfo {
    prb_String  name;
    prb_String  downloadDir;
    prb_String  includeDir;
    prb_String  includeFlag;
    prb_String  libFile;
    prb_String  compileFlags;
    prb_String* sourcesRelToDownload;
    int32_t     sourcesCount;
} StaticLibInfo;

static StaticLibInfo
getStaticLibInfo(
    ProjectInfo project,
    prb_String  name,
    prb_String  includeDirRelToDownload,
    prb_String  compileFlags,
    prb_String* sourcesRelToDownload,
    int32_t     sourcesCount
) {
    StaticLibInfo result = {
        .name = name,
        .downloadDir = prb_pathJoin(project.rootDir, name),
        .sourcesRelToDownload = sourcesRelToDownload,
        .sourcesCount = sourcesCount,
    };
    result.includeDir = prb_pathJoin(result.downloadDir, includeDirRelToDownload);
    result.includeFlag = prb_fmt("-I%.*s", prb_LIT(result.includeDir));
    result.compileFlags = prb_fmt("%.*s %.*s", prb_LIT(compileFlags), prb_LIT(result.includeFlag));

#if prb_PLATFORM_WINDOWS
    prb_String libFilename = prb_fmt("%.*s.lib", prb_LIT(name));
#elif prb_PLATFORM_LINUX
    prb_String libFilename = prb_fmt("%.*s.a", prb_LIT(name));
#else
#error unimplemented
#endif

    result.libFile = prb_pathJoin(project.compileOutDir, libFilename);
    return result;
}

static prb_ProcessHandle
gitClone(prb_String downloadDir, prb_String downloadUrl) {
    prb_TempMemory    temp = prb_beginTempMemory();
    prb_ProcessHandle handle = {};
    if (!prb_isDirectory(downloadDir) || prb_directoryIsEmpty(downloadDir)) {
        prb_String cmd = prb_fmtAndPrintln("git clone --depth 1 %.*s %.*s", prb_LIT(downloadUrl), prb_LIT(downloadDir));
        handle = prb_execCmd(cmd, prb_ProcessFlag_DontWait, (prb_String) {});
    } else {
        prb_String name = prb_getLastEntryInPath(downloadDir);
        prb_fmtAndPrintln("skip git clone %.*s", prb_LIT(name));
        handle.valid = true;
        handle.completed = true;
        handle.completionStatus = prb_Success;
    }
    prb_endTempMemory(temp);
    return handle;
}

typedef enum Compiler {
    Compiler_Gcc,
    Compiler_Clang,
    Compiler_Msvc,
} Compiler;

static prb_String
constructCompileCmd(Compiler compiler, prb_String flags, prb_String inputPath, prb_String outputPath, prb_String linkFlags) {
    prb_String cmd = prb_beginString();

    switch (compiler) {
        case Compiler_Gcc: prb_addStringSegment(&cmd, "gcc -g"); break;
        case Compiler_Clang: prb_addStringSegment(&cmd, "clang -g"); break;
        case Compiler_Msvc: prb_addStringSegment(&cmd, "cl /nologo /diagnostics:column /FC /Zi"); break;
    }

    prb_addStringSegment(&cmd, " %.*s", prb_LIT(flags));
    bool isObj = prb_strEndsWith(outputPath, prb_STR("obj"), prb_StringFindMode_Exact);
    if (isObj) {
        prb_addStringSegment(&cmd, " -c");
    }

#if prb_PLATFORM_WINDOWS
    if (compiler == Compiler_Msvc) {
        prb_String pdbPath = prb_replaceExt(outputPath, prb_STR("pdb"));
        prb_addStringSegment(&cmd, " /Fd%.s", pdbPath);
    }
#endif

    switch (compiler) {
        case Compiler_Gcc:
        case Compiler_Clang: prb_addStringSegment(&cmd, " %.*s -o %.*s", prb_LIT(inputPath), prb_LIT(outputPath)); break;
        case Compiler_Msvc: {
            prb_String objPath = isObj ? outputPath : prb_replaceExt(outputPath, prb_STR("obj"));
            prb_addStringSegment(&cmd, " /Fo%.*s", prb_LIT(objPath));
            if (!isObj) {
                prb_addStringSegment(&cmd, " /Fe%.*s", prb_LIT(outputPath));
            }
        } break;
    }

    if (linkFlags.ptr && linkFlags.len > 0) {
        switch (compiler) {
            case Compiler_Gcc:
            case Compiler_Clang: prb_addStringSegment(&cmd, " %.*s", prb_LIT(linkFlags)); break;
            case Compiler_Msvc: prb_addStringSegment(&cmd, "-link -incremental:no %.*s", prb_LIT(linkFlags)); break;
        }
        prb_addStringSegment(&cmd, " %.*s", prb_LIT(linkFlags));
    }

    prb_endString();
    prb_writeToStdout(cmd);
    prb_writeToStdout(prb_STR("\n"));
    return cmd;
}

typedef struct StringFound {
    char* key;
    bool  value;
} StringFound;

static prb_Status
compileStaticLib(ProjectInfo project, Compiler compiler, StaticLibInfo lib) {
    prb_TempMemory temp = prb_beginTempMemory();
    prb_Status     result = prb_Success;

    prb_String objDir = prb_pathJoin(project.compileOutDir, lib.name);
    prb_createDirIfNotExists(objDir);

    prb_String* inputPaths = 0;
    for (int32_t srcIndex = 0; srcIndex < lib.sourcesCount; srcIndex++) {
        prb_String           srcRelToDownload = lib.sourcesRelToDownload[srcIndex];
        prb_PathFindIterator iter = prb_createPathFindIter((prb_PathFindSpec) {lib.downloadDir, prb_PathFindMode_Glob, .glob.pattern = srcRelToDownload});
        while (prb_pathFindIterNext(&iter)) {
            arrput(inputPaths, iter.curPath);
        }
        prb_destroyPathFindIter(&iter);
    }
    prb_assert(arrlen(inputPaths) > 0);

    // NOTE(khvorov) Recompile everything whenever any .h file changes
    uint64_t latestHFileChange = 0;
    {
        prb_LastModResult lm = prb_getLastModifiedFromFindSpec(
            (prb_PathFindSpec) {lib.downloadDir, prb_PathFindMode_Glob, .recursive = true, .glob.pattern = prb_STR("*.h")},
            prb_LastModKind_Latest
        );
        prb_assert(lm.success);
        latestHFileChange = lm.timestamp;
    }

    StringFound* existingObjs = 0;
    {
        prb_PathFindIterator iter = prb_createPathFindIter((prb_PathFindSpec) {.dir = objDir, .mode = prb_PathFindMode_AllEntriesInDir});
        while (prb_pathFindIterNext(&iter)) {
            shput(existingObjs, iter.curPath.ptr, false);
        }
        prb_destroyPathFindIter(&iter);
    }

    prb_String*        outputFilepaths = 0;
    prb_ProcessHandle* processes = 0;
    for (int32_t inputPathIndex = 0; inputPathIndex < arrlen(inputPaths); inputPathIndex++) {
        prb_String inputFilepath = inputPaths[inputPathIndex];
        prb_String inputFilename = prb_getLastEntryInPath(inputFilepath);
        prb_String outputFilename = prb_replaceExt(inputFilename, prb_STR("obj"));
        prb_String outputFilepath = prb_pathJoin(objDir, outputFilename);
        arrput(outputFilepaths, outputFilepath);
        if (shgeti(existingObjs, (char*)outputFilepath.ptr) != -1) {
            shput(existingObjs, (char*)outputFilepath.ptr, true);
        }

        prb_LastModResult sourceLastMod = prb_getLastModifiedFromPath(inputFilepath);
        prb_assert(sourceLastMod.success);

        prb_LastModResult outputLastMod = prb_getLastModifiedFromPath(outputFilepath);

        if (!outputLastMod.success || sourceLastMod.timestamp > outputLastMod.timestamp || latestHFileChange > outputLastMod.timestamp) {
            prb_String        cmd = constructCompileCmd(compiler, lib.compileFlags, inputFilepath, outputFilepath, prb_STR(""));
            prb_ProcessHandle process = prb_execCmd(cmd, prb_ProcessFlag_DontWait, (prb_String) {});
            arrput(processes, process);
        }
    }

    // NOTE(khvorov) Remove all objs that don't correspond to any inputs
    for (int32_t existingObjIndex = 0; existingObjIndex < shlen(existingObjs); existingObjIndex++) {
        StringFound existingObj = existingObjs[existingObjIndex];
        if (!existingObj.value) {
            prb_removeFileIfExists(prb_STR(existingObj.key));
        }
    }

    if (arrlen(processes) == 0) {
        prb_fmtAndPrintln("skip compile %.*s", prb_LIT(lib.name));
    }

    prb_Status compileStatus = prb_waitForProcesses(processes, arrlen(processes));
    result = compileStatus;
    if (compileStatus == prb_Success) {
        prb_String objsPathsString = prb_stringsJoin(outputFilepaths, arrlen(outputFilepaths), prb_STR(" "));

        prb_LastModResult sourceLastMod = prb_getLastModifiedFromPaths(outputFilepaths, arrlen(outputFilepaths), prb_LastModKind_Latest);
        prb_assert(sourceLastMod.success);
        prb_LastModResult outputLastMod = prb_getLastModifiedFromPath(lib.libFile);
        prb_Status        libStatus = prb_Success;
        if (!outputLastMod.success || (sourceLastMod.timestamp > outputLastMod.timestamp)) {
#if prb_PLATFORM_WINDOWS
            prb_String libCmd = prb_fmtAndPrintln("lib /nologo -out:%.*s %.*s", libFile, objsPattern);
#elif prb_PLATFORM_LINUX
            prb_String libCmd = prb_fmtAndPrintln("ar rcs %.*s %.*s", prb_LIT(lib.libFile), prb_LIT(objsPathsString));
#endif
            prb_removeFileIfExists(lib.libFile);
            prb_ProcessHandle libHandle = prb_execCmd(libCmd, 0, (prb_String) {});
            prb_assert(libHandle.completed);
            libStatus = libHandle.completionStatus;
        } else {
            prb_fmtAndPrintln("skip lib %.*s", prb_LIT(lib.name));
        }

        result = libStatus;
    }

    prb_endTempMemory(temp);
    return result;
}

static void
compileAndRunBidiGenTab(Compiler compiler, prb_String src, prb_String flags, prb_String runArgs, prb_String outpath) {
    prb_TempMemory temp = prb_beginTempMemory();
    if (!prb_isFile(outpath)) {
#if prb_PLATFORM_WINDOWS
        prb_String exeFilename = prb_replaceExt(stc, prb_STR("exe"));
#elif prb_PLATFORM_LINUX
        prb_String exeFilename = prb_replaceExt(src, prb_STR("bin"));
#else
#error unimplemented
#endif
        prb_String        packtabPath = prb_pathJoin(prb_getParentDir(src), prb_STR("packtab.c"));
        prb_String        cmd = constructCompileCmd(compiler, flags, prb_fmt("%.*s %.*s", prb_LIT(packtabPath), prb_LIT(src)), exeFilename, prb_STR(""));
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
    prb_endTempMemory(temp);
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
    prb_writeEntireFile(path, newContent.ptr, newContent.len);
}

int
main() {
    // TODO(khvorov) Argument parsing
    // TODO(khvorov) Release build
    // TODO(khvorov) Clone a specific commit probably
    prb_TimeStart scriptStartTime = prb_timeStart();
    prb_init(1 * prb_GIGABYTE);

    ProjectInfo project = {.rootDir = prb_getParentDir(prb_STR(__FILE__))};
    project.compileOutDir = prb_pathJoin(project.rootDir, prb_STR("build-debug"));
    prb_createDirIfNotExists(project.compileOutDir);

#if prb_PLATFORM_WINDOWS
    Compiler compiler = Compiler_Msvc;
#elif prb_PLATFORM_LINUX
    Compiler compiler = Compiler_Gcc;
#else
#error unimlemented
#endif

    //
    // SECTION Setup
    //

    // NOTE(khvorov) Fribidi

    prb_String fribidiCompileSouces[] = {prb_STR("lib/*.c")};

    prb_String fribidiNoConfigFlag = prb_STR("-DDONT_HAVE_FRIBIDI_CONFIG_H -DDONT_HAVE_FRIBIDI_UNICODE_VERSION_H");

    // TODO(khvorov) Custom allocators for fribidi
    StaticLibInfo fribidi = getStaticLibInfo(
        project,
        prb_STR("fribidi"),
        prb_STR("lib"),
        prb_fmt("%.*s -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1 -DHAVE_STRINGIZE=1", prb_LIT(fribidiNoConfigFlag)),
        fribidiCompileSouces,
        prb_arrayLength(fribidiCompileSouces)
    );

    // NOTE(khvorov) ICU

    // TODO(khvorov) Custom allocation for ICU

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

    StaticLibInfo icu = getStaticLibInfo(
        project,
        prb_STR("icu"),
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
        project,
        prb_STR("freetype"),
        prb_STR("include"),
        prb_fmt("-DFT2_BUILD_LIBRARY -DFT_CONFIG_OPTION_DISABLE_STREAM_SUPPORT -DFT_CONFIG_OPTION_USE_HARFBUZZ"),
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
        project,
        prb_STR("harfbuzz"),
        prb_STR("src"),
        prb_fmt("%.*s %.*s -DHAVE_ICU=1 -DHAVE_FREETYPE=1 -DHB_CUSTOM_MALLOC=1", prb_LIT(icu.includeFlag), prb_LIT(freetype.includeFlag)),
        harfbuzzCompileSources,
        prb_arrayLength(harfbuzzCompileSources)
    );

    // NOTE(khvorov) Freetype and harfbuzz depend on each other
    freetype.compileFlags = prb_fmt("%.*s %.*s", prb_LIT(freetype.compileFlags), prb_LIT(harfbuzz.includeFlag));

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
        project,
        prb_STR("sdl"),
        prb_STR("include"),
        prb_stringsJoin(sdlCompileFlags, prb_arrayLength(sdlCompileFlags), prb_STR(" ")),
        sdlCompileSources,
        prb_arrayLength(sdlCompileSources)
    );

    bool sdlNotDownloaded = !prb_isDirectory(sdl.downloadDir) || prb_directoryIsEmpty(sdl.downloadDir);

    //
    // SECTION Download
    //

    prb_ProcessHandle* downloadHandles = 0;
    arrput(downloadHandles, gitClone(fribidi.downloadDir, prb_STR("https://github.com/fribidi/fribidi")));
    arrput(downloadHandles, gitClone(icu.downloadDir, prb_STR("https://github.com/unicode-org/icu")));
    arrput(downloadHandles, gitClone(freetype.downloadDir, prb_STR("https://github.com/freetype/freetype")));
    arrput(downloadHandles, gitClone(harfbuzz.downloadDir, prb_STR("https://github.com/harfbuzz/harfbuzz")));
    arrput(downloadHandles, gitClone(sdl.downloadDir, prb_STR("https://github.com/libsdl-org/SDL")));
    prb_assert(prb_waitForProcesses(downloadHandles, arrlen(downloadHandles)) == prb_Success);

    //
    // SECTION Pre-compilation stuff
    //

    // NOTE(khvorov) Generate fribidi tables
    {
        prb_String gentabDir = prb_pathJoin(fribidi.downloadDir, prb_STR("gen.tab"));
        prb_String flags = prb_fmt(
            "%.*s %.*s -DHAVE_STDLIB_H=1 -DHAVE_STRING_H -DHAVE_STRINGIZE",
            prb_LIT(fribidiNoConfigFlag),
            prb_LIT(fribidi.includeFlag)
        );
        prb_String datadir = prb_pathJoin(gentabDir, prb_STR("unidata"));
        prb_String unidat = prb_pathJoin(datadir, prb_STR("UnicodeData.txt"));

        // NOTE(khvorov) This max-depth is also known as compression and is set to 2 in makefiles
        int32_t maxDepth = 2;

        prb_String bracketsPath = prb_pathJoin(datadir, prb_STR("BidiBrackets.txt"));
        compileAndRunBidiGenTab(
            compiler,
            prb_pathJoin(gentabDir, prb_STR("gen-brackets-tab.c")),
            flags,
            prb_fmt("%d %.*s %.*s", maxDepth, prb_LIT(bracketsPath), prb_LIT(unidat)),
            prb_pathJoin(fribidi.includeDir, prb_STR("brackets.tab.i"))
        );

        compileAndRunBidiGenTab(
            compiler,
            prb_pathJoin(gentabDir, prb_STR("gen-arabic-shaping-tab.c")),
            flags,
            prb_fmt("%d %.*s", maxDepth, prb_LIT(unidat)),
            prb_pathJoin(fribidi.includeDir, prb_STR("arabic-shaping.tab.i"))
        );

        prb_String shapePath = prb_pathJoin(datadir, prb_STR("ArabicShaping.txt"));
        compileAndRunBidiGenTab(
            compiler,
            prb_pathJoin(gentabDir, prb_STR("gen-joining-type-tab.c")),
            flags,
            prb_fmt("%d %.*s %.*s", maxDepth, prb_LIT(unidat), prb_LIT(shapePath)),
            prb_pathJoin(fribidi.includeDir, prb_STR("joining-type.tab.i"))
        );

        compileAndRunBidiGenTab(
            compiler,
            prb_pathJoin(gentabDir, prb_STR("gen-brackets-type-tab.c")),
            flags,
            prb_fmt("%d %.*s", maxDepth, prb_LIT(bracketsPath)),
            prb_pathJoin(fribidi.includeDir, prb_STR("brackets-type.tab.i"))
        );

        prb_String mirrorPath = prb_pathJoin(datadir, prb_STR("BidiMirroring.txt"));
        compileAndRunBidiGenTab(
            compiler,
            prb_pathJoin(gentabDir, prb_STR("gen-mirroring-tab.c")),
            flags,
            prb_fmt("%d %.*s", maxDepth, prb_LIT(mirrorPath)),
            prb_pathJoin(fribidi.includeDir, prb_STR("mirroring.tab.i"))
        );

        compileAndRunBidiGenTab(
            compiler,
            prb_pathJoin(gentabDir, prb_STR("gen-bidi-type-tab.c")),
            flags,
            prb_fmt("%d %.*s", maxDepth, prb_LIT(unidat)),
            prb_pathJoin(fribidi.includeDir, prb_STR("bidi-type.tab.i"))
        );
    }

    // NOTE(khvorov) Fix SDL
    if (sdlNotDownloaded) {
        prb_String downloadDir = sdl.downloadDir;

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

    //
    // SECTION Compile
    //

    // NOTE(khvorov) Running compilation of multiple libraries in parallel is
    // probably not worth it since the translation units within each library are
    // already compiling in parallel and there are more of them than cores on
    // desktop pcs.

    // prb_clearDirectory(prb_pathJoin(compileOutDir, fribidiName));
    prb_assert(compileStaticLib(project, compiler, fribidi) == prb_Success);

    // prb_clearDirectory(prb_pathJoin(compileOutDir, icuName));
    prb_assert(compileStaticLib(project, compiler, icu) == prb_Success);

    // prb_clearDirectory(prb_pathJoin(compileOutDir, freetypeName));
    prb_assert(compileStaticLib(project, compiler, freetype) == prb_Success);

    // prb_clearDirectory(prb_pathJoin(compileOutDir, harfbuzzName));
    prb_assert(compileStaticLib(project, compiler, harfbuzz) == prb_Success);

    // prb_clearDirectory(prb_pathJoin(compileOutDir, sdlName));
    prb_assert(compileStaticLib(project, compiler, sdl) == prb_Success);

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
        prb_STR("-Wall -Wextra -Wno-unused-function"),
    };

    prb_String mainFiles[] = {
        prb_pathJoin(project.rootDir, prb_STR("example.c")),
        freetype.libFile,
        sdl.libFile,
        harfbuzz.libFile,
        icu.libFile,
        fribidi.libFile,
    };

#if prb_PLATFORM_WINDOWS
    prb_String mainOutName = prb_STR("example.exe");
    prb_String mainLinkFlags = prb_STR("-subsystem:windows User32.lib");
#elif prb_PLATFORM_LINUX
    prb_String mainOutName = prb_STR("example.bin");
    // TODO(khvorov) Get rid of -lm and -ldl
    prb_String mainLinkFlags = prb_STR("-lX11 -lm -lstdc++ -ldl -lfontconfig");
#endif

    prb_String mainCmd = constructCompileCmd(
        compiler,
        prb_stringsJoin(mainFlags, prb_arrayLength(mainFlags), prb_STR(" ")),
        prb_stringsJoin(mainFiles, prb_arrayLength(mainFiles), prb_STR(" ")),
        prb_pathJoin(project.compileOutDir, mainOutName),
        mainLinkFlags
    );

    prb_ProcessHandle mainHandle = prb_execCmd(mainCmd, 0, (prb_String) {});
    prb_assert(mainHandle.completed && mainHandle.completionStatus == prb_Success);

    prb_fmtAndPrintln("total: %.2fms", prb_getMsFrom(scriptStartTime));
    return 0;
}
