#define prb_AssertAction_PrintAndCrash
#include "../programmable_build.h"

#define function static

typedef int32_t i32;

function void
test_printColor(void) {
    prb_fmtAndPrintln("color printing:");
    prb_fmtAndPrintlnColor(prb_ColorID_Blue, "blue");
    prb_fmtAndPrintlnColor(prb_ColorID_Cyan, "cyan");
    prb_fmtAndPrintlnColor(prb_ColorID_Magenta, "magenta");
    prb_fmtAndPrintlnColor(prb_ColorID_Yellow, "yellow");
    prb_fmtAndPrintlnColor(prb_ColorID_Red, "red");
    prb_fmtAndPrintlnColor(prb_ColorID_Green, "green");
    prb_fmtAndPrintlnColor(prb_ColorID_Black, "black");
    prb_fmtAndPrintlnColor(prb_ColorID_White, "white");
}

function void
test_fileformat(void) {
    // TODO(khvorov) Make sure we can easily get to absolute path from getParentDir and such
    // prb_Bytes fileContents = prb_readEntireFile("programmable_build.h");
    // char*     fileContentsString = (char*)fileContents.data;
    // TODO(khvorov) Implement
}

int
main() {
    prb_init(1 * prb_GIGABYTE);

    test_printColor();
    test_fileformat();

    return 0;
}
