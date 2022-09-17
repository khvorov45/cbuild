#include "../programmable_build.h"

#define function static

function void
test_printColor(void) {
    prb_println(prb_STR("== color printing =="));
    prb_fmtAndPrintColor(prb_ColorID_Blue, "blue\n");
    prb_fmtAndPrintlnColor(prb_ColorID_Cyan, "cyan");
    prb_printColor(prb_ColorID_Magenta, prb_STR("magenta\n"));
    prb_printlnColor(prb_ColorID_Yellow, prb_STR("yellow"));
    prb_printlnColor(prb_ColorID_Red, prb_STR("red"));
    prb_printlnColor(prb_ColorID_Green, prb_STR("green"));
    prb_printlnColor(prb_ColorID_Black, prb_STR("black"));
    prb_printlnColor(prb_ColorID_White, prb_STR("white"));
    prb_println(prb_STR("===="));
}

int
main() {
    prb_init();
    prb_println(prb_STR("== tests =="));
    test_printColor();
    prb_printlnColor(prb_ColorID_Green, prb_STR("all tests passed"));
    return 0;
}
