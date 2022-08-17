#define PROGRAMMABLE_BUILD_IMPLEMENTATION
#include "../programmable_build.h"

int
main() {
    prb_init(prb_getParentDir(prb_STR(__FILE__)));

    prb_String freetypeDownloadDir = prb_STR("freetype");
    prb_StepHandle freetypeDownloadHandle = prb_addStep(
        prb_gitClone,
        (prb_StepData
        ) {.kind = prb_StepDataKind_GitClone,
           .gitClone =
               {
                   .url = prb_STR("https://github.com/freetype/freetype"),
                   .dest = freetypeDownloadDir,
               }}
    );

    prb_String freetypeIncludeFlag = prb_createIncludeFlag(prb_pathJoin(freetypeDownloadDir, prb_STR("include")));
    prb_StepHandle freetypeCompileHandle;
    {
        prb_String sources[] = {
            // Required
            prb_STR("base/ftsystem.c"),
            prb_STR("base/ftinit.c"),
            prb_STR("base/ftdebug.c"),
            prb_STR("base/ftbase.c"),

            // Recommended
            prb_STR("base/ftbbox.c"),
            prb_STR("base/ftglyph.c"),

            // Optional
            prb_STR("base/ftbdf.c"),
            prb_STR("base/ftbitmap.c"),
            prb_STR("base/ftcid.c"),
            prb_STR("base/ftfstype.c"),
            prb_STR("base/ftgasp.c"),
            prb_STR("base/ftgxval.c"),
            prb_STR("base/ftmm.c"),
            prb_STR("base/ftotval.c"),
            prb_STR("base/ftpatent.c"),
            prb_STR("base/ftpfr.c"),
            prb_STR("base/ftstroke.c"),
            prb_STR("base/ftsynth.c"),
            prb_STR("base/fttype1.c"),
            prb_STR("base/ftwinfnt.c"),

            // Font drivers
            prb_STR("bdf/bdf.c"),
            prb_STR("cff/cff.c"),
            prb_STR("cid/type1cid.c"),
            prb_STR("pcf/pcf.c"),
            prb_STR("pfr/pfr.c"),
            prb_STR("sfnt/sfnt.c"),
            prb_STR("truetype/truetype.c"),
            prb_STR("type1/type1.c"),
            prb_STR("type42/type42.c"),
            prb_STR("winfonts/winfnt.c"),

            // Rasterisers
            prb_STR("raster/raster.c"),
            prb_STR("sdf/sdf.c"),
            prb_STR("smooth/smooth.c"),
            prb_STR("svg/svg.c"),

            // Auxillary
            prb_STR("autofit/autofit.c"),
            prb_STR("cache/ftcache.c"),
            prb_STR("gzip/ftgzip.c"),
            prb_STR("lzw/ftlzw.c"),
            prb_STR("bzip2/ftbzip2.c"),
            prb_STR("gxvalid/gxvalid.c"),
            prb_STR("otvalid/otvalid.c"),
            prb_STR("psaux/psaux.c"),
            prb_STR("pshinter/pshinter.c"),
            prb_STR("psnames/psnames.c"),
        };

        prb_String flags[] = {freetypeIncludeFlag, prb_STR("-DFT2_BUILD_LIBRARY")};

        freetypeCompileHandle = prb_addStep(
            prb_compileStaticLibrary,
            (prb_StepData
            ) {.kind = prb_StepDataKind_Compile,
               .compile =
                   {
                       .dir = prb_pathJoin(freetypeDownloadDir, prb_STR("src")),
                       .sources = sources,
                       .sourcesCount = prb_arrayLength(sources),
                       .flags = flags,
                       .flagsCount = prb_arrayLength(flags),
                   }}
        );

        prb_setDependency(freetypeCompileHandle, freetypeDownloadHandle);
    }

    {
        prb_String sources[] = {prb_STR("example.c")};

        prb_StepHandle exeCompileHandle = prb_addStep(
            prb_compileExecutable,
            (prb_StepData) {
                .kind = prb_StepDataKind_Compile,
                .compile = {.sources = sources, .sourcesCount = prb_arrayLength(sources)},
            }
        );

        prb_setDependency(exeCompileHandle, freetypeCompileHandle);
    }

    prb_run();
}
