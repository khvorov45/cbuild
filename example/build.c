#include "../programmable_build.h"

typedef struct StaticLib {
    bool       success;
    prb_String includeFlag;
    prb_String libFile;
} StaticLib;

typedef void (*PostDownloadCallback)(prb_String downloadDir);

StaticLib
downloadAndCompileStaticLib(
    prb_String           name,
    prb_String           downloadUrl,
    char**               compileSourcesRelToDownload,
    int32_t              compileSourcesRelToDownloadCount,
    prb_String*          extraCompileFlags,
    int32_t              extraCompileFlagsCount,
    prb_String           compileCmdStart,
    prb_String           rootDir,
    prb_String           compileOutDir,
    PostDownloadCallback postDownloadCallback
) {
    StaticLib  result = {0};
    prb_String downloadDir = prb_pathJoin(rootDir, name);

    prb_CompletionStatus downloadStatus = prb_CompletionStatus_Success;
    if (!prb_isDirectory(downloadDir) || prb_directoryIsEmpty(downloadDir)) {
        prb_String cmd = prb_fmtAndPrintln("git clone --depth 1 %s %s", downloadUrl, downloadDir);
        downloadStatus = prb_execCmdAndWait(cmd);
        if (downloadStatus == prb_CompletionStatus_Success && postDownloadCallback) {
            postDownloadCallback(downloadDir);
        }
    } else {
        prb_fmtAndPrintln("skip git clone %s", name);
    }

    if (downloadStatus == prb_CompletionStatus_Success) {
        prb_String objDir = prb_pathJoin(compileOutDir, name);
        prb_createDirIfNotExists(objDir);

        prb_String includeDir = prb_pathJoin(downloadDir, "include");
        prb_String includeFlag = prb_fmt("-I%s", includeDir);

        prb_String cmdStart = prb_fmt(
            "%s %s %s",
            compileCmdStart,
            includeFlag,
            prb_stringsJoin(extraCompileFlags, extraCompileFlagsCount, " ")
        );

#if prb_PLATFORM_WINDOWS
        prb_String pdbPath = prb_pathJoin(compileOutDir, prb_fmt("%s.pdb", name));
        prb_String pdbOutputFlag = prb_fmt("/Fd%s"), pdbPath);
        cmdStart = prb_fmt("%s %s", cmdStart, pdbOutputFlag);
#endif

        int32_t     compileSourcesCount = compileSourcesRelToDownloadCount;
        prb_String* compileSources = prb_allocArray(prb_String, compileSourcesCount);
        for (int32_t sourceIndex = 0; sourceIndex < compileSourcesCount; sourceIndex++) {
            compileSources[sourceIndex] = prb_pathJoin(downloadDir, compileSourcesRelToDownload[sourceIndex]);
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
        // TODO(khvorov) Probably just search the whole directory recursively for .h files
        prb_String hfilesInIncludePattern = prb_pathJoin(includeDir, "*.h");
        uint64_t   latestHFileChange = prb_getLatestLastModifiedFromPattern(hfilesInIncludePattern);
        for (int32_t inputMatchIndex = 0; inputMatchIndex < allInputMatchesCount; inputMatchIndex++) {
            prb_StringArray inputMatch = allInputMatches[inputMatchIndex];
            for (int32_t inputFilepathIndex = 0; inputFilepathIndex < inputMatch.len; inputFilepathIndex++) {
                prb_String inputFilepath = inputMatch.ptr[inputFilepathIndex];
                prb_String inputDir = prb_getParentDir(inputFilepath);
                prb_String adjacentHFilesPattern = prb_pathJoin(inputDir, "*.h");
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
                prb_String outputFilename = prb_replaceExt(inputFilename, "obj");
                prb_String outputFilepath = prb_pathJoin(objDir, outputFilename);

                allOutputFilepaths[allOutputFilepathsCount++] = outputFilepath;

                uint64_t sourceLastMod = prb_getLatestLastModifiedFromPattern(inputFilepath);
                uint64_t outputLastMod = prb_getEarliestLastModifiedFromPattern(outputFilepath);

                if (sourceLastMod > outputLastMod || latestHFileChange > outputLastMod) {
#if prb_PLATFORM_WINDOWS
                    prb_fmt("/Fo%s/", objDir);
#elif prb_PLATFORM_LINUX
                    prb_String cmd = prb_fmt("%s -c -o %s %s", cmdStart, outputFilepath, inputFilepath);
#endif
                    prb_println(cmd);
                    processes[processCount++] = prb_execCmdAndDontWait(cmd);
                }
            }
        }

        if (processCount == 0) {
            prb_fmtAndPrintln("skip compile %s", name);
        }

        prb_CompletionStatus compileStatus = prb_waitForProcesses(processes, processCount);
        if (compileStatus == prb_CompletionStatus_Success) {
#if prb_PLATFORM_WINDOWS
            prb_String staticLibFileExt = "lib");
#elif prb_PLATFORM_LINUX
            prb_String staticLibFileExt = "a";
#endif
            prb_String libFile = prb_pathJoin(compileOutDir, prb_fmt("%s.%s", name, staticLibFileExt));

            prb_String objsPathsString = prb_stringsJoin(allOutputFilepaths, allOutputFilepathsCount, " ");
#if prb_PLATFORM_WINDOWS
            prb_String libCmd = prb_fmt("lib /nologo -out:%s %s", libFile, objsPattern);
#elif prb_PLATFORM_LINUX
            prb_String libCmd = prb_fmt("ar rcs %s %s", libFile, objsPathsString);
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
                result = (StaticLib) {.success = true, .includeFlag = includeFlag, .libFile = libFile};
            }
        }
    }

    return result;
}

void
sdlMods(prb_String downloadDir) {
    // NOTE(khvorov) Purge dynamic api because otherwise you have to compile a lot more of sdl
    prb_String dynapiPath = prb_pathJoin(downloadDir, "src/dynapi/SDL_dynapi.h");
    prb_textfileReplace(dynapiPath, "#define SDL_DYNAMIC_API 1", "#define SDL_DYNAMIC_API 0");

    // NOTE(khvorov) This XMissingExtension function is in X11 extensions and SDL doesn't use it.
    // Saves us from having to -lXext for no reason
    prb_String x11sym = prb_pathJoin(downloadDir, "src/video/x11/SDL_x11sym.h");
    prb_textfileReplace(
        x11sym,
        "SDL_X11_SYM(int,XMissingExtension,(Display* a,_Xconst char* b),(a,b),return)",
        "//SDL_X11_SYM(int,XMissingExtension,(Display* a,_Xconst char* b),(a,b),return"
    );
}

int
main() {
    // TODO(khvorov) Argument parsing
    // TODO(khvorov) Release build
    prb_init();
    prb_TimeStart scriptStartTime = prb_timeStart();

    prb_String rootDir = prb_getParentDir(__FILE__);

    prb_String compileOutDir = prb_pathJoin(rootDir, "build-debug");
    prb_createDirIfNotExists(compileOutDir);

#if prb_PLATFORM_WINDOWS
    prb_String compileCmdStart = "cl /nologo /diagnostics:column /FC /Zi";
#elif prb_PLATFORM_LINUX
    prb_String compileCmdStart = "gcc -g";
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
        "freetype",
        "https://github.com/freetype/freetype",
        freetypeCompileSources,
        prb_arrayLength(freetypeCompileSources),
        freetypeCompileFlags,
        prb_arrayLength(freetypeCompileFlags),
        compileCmdStart,
        rootDir,
        compileOutDir,
        0
    );

    if (!freetype.success) {
        return 1;
    };

    //
    // SECTION SDL
    //

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
        "-DNO_SHARED_MEMORY=1",
#endif
    };

    StaticLib sdl = downloadAndCompileStaticLib(
        "sdl",
        "https://github.com/libsdl-org/SDL",
        sdlCompileSources,
        prb_arrayLength(sdlCompileSources),
        sdlCompileFlags,
        prb_arrayLength(sdlCompileFlags),
        compileCmdStart,
        rootDir,
        compileOutDir,
        sdlMods
    );

    if (!sdl.success) {
        return 1;
    }

    //
    // SECTION Pack font into a C array
    //

    prb_String fontFilePath = prb_pathJoin(rootDir, "LiberationMono-Regular.ttf");
    prb_String fontArrayPath = prb_pathJoin(rootDir, "fontdata.c");
    if (!prb_isFile(fontArrayPath)) {
        prb_binaryToCArray(fontFilePath, fontArrayPath, "fontdata");
    }

    //
    // SECTION Main program
    //

    prb_String mainFlags[] = {
        freetype.includeFlag,
        sdl.includeFlag,
        "-Wall -Wextra -Wno-unused-parameter",
#if prb_PLATFORM_WINDOWS
        "-Zi"),
        prb_stringJoin2("-Fo"), prb_pathJoin(compileOutDir, "example.obj"))),
        prb_stringJoin2("-Fe"), prb_pathJoin(compileOutDir, "example.exe"))),
        prb_stringJoin2("-Fd"), prb_pathJoin(compileOutDir, "example.pdb"))),
#elif prb_PLATFORM_LINUX
        prb_fmt("-o %s/example.bin", compileOutDir),
#endif
    };

    prb_String mainFiles[] = {
        prb_pathJoin(rootDir, "example.c"),
        freetype.libFile,
        sdl.libFile,
#if prb_PLATFORM_LINUX
#endif
    };

#if prb_PLATFORM_WINDOWS
    prb_String mainLinkFlags = 
        " -link -incremental:no -subsystem:windows "
        "Ole32.lib Advapi32.lib Winmm.lib User32.lib Gdi32.lib OleAut32.lib "
        "Imm32.lib Shell32.lib Version.lib Cfgmgr32.lib Hid.lib "
    );
#elif prb_PLATFORM_LINUX
    prb_String mainLinkFlags = "-lX11";
#endif

    prb_String mainCmd = prb_fmtAndPrintln(
        "%s %s %s %s",
        compileCmdStart,
        prb_stringsJoin(mainFlags, prb_arrayLength(mainFlags), " "),
        prb_stringsJoin(mainFiles, prb_arrayLength(mainFiles), " "),
        mainLinkFlags
    );

    prb_CompletionStatus mainStatus = prb_execCmdAndWait(mainCmd);

    if (mainStatus == prb_CompletionStatus_Success) {
        prb_fmtAndPrintln("total: %.2fms", prb_getMsFrom(scriptStartTime));
    } else {
        return 1;
    }

    return 0;
}
