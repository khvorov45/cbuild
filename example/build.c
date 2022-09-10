#include "../programmable_build.h"

typedef struct GitClone {
    prb_String url;
    prb_String dest;
} GitClone;

prb_CompletionStatus
gitClone(void* dataInit) {
    GitClone* data = (GitClone*)dataInit;
    prb_CompletionStatus status = prb_CompletionStatus_Success;
    if (!prb_isDirectory(data->dest) || prb_directoryIsEmpty(data->dest)) {
        prb_String cmd = prb_fmtAndPrintln("git clone %s %s", data->url.ptr, data->dest.ptr);
        status = prb_execCmdAndWait(cmd);
    } else {
        prb_fmtAndPrintln("skip git clone %s", prb_getLastEntryInPath(data->dest).ptr);
    }
    return status;
}

typedef struct CompileToObjsInDir {
    prb_String outDir;
    prb_String cmdStart;
    prb_String* inputPatterns;
    int32_t inputPatternsCount;
} CompileToObjsInDir;

prb_CompletionStatus
compileToObjsInDir(void* dataInit) {
    CompileToObjsInDir* data = (CompileToObjsInDir*)dataInit;

    prb_StringArray allInputFilepaths = {0};
    for (int32_t inputPatternIndex = 0; inputPatternIndex < data->inputPatternsCount; inputPatternIndex++) {
        prb_String inputPattern = data->inputPatterns[inputPatternIndex];
        prb_StringArray inputFilepaths = prb_getAllMatches(inputPattern);
        allInputFilepaths = prb_stringArrayJoin2(allInputFilepaths, inputFilepaths);
    }

    prb_ProcessHandle* processes = prb_allocArray(prb_ProcessHandle, allInputFilepaths.len);
    int32_t processCount = 0;
    for (int32_t inputFilepathIndex = 0; inputFilepathIndex < allInputFilepaths.len; inputFilepathIndex++) {
        prb_String inputFilepath = allInputFilepaths.ptr[inputFilepathIndex];
        prb_String inputFilename = prb_getLastEntryInPath(inputFilepath);
        prb_String outputFilename = prb_replaceExt(inputFilename, prb_STR("obj"));
        prb_String outputFilepath = prb_pathJoin(data->outDir, outputFilename);

        uint64_t sourceLastMod = prb_getLatestLastModifiedFromPattern(inputFilepath);
        uint64_t outputLastMod = prb_getEarliestLastModifiedFromPattern(outputFilepath);

        if (sourceLastMod > outputLastMod) {
#if prb_PLATFORM_WINDOWS
            prb_fmt("/Fo%s/", objDir.ptr);
#elif prb_PLATFORM_LINUX
            prb_String cmd = prb_fmt("%s -c -o %s %s", data->cmdStart.ptr, outputFilepath.ptr, inputFilepath.ptr);
#endif
            prb_println(cmd);
            processes[processCount++] = prb_execCmdAndDontWait(cmd);
        }
    }

    prb_CompletionStatus status = prb_waitForProcesses(processes, processCount);
    return status;
}

typedef struct MakeStaticLibFromObjsInDir {
    prb_String objDir;
    prb_String libFile;
} MakeStaticLibFromObjsInDir;

prb_CompletionStatus
makeStaticLibFromObjsInDir(void* dataInit) {
    MakeStaticLibFromObjsInDir* data = (MakeStaticLibFromObjsInDir*)dataInit;
    prb_String objsPattern = prb_pathJoin(data->objDir, prb_STR("*.obj"));
#if prb_PLATFORM_WINDOWS
    prb_String libCmd = prb_fmt("lib /nologo -out:%s %s", data->libFile.ptr, objsPattern.ptr);
#elif prb_PLATFORM_LINUX
    prb_StringArray objsPaths = prb_getAllMatches(objsPattern);
    prb_String objsPathsString = prb_stringsJoin(objsPaths.ptr, objsPaths.len, prb_STR(" "));
    prb_String libCmd = prb_fmt("ar rcs %s %s", data->libFile.ptr, objsPathsString.ptr);
#endif

    prb_CompletionStatus status = prb_CompletionStatus_Success;
    uint64_t sourceLastMod = prb_getLatestLastModifiedFromPattern(objsPattern);
    uint64_t outputLastMod = prb_getEarliestLastModifiedFromPattern(data->libFile);
    if (sourceLastMod > outputLastMod) {
        prb_println(libCmd);
        status = prb_execCmdAndWait(libCmd);
    } else {
        prb_fmtAndPrintln("skip %s", prb_getLastEntryInPath(data->libFile));
    }

    return status;
}

typedef struct AlwaysRun {
    prb_String cmd;
} AlwaysRun;

prb_CompletionStatus
alwaysRun(void* dataInit) {
    AlwaysRun* data = (AlwaysRun*)dataInit;
    prb_println(data->cmd);
    prb_CompletionStatus status = prb_execCmdAndWait(data->cmd);
    return status;
}

typedef struct StaticLib {
    prb_StepHandle finalHandle;
    prb_String includeFlag;
    prb_String libFile;
    prb_String outDir;
} StaticLib;

StaticLib
downloadAndCompileStaticLib(
    prb_String name,
    prb_String downloadUrl,
    char** compileSourcesRelToDownload,
    int32_t compileSourcesRelToDownloadCount,
    char** extraCompileFlagsCstr,
    int32_t extraCompileFlagsCstrCount,
    prb_String compileCmdStart,
    prb_String rootDir,
    prb_String compileOutDir
) {
    prb_String downloadDir = prb_pathJoin(rootDir, name);
    prb_addStep(prb_DependOn_Nothing, gitClone, GitClone, {.url = downloadUrl, .dest = downloadDir});

    prb_String objDir = prb_pathJoin(compileOutDir, name);
    prb_createDirIfNotExists(objDir);

    prb_String includeFlag = prb_fmt("-I%s", prb_pathJoin(downloadDir, prb_STR("include")).ptr);
    prb_StringArray extraCompileFlags = prb_stringArrayFromCstrings(extraCompileFlagsCstr, extraCompileFlagsCstrCount);

    prb_String cmdStart = prb_fmt(
        "%s %s %s",
        compileCmdStart.ptr,
        includeFlag.ptr,
        prb_stringsJoin(extraCompileFlags.ptr, extraCompileFlags.len, prb_STR(" ")).ptr
    );

#if prb_PLATFORM_WINDOWS
    prb_String pdbPath = prb_pathJoin(compileOutDir, prb_fmt("%s.pdb", name.ptr));
    prb_String pdbOutputFlag = prb_fmt(prb_STR("/Fd%s"), pdbPath.ptr);
    cmdStart = prb_fmt("%s %s", cmdStart, pdbOutputFlag);
#endif

    int32_t compileSourcesCount = compileSourcesRelToDownloadCount;
    prb_String* compileSources = prb_allocArray(prb_String, compileSourcesCount);
    for (int32_t sourceIndex = 0; sourceIndex < compileSourcesCount; sourceIndex++) {
        compileSources[sourceIndex] = prb_pathJoin(downloadDir, prb_STR(compileSourcesRelToDownload[sourceIndex]));
    }

    prb_addStep(
        prb_DependOn_LastAdded,
        compileToObjsInDir,
        CompileToObjsInDir,
        {.outDir = objDir,
         .cmdStart = cmdStart,
         .inputPatterns = compileSources,
         .inputPatternsCount = compileSourcesCount}
    );

#if prb_PLATFORM_WINDOWS
    prb_String staticLibFileExt = prb_STR("lib");
#elif prb_PLATFORM_LINUX
    prb_String staticLibFileExt = prb_STR("a");
#endif

    prb_String libFile = prb_pathJoin(compileOutDir, prb_fmt("%s.%s", name.ptr, staticLibFileExt.ptr));

    prb_addStep(
        prb_DependOn_LastAdded,
        makeStaticLibFromObjsInDir,
        MakeStaticLibFromObjsInDir,
        {.objDir = objDir, .libFile = libFile}
    );

    prb_StepHandle finalHandle = prb_getLastAddedStep();

    StaticLib result = {.finalHandle = finalHandle, .includeFlag = includeFlag, .libFile = libFile, .outDir = objDir};
    return result;
}

int
main() {
    prb_init();

    prb_String rootDir = prb_getParentDir(prb_STR(__FILE__));

    // TODO(khvorov) Release build
    // TODO(khvorov) Make sure we are not dependent on libc
    prb_String compileOutDir = prb_pathJoin(rootDir, prb_STR("build-debug"));
    prb_createDirIfNotExists(compileOutDir);

#if prb_PLATFORM_WINDOWS
    prb_String compileCmdStart = prb_STR("cl /nologo /diagnostics:column /FC /Zi ");
#elif prb_PLATFORM_LINUX
    prb_String compileCmdStart = prb_STR("gcc -g ");
#endif

    char* freetypeCompileSources[] = {
        // Required
        "src/base/ftsystem.c",
        "src/base/ftinit.c",
        "src/base/ftdebug.c",
        "src/base/ftbase.c",

        // Recommended
        "src/base/ftbbox.c",
        "src/base/ftglyph.c",

        // Optional
        "src/base/ftbdf.c",
        "src/base/ftbitmap.c",
        "src/base/ftcid.c",
        "src/base/ftfstype.c",
        "src/base/ftgasp.c",
        "src/base/ftgxval.c",
        "src/base/ftmm.c",
        "src/base/ftotval.c",
        "src/base/ftpatent.c",
        "src/base/ftpfr.c",
        "src/base/ftstroke.c",
        "src/base/ftsynth.c",
        "src/base/fttype1.c",
        "src/base/ftwinfnt.c",

        // Font drivers
        "src/bdf/bdf.c",
        "src/cff/cff.c",
        "src/cid/type1cid.c",
        "src/pcf/pcf.c",
        "src/pfr/pfr.c",
        "src/sfnt/sfnt.c",
        "src/truetype/truetype.c",
        "src/type1/type1.c",
        "src/type42/type42.c",
        "src/winfonts/winfnt.c",

        // Rasterisers
        "src/raster/raster.c",
        "src/sdf/sdf.c",
        "src/smooth/smooth.c",
        "src/svg/svg.c",

        // Auxillary
        "src/autofit/autofit.c",
        "src/cache/ftcache.c",
        "src/gzip/ftgzip.c",
        "src/lzw/ftlzw.c",
        "src/bzip2/ftbzip2.c",
        "src/gxvalid/gxvalid.c",
        "src/otvalid/otvalid.c",
        "src/psaux/psaux.c",
        "src/pshinter/pshinter.c",
        "src/psnames/psnames.c",
    };

    char* freetypeCompileFlags[] = {
        "-DFT2_BUILD_LIBRARY",
    };

    StaticLib freetype = downloadAndCompileStaticLib(
        prb_STR("freetype"),
        prb_STR("https://github.com/freetype/freetype"),
        freetypeCompileSources,
        prb_arrayLength(freetypeCompileSources),
        freetypeCompileFlags,
        prb_arrayLength(freetypeCompileFlags),
        compileCmdStart,
        rootDir,
        compileOutDir
    );
    // prb_clearDirectory(freetype.outDir);
    // prb_removeFileIfExists(freetype.libFile);

    char* sdlCompileSources[] = {
        "src/atomic/*.c",
        "src/thread/*.c",
        "src/thread/generic/*.c",
        "src/events/*.c",
        "src/file/*.c",
        "src/stdlib/*.c",
        "src/libm/*.c",
        "src/locale/*.c",
        "src/timer/*.c",
        "src/video/*.c",
        "src/video/dummy/*.c",
        "src/video/yuv2rgb/*.c",
        "src/render/*.c",
        "src/render/software/*.c",
        "src/cpuinfo/*.c",
        "src/*.c",
#if prb_PLATFORM_WINDOWS
        "src/core/windows/windows.c",
        "src/filesystem/windows/*.c",
        "src/timer/windows/*.c",
        "src/thread/windows/*.c",
        "src/video/windows/*.c",
        "src/loadso/windows/*.c",
        "src/locale/windows/*.c",
        "src/main/windows/*.c",
#elif prb_PLATFORM_LINUX
        "src/timer/unix/*.c",
        "src/filesystem/unix/*.c",
        "src/loadso/dlopen/*.c",
        "src/video/x11/*.c",
        "src/core/unix/SDL_poll.c",
#endif
    };

    // TODO(khvorov) Remove existing static lib before making a new one
    // TODO(khvorov) Purge sdl dynamic api programmatically
    // TODO(khvorov) Make a static linux executable
    char* sdlCompileFlags[] = {
        "-DSDL_AUDIO_DISABLED=1",
        "-DSDL_HAPTIC_DISABLED=1",
        "-DSDL_HIDAPI_DISABLED=1",
        "-DSDL_SENSOR_DISABLED=1",
        "-DSDL_JOYSTICK_DISABLED=1",
        "-DSDL_VIDEO_RENDER_D3D=0",
        "-DSDL_VIDEO_RENDER_D3D11=0",
        "-DSDL_VIDEO_RENDER_D3D12=0",
        "-DSDL_VIDEO_RENDER_OGL=0",
        "-DSDL_VIDEO_RENDER_OGL_ES2=0",
#if prb_PLATFORM_LINUX
        "-Wno-deprecated-declarations",
        "-DHAVE_STRING_H=1",
        "-DHAVE_STDIO_H=1",
        "-DSDL_TIMER_UNIX=1",
        "-DSDL_FILESYSTEM_UNIX=1",
        "-DSDL_LOADSO_DLOPEN=1",
        "-DSDL_VIDEO_DRIVER_X11=1",
        "-DSDL_VIDEO_DRIVER_X11_SUPPORTS_GENERIC_EVENTS=1",
#endif
    };

    StaticLib sdl = downloadAndCompileStaticLib(
        prb_STR("sdl"),
        prb_STR("https://github.com/libsdl-org/SDL"),
        sdlCompileSources,
        prb_arrayLength(sdlCompileSources),
        sdlCompileFlags,
        prb_arrayLength(sdlCompileFlags),
        compileCmdStart,
        rootDir,
        compileOutDir
    );
    // prb_clearDirectory(sdl.outDir);
    // prb_removeFileIfExists(sdl.libFile);

    //
    // SECTION Main program
    //

    prb_String mainFlags[] = {
        freetype.includeFlag,
        sdl.includeFlag,
#if prb_PLATFORM_WINDOWS
        prb_STR("-Zi"),
        prb_stringJoin2(prb_STR("-Fo"), prb_pathJoin(compileOutDir, prb_STR("example.obj"))),
        prb_stringJoin2(prb_STR("-Fe"), prb_pathJoin(compileOutDir, prb_STR("example.exe"))),
        prb_stringJoin2(prb_STR("-Fd"), prb_pathJoin(compileOutDir, prb_STR("example.pdb"))),
#elif prb_PLATFORM_LINUX
        prb_fmt("-o %s/example.bin", compileOutDir.ptr),
#endif
    };

    prb_String mainFiles[] = {
        prb_pathJoin(rootDir, prb_STR("example.c")),
        freetype.libFile,
        sdl.libFile,
    };

#if prb_PLATFORM_WINDOWS
    prb_String mainLinkFlags = prb_STR(
        " -link -incremental:no -subsystem:windows "
        "Ole32.lib Advapi32.lib Winmm.lib User32.lib Gdi32.lib OleAut32.lib "
        "Imm32.lib Shell32.lib Version.lib Cfgmgr32.lib Hid.lib "
    )
#elif prb_PLATFORM_LINUX
    prb_String mainLinkFlags = prb_STR("-lX11 -lXext");
#endif

        prb_String cmd = prb_fmt(
            "%s %s %s %s",
            compileCmdStart.ptr,
            prb_stringsJoin(mainFlags, prb_arrayLength(mainFlags), prb_STR(" ")).ptr,
            prb_stringsJoin(mainFiles, prb_arrayLength(mainFiles), prb_STR(" ")).ptr,
            mainLinkFlags.ptr
        );

    prb_addStep(prb_DependOn_Nothing, alwaysRun, AlwaysRun, {.cmd = cmd});
    prb_StepHandle exeCompileHandle = prb_getLastAddedStep();
    prb_setDependency(exeCompileHandle, freetype.finalHandle);
    prb_setDependency(exeCompileHandle, sdl.finalHandle);

    prb_run();
}
