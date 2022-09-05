#include "../programmable_build.h"

typedef struct GitClone {
    prb_String url;
    prb_String dest;
} GitClone;

typedef struct Compile {
    prb_String name;
    prb_String cmd;
    prb_String* watch;
    int32_t watchCount;
    prb_String* outputs;
    int32_t outputsCount;
} Compile;

typedef struct StaticLib {
    prb_StepHandle finalHandle;
    prb_String includeFlag;
    prb_String libFile;
} StaticLib;

prb_CompletionStatus
gitClone(void* dataInit) {
    GitClone* data = (GitClone*)dataInit;
    prb_CompletionStatus status = prb_CompletionStatus_Success;
    if (!prb_isDirectory(data->dest) || prb_directoryIsEmpty(data->dest)) {
        prb_String cmd = prb_fmtAndPrintln("git clone %s %s", data->url.ptr, data->dest.ptr);
        status = prb_execCmd(cmd);
    } else {
        prb_fmtAndPrintln("skip git clone %s", prb_getLastEntryInPath(data->dest).ptr);
    }
    return status;
}

prb_CompletionStatus
compile(void* dataInit) {
    Compile* data = (Compile*)dataInit;
    prb_CompletionStatus status = prb_CompletionStatus_Success;

    uint64_t sourceLastMod = prb_getLatestLastModifiedFromPatterns(data->watch, data->watchCount);
    uint64_t outputsLastMod = prb_getEarliestLastModifiedFromPatterns(data->outputs, data->outputsCount);
    if (sourceLastMod > outputsLastMod || data->watchCount == 0 || data->outputsCount == 0) {
        prb_fmtAndPrintln("%s", data->cmd.ptr);
        status = prb_execCmd(data->cmd);
    } else {
        prb_fmtAndPrintln("skip %s", data->name.ptr);
    }

    return status;
}

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
#ifdef prb_PLATFORM_WINDOWS
    prb_String staticLibCmdStart = prb_STR("lib /nologo ");
    prb_String staticLibFileExt = prb_STR("lib");
#elif defined(prb_PLATFORM_LINUX)
    prb_String staticLibCmdStart = prb_STR("ar rcs ");
    prb_String staticLibFileExt = prb_STR("a");
#endif

    prb_String downloadDir = prb_pathJoin(rootDir, name);
    prb_String includeFlag = prb_fmt("-I%s", prb_pathJoin(downloadDir, prb_STR("include")).ptr);

    prb_String libFile = prb_pathJoin(compileOutDir, prb_fmt("%s.%s", name.ptr, staticLibFileExt.ptr));

    prb_addStep(prb_DependOn_Nothing, gitClone, GitClone, {.url = downloadUrl, .dest = downloadDir});

    prb_String objDir = prb_pathJoin(compileOutDir, name);
    prb_createDirIfNotExists(objDir);

    prb_String objOutputs = prb_pathJoin(objDir, prb_STR("*.obj"));

#ifdef prb_PLATFORM_WINDOWS
    prb_String pdbPath = prb_pathJoin(compileOutDir, prb_fmt("%s.pdb", name.ptr));
#endif

    prb_String compileFlags[] = {
        includeFlag,
        prb_STR("-c"),
#ifdef prb_PLATFORM_WINDOWS
        prb_fmt("/Fo%s/", objDir.ptr),
        prb_fmt(prb_STR("/Fd%s"), pdbPath.ptr),
#elif defined(prb_PLATFORM_LINUX)
        prb_fmt("-o %s/", objDir.ptr),
#endif
    };

    int32_t extraCompileFlagsCount = extraCompileFlagsCstrCount;
    prb_String* extraCompileFlags = prb_allocArray(prb_String, extraCompileFlagsCount);
    for (int32_t flagIndex = 0; flagIndex < extraCompileFlagsCount; flagIndex++) {
        extraCompileFlags[flagIndex] = prb_STR(extraCompileFlagsCstr[flagIndex]);
    }

    prb_stringArrayJoin2(
        (prb_StringArray) {compileFlags, prb_arrayLength(compileFlags)},
        (prb_StringArray) {extraCompileFlags, extraCompileFlagsCount}
    );

    int32_t compileSourcesCount = compileSourcesRelToDownloadCount;
    prb_String* compileSources = prb_allocArray(prb_String, compileSourcesCount);
    for (int32_t sourceIndex = 0; sourceIndex < compileSourcesCount; sourceIndex++) {
        compileSources[sourceIndex] = prb_pathJoin(downloadDir, prb_STR(compileSourcesRelToDownload[sourceIndex]));
    }

    prb_String compileCmd = prb_fmt(
        "%s %s %s",
        compileCmdStart.ptr,
        prb_stringsJoin(compileFlags, prb_arrayLength(compileFlags), prb_STR(" ")).ptr,
        prb_stringsJoin(compileSources, compileSourcesCount, prb_STR(" ")).ptr
    );

    prb_StringArray compileOutputs = prb_stringArrayJoin2(
        (prb_StringArray) {&objOutputs, 1},
#ifdef prb_PLATFORM_WINDOWS
        (prb_StringArray) {&pdbPath, 1}
#elif defined(prb_PLATFORM_LINUX)
        (prb_StringArray) {0}
#endif
    );

    prb_addStep(
        prb_DependOn_LastAdded,
        compile,
        Compile,
        {.name = prb_fmt("%s compile", name.ptr),
         .cmd = compileCmd,
         .watch = compileSources,
         .watchCount = compileSourcesCount,
         .outputs = compileOutputs.ptr,
         .outputsCount = compileOutputs.len}
    );

    prb_String libFlags[] = {
#ifdef prb_PLATFORM_WINDOWS
        prb_stringJoin2(prb_STR("-out:"), libFile),
#elif defined(prb_PLATFORM_LINUX)
        libFile,
#endif
    };

    prb_String libCmd = prb_fmt(
        "%s %s %s",
        staticLibCmdStart.ptr,
        prb_stringsJoin(libFlags, prb_arrayLength(libFlags), prb_STR(" ")).ptr,
        objOutputs.ptr
    );

    prb_addStep(
        prb_DependOn_LastAdded,
        compile,
        Compile,
        {.name = prb_fmt("%s lib", name.ptr),
         .cmd = libCmd,
         .watch = compileOutputs.ptr,
         .watchCount = compileOutputs.len,
         .outputs = &libFile,
         .outputsCount = 1}
    );
    prb_StepHandle finalHandle = prb_getLastAddedStep();

    StaticLib result = {.finalHandle = finalHandle, .includeFlag = includeFlag, .libFile = libFile};
    return result;
}

int
main() {
    prb_init();

    prb_String rootDir = prb_getParentDir(prb_STR(__FILE__));

    prb_String compileOutDir = prb_pathJoin(rootDir, prb_STR("build-debug"));
    prb_createDirIfNotExists(compileOutDir);

#ifdef prb_PLATFORM_WINDOWS
    prb_String compileCmdStart = prb_STR("cl /nologo /diagnostics:column /FC /Zi ");
#elif defined(prb_PLATFORM_LINUX)
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

    char* sdlCompileSources[] = {
        "src/atomic/*.c",
        "src/audio/*.c",
        "src/dynapi/*.c",
        "src/thread/*.c",
        "src/thread/generic/*.c",
        "src/events/*.c",
        "src/file/*.c",
        "src/haptic/*.c",
        "src/joystick/*.c",
        "src/joystick/dummy/*.c",
        "src/joystick/hidapi/*.c",
        "src/joystick/virtual/*.c",
        "src/hidapi/*.c",
        "src/stdlib/*.c",
        "src/libm/*.c",
        "src/locale/*.c",
        "src/timer/*.c",
        "src/video/*.c",
        "src/video/dummy/*.c",
        "src/video/yuv2rgb/*.c",
        "src/misc/*.c",
        "src/power/*.c",
        "src/render/*.c",
        "src/render/software/*.c",
        "src/sensor/*.c",
        "src/sensor/dummy/*.c",
        "src/cpuinfo/*.c",
        "src/timer/*.c",
        "src/thread/*.c",
        "src/*.c",
#ifdef prb_PLATFORM_WINDOWS
        "src/audio/dummy/*.c",
        "src/audio/disk/*.c",
        "src/audio/winmm/*.c",
        "src/audio/directsound/*.c",
        "src/audio/wasapi/*.c",
        "src/core/windows/*.c",
        "src/filesystem/windows/*.c",
        "src/haptic/windows/*.c",
        "src/hidapi/windows/*.c",
        "src/joystick/windows/*.c",
        "src/timer/windows/*.c",
        "src/video/windows/*.c",
        "src/loadso/windows/*.c",
        "src/locale/windows/*.c",
        "src/main/windows/*.c",
        "src/misc/windows/*.c",
        "src/render/direct3d/*.c",
        "src/render/direct3d12/*.c",
        "src/render/direct3d11/*.c",
        "src/power/windows/*.c",
        "src/sensor/windows/*.c",
        "src/timer/windows/*.c",
        "src/thread/windows/*.c",
#endif
    };

    char* sdlCompileFlags[] = {
        "-DSDL_AUDIO_DISABLED",
        "-DSDL_HAPTIC_DISABLED",
        "-DSDL_HIDAPI_DISABLED",
        "-DSDL_SENSOR_DISABLED",
        "-DSDL_JOYSTICK_DISABLED",
        "-DSDL_VIDEO_RENDER_D3D=0",
        "-DSDL_VIDEO_RENDER_D3D11=0",
        "-DSDL_VIDEO_RENDER_D3D12=0",
        "-DSDL_VIDEO_RENDER_OGL=0",
        "-DSDL_VIDEO_RENDER_OGL_ES2=0",
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

    //
    // SECTION Main program
    //

    {
        prb_String flags[] = {
            freetype.includeFlag,
            sdl.includeFlag,
#ifdef prb_PLATFORM_WINDOWS
            prb_STR("-Zi"),
            prb_stringJoin2(prb_STR("-Fo"), prb_pathJoin(compileOutDir, prb_STR("example.obj"))),
            prb_stringJoin2(prb_STR("-Fe"), prb_pathJoin(compileOutDir, prb_STR("example.exe"))),
            prb_stringJoin2(prb_STR("-Fd"), prb_pathJoin(compileOutDir, prb_STR("example.pdb"))),
#endif
        };

        prb_String files[] = {
            prb_pathJoin(rootDir, prb_STR("example.c")),
            freetype.libFile,
            sdl.libFile,
        };

        prb_String cmd = prb_fmt(
            "%s %s %s",
            compileCmdStart.ptr,
            prb_stringsJoin(flags, prb_arrayLength(flags), prb_STR(" ")).ptr,
            prb_stringsJoin(files, prb_arrayLength(files), prb_STR(" ")).ptr
        );

#ifdef prb_PLATFORM_WINDOWS
        cmd = prb_stringJoin2(
            cmd,
            prb_STR(" -link -incremental:no -subsystem:windows "
                    "Ole32.lib Advapi32.lib Winmm.lib User32.lib Gdi32.lib OleAut32.lib "
                    "Imm32.lib Shell32.lib Version.lib Cfgmgr32.lib Hid.lib ")
        );
#endif

        prb_addStep(prb_DependOn_Nothing, compile, Compile, {.cmd = cmd});
        prb_StepHandle exeCompileHandle = prb_getLastAddedStep();
        prb_setDependency(exeCompileHandle, freetype.finalHandle);
        prb_setDependency(exeCompileHandle, sdl.finalHandle);
    }

    prb_run();
}
