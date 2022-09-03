#include "../programmable_build.h"

typedef struct GitClone {
    prb_String url;
    prb_String dest;
} GitClone;

typedef struct Compile {
    prb_String name;
    prb_String* cmds;
    int32_t cmdCount;
    prb_String* watch;
    int32_t watchCount;
    prb_String* outputs;
    int32_t outputsCount;
} Compile;

prb_CompletionStatus
gitClone(void* dataInit) {
    GitClone* data = (GitClone*)dataInit;
    prb_CompletionStatus status = prb_CompletionStatus_Success;
    if (!prb_directoryExists(data->dest) || prb_directoryIsEmpty(data->dest)) {
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
        for (int32_t cmdIndex = 0; cmdIndex < data->cmdCount; cmdIndex++) {
            prb_String cmd = data->cmds[cmdIndex];
            prb_fmtAndPrintln("%s", cmd.ptr);
            status = prb_execCmd(cmd);
        }
    } else {
        prb_fmtAndPrintln("skip %s", data->name.ptr);
    }

    return status;
}

int
main() {
    prb_init();

    prb_String rootDir = prb_getParentDir(prb_STR(__FILE__));

    prb_String compileOutDir = prb_pathJoin(rootDir, prb_STR("build-debug"));
    prb_createDirIfNotExists(compileOutDir);

#ifdef prb_PLATFORM_WINDOWS
    prb_String compileCmdStart = prb_STR("cl /nologo /diagnostics:column /FC ");
    prb_String staticLibCmdStart = prb_STR("lib /nologo ");
    prb_String staticLibFileExt = prb_STR("lib");
#elif defined(prb_PLATFORM_LINUX)
    prb_String compileCmdStart = prb_STR("gcc -Wall -Wextra -g ");
    prb_String staticLibCmdStart = prb_STR("ar rcs ");
    prb_String staticLibFileExt = prb_STR("a");
#endif

    //
    // SECTION Freetype
    //

    prb_String freetypeDownloadDir = prb_pathJoin(rootDir, prb_STR("freetype"));
    prb_String freetypeIncludeFlag = prb_fmt("-I%s", prb_pathJoin(freetypeDownloadDir, prb_STR("include")).ptr);

    prb_String freetypeLibFile = prb_pathJoin(compileOutDir, prb_fmt("freetype.%s", staticLibFileExt.ptr));

    prb_StepHandle freetypeFinalHandle;
    {
        prb_addStep(
            prb_DependOn_Nothing,
            gitClone,
            GitClone,
            {.url = prb_STR("https://github.com/freetype/freetype"), .dest = freetypeDownloadDir}
        );

        prb_String compileSources[] = {
            // Required
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftsystem.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftinit.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftdebug.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftbase.c")),

            // Recommended
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftbbox.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftglyph.c")),

            // Optional
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftbdf.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftbitmap.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftcid.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftfstype.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftgasp.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftgxval.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftmm.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftotval.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftpatent.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftpfr.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftstroke.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftsynth.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/fttype1.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/base/ftwinfnt.c")),

            // Font drivers
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/bdf/bdf.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/cff/cff.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/cid/type1cid.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/pcf/pcf.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/pfr/pfr.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/sfnt/sfnt.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/truetype/truetype.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/type1/type1.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/type42/type42.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/winfonts/winfnt.c")),

            // Rasterisers
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/raster/raster.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/sdf/sdf.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/smooth/smooth.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/svg/svg.c")),

            // Auxillary
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/autofit/autofit.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/cache/ftcache.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/gzip/ftgzip.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/lzw/ftlzw.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/bzip2/ftbzip2.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/gxvalid/gxvalid.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/otvalid/otvalid.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/psaux/psaux.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/pshinter/pshinter.c")),
            prb_pathJoin(freetypeDownloadDir, prb_STR("src/psnames/psnames.c")),
        };

        prb_String objDir = prb_pathJoin(compileOutDir, prb_STR("freetype"));
        prb_createDirIfNotExists(objDir);

        prb_String objOutputs[] = {prb_pathJoin(objDir, prb_STR("*.obj"))};

#ifdef prb_PLATFORM_WINDOWS
        prb_String pdbPath = prb_pathJoin(compileOutDir, prb_STR("freetype.pdb"));
#endif

        prb_String compileFlags[] = {
            freetypeIncludeFlag,
            prb_STR("-DFT2_BUILD_LIBRARY"),
            prb_STR("-c"),
#ifdef prb_PLATFORM_WINDOWS
            prb_STR("-Zi"),
            prb_fmt("/Fo%s/", objDir.ptr),
            prb_fmt(prb_STR("/Fd%s"), pdbPath.ptr),
#elif defined(prb_PLATFORM_LINUX)
            prb_fmt("-o %s/", objDir.ptr),
#endif
        };

        prb_String compileCmd = prb_fmt(
            "%s %s %s",
            compileCmdStart.ptr,
            prb_stringsJoin(compileFlags, prb_arrayLength(compileFlags), prb_STR(" ")).ptr,
            prb_stringsJoin(compileSources, prb_arrayLength(compileSources), prb_STR(" ")).ptr
        );

        prb_StringArray compileOutputs = {objOutputs, prb_arrayLength(objOutputs)};
#ifdef prb_PLATFORM_WINDOWS
        compileOutputs = prb_stringArrayJoin2(compileOutputs, (prb_StringArray) {&pdbPath, 1});
#endif

        prb_addStep(
            prb_DependOn_LastAdded,
            compile,
            Compile,
            {.name = prb_STR("freetype compile"),
             .cmds = &compileCmd,
             .cmdCount = 1,
             .watch = compileSources,
             .watchCount = prb_arrayLength(compileSources),
             .outputs = compileOutputs.ptr,
             .outputsCount = compileOutputs.len}
        );

        prb_String libFlags[] = {
#ifdef prb_PLATFORM_WINDOWS
            prb_stringJoin2(prb_STR("-out:"), freetypeLibFile),
#elif defined(prb_PLATFORM_LINUX)
            freetypeLibFile,
#endif
        };

        prb_String libCmd = prb_fmt(
            "%s %s %s",
            staticLibCmdStart.ptr,
            prb_stringsJoin(libFlags, prb_arrayLength(libFlags), prb_STR(" ")).ptr,
            prb_stringsJoin(objOutputs, prb_arrayLength(objOutputs), prb_STR(" ")).ptr
        );

        prb_addStep(
            prb_DependOn_LastAdded,
            compile,
            Compile,
            {.name = prb_STR("freetype lib"),
             .cmds = &libCmd,
             .cmdCount = 1,
             .watch = objOutputs,
             .watchCount = prb_arrayLength(objOutputs),
             .outputs = &freetypeLibFile,
             .outputsCount = 1}
        );
        freetypeFinalHandle = prb_getLastAddedStep();
    }

    //
    // SECTION SDL
    //

    prb_String sdlDownloadDir = prb_pathJoin(rootDir, prb_STR("sdl"));
    prb_String sdlIncludeFlag = prb_fmt("-I%s", prb_pathJoin(sdlDownloadDir, prb_STR("include")).ptr);

    prb_String sdlLibFile = prb_pathJoin(compileOutDir, prb_fmt("sdl.%s", staticLibFileExt));

    prb_StepHandle sdlFinalHandle;
    {
        prb_addStep(
            prb_DependOn_Nothing,
            gitClone,
            GitClone,
            {.url = prb_STR("https://github.com/libsdl-org/SDL"), .dest = sdlDownloadDir}
        );

        prb_String compileSources[] = {
            prb_pathJoin(sdlDownloadDir, prb_STR("src/atomic/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/audio/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/dynapi/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/thread/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/thread/generic/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/events/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/file/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/haptic/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/joystick/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/joystick/dummy/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/joystick/hidapi/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/joystick/virtual/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/hidapi/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/stdlib/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/libm/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/locale/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/timer/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/video/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/video/dummy/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/video/yuv2rgb/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/misc/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/power/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/render/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/render/software/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/sensor/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/sensor/dummy/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/cpuinfo/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/timer/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/thread/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/*.c")),
#ifdef prb_PLATFORM_WINDOWS
            prb_pathJoin(sdlDownloadDir, prb_STR("src/audio/dummy/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/audio/disk/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/audio/winmm/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/audio/directsound/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/audio/wasapi/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/core/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/filesystem/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/haptic/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/hidapi/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/joystick/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/timer/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/video/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/loadso/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/locale/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/main/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/misc/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/render/direct3d/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/render/direct3d12/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/render/direct3d11/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/power/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/sensor/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/timer/windows/*.c")),
            prb_pathJoin(sdlDownloadDir, prb_STR("src/thread/windows/*.c")),
#endif
        };

        prb_String objDir = prb_pathJoin(compileOutDir, prb_STR("sdl"));
        prb_createDirIfNotExists(objDir);

        prb_String objOutputs[] = {prb_pathJoin(objDir, prb_STR("*.obj"))};

#ifdef prb_PLATFORM_WINDOWS
        prb_String pdbPath = prb_pathJoin(compileOutDir, prb_STR("sdl.pdb"));
#endif

        prb_String compileFlags[] = {
            sdlIncludeFlag,
            prb_STR("-DSDL_AUDIO_DISABLED"),
            prb_STR("-DSDL_HAPTIC_DISABLED"),
            prb_STR("-DSDL_HIDAPI_DISABLED"),
            prb_STR("-DSDL_SENSOR_DISABLED"),
            prb_STR("-DSDL_JOYSTICK_DISABLED"),
            prb_STR("-DSDL_VIDEO_RENDER_D3D=0"),
            prb_STR("-DSDL_VIDEO_RENDER_D3D11=0"),
            prb_STR("-DSDL_VIDEO_RENDER_D3D12=0"),
            prb_STR("-DSDL_VIDEO_RENDER_OGL=0"),
            prb_STR("-DSDL_VIDEO_RENDER_OGL_ES2=0"),
            prb_STR("-c"),
#ifdef prb_PLATFORM_WINDOWS
            prb_STR("-Zi"),
            prb_stringJoin2(prb_STR("/Fo"), prb_stringJoin2(objDir, prb_STR("/"))),
            prb_stringJoin2(prb_STR("/Fd"), pdbPath),
#endif
        };

        prb_String compileCmd = prb_fmt(
            "%s %s %s",
            compileCmdStart.ptr,
            prb_stringsJoin(compileFlags, prb_arrayLength(compileFlags), prb_STR(" ")).ptr,
            prb_stringsJoin(compileSources, prb_arrayLength(compileSources), prb_STR(" ")).ptr
        );

        prb_StringArray compileOutputs = {objOutputs, prb_arrayLength(objOutputs)};
#ifdef prb_PLATFORM_WINDOWS
        compileOutputs = prb_stringArrayJoin2(compileOutputs, (prb_StringArray) {&pdbPath, 1});
#endif

        prb_addStep(
            prb_DependOn_LastAdded,
            compile,
            Compile,
            {.name = prb_STR("sdl compile"),
             .cmds = &compileCmd,
             .cmdCount = 1,
             .watch = compileSources,
             .watchCount = prb_arrayLength(compileSources),
             .outputs = compileOutputs.ptr,
             .outputsCount = compileOutputs.len}
        );

        prb_String libFlags[] = {
#ifdef prb_PLATFORM_WINDOWS
            prb_stringJoin2(prb_STR("-out:"), sdlLibFile),
#elif defined(prb_PLATFORM_LINUX)
            sdlLibFile
#endif
        };

        prb_String libCmd = prb_fmt(
            "%s %s %s",
            staticLibCmdStart.ptr,
            prb_stringsJoin(libFlags, prb_arrayLength(libFlags), prb_STR(" ")).ptr,
            prb_stringsJoin(objOutputs, prb_arrayLength(objOutputs), prb_STR(" ")).ptr
        );

        prb_addStep(
            prb_DependOn_LastAdded,
            compile,
            Compile,
            {.name = prb_STR("sdl lib"),
             .cmds = &libCmd,
             .cmdCount = 1,
             .watch = objOutputs,
             .watchCount = prb_arrayLength(objOutputs),
             .outputs = &sdlLibFile,
             .outputsCount = 1}
        );
        sdlFinalHandle = prb_getLastAddedStep();
    }

    //
    // SECTION Main program
    //

    {
        prb_String flags[] = {
            freetypeIncludeFlag,
            sdlIncludeFlag,
#ifdef prb_PLATFORM_WINDOWS
            prb_STR("-Zi"),
            prb_stringJoin2(prb_STR("-Fo"), prb_pathJoin(compileOutDir, prb_STR("example.obj"))),
            prb_stringJoin2(prb_STR("-Fe"), prb_pathJoin(compileOutDir, prb_STR("example.exe"))),
            prb_stringJoin2(prb_STR("-Fd"), prb_pathJoin(compileOutDir, prb_STR("example.pdb"))),
#endif
        };

        prb_String files[] = {
            prb_pathJoin(rootDir, prb_STR("example.c")),
            freetypeLibFile,
            sdlLibFile,
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

        prb_addStep(prb_DependOn_Nothing, compile, Compile, {.cmds = &cmd, .cmdCount = 1});
        prb_StepHandle exeCompileHandle = prb_getLastAddedStep();
        prb_setDependency(exeCompileHandle, freetypeFinalHandle);
        prb_setDependency(exeCompileHandle, sdlFinalHandle);
    }

    prb_run();
}
