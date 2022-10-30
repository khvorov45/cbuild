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

    prb_String* headerSectionNames = 0;
    prb_String* implSectionNames = 0;

    while (prb_lineIterNext(&lineIter) == prb_CompletionStatus_Success) {
        if (prb_strStartsWith(lineIter.line, "// SECTION")) {
            i32 nameOffset = prb_strlen("// SECTION ");
            prb_assert(lineIter.lineLen > nameOffset);  // NOTE(khvorov) Make sure there is a name
            char* nameStart = lineIter.line + nameOffset;
            i32   nameLen = lineIter.lineLen - nameOffset;
            i32   implementationIndex = prb_strFindIndex(
                nameStart,
                nameLen,
                " (implementation)",
                prb_StringFindMode_Exact,
                prb_StringFindDir_FromStart
            );
            if (implementationIndex != -1) {
                nameLen = implementationIndex;
            }

            prb_String name = prb_fmt("%.*s", nameLen, nameStart);
            if (implementationIndex != -1) {
                stbds_arrput(implSectionNames, name);
            } else {
                stbds_arrput(headerSectionNames, name);
            }
        }

        // TODO(khvorov) Make sure functions in the header match the ones in the implementation
    }

    prb_assert(stbds_arrlen(headerSectionNames) == stbds_arrlen(implSectionNames));

    for (i32 sectionIndex = 0; sectionIndex < stbds_arrlen(implSectionNames); sectionIndex++) {
        prb_String headerName = headerSectionNames[sectionIndex];
        prb_String implName = implSectionNames[sectionIndex];
        prb_assert(prb_streq(headerName, implName));
    }
}

int
main() {
    prb_TimeStart testStart = prb_timeStart();
    prb_init(1 * prb_GIGABYTE);

    test_getParentDir();
    test_fileformat();

    test_printColor();

    prb_fmtAndPrintln("tests took %.2fms", prb_getMsFrom(testStart));
    prb_terminate(0);
    prb_assert(!"unreachable");
    return 0;
}
