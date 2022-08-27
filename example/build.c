#define PROGRAMMABLE_BUILD_IMPLEMENTATION
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
        prb_String cmd = prb_stringJoin4(prb_STR("git clone "), data->url, prb_STR(" "), data->dest);
        prb_logMessageLn(cmd);
        status = prb_execCmd(cmd);
    } else {
        prb_logMessageLn(prb_stringJoin2(prb_STR("skip git clone "), prb_getLastEntryInPath(data->dest)));
    }
    return status;
}

prb_CompletionStatus
compile(void* dataInit) {
    Compile* data = (Compile*)dataInit;
    prb_CompletionStatus status = prb_CompletionStatus_Success;

    uint64_t sourceLastMod = prb_getLastModifiedFromPatterns(data->watch, data->watchCount);
    uint64_t outputsLastMod = prb_getLastModifiedFromPatterns(data->outputs, data->outputsCount);
    if (sourceLastMod > outputsLastMod || data->watchCount == 0 || data->outputsCount == 0) {
        for (int32_t cmdIndex = 0; cmdIndex < data->cmdCount; cmdIndex++) {
            prb_String cmd = data->cmds[cmdIndex];
            prb_logMessageLn(cmd);
            status = prb_execCmd(cmd);
        }
    } else {
        prb_logMessageLn(prb_stringJoin2(prb_STR("skip "), data->name));
    }

    return status;
}

int
main() {
    prb_String rootDir = prb_getParentDir(prb_STR(__FILE__));

    prb_String compileOutDir = prb_pathJoin2(rootDir, prb_STR("build-debug"));
    prb_createDirIfNotExists(compileOutDir);

#if prb_PLATFORM == prb_PLATFORM_WINDOWS
    prb_String compileCmdStart = prb_STR("cl /nologo /diagnostics:column /FC ");
    prb_String staticLibCmdStart = prb_STR("lib /nologo ");
#endif

    //
    // SECTION Freetype
    //

    prb_String freetypeDownloadDir = prb_pathJoin2(rootDir, prb_STR("freetype"));
    prb_String freetypeIncludeFlag =
        prb_stringJoin2(prb_STR("-I"), prb_pathJoin2(freetypeDownloadDir, prb_STR("include")));

#if prb_PLATFORM == prb_PLATFORM_WINDOWS
    prb_String freetypeLibFile = prb_pathJoin2(compileOutDir, prb_STR("freetype.lib"));
#endif

    prb_StepHandle freetypeFinalHandle;
    {
        prb_StepHandle downloadHandle = prb_addStep(
            gitClone,
            &(GitClone) {.url = prb_STR("https://github.com/freetype/freetype"), .dest = freetypeDownloadDir}
        );

        prb_String compileSources[] = {
            // Required
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftsystem.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftinit.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftdebug.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftbase.c")),

            // Recommended
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftbbox.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftglyph.c")),

            // Optional
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftbdf.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftbitmap.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftcid.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftfstype.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftgasp.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftgxval.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftmm.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftotval.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftpatent.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftpfr.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftstroke.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftsynth.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/fttype1.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/base/ftwinfnt.c")),

            // Font drivers
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/bdf/bdf.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/cff/cff.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/cid/type1cid.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/pcf/pcf.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/pfr/pfr.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/sfnt/sfnt.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/truetype/truetype.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/type1/type1.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/type42/type42.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/winfonts/winfnt.c")),

            // Rasterisers
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/raster/raster.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/sdf/sdf.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/smooth/smooth.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/svg/svg.c")),

            // Auxillary
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/autofit/autofit.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/cache/ftcache.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/gzip/ftgzip.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/lzw/ftlzw.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/bzip2/ftbzip2.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/gxvalid/gxvalid.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/otvalid/otvalid.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/psaux/psaux.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/pshinter/pshinter.c")),
            prb_pathJoin2(freetypeDownloadDir, prb_STR("src/psnames/psnames.c")),
        };

        prb_String objDir = prb_pathJoin2(compileOutDir, prb_STR("freetype"));
        prb_createDirIfNotExists(objDir);

        prb_String objOutputs[] = {prb_pathJoin2(objDir, prb_STR("*.obj"))};

        prb_String compileFlags[] = {
            freetypeIncludeFlag,
            prb_STR("-DFT2_BUILD_LIBRARY"),
            prb_STR("-c"),
#if prb_PLATFORM == prb_PLATFORM_WINDOWS
            prb_STR("-Zi"),
            prb_stringJoin2(prb_STR("/Fo"), prb_stringJoin2(objDir, prb_STR("/"))),
            prb_stringJoin2(prb_STR("/Fd"), prb_pathJoin2(compileOutDir, prb_STR("freetype.pdb"))),
#endif
        };

        prb_String compileCmd = prb_stringJoin3(
            compileCmdStart,
            prb_stringsJoin(compileFlags, prb_arrayLength(compileFlags), prb_STR(" ")),
            prb_stringsJoin(compileSources, prb_arrayLength(compileSources), prb_STR(" "))
        );

        prb_StepHandle compileHandle = prb_addStep(
            compile,
            &(Compile
            ) {.name = prb_STR("freetype compile"),
               .cmds = &compileCmd,
               .cmdCount = 1,
               .watch = compileSources,
               .watchCount = prb_arrayLength(compileSources),
               .outputs = objOutputs,
               .outputsCount = prb_arrayLength(objOutputs)}
        );

        prb_setDependency(compileHandle, downloadHandle);

        prb_String libFlags[] = {
#if prb_PLATFORM == prb_PLATFORM_WINDOWS
            prb_stringJoin2(prb_STR("-out:"), freetypeLibFile),
#endif
        };

        prb_String libCmd = prb_stringJoin3(
            staticLibCmdStart,
            prb_stringsJoin(libFlags, prb_arrayLength(libFlags), prb_STR(" ")),
            prb_stringsJoin(objOutputs, prb_arrayLength(objOutputs), prb_STR(" "))
        );

        freetypeFinalHandle = prb_addStep(
            compile,
            &(Compile
            ) {.name = prb_STR("freetype lib"),
               .cmds = &libCmd,
               .cmdCount = 1,
               .watch = objOutputs, // TODO(khvorov) add pdb
               .watchCount = prb_arrayLength(objOutputs),
               .outputs = &freetypeLibFile,
               .outputsCount = 1}
        );

        prb_setDependency(freetypeFinalHandle, compileHandle);
    }

    //
    // SECTION SDL
    //

    prb_String sdlDownloadDir = prb_pathJoin2(rootDir, prb_STR("sdl"));
    prb_String sdlIncludeFlag =
        prb_stringJoin2(prb_STR("-I"), prb_pathJoin2(sdlDownloadDir, prb_STR("include")));

#if prb_PLATFORM == prb_PLATFORM_WINDOWS
    prb_String sdlLibFile = prb_pathJoin2(compileOutDir, prb_STR("sdl.lib"));
#endif

    prb_StepHandle sdlFinalHandle;
    {
        prb_StepHandle downloadHandle = prb_addStep(
            gitClone,
            &(GitClone) {.url = prb_STR("https://github.com/libsdl-org/SDL"), .dest = sdlDownloadDir}
        );

        prb_String compileSources[] = {
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/atomic/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/audio/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/dynapi/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/thread/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/thread/generic/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/events/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/file/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/haptic/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/joystick/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/joystick/dummy/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/joystick/hidapi/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/joystick/virtual/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/hidapi/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/stdlib/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/libm/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/locale/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/timer/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/video/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/video/dummy/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/video/yuv2rgb/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/misc/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/power/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/render/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/render/software/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/sensor/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/sensor/dummy/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/cpuinfo/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/timer/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/thread/*.c")),
            prb_pathJoin2(sdlDownloadDir, prb_STR("src/*.c")),
            #if prb_PLATFORM == prb_PLATFORM_WINDOWS
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/audio/dummy/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/audio/disk/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/audio/winmm/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/audio/directsound/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/audio/wasapi/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/core/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/filesystem/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/haptic/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/hidapi/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/joystick/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/timer/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/video/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/loadso/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/locale/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/main/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/misc/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/render/direct3d/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/render/direct3d12/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/render/direct3d11/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/power/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/sensor/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/timer/windows/*.c")),
                prb_pathJoin2(sdlDownloadDir, prb_STR("src/thread/windows/*.c")),
            #endif
        };

        prb_String objDir = prb_pathJoin2(compileOutDir, prb_STR("sdl"));
        prb_createDirIfNotExists(objDir);

        prb_String objOutputs[] = {prb_pathJoin2(objDir, prb_STR("*.obj"))};

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
#if prb_PLATFORM == prb_PLATFORM_WINDOWS
            prb_STR("-Zi"),
            prb_stringJoin2(prb_STR("/Fo"), prb_stringJoin2(objDir, prb_STR("/"))),
            prb_stringJoin2(prb_STR("/Fd"), prb_pathJoin2(compileOutDir, prb_STR("sdl.pdb"))),
#endif
        };

        prb_String compileCmd = prb_stringJoin3(
            compileCmdStart,
            prb_stringsJoin(compileFlags, prb_arrayLength(compileFlags), prb_STR(" ")),
            prb_stringsJoin(compileSources, prb_arrayLength(compileSources), prb_STR(" "))
        );

        prb_StepHandle compileHandle = prb_addStep(
            compile,
            &(Compile
            ) {.name = prb_STR("sdl compile"),
               .cmds = &compileCmd,
               .cmdCount = 1,
               .watch = compileSources, // TODO(khvorov) add pdb
               .watchCount = prb_arrayLength(compileSources),
               .outputs = objOutputs,
               .outputsCount = prb_arrayLength(objOutputs)}
        );

        prb_setDependency(compileHandle, downloadHandle);

        prb_String libFlags[] = {
#if prb_PLATFORM == prb_PLATFORM_WINDOWS
            prb_stringJoin2(prb_STR("-out:"), sdlLibFile),
#endif
        };

        prb_String libCmd = prb_stringJoin3(
            staticLibCmdStart,
            prb_stringsJoin(libFlags, prb_arrayLength(libFlags), prb_STR(" ")),
            prb_stringsJoin(objOutputs, prb_arrayLength(objOutputs), prb_STR(" "))
        );

        sdlFinalHandle = prb_addStep(
            compile,
            &(Compile
            ) {.name = prb_STR("sdl lib"),
               .cmds = &libCmd,
               .cmdCount = 1,
               .watch = objOutputs,
               .watchCount = prb_arrayLength(objOutputs),
               .outputs = &sdlLibFile,
               .outputsCount = 1}
        );

        prb_setDependency(sdlFinalHandle, compileHandle);
    }

    //
    // SECTION Main program
    //

    {
        prb_String flags[] = {
            freetypeIncludeFlag,
            sdlIncludeFlag,
#if prb_PLATFORM == prb_PLATFORM_WINDOWS
            prb_STR("-Zi"),
            prb_stringJoin2(prb_STR("-Fo"), prb_pathJoin2(compileOutDir, prb_STR("example.obj"))),
            prb_stringJoin2(prb_STR("-Fe"), prb_pathJoin2(compileOutDir, prb_STR("example.exe"))),
            prb_stringJoin2(prb_STR("-Fd"), prb_pathJoin2(compileOutDir, prb_STR("example.pdb"))),
#endif
        };

        prb_String files[] = {
            prb_pathJoin2(rootDir, prb_STR("example.c")),
            freetypeLibFile,
            sdlLibFile,
        };

        prb_String cmd = prb_stringJoin3(
            compileCmdStart,
            prb_stringsJoin(flags, prb_arrayLength(flags), prb_STR(" ")),
            prb_stringsJoin(files, prb_arrayLength(files), prb_STR(" "))
        );

#if prb_PLATFORM == prb_PLATFORM_WINDOWS
        cmd = prb_stringJoin2(cmd, prb_STR(
            " -link -incremental:no -subsystem:windows " 
            "Ole32.lib Advapi32.lib Winmm.lib User32.lib Gdi32.lib OleAut32.lib "
            "Imm32.lib Shell32.lib Version.lib Cfgmgr32.lib Hid.lib "
        ));
#endif

        prb_StepHandle exeCompileHandle = prb_addStep(compile, &(Compile) {.cmds = &cmd, .cmdCount = 1});

        prb_setDependency(exeCompileHandle, freetypeFinalHandle);
        prb_setDependency(exeCompileHandle, sdlFinalHandle);
    }

    prb_run();
}
