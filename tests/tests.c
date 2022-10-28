#define prb_AssertAction_PrintAndCrash
#include "../programmable_build.h"

#define function static

typedef int32_t i32;
typedef size_t  usize;

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
test_getParentDir(void) {
    prb_String cwd = prb_getCurrentWorkingDir();

    // NOTE(sen) Given a name with no parent - return current working dir
    {
        prb_String cases[] = {"test", "test/", "test\\"};
        for (usize testIndex = 0; testIndex < prb_arrayLength(cases); testIndex++) {
            prb_assert(prb_streq(prb_getParentDir(cases[testIndex]), cwd));
        }
    }

    // TODO(khvorov) More cases
}

function void
test_fileformat(void) {
    prb_Bytes        fileContents = prb_readEntireFile("programmable_build.h");
    prb_LineIterator lineIter = prb_createLineIter(fileContents.data, fileContents.len);
    while (prb_lineIterNext(&lineIter) == prb_CompletionStatus_Success) {
        // TODO(khvorov) Make sure sections and functions in the header match the ones in the implementation
        if (prb_strStartsWith(lineIter.line, "// SECTION")) {
            prb_writeToStdout(lineIter.line, lineIter.lineLen);
            prb_writeToStdout("\n", 1);
        }
    }
}

int
main() {
    prb_init(1 * prb_GIGABYTE);

    test_getParentDir();
    test_fileformat();

    test_printColor();

    return 0;
}
