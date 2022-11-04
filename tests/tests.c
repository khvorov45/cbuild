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
            prb_StringFindResult onePastNameEndRes = prb_strFind((prb_StringFindSpec) {
                .str = lineIter.line,
                .strLen = lineIter.lineLen,
                .pattern = "(",
                .patternLen = 1,
                .mode = prb_StringFindMode_AnyChar,
                .direction = prb_StringDirection_FromStart,
            });
            prb_assert(onePastNameEndRes.found);
            prb_StringWindow     win = prb_createStringWindow(lineIter.line, onePastNameEndRes.matchByteIndex);
            prb_StringFindResult nameStartRes = prb_strFind((prb_StringFindSpec) {
                .str = win.cur,
                .strLen = win.curLen,
                .pattern = " ",
                .patternLen = 1,
                .mode = prb_StringFindMode_AnyChar,
                .direction = prb_StringDirection_FromEnd,
            });
            prb_assert(nameStartRes.found);
            prb_strWindowForward(&win, nameStartRes.matchByteIndex + 1);
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
            prb_StringFindResult implementationRes = prb_strFind((prb_StringFindSpec) {
                .str = lineIter.line,
                .strLen = lineIter.lineLen,
                .pattern = " (implementation)",
                .patternLen = -1,
                .mode = prb_StringFindMode_Exact,
                .direction = prb_StringDirection_FromStart,
            }
            );
            prb_assert(implementationRes.found);
            prb_String name = prb_fmt("%.*s", implementationRes.matchByteIndex, lineIter.line);
            arrput(implNames, name);
        } else if (prb_strStartsWith(lineIter.line, "prb_PUBLICDEF")) {
            prb_assert(prb_lineIterNext(&lineIter) == prb_CompletionStatus_Success);
            prb_assert(prb_strStartsWith(lineIter.line, "prb_"));
            prb_StringFindResult nameLenRes = prb_strFind((prb_StringFindSpec) {
                .str = lineIter.line,
                .strLen = lineIter.lineLen,
                .pattern = "(",
                .patternLen = 1,
                .mode = prb_StringFindMode_AnyChar,
                .direction = prb_StringDirection_FromStart,
            });
            prb_assert(nameLenRes.found);
            prb_String name = prb_fmt("%.*s", nameLenRes.matchByteIndex, lineIter.line);
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
test_strFind(void) {
    prb_StringFindSpec spec = {
        .str = "p1at4pattern1 pattern2 pattern3p2a.t",
        .strLen = -1,
        .pattern = "pattern",
        .patternLen = -1,
        .mode = prb_StringFindMode_Exact,
        .direction = prb_StringDirection_FromStart,
    };

    {
        prb_StringFindResult res = prb_strFind(spec);
        prb_assert(res.found && res.matchByteIndex == 5 && res.matchLen == 7);
    }

    {
        spec.direction = prb_StringDirection_FromEnd;
        prb_StringFindResult res = prb_strFind(spec);
        prb_assert(res.found && res.matchByteIndex == 23 && res.matchLen == 7);
        spec.direction = prb_StringDirection_FromStart;
    }

    spec.str = "p1at4pat1ern1 pat1ern2 pat1ern3p2a.p";
    {
        prb_StringFindResult res = prb_strFind(spec);
        prb_assert(!res.found && res.matchByteIndex == 0 && res.matchLen == 0);
    }

    {
        spec.direction = prb_StringDirection_FromEnd;
        prb_StringFindResult res = prb_strFind(spec);
        prb_assert(!res.found && res.matchByteIndex == 0 && res.matchLen == 0);
        spec.direction = prb_StringDirection_FromStart;
    }

    spec.str = "中华人民共和国是目前世界上人口最多的国家";
    spec.pattern = "民共和国";
    {
        prb_StringFindResult res = prb_strFind(spec);
        prb_assert(res.found && res.matchByteIndex == 3 * 3 && res.matchLen == 4 * 3);
        spec.direction = prb_StringDirection_FromEnd;
        res = prb_strFind(spec);
        prb_assert(res.found && res.matchByteIndex == 3 * 3 && res.matchLen == 4 * 3);
        spec.direction = prb_StringDirection_FromStart;
    }

    {
        spec.mode = prb_StringFindMode_AnyChar;
        prb_StringFindResult res = prb_strFind(spec);
        prb_assert(res.found && res.matchByteIndex == 3 * 3 && res.matchLen == 1 * 3);
        spec.direction = prb_StringDirection_FromEnd;
        res = prb_strFind(spec);
        prb_assert(res.found && res.matchByteIndex == 18 * 3 && res.matchLen == 1 * 3);
        spec.direction = prb_StringDirection_FromStart;
        spec.mode = prb_StringFindMode_Exact;
    }
}

int
main() {
    prb_TimeStart testStart = prb_timeStart();
    prb_init(1 * prb_GIGABYTE);

    test_getParentDir();
    test_fileformat();
    test_strFind();

    test_printColor();

    prb_fmtAndPrintln("tests took %.2fms", prb_getMsFrom(testStart));
    prb_terminate(0);
    prb_assert(!"unreachable");
    return 0;
}
