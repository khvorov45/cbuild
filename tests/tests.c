#include "../programmable_build.h"

#define function static

typedef uint8_t u8;
typedef int32_t i32;
typedef size_t  usize;

function void
test_memeq(void) {
    const char* p1 = "test1";
    const char* p2 = "test12";
    prb_assert(prb_memeq(p1, p2, prb_strlen(p1)));
    prb_assert(!prb_memeq(p1, p2, prb_strlen(p2)));
}

function void
test_alignment() {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_assert(prb_getOffsetForAlignment((void*)0, 1) == 0);
    prb_assert(prb_getOffsetForAlignment((void*)1, 1) == 0);
    prb_assert(prb_getOffsetForAlignment((void*)1, 2) == 1);
    prb_assert(prb_getOffsetForAlignment((void*)1, 4) == 3);
    prb_assert(prb_getOffsetForAlignment((void*)2, 4) == 2);
    prb_assert(prb_getOffsetForAlignment((void*)3, 4) == 1);
    prb_assert(prb_getOffsetForAlignment((void*)4, 4) == 0);

    i32 arbitraryAlignment = 16;
    prb_globalArenaAlignFreePtr(arbitraryAlignment);
    prb_assert(prb_getOffsetForAlignment(prb_globalArenaCurrentFreePtr(), arbitraryAlignment) == 0);
    prb_globalArenaChangeUsed(1);
    prb_assert(prb_getOffsetForAlignment(prb_globalArenaCurrentFreePtr(), arbitraryAlignment) == arbitraryAlignment - 1);
    prb_globalArenaAlignFreePtr(arbitraryAlignment);
    prb_assert(prb_getOffsetForAlignment(prb_globalArenaCurrentFreePtr(), arbitraryAlignment) == 0);

    prb_endTempMemory(temp);
}

function void
test_allocation() {
    prb_TempMemory temp1 = prb_beginTempMemory();

    {
        prb_TempMemory temp2 = prb_beginTempMemory();

        i32 arbitrarySize = 100;
        u8* ptr = (u8*)prb_allocAndZero(arbitrarySize, 1);
        u8  arbitraryValue = 12;
        ptr[0] = arbitraryValue;

        prb_assert(prb_globalArena.used == temp2.usedAtBegin + arbitrarySize);
        prb_endTempMemory(temp2);
        prb_assert(prb_globalArena.used == temp2.usedAtBegin);

        ptr = (u8*)prb_globalArenaCurrentFreePtr();
        prb_assert(ptr[0] == arbitraryValue);
        prb_assert(ptr == (u8*)prb_allocAndZero(1, 1));
        prb_assert(ptr[0] == 0);
    }

    {
        u8* arr = 0;
        i32 arbitraryCapacity = 10;
        arrsetcap(arr, arbitraryCapacity);
        i32 targetLen = arbitraryCapacity * 2;
        for (i32 index = 0; index < targetLen; index++) {
            arrput(arr, index);
        }
        prb_assert(arrlen(arr) == targetLen);
        for (i32 index = 0; index < targetLen; index++) {
            prb_assert(arr[index] == index);
        }
    }

    prb_endTempMemory(temp1);
}

function void
test_printColor(void) {
    prb_TempMemory temp = prb_beginTempMemory();
    prb_fmtAndPrintln("color printing:");
    prb_fmtAndPrintlnColor(prb_ColorID_Blue, "blue");
    prb_fmtAndPrintlnColor(prb_ColorID_Cyan, "cyan");
    prb_fmtAndPrintlnColor(prb_ColorID_Magenta, "magenta");
    prb_fmtAndPrintlnColor(prb_ColorID_Yellow, "yellow");
    prb_fmtAndPrintlnColor(prb_ColorID_Red, "red");
    prb_fmtAndPrintlnColor(prb_ColorID_Green, "green");
    prb_fmtAndPrintlnColor(prb_ColorID_Black, "black");
    prb_fmtAndPrintlnColor(prb_ColorID_White, "white");
    prb_endTempMemory(temp);
}

function void
test_pathManip(void) {
    prb_TempMemory temp = prb_beginTempMemory();
    prb_String     cwd = prb_getCurrentWorkingDir();
    const char*    cases[] = {
           "test",
           "test/",
           "test/child",
           "test/child/child2",
#if prb_PLATFORM_WINDOWS
        "test\\",
        "test\\child",
        "test/child\\child2",
        "C:",
        "C://",
        "C:/",
        "C:\\",
        "C:\\\\",
        "//network"
#elif prb_PLATFORM_LINUX
        "/home",
        "/home/",
        "/",
#endif
    };

    const char* correctParent[] = {
        cwd.str,
        cwd.str,
        "test/",
        "test/child/",
#if prb_PLATFORM_WINDOWS
        cwd.str,
        "test\\",
        "test/child\\",
        "C:",
        "C://",
        "C:/",
        "C:\\",
        "C:\\\\",
        "//network"
#elif prb_PLATFORM_LINUX
        "/",
        "/",
        "/",
#else
#error unimplemented
#endif
    };

    const char* correctLast[] = {
        "test",
        "test/",
        "child",
        "child2",
#if prb_PLATFORM_WINDOWS
        "test\\",
        "child",
        "child2",
        "C:",
        "C://",
        "C:/",
        "C:\\",
        "C:\\\\",
        "//network"
#elif prb_PLATFORM_LINUX
        "home",
        "home/",
        "/",
#endif
    };

    prb_assert(prb_arrayLength(cases) == prb_arrayLength(correctParent));
    for (usize testIndex = 0; testIndex < prb_arrayLength(cases); testIndex++) {
        const char* cs = cases[testIndex];
        prb_String  resp = prb_getParentDir(prb_STR(cs));
        const char* cr = correctParent[testIndex];
        prb_assert(prb_streq(resp, prb_STR(cr)));
    }

    prb_assert(prb_arrayLength(cases) == prb_arrayLength(correctLast));
    for (usize testIndex = 0; testIndex < prb_arrayLength(cases); testIndex++) {
        const char* cs = cases[testIndex];
        prb_String  resp = prb_getLastEntryInPath(prb_STR(cs));
        const char* cr = correctLast[testIndex];
        prb_assert(prb_streq(resp, prb_STR(cr)));
    }

    prb_assert(prb_streq(prb_pathJoin(prb_STR("a"), prb_STR("b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(prb_STR("a/"), prb_STR("b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(prb_STR("a"), prb_STR("/b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(prb_STR("a/"), prb_STR("/b")), prb_STR("a/b")));

#if prb_PLATFORM_WINDOWS
    prb_assert(prb_streq(prb_pathJoin(prb_STR("a\\"), prb_STR("b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(prb_STR("a"), prb_STR("\\b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(prb_STR("a\\"), prb_STR("\\b")), prb_STR("a/b")));
#elif prb_PLATFORM_LINUX
    prb_assert(prb_streq(prb_pathJoin(prb_STR("a\\"), prb_STR("b")), prb_STR("a\\/b")));
    prb_assert(prb_streq(prb_pathJoin(prb_STR("a"), prb_STR("\\b")), prb_STR("a/\\b")));
    prb_assert(prb_streq(prb_pathJoin(prb_STR("a\\"), prb_STR("\\b")), prb_STR("a\\/\\b")));
#endif

    prb_endTempMemory(temp);
}

function void
test_filesystem(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_String cwd = prb_getCurrentWorkingDir();
    prb_assert(prb_isDirectory(cwd));
    prb_assert(!prb_isFile(cwd));
    prb_assert(prb_isFile(prb_STR(__FILE__)));
    prb_assert(!prb_isDirectory(prb_STR(__FILE__)));

    prb_String dir = prb_pathJoin(prb_getParentDir(prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_clearDirectory(dir);
    prb_assert(prb_directoryIsEmpty(dir));
    prb_String dirWithTrailingSlash = prb_fmt("%.*s/", prb_LIT(dir));
    prb_assert(prb_isDirectory(dirWithTrailingSlash));

    prb_String file = prb_pathJoin(dir, prb_STR("temp.txt"));
    prb_writeEntireFile(file, (prb_Bytes) {(u8*)"1", 1});
    prb_assert(!prb_directoryIsEmpty(dir));
    prb_assert(prb_isFile(file));
    i32   usedBefore = prb_globalArena.used;
    char* ptr = (char*)prb_globalArenaCurrentFreePtr();
    ptr[0] = '2';
    ptr[1] = '2';
    prb_Bytes fileRead = prb_readEntireFile(file);
    prb_assert(prb_globalArena.used == usedBefore + 2);
    prb_assert(prb_streq((prb_String) {(const char*)fileRead.data, fileRead.len}, prb_STR("1")));
    prb_assert(fileRead.len == 1);
    prb_removeFileIfExists(file);
    prb_assert(!prb_isFile(file));
    prb_assert(prb_directoryIsEmpty(dir));

    prb_removeDirectoryIfExists(dir);
    prb_assert(!prb_isDirectory(dir));
    prb_assert(!prb_isFile(dir));

    prb_endTempMemory(temp);
}

function void
test_strings(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_String str = prb_beginString();
    prb_addStringSegment(&str, "%s", "one");
    prb_addStringSegment(&str, "%s", " two");
    prb_addStringSegment(&str, "%s", " three");
    prb_endString();

    prb_String target = prb_STR("one two three");
    prb_assert(prb_streq(str, target));
    prb_assert(prb_globalArena.used == temp.usedAtBegin + target.len + 1);

    prb_endTempMemory(temp);
}

function void
test_strFindIter(void) {
    prb_String         str = prb_STR("prog arg1:val1 arg2:val2 arg3:val3");
    prb_StringFindSpec spec = {
        .str = str,
        .pattern = prb_STR(":"),
        .mode = prb_StringFindMode_Exact,
        .direction = prb_StringDirection_FromStart,
    };

    {
        prb_StrFindIterator iter = prb_createStrFindIter(spec);

        prb_assert(prb_strFindIterNext(&iter) == prb_Success);
        prb_assert(iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == prb_strlen("prog arg1"));
        prb_assert(iter.curResult.matchLen == 1);

        prb_assert(prb_strFindIterNext(&iter) == prb_Success);
        prb_assert(iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == prb_strlen("prog arg1:val1 arg2"));
        prb_assert(iter.curResult.matchLen == 1);

        prb_assert(prb_strFindIterNext(&iter) == prb_Success);
        prb_assert(iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == prb_strlen("prog arg1:val1 arg2:val2 arg3"));
        prb_assert(iter.curResult.matchLen == 1);

        prb_assert(prb_strFindIterNext(&iter) == prb_Failure);
        prb_assert(!iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == 0);
        prb_assert(iter.curResult.matchLen == 0);
    }

    {
        spec.direction = prb_StringDirection_FromEnd;
        prb_StrFindIterator iter = prb_createStrFindIter(spec);

        prb_assert(prb_strFindIterNext(&iter) == prb_Success);
        prb_assert(iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == prb_strlen("prog arg1:val1 arg2:val2 arg3"));
        prb_assert(iter.curResult.matchLen == 1);

        prb_assert(prb_strFindIterNext(&iter) == prb_Success);
        prb_assert(iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == prb_strlen("prog arg1:val1 arg2"));
        prb_assert(iter.curResult.matchLen == 1);

        prb_assert(prb_strFindIterNext(&iter) == prb_Success);
        prb_assert(iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == prb_strlen("prog arg1"));
        prb_assert(iter.curResult.matchLen == 1);

        prb_assert(prb_strFindIterNext(&iter) == prb_Failure);
        prb_assert(!iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == 0);
        prb_assert(iter.curResult.matchLen == 0);

        spec.direction = prb_StringDirection_FromStart;
    }
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
    prb_TempMemory temp = prb_beginTempMemory();

    prb_Bytes        fileContents = prb_readEntireFile(prb_STR("programmable_build.h"));
    prb_LineIterator lineIter = prb_createLineIter((prb_String) {(const char*)fileContents.data, fileContents.len});

    prb_String* headerNames = 0;
    while (prb_lineIterNext(&lineIter) == prb_Success) {
        prb_assert(!prb_strStartsWith(lineIter.curLine, prb_STR("prb_PUBLICDEF"), prb_StringFindMode_Exact));

        if (prb_strStartsWith(lineIter.curLine, prb_STR("// SECTION"), prb_StringFindMode_Exact)) {
            prb_String name = prb_fmt("%.*s", lineIter.curLine.len, lineIter.curLine.str);
            arrput(headerNames, name);
        } else if (prb_strStartsWith(lineIter.curLine, prb_STR("prb_PUBLICDEC"), prb_StringFindMode_Exact)) {
            prb_StringFindResult onePastNameEndRes = prb_strFind((prb_StringFindSpec) {
                .str = lineIter.curLine,
                .pattern = prb_STR("("),
                .mode = prb_StringFindMode_AnyChar,
                .direction = prb_StringDirection_FromStart,
            });
            prb_assert(onePastNameEndRes.found);
            prb_String           win = {lineIter.curLine.str, onePastNameEndRes.matchByteIndex};
            prb_StringFindResult nameStartRes = prb_strFind((prb_StringFindSpec) {
                .str = win,
                .pattern = prb_STR(" "),
                .mode = prb_StringFindMode_AnyChar,
                .direction = prb_StringDirection_FromEnd,
            });
            prb_assert(nameStartRes.found);
            win = prb_strSliceForward(win, nameStartRes.matchByteIndex + 1);
            prb_String name = prb_fmt("%.*s", win.len, win.str);
            arrput(headerNames, name);
        } else if (prb_strStartsWith(lineIter.curLine, prb_STR("#ifdef prb_IMPLEMENTATION"), prb_StringFindMode_Exact)) {
            break;
        }
    }

    prb_String* implNames = 0;
    while (prb_lineIterNext(&lineIter) == prb_Success) {
        prb_assert(!prb_strStartsWith(lineIter.curLine, prb_STR("prb_PUBLICDEC"), prb_StringFindMode_Exact));

        if (prb_strStartsWith(lineIter.curLine, prb_STR("// SECTION"), prb_StringFindMode_Exact)) {
            prb_StringFindResult implementationRes = prb_strFind((prb_StringFindSpec) {
                .str = lineIter.curLine,
                .pattern = prb_STR(" (implementation)"),
                .mode = prb_StringFindMode_Exact,
                .direction = prb_StringDirection_FromStart,
            }
            );
            prb_assert(implementationRes.found);
            prb_String name = prb_fmt("%.*s", implementationRes.matchByteIndex, lineIter.curLine.str);
            arrput(implNames, name);
        } else if (prb_strStartsWith(lineIter.curLine, prb_STR("prb_PUBLICDEF"), prb_StringFindMode_Exact)) {
            prb_assert(prb_lineIterNext(&lineIter) == prb_Success);
            prb_assert(prb_strStartsWith(lineIter.curLine, prb_STR("prb_"), prb_StringFindMode_Exact));
            prb_StringFindResult nameLenRes = prb_strFind((prb_StringFindSpec) {
                .str = lineIter.curLine,
                .pattern = prb_STR("("),
                .mode = prb_StringFindMode_AnyChar,
                .direction = prb_StringDirection_FromStart,
            });
            prb_assert(nameLenRes.found);
            prb_String name = prb_fmt("%.*s", nameLenRes.matchByteIndex, lineIter.curLine.str);
            arrput(implNames, name);
        }
    }

    prb_String* headerNotInImpl = setdiff(headerNames, implNames);
    if (arrlen(headerNotInImpl) > 0) {
        prb_fmtAndPrintln("names in header but not in impl:");
        for (i32 index = 0; index < arrlen(headerNotInImpl); index++) {
            prb_String str = headerNotInImpl[index];
            prb_writeToStdout(str);
            prb_writeToStdout(prb_STR("\n"));
        }
    }
    prb_assert(arrlen(headerNotInImpl) == 0);

    prb_String* implNotInHeader = setdiff(implNames, headerNames);
    if (arrlen(implNotInHeader) > 0) {
        prb_fmtAndPrintln("names in impl but not in header:");
        for (i32 index = 0; index < arrlen(implNotInHeader); index++) {
            prb_String str = implNotInHeader[index];
            prb_writeToStdout(str);
            prb_writeToStdout(prb_STR("\n"));
        }
    }
    prb_assert(arrlen(implNotInHeader) == 0);

    for (i32 index = 0; index < arrlen(headerNames); index++) {
        prb_String headerName = headerNames[index];
        prb_String implName = implNames[index];
        prb_assert(prb_streq(headerName, implName));
    }

    prb_endTempMemory(temp);
}

function void
test_strFind(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_StringFindSpec spec = {
        .str = prb_STR("p1at4pattern1 pattern2 pattern3p2a.t"),
        .pattern = prb_STR("pattern"),
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

    spec.str = prb_STR("p1at4pat1ern1 pat1ern2 pat1ern3p2a.p");
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

    spec.str = prb_STR("中华人民共和国是目前世界上人口最多的国家");
    spec.pattern = prb_STR("民共和国");
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

    spec.str = prb_STR("prb_PUBLICDEC prb_StringWindow prb_createStringWindow(void* ptr, i32 len)");
    spec.pattern = prb_STR("prb_[^[:space:]]*\\(");
    spec.mode = prb_StringFindMode_RegexPosix;
    {
        prb_StringFindResult res = prb_strFind(spec);
        prb_assert(
            res.found
            && res.matchByteIndex == prb_strlen("prb_PUBLICDEC prb_StringWindow ")
            && res.matchLen == prb_strlen("prb_createStringWindow(")
        );
    }

    spec.str.str = "prb_one() prb_2()";
    {
        spec.direction = prb_StringDirection_FromEnd;
        prb_StringFindResult res = prb_strFind(spec);
        prb_assert(
            res.found
            && res.matchByteIndex == prb_strlen("prb_one() ")
            && res.matchLen == prb_strlen("prb_2(")
        );
        spec.direction = prb_StringDirection_FromStart;
    }

    prb_endTempMemory(temp);
}

function void
test_strStartsEnds(void) {
    prb_assert(prb_strStartsWith(prb_STR("123abc"), prb_STR("123"), prb_StringFindMode_Exact));
    prb_assert(!prb_strStartsWith(prb_STR("123abc"), prb_STR("abc"), prb_StringFindMode_Exact));
    prb_assert(!prb_strEndsWith(prb_STR("123abc"), prb_STR("123"), prb_StringFindMode_Exact));
    prb_assert(prb_strEndsWith(prb_STR("123abc"), prb_STR("abc"), prb_StringFindMode_Exact));
}

function void
test_pathsInDir(void) {
    prb_String dirname = prb_pathJoin(prb_getParentDir(prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_clearDirectory(dirname);
    const char* fileNames[] = {"file1.c", "file2.c", "file1.h", "file2.h"};
    i32         fileCount = prb_arrayLength(fileNames);
    prb_String* filePaths = 0;
    arrsetcap(filePaths, fileCount);
    for (i32 fileIndex = 0; fileIndex < fileCount; fileIndex++) {
        prb_String filename = prb_STR(fileNames[fileIndex]);
        prb_String filepath = prb_pathJoin(dirname, filename);
        arrput(filePaths, filepath);
        prb_writeEntireFile(filepath, (prb_Bytes) {(u8*)filename.str, filename.len});
    }

    prb_String* allFiles = prb_listAllEntriesInDir(dirname);
    prb_assert(arrlen(allFiles) == fileCount);
    for (i32 fileIndex = 0; fileIndex < fileCount; fileIndex++) {
        prb_String filename = allFiles[fileIndex];
        prb_assert(
            prb_strEndsWith(filename, prb_STR("file1.c"), prb_StringFindMode_Exact)
            || prb_strEndsWith(filename, prb_STR("file2.c"), prb_StringFindMode_Exact)
            || prb_strEndsWith(filename, prb_STR("file1.h"), prb_StringFindMode_Exact)
            || prb_strEndsWith(filename, prb_STR("file2.h"), prb_StringFindMode_Exact)
        );
    }

    prb_String* cfiles = prb_findAllMatchingPaths(prb_pathJoin(dirname, prb_STR("*c")));
    prb_assert(arrlen(cfiles) == 2);
    for (i32 fileIndex = 0; fileIndex < 2; fileIndex++) {
        prb_String filename = cfiles[fileIndex];
        prb_assert(
            prb_strEndsWith(filename, prb_STR("file1.c"), prb_StringFindMode_Exact)
            || prb_strEndsWith(filename, prb_STR("file2.c"), prb_StringFindMode_Exact)
        );
    }

    prb_removeDirectoryIfExists(dirname);
}

function void
test_lineIter(void) {
    prb_String       lines = prb_STR("line1\r\nline2\nline3\rline4\n\nline6\r\rline8\r\n\r\nline10\r\n\nline12\r\r\nline14");
    prb_LineIterator iter = prb_createLineIter(lines);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line1"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 2);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line2"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 1);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line3"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 1);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line4"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 1);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 1);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line6"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 1);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 1);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line8"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 2);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 2);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line10"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 6);
    prb_assert(iter.curLineEndLen == 2);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 1);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line12"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 6);
    prb_assert(iter.curLineEndLen == 1);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 2);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line14"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 6);
    prb_assert(iter.curLineEndLen == 0);

    prb_assert(prb_lineIterNext(&iter) == prb_Failure);

    lines = prb_STR("\n");
    iter = prb_createLineIter(lines);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 1);

    prb_assert(prb_lineIterNext(&iter) == prb_Failure);
}

int
main() {
    prb_TimeStart testStart = prb_timeStart();
    prb_init(1 * prb_GIGABYTE);
    void* baseStart = prb_globalArena.base;
    prb_assert(prb_globalArena.tempCount == 0);

    test_memeq();
    test_alignment();
    test_allocation();
    test_pathManip();
    test_filesystem();
    test_strings();
    test_fileformat();
    test_strFind();
    test_strFindIter();
    test_strStartsEnds();
    test_lineIter();
    test_pathsInDir();

    test_printColor();

    prb_assert(prb_globalArena.tempCount == 0);
    prb_assert(prb_globalArena.base == baseStart);

    prb_fmtAndPrintln("tests took %.2fms", prb_getMsFrom(testStart));
    prb_terminate(0);
    prb_assert(!"unreachable");
    return 0;
}
