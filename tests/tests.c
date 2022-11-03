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

function prb_String*
setdiff(prb_String* arr1, prb_String* arr2) {
    prb_String* result = 0;
    for (i32 arr1Index = 0; arr1Index < arrlen(arr1); arr1Index++) {
        prb_String str1 = arr1[arr1Index];
        bool       foundIn2 = false;
        for (i32 arr2Index = 0; arr2Index < arrlen(arr2); arr2Index++) {
            prb_String str2 = arr2[arr2Index];
            if (prb_streq(str1, str2)) {
                foundIn2 = true;
                break;
            }
        }
        if (!foundIn2) {
            arrput(result, str1);
        }
    }
    return result;
}

function void
test_fileformat(void) {
    prb_Bytes        fileContents = prb_readEntireFile("programmable_build.h");
    prb_LineIterator lineIter = prb_createLineIter(fileContents.data, fileContents.len);

    prb_String* headerNames = 0;
    while (prb_lineIterNext(&lineIter) == prb_CompletionStatus_Success) {
        prb_assert(!prb_strStartsWith(lineIter.line, "prb_PUBLICDEF"));

        if (prb_strStartsWith(lineIter.line, "// SECTION")) {
            prb_String name = prb_fmt("%.*s", lineIter.lineLen, lineIter.line);
            arrput(headerNames, name);
        } else if (prb_strStartsWith(lineIter.line, "prb_PUBLICDEC")) {
            i32 onePastNameEnd =
                prb_strFindByteIndex(lineIter.line, lineIter.lineLen, "(", prb_StringFindMode_AnyChar, prb_StringFindDir_FromStart);
            prb_assert(onePastNameEnd != -1);
            prb_StringWindow win = prb_createStringWindow(lineIter.line, onePastNameEnd);
            i32              nameStart =
                prb_strFindByteIndex(win.cur, win.curLen, " ", prb_StringFindMode_AnyChar, prb_StringFindDir_FromEnd);
            prb_assert(nameStart != -1);
            prb_strWindowForward(&win, nameStart + 1);
            prb_String name = prb_fmt("%.*s", win.curLen, win.cur);
            arrput(headerNames, name);
        } else if (prb_strStartsWith(lineIter.line, "#ifdef prb_IMPLEMENTATION")) {
            break;
        }
    }

    prb_String* implNames = 0;
    while (prb_lineIterNext(&lineIter) == prb_CompletionStatus_Success) {
        prb_assert(!prb_strStartsWith(lineIter.line, "prb_PUBLICDEC"));

        if (prb_strStartsWith(lineIter.line, "// SECTION")) {
            i32 implementationIndex =
                prb_strFindByteIndex(lineIter.line, lineIter.lineLen, " (implementation)", prb_StringFindMode_Exact, prb_StringFindDir_FromStart);
            prb_assert(implementationIndex != -1);
            prb_String name = prb_fmt("%.*s", implementationIndex, lineIter.line);
            arrput(implNames, name);
        } else if (prb_strStartsWith(lineIter.line, "prb_PUBLICDEF")) {
            prb_assert(prb_lineIterNext(&lineIter) == prb_CompletionStatus_Success);
            prb_assert(prb_strStartsWith(lineIter.line, "prb_"));
            i32 nameLen =
                prb_strFindByteIndex(lineIter.line, lineIter.lineLen, "(", prb_StringFindMode_AnyChar, prb_StringFindDir_FromStart);
            prb_assert(nameLen != -1);
            prb_String name = prb_fmt("%.*s", nameLen, lineIter.line);
            arrput(implNames, name);
        }
    }

    prb_String* headerNotInImpl = setdiff(headerNames, implNames);
    if (arrlen(headerNotInImpl) > 0) {
        prb_fmtAndPrintln("names in header but not in impl:");
        for (i32 index = 0; index < arrlen(headerNotInImpl); index++) {
            prb_String str = headerNotInImpl[index];
            prb_fmtAndPrintln("%s", str);
        }
    }
    prb_assert(arrlen(headerNotInImpl) == 0);

    prb_String* implNotInHeader = setdiff(implNames, headerNames);
    if (arrlen(implNotInHeader) > 0) {
        prb_fmtAndPrintln("names in impl but not in header:");
        for (i32 index = 0; index < arrlen(implNotInHeader); index++) {
            prb_String str = implNotInHeader[index];
            prb_fmtAndPrintln("%s", str);
        }
    }
    prb_assert(arrlen(implNotInHeader) == 0);

    for (i32 index = 0; index < arrlen(headerNames); index++) {
        prb_String headerName = headerNames[index];
        prb_String implName = implNames[index];
        prb_assert(prb_streq(headerName, implName));
    }
}

function void
test_strFindIndex(void) {
    prb_String strWith = "p1at4pattern1 pattern2 pattern3p2a.t";

    {
        i32 patIndex = prb_strFindByteIndex(strWith, -1, "pattern", prb_StringFindMode_Exact, prb_StringFindDir_FromStart);
        prb_assert(patIndex == 5);
    }

    {
        i32 patIndex = prb_strFindByteIndex(strWith, -1, "pattern", prb_StringFindMode_Exact, prb_StringFindDir_FromEnd);
        prb_assert(patIndex == 23);
    }

    prb_String strWithout = "p1at4pat1ern1 pat1ern2 pat1ern3p2a.p";
    {
        i32 patIndex = prb_strFindByteIndex(strWithout, -1, "pattern", prb_StringFindMode_Exact, prb_StringFindDir_FromStart);
        prb_assert(patIndex == -1);
    }

    {
        i32 patIndex = prb_strFindByteIndex(strWithout, -1, "pattern", prb_StringFindMode_Exact, prb_StringFindDir_FromEnd);
        prb_assert(patIndex == -1);
    }
}

int
main() {
    prb_TimeStart testStart = prb_timeStart();
    prb_init(1 * prb_GIGABYTE);

    test_getParentDir();
    test_fileformat();
    test_strFindIndex();

    test_printColor();

    prb_fmtAndPrintln("tests took %.2fms", prb_getMsFrom(testStart));
    prb_terminate(0);
    prb_assert(!"unreachable");
    return 0;
}
