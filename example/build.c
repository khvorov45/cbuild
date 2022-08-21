#define PROGRAMMABLE_BUILD_IMPLEMENTATION
#include "../programmable_build.h"

typedef struct GitClone {
    prb_String url;
    prb_String dest;
} GitClone;

typedef struct Compile {
    prb_String name;
    prb_String cmdStart;
    prb_String* sources;
    int32_t sourcesCount;
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

    uint64_t sourceLastMod = prb_getLastModifiedFromPatterns(data->sources, data->sourcesCount);
    uint64_t outputsLastMod = prb_getLastModifiedFromPatterns(data->outputs, data->outputsCount);
    if (sourceLastMod > outputsLastMod) {
        prb_String cmd = prb_stringJoin2(data->cmdStart, prb_stringsJoin(data->sources, data->sourcesCount, prb_STR(" ")));
        prb_logMessageLn(cmd);
        status = prb_execCmd(cmd);
    } else {
        prb_logMessageLn(prb_stringJoin2(prb_STR("skip compile "), data->name));
    }

    return status;
}

int
main() {
    prb_String rootDir = prb_getParentDir(prb_STR(__FILE__));

    prb_String freetypeDownloadDir = prb_pathJoin2(rootDir, prb_STR("freetype"));
    prb_StepHandle freetypeDownloadHandle = prb_addStep(
        gitClone,
        &(GitClone) {.url = prb_STR("https://github.com/freetype/freetype"), .dest = freetypeDownloadDir}
    );

    prb_String compileOutDir = prb_pathJoin2(rootDir, prb_STR("build-debug"));
    prb_createDirIfNotExists(compileOutDir);
#if prb_PLATFORM == prb_PLATFORM_WINDOWS
    prb_String compileCmdStart = prb_STR("cl /nologo /diagnostics:column /FC ");
#endif

    prb_String freetypeIncludeFlag =
        prb_stringJoin2(prb_STR("-I"), prb_pathJoin2(freetypeDownloadDir, prb_STR("include")));
    prb_StepHandle freetypeCompileHandle;
    {
        prb_String sources[] = {
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

        prb_String freetypeObjDir = prb_pathJoin2(compileOutDir, prb_STR("freetype"));
        prb_createDirIfNotExists(freetypeObjDir);

        prb_String outputs[] = {prb_pathJoin2(freetypeObjDir, prb_STR("*.obj"))};

        prb_String flagsArr[] = {
            freetypeIncludeFlag,
            prb_STR("-DFT2_BUILD_LIBRARY"),
            prb_STR("-c"),
#if prb_PLATFORM == prb_PLATFORM_WINDOWS
            prb_STR("-Zi"),
            prb_stringJoin2(prb_STR("/Fo"), prb_stringJoin2(freetypeObjDir, prb_STR("/")))
#endif
        };

        freetypeCompileHandle = prb_addStep(
            compile,
            &(Compile) {
                .name = prb_STR("freetype"),
                .cmdStart = prb_stringJoin2(
                    compileCmdStart,
                    prb_stringsJoin(flagsArr, prb_arrayLength(flagsArr), prb_STR(" "))
                ),
                .sources = sources,
                .sourcesCount = prb_arrayLength(sources),
                .outputs = outputs,
                .outputsCount = prb_arrayLength(outputs)
            }
        );

        prb_setDependency(freetypeCompileHandle, freetypeDownloadHandle);
    }

    {
        prb_String sources[] = {prb_pathJoin2(rootDir, prb_STR("example.c"))};

        prb_StepHandle exeCompileHandle = prb_addStep(
            compile,
            &(Compile) {
                .name = prb_STR("example"),
                .cmdStart = compileCmdStart,
                .sources = sources,
                .sourcesCount = prb_arrayLength(sources),
            }
        );

        prb_setDependency(exeCompileHandle, freetypeCompileHandle);
    }

    prb_run();
}
