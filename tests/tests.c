#include "../programmable_build.h"

#define function static

function void
test_printColor(void) {
    prb_println("color printing:");
    prb_fmtAndPrintColor(prb_ColorID_Blue, "blue\n");
    prb_fmtAndPrintlnColor(prb_ColorID_Cyan, "cyan");
    prb_printColor(prb_ColorID_Magenta, "magenta\n");
    prb_printlnColor(prb_ColorID_Yellow, "yellow");
    prb_printlnColor(prb_ColorID_Red, "red");
    prb_printlnColor(prb_ColorID_Green, "green");
    prb_printlnColor(prb_ColorID_Black, "black");
    prb_printlnColor(prb_ColorID_White, "white");
}

int
main() {
    prb_init();
    test_printColor();
    prb_printlnColor(prb_ColorID_Green, "all tests passed");
    return 0;
}
