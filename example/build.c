#include "../programmable_build.h"

typedef struct StaticLib {
    bool       success;
    prb_String includeFlag;
    prb_String libFile;
} StaticLib;

StaticLib
downloadAndCompileStaticLib(
    prb_String name,
    prb_String downloadUrl,
    char**     compileSourcesRelToDownload,
    int32_t    compileSourcesRelToDownloadCount,
    char**     extraCompileFlagsCstr,
    int32_t    extraCompileFlagsCstrCount,
    prb_String compileCmdStart,
    prb_String rootDir,
    prb_String compileOutDir
) {
    StaticLib  result = {0};
    prb_String downloadDir = prb_pathJoin(rootDir, name);

    prb_CompletionStatus downloadStatus = prb_CompletionStatus_Success;
    if (!prb_isDirectory(downloadDir) || prb_directoryIsEmpty(downloadDir)) {
        prb_String cmd = prb_fmtAndPrintln("git clone %s %s", downloadUrl.ptr, downloadDir.ptr);
        downloadStatus = prb_execCmdAndWait(cmd);
    } else {
        prb_fmtAndPrintln("skip git clone %s", name.ptr);
    }

    if (downloadStatus == prb_CompletionStatus_Success) {
        prb_String objDir = prb_pathJoin(compileOutDir, name);
        prb_createDirIfNotExists(objDir);

        prb_String includeDir = prb_pathJoin(downloadDir, prb_STR("include"));
        prb_String includeFlagForUser = prb_fmt("-I%s", includeDir.ptr);
        prb_String includeFlagForLibrary = prb_fmt("-I%s", includeDir.ptr);
        if (prb_streq(name, prb_STR("xlib"))) {
            prb_clearDirectory(objDir);  // TODO(khvorov) Remove when done with xlib
            includeFlagForLibrary = prb_fmt(
                "%s %s/X11 -I%s/src/xcms -I%s/src/xlibi18n",
                includeFlagForUser.ptr,
                includeFlagForUser.ptr,
                downloadDir.ptr,
                downloadDir.ptr
            );
        }
        prb_StringArray extraCompileFlags =
            prb_stringArrayFromCstrings(extraCompileFlagsCstr, extraCompileFlagsCstrCount);

        prb_String cmdStart = prb_fmt(
            "%s %s %s",
            compileCmdStart.ptr,
            includeFlagForLibrary.ptr,
            prb_stringsJoin(extraCompileFlags.ptr, extraCompileFlags.len, prb_STR(" ")).ptr
        );

#if prb_PLATFORM_WINDOWS
        prb_String pdbPath = prb_pathJoin(compileOutDir, prb_fmt("%s.pdb", name.ptr));
        prb_String pdbOutputFlag = prb_fmt(prb_STR("/Fd%s"), pdbPath.ptr);
        cmdStart = prb_fmt("%s %s", cmdStart, pdbOutputFlag);
#endif

        int32_t     compileSourcesCount = compileSourcesRelToDownloadCount;
        prb_String* compileSources = prb_allocArray(prb_String, compileSourcesCount);
        for (int32_t sourceIndex = 0; sourceIndex < compileSourcesCount; sourceIndex++) {
            compileSources[sourceIndex] = prb_pathJoin(downloadDir, prb_STR(compileSourcesRelToDownload[sourceIndex]));
        }

        int32_t          allInputMatchesCount = compileSourcesCount;
        prb_StringArray* allInputMatches = prb_allocArray(prb_StringArray, allInputMatchesCount);
        int32_t          allInputFilepathsCount = 0;
        for (int32_t inputPatternIndex = 0; inputPatternIndex < compileSourcesCount; inputPatternIndex++) {
            prb_String      inputPattern = compileSources[inputPatternIndex];
            prb_StringArray inputMatches = prb_getAllMatches(inputPattern);
            allInputMatches[inputPatternIndex] = inputMatches;
            allInputFilepathsCount += inputMatches.len;
        }

        // NOTE(khvorov) Recompile everything whenever any .h file changes
        prb_String hfilesInIncludePattern = prb_pathJoin(includeDir, prb_STR("*.h"));
        uint64_t   latestHFileChange = prb_getLatestLastModifiedFromPattern(hfilesInIncludePattern);
        for (int32_t inputMatchIndex = 0; inputMatchIndex < allInputMatchesCount; inputMatchIndex++) {
            prb_StringArray inputMatch = allInputMatches[inputMatchIndex];
            for (int32_t inputFilepathIndex = 0; inputFilepathIndex < inputMatch.len; inputFilepathIndex++) {
                prb_String inputFilepath = inputMatch.ptr[inputFilepathIndex];
                prb_String inputDir = prb_getParentDir(inputFilepath);
                prb_String adjacentHFilesPattern = prb_pathJoin(inputDir, prb_STR("*.h"));
                latestHFileChange =
                    prb_max(latestHFileChange, prb_getLatestLastModifiedFromPattern(adjacentHFilesPattern));
            }
        }

        prb_String*        allOutputFilepaths = prb_allocArray(prb_String, allInputFilepathsCount);
        prb_ProcessHandle* processes = prb_allocArray(prb_ProcessHandle, allInputFilepathsCount);
        int32_t            processCount = 0;
        int32_t            allOutputFilepathsCount = 0;
        for (int32_t inputMatchIndex = 0; inputMatchIndex < allInputMatchesCount; inputMatchIndex++) {
            prb_StringArray inputMatch = allInputMatches[inputMatchIndex];
            for (int32_t inputFilepathIndex = 0; inputFilepathIndex < inputMatch.len; inputFilepathIndex++) {
                prb_String inputFilepath = inputMatch.ptr[inputFilepathIndex];
                prb_String inputFilename = prb_getLastEntryInPath(inputFilepath);
                prb_String outputFilename = prb_replaceExt(inputFilename, prb_STR("obj"));
                prb_String outputFilepath = prb_pathJoin(objDir, outputFilename);
                allOutputFilepaths[allOutputFilepathsCount++] = outputFilepath;

                uint64_t sourceLastMod = prb_getLatestLastModifiedFromPattern(inputFilepath);
                uint64_t outputLastMod = prb_getEarliestLastModifiedFromPattern(outputFilepath);

                if (sourceLastMod > outputLastMod || latestHFileChange > outputLastMod) {
#if prb_PLATFORM_WINDOWS
                    prb_fmt("/Fo%s/", objDir.ptr);
#elif prb_PLATFORM_LINUX
                    prb_String cmd = prb_fmt("%s -c -o %s %s", cmdStart.ptr, outputFilepath.ptr, inputFilepath.ptr);
#endif
                    prb_println(cmd);
                    processes[processCount++] = prb_execCmdAndDontWait(cmd);
                }
            }
        }

        if (processCount == 0) {
            prb_fmtAndPrintln("skip compile %s", name.ptr);
        }

        prb_CompletionStatus compileStatus = prb_waitForProcesses(processes, processCount);
        if (compileStatus == prb_CompletionStatus_Success) {
#if prb_PLATFORM_WINDOWS
            prb_String staticLibFileExt = prb_STR("lib");
#elif prb_PLATFORM_LINUX
            prb_String staticLibFileExt = prb_STR("a");
#endif
            prb_String libFile = prb_pathJoin(compileOutDir, prb_fmt("%s.%s", name.ptr, staticLibFileExt.ptr));

            prb_String objsPathsString = prb_stringsJoin(allOutputFilepaths, allOutputFilepathsCount, prb_STR(" "));
#if prb_PLATFORM_WINDOWS
            prb_String libCmd = prb_fmt("lib /nologo -out:%s %s", libFile.ptr, objsPattern.ptr);
#elif prb_PLATFORM_LINUX
            prb_String libCmd = prb_fmt("ar rcs %s %s", libFile.ptr, objsPathsString.ptr);
#endif

            uint64_t sourceLastMod = prb_getLatestLastModifiedFromPatterns(allOutputFilepaths, allOutputFilepathsCount);
            uint64_t outputLastMod = prb_getEarliestLastModifiedFromPattern(libFile);
            prb_CompletionStatus libStatus = prb_CompletionStatus_Success;
            if (sourceLastMod > outputLastMod) {
                prb_println(libCmd);
                prb_removeFileIfExists(libFile);
                libStatus = prb_execCmdAndWait(libCmd);
            } else {
                prb_fmtAndPrintln("skip lib %s", name);
            }

            if (libStatus == prb_CompletionStatus_Success) {
                result = (StaticLib) {.success = true, .includeFlag = includeFlagForUser, .libFile = libFile};
            }
        }
    }

    return result;
}

int
main() {
    // TODO(khvorov) Argument parsing
    // TODO(khvorov) Release build
    // TODO(khvorov) Make a static linux executable
    prb_init();
    prb_TimeStart scriptStartTime = prb_timeStart();

    prb_String rootDir = prb_getParentDir(prb_STR(__FILE__));

    prb_String compileOutDir = prb_pathJoin(rootDir, prb_STR("build-debug"));
    prb_createDirIfNotExists(compileOutDir);

#if prb_PLATFORM_WINDOWS
    prb_String compileCmdStart = prb_STR("cl /nologo /diagnostics:column /FC /Zi");
#elif prb_PLATFORM_LINUX
    prb_String compileCmdStart = prb_STR("gcc -g");
#endif

    //
    // SECTION Freetype
    //

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

    if (!freetype.success) {
        return 1;
    };

    //
    // SECTION SDL
    //

    // TODO(khvorov) Purge sdl dynamic api programmatically
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

    if (!sdl.success) {
        return 1;
    }

#if prb_PLATFORM_LINUX

    //
    // SECTION Xlib
    //

    char* xlibSources[] = {"src/Window.c"};
    char* xlibFlags[] = {};

    StaticLib xlib = downloadAndCompileStaticLib(
        prb_STR("xlib"),
        prb_STR("https://github.com/freedesktop/xorg-libX11"),
        xlibSources,
        prb_arrayLength(xlibSources),
        xlibFlags,
        prb_arrayLength(xlibFlags),
        compileCmdStart,
        rootDir,
        compileOutDir
    );

    if (!xlib.success) {
        return 1;
    }
#endif

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
#if prb_PLATFORM_LINUX
        xlib.libFile,
#endif
    };

#if prb_PLATFORM_WINDOWS
    prb_String mainLinkFlags = prb_STR(
        " -link -incremental:no -subsystem:windows "
        "Ole32.lib Advapi32.lib Winmm.lib User32.lib Gdi32.lib OleAut32.lib "
        "Imm32.lib Shell32.lib Version.lib Cfgmgr32.lib Hid.lib "
    );
#elif prb_PLATFORM_LINUX
    prb_String mainLinkFlags = prb_STR("-lX11 -lXext");
#endif

    prb_String mainCmd = prb_fmt(
        "%s %s %s %s",
        compileCmdStart.ptr,
        prb_stringsJoin(mainFlags, prb_arrayLength(mainFlags), prb_STR(" ")).ptr,
        prb_stringsJoin(mainFiles, prb_arrayLength(mainFiles), prb_STR(" ")).ptr,
        mainLinkFlags.ptr
    );

    prb_println(mainCmd);
    prb_CompletionStatus mainStatus = prb_execCmdAndWait(mainCmd);

    if (mainStatus == prb_CompletionStatus_Success) {
        prb_fmtAndPrintln("total: %.2fms", prb_getMsFrom(scriptStartTime));
    } else {
        return 1;
    }

    return 0;
}
