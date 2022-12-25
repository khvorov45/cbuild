#include "../cbuild.h"

#define function static
#define global_variable static

typedef uint8_t  u8;
typedef uint64_t u64;
typedef int32_t  i32;
typedef uint32_t u32;
typedef size_t   usize;

global_variable prb_Str globalSuffix = prb_STR("");

function prb_Str*
setdiff(prb_Str* arr1, prb_Str* arr2) {
    prb_Str* result = 0;
    for (i32 arr1Index = 0; arr1Index < arrlen(arr1); arr1Index++) {
        prb_Str str1 = arr1[arr1Index];
        bool    foundIn2 = false;
        for (i32 arr2Index = 0; arr2Index < arrlen(arr2); arr2Index++) {
            prb_Str str2 = arr2[arr2Index];
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
testNameToPrbName(prb_Arena* arena, prb_Str testName, prb_Str** prbNames) {
    if (prb_streq(testName, prb_STR("test_pathFindIter"))) {
        arrput(*prbNames, prb_STR("prb_createPathFindIter"));
        arrput(*prbNames, prb_STR("prb_pathFindIterNext"));
        arrput(*prbNames, prb_STR("prb_destroyPathFindIter"));
    } else if (prb_streq(testName, prb_STR("test_utf8CharIter"))) {
        arrput(*prbNames, prb_STR("prb_createUtf8CharIter"));
        arrput(*prbNames, prb_STR("prb_utf8CharIterNext"));
    } else if (prb_streq(testName, prb_STR("test_strScanner"))) {
        arrput(*prbNames, prb_STR("prb_createStrScanner"));
        arrput(*prbNames, prb_STR("prb_strScannerMove"));
    } else if (prb_streq(testName, prb_STR("test_pathEntryIter"))) {
        arrput(*prbNames, prb_STR("prb_createPathEntryIter"));
        arrput(*prbNames, prb_STR("prb_pathEntryIterNext"));
    } else if (prb_streq(testName, prb_STR("test_env"))) {
        arrput(*prbNames, prb_STR("prb_setenv"));
        arrput(*prbNames, prb_STR("prb_getenv"));
        arrput(*prbNames, prb_STR("prb_unsetenv"));
    } else if (prb_streq(testName, prb_STR("test_writeToStdout"))) {
        arrput(*prbNames, prb_STR("prb_writeToStdout"));
        arrput(*prbNames, prb_STR("prb_writelnToStdout"));
        arrput(*prbNames, prb_STR("prb_colorEsc"));
    } else if (prb_streq(testName, prb_STR("test_process"))) {
        arrput(*prbNames, prb_STR("prb_createProcess"));
        arrput(*prbNames, prb_STR("prb_launchProcesses"));
        arrput(*prbNames, prb_STR("prb_waitForProcesses"));
    } else if (prb_streq(testName, prb_STR("test_jobs"))) {
        arrput(*prbNames, prb_STR("prb_createJob"));
        arrput(*prbNames, prb_STR("prb_launchJobs"));
        arrput(*prbNames, prb_STR("prb_waitForJobs"));
    } else {
        prb_assert(prb_strStartsWith(testName, prb_STR("test_")));
        prb_Str noPrefix = prb_strSlice(testName, 5, testName.len);
        prb_Str nameWithPrefix = prb_fmt(arena, "prb_%.*s", prb_LIT(noPrefix));
        arrput(*prbNames, nameWithPrefix);
    }
}

function prb_Str
getTempPath(prb_Arena* arena, const char* funcName) {
    prb_Str funcNameWithNonascii = prb_fmt(arena, "%s太阳😐-%.*s", funcName, prb_LIT(globalSuffix));
    prb_Str dir = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), funcNameWithNonascii);
    return dir;
}

function bool
strIn(prb_Str str, prb_Str* arr, i32 arrc) {
    bool result = false;
    for (i32 ind = 0; ind < arrc && !result; ind++) {
        prb_Str arrv = arr[ind];
        if (prb_streq(str, arrv)) {
            result = true;
        }
    }
    return result;
}

function void
testMacros(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_assert(prb_max(1, 2) == 2);
    prb_assert(prb_min(1, 2) == 1);
    prb_assert(prb_clamp(0, 2, 5) == 2);
    prb_assert(prb_clamp(4, 2, 5) == 4);
    prb_assert(prb_clamp(6, 2, 5) == 5);

    i32 testArr[] = {1, 2, 3};
    prb_assert(prb_arrayCount(testArr) == 3);

    prb_arenaAlignFreePtr(arena, alignof(i32));
    void* ptrBefore = prb_arenaFreePtr(arena);
    i32*  arr = prb_arenaAllocArray(arena, i32, 3);
    prb_assert(arr == ptrBefore);
    void* ptrAfter = prb_arenaFreePtr(arena);
    prb_assert(ptrAfter == (u8*)ptrBefore + sizeof(i32) * 3);

    prb_arenaAlignFreePtr(arena, alignof(prb_Str));
    ptrBefore = prb_arenaFreePtr(arena);
    prb_Str* strPtr = prb_arenaAllocStruct(arena, prb_Str);
    prb_assert(strPtr == ptrBefore);
    ptrAfter = prb_arenaFreePtr(arena);
    prb_assert(ptrAfter == (u8*)ptrBefore + sizeof(prb_Str));

    prb_assert(prb_isPowerOf2(1));
    prb_assert(prb_isPowerOf2(2));
    prb_assert(!prb_isPowerOf2(3));
    prb_assert(prb_isPowerOf2(4));
    prb_assert(!prb_isPowerOf2(5));
    prb_assert(prb_isPowerOf2(8));

    prb_endTempMemory(temp);
}

//
// SECTION Memory
//

function void
test_memeq(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    const char* p1 = "test1";
    const char* p2 = "test12";
    prb_assert(prb_memeq(p1, p2, prb_strlen(p1)));
    prb_assert(!prb_memeq(p1, p2, prb_strlen(p2)));
}

function void
test_getOffsetForAlignment(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_assert(prb_getOffsetForAlignment((void*)1, 1) == 0);
    prb_assert(prb_getOffsetForAlignment((void*)1, 2) == 1);
    prb_assert(prb_getOffsetForAlignment((void*)1, 4) == 3);
    prb_assert(prb_getOffsetForAlignment((void*)2, 4) == 2);
    prb_assert(prb_getOffsetForAlignment((void*)3, 4) == 1);
    prb_assert(prb_getOffsetForAlignment((void*)4, 4) == 0);

    prb_endTempMemory(temp);
}

function void
test_vmemAlloc(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);
    int32_t bytes = 100;
    void*   ptr = prb_vmemAlloc(bytes);
    prb_memset(ptr, 1, bytes);
}

function void
test_createArenaFromVmem(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    int32_t   bytes = 100;
    prb_Arena newArena = prb_createArenaFromVmem(bytes);
    prb_arenaAllocAndZero(&newArena, bytes, 1);
}

function void
test_createArenaFromArena(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);
    int32_t        bytes = 100;
    prb_Arena      newArena = prb_createArenaFromArena(arena, bytes);
    prb_Str        arenaStr = prb_fmt(arena, "arena");
    prb_Str        newArenaStr = prb_fmt(&newArena, "new");
    prb_assert(prb_streq(arenaStr, prb_STR("arena")));
    prb_assert(prb_streq(newArenaStr, prb_STR("new")));
    prb_endTempMemory(temp);
}

function void
test_arenaAllocAndZero(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    i32 arbitrarySize = 100;
    u8* ptr = (u8*)prb_arenaAllocAndZero(arena, arbitrarySize, 1);
    u8  arbitraryValue = 12;
    ptr[0] = arbitraryValue;

    prb_endTempMemory(temp);
    temp = prb_beginTempMemory(arena);

    prb_assert(ptr[0] == arbitraryValue);
    prb_assert(ptr == (u8*)prb_arenaAllocAndZero(arena, 1, 1));
    prb_assert(ptr[0] == 0);

    prb_endTempMemory(temp);
}

function void
test_arenaAlignFreePtr(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    i32 arbitraryAlignment = 16;
    prb_arenaAlignFreePtr(arena, arbitraryAlignment);
    prb_assert(prb_getOffsetForAlignment(prb_arenaFreePtr(arena), arbitraryAlignment) == 0);
    prb_arenaChangeUsed(arena, 1);
    prb_assert(prb_getOffsetForAlignment(prb_arenaFreePtr(arena), arbitraryAlignment) == arbitraryAlignment - 1);
    prb_arenaAlignFreePtr(arena, arbitraryAlignment);
    prb_assert(prb_getOffsetForAlignment(prb_arenaFreePtr(arena), arbitraryAlignment) == 0);

    prb_endTempMemory(temp);
}

function void
test_arenaFreePtr(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    uint8_t* ptrInit = (uint8_t*)prb_arenaFreePtr(arena);
    i32      size = 1;
    prb_arenaAllocAndZero(arena, size, 1);
    prb_assert(prb_arenaFreePtr(arena) == ptrInit + size);

    prb_endTempMemory(temp);
}

function void
test_arenaFreeSize(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    i32 sizeInit = prb_arenaFreeSize(arena);
    i32 size = 1;
    prb_arenaAllocAndZero(arena, size, 1);
    prb_assert(prb_arenaFreeSize(arena) == sizeInit - size);

    prb_endTempMemory(temp);
}

function void
test_arenaChangeUsed(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    i32 init = arena->used;
    i32 delta = 100;
    prb_arenaChangeUsed(arena, delta);
    prb_assert(arena->used == init + delta);
    delta *= -1;
    prb_arenaChangeUsed(arena, delta);
    prb_assert(arena->used == init);

    prb_endTempMemory(temp);
}

function void
test_beginTempMemory(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_assert(temp.usedAtBegin == arena->used);
    prb_assert(temp.tempCountAtBegin == arena->tempCount - 1);
    prb_endTempMemory(temp);
}

function void
test_endTempMemory(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_arenaAllocAndZero(arena, 100, 1);
    prb_endTempMemory(temp);
    prb_assert(arena->used == temp.usedAtBegin);
    prb_assert(arena->tempCount == temp.tempCountAtBegin);
}

//
// SECTION Filesystem
//

function void
test_pathExists(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str dir = getTempPath(arena, __FUNCTION__);
    prb_assert(prb_removeFileOrDirIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_pathExists(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_pathExists(arena, dir));
    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_pathExists(arena, dir));

    prb_Str dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_removeDirIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_pathExists(arena, dirTrailingSlash));
    prb_assert(!prb_pathExists(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(prb_pathExists(arena, dirTrailingSlash));
    prb_assert(prb_pathExists(arena, dir));
    prb_assert(prb_removeDirIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_pathExists(arena, dirTrailingSlash));
    prb_assert(!prb_pathExists(arena, dir));

    prb_Str dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_removeDirIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_pathExists(arena, dirNotNull));
    prb_assert(!prb_pathExists(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dirNotNull) == prb_Success);
    prb_assert(prb_pathExists(arena, dirNotNull));
    prb_assert(prb_pathExists(arena, dir));
    prb_assert(prb_removeDirIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_pathExists(arena, dirNotNull));
    prb_assert(!prb_pathExists(arena, dir));

    prb_Str filepath = getTempPath(arena, __FUNCTION__);
    prb_Str filepathNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(filepath));
    filepathNotNull.len = filepath.len;

    prb_assert(prb_removeFileIfExists(arena, filepath) == prb_Success);
    prb_assert(!prb_pathExists(arena, filepath));
    prb_assert(!prb_pathExists(arena, filepathNotNull));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(prb_pathExists(arena, filepath));
    prb_assert(prb_pathExists(arena, filepathNotNull));
    prb_assert(prb_removeFileIfExists(arena, filepath) == prb_Success);
    prb_assert(!prb_pathExists(arena, filepath));
    prb_assert(!prb_pathExists(arena, filepathNotNull));

    prb_assert(prb_removeFileIfExists(arena, filepathNotNull) == prb_Success);
    prb_assert(!prb_pathExists(arena, filepathNotNull));
    prb_assert(!prb_pathExists(arena, filepath));
    prb_assert(prb_writeEntireFile(arena, filepathNotNull, "1", 1) == prb_Success);
    prb_assert(prb_pathExists(arena, filepathNotNull));
    prb_assert(prb_pathExists(arena, filepath));
    prb_assert(prb_removeFileIfExists(arena, filepathNotNull) == prb_Success);
    prb_assert(!prb_pathExists(arena, filepathNotNull));
    prb_assert(!prb_pathExists(arena, filepath));

    prb_assert(prb_pathExists(arena, prb_STR(__FILE__)));

    prb_endTempMemory(temp);
}

function void
test_pathIsAbsolute(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);

    prb_assert(!prb_pathIsAbsolute(prb_getLastEntryInPath(prb_STR(__FILE__))));

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    prb_assert(prb_pathIsAbsolute(prb_STR("/home")));
    prb_assert(prb_pathIsAbsolute(prb_STR("/nonexistant")));

#else
#error unimplemented
#endif
}

function void
test_getAbsolutePath(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_Str cwd = prb_getWorkingDir(arena);
    prb_Str filename = prb_STR("test.txt");
    prb_Str fileAbs = prb_getAbsolutePath(arena, filename);
    prb_Str fileWithCwd = prb_pathJoin(arena, cwd, filename);
    prb_assert(prb_streq(fileAbs, fileWithCwd));

    // TODO(khvorov) Test trailing slash
    prb_assert(prb_streq(prb_getAbsolutePath(arena, prb_STR("/home")), prb_STR("/home")));
    prb_assert(prb_streq(prb_getAbsolutePath(arena, prb_STR("/nonexistant/file.txt")), prb_STR("/nonexistant/file.txt")));
    prb_assert(prb_streq(prb_getAbsolutePath(arena, prb_STR("dir/file.md")), prb_pathJoin(arena, cwd, prb_STR("dir/file.md"))));
    prb_assert(prb_streq(prb_getAbsolutePath(arena, prb_STR("./file.md")), prb_pathJoin(arena, cwd, prb_STR("file.md"))));
    prb_assert(prb_streq(prb_getAbsolutePath(arena, prb_STR("/path/./file.md")), prb_STR("/path/file.md")));
    prb_assert(prb_streq(prb_getAbsolutePath(arena, prb_STR("/path/../file.md")), prb_STR("/file.md")));
    prb_assert(prb_streq(prb_getAbsolutePath(arena, prb_STR("../file.md")), prb_pathJoin(arena, prb_getParentDir(arena, cwd), prb_STR("file.md"))));
    prb_assert(prb_streq(prb_getAbsolutePath(arena, prb_STR("////")), prb_STR("/")));
    prb_assert(prb_streq(prb_getAbsolutePath(arena, prb_STR("////home///other")), prb_STR("/home/other")));
    prb_assert(prb_streq(prb_getAbsolutePath(arena, prb_STR("home///other")), prb_pathJoin(arena, cwd, prb_STR("home/other"))));
}

function void
test_isDir(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str dir = getTempPath(arena, __FUNCTION__);
    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_isDir(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_isDir(arena, dir));
    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_isDir(arena, dir));

    prb_Str dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_removeDirIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_isDir(arena, dirTrailingSlash));
    prb_assert(!prb_isDir(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(prb_isDir(arena, dirTrailingSlash));
    prb_assert(prb_isDir(arena, dir));
    prb_assert(prb_removeDirIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_isDir(arena, dirTrailingSlash));
    prb_assert(!prb_isDir(arena, dir));

    prb_Str dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_removeDirIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_isDir(arena, dirNotNull));
    prb_assert(!prb_isDir(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dirNotNull) == prb_Success);
    prb_assert(prb_isDir(arena, dirNotNull));
    prb_assert(prb_isDir(arena, dir));
    prb_assert(prb_removeDirIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_isDir(arena, dirNotNull));
    prb_assert(!prb_isDir(arena, dir));

    prb_assert(!prb_isDir(arena, prb_STR(__FILE__)));

    prb_endTempMemory(temp);
}

function void
test_isFile(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str filepath = getTempPath(arena, __FUNCTION__);
    prb_Str filepathNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(filepath));
    filepathNotNull.len = filepath.len;

    prb_assert(prb_removeFileIfExists(arena, filepath) == prb_Success);
    prb_assert(!prb_isFile(arena, filepath));
    prb_assert(!prb_isFile(arena, filepathNotNull));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(prb_isFile(arena, filepath));
    prb_assert(prb_isFile(arena, filepathNotNull));
    prb_assert(prb_removeFileIfExists(arena, filepath) == prb_Success);
    prb_assert(!prb_isFile(arena, filepath));
    prb_assert(!prb_isFile(arena, filepathNotNull));

    prb_assert(prb_removeFileIfExists(arena, filepathNotNull) == prb_Success);
    prb_assert(!prb_isFile(arena, filepathNotNull));
    prb_assert(!prb_isFile(arena, filepath));
    prb_assert(prb_writeEntireFile(arena, filepathNotNull, "1", 1) == prb_Success);
    prb_assert(prb_isFile(arena, filepathNotNull));
    prb_assert(prb_isFile(arena, filepath));
    prb_assert(prb_removeFileIfExists(arena, filepathNotNull) == prb_Success);
    prb_assert(!prb_isFile(arena, filepathNotNull));
    prb_assert(!prb_isFile(arena, filepath));

    prb_assert(prb_isFile(arena, prb_STR(__FILE__)));
    prb_assert(!prb_isFile(arena, prb_getParentDir(arena, prb_STR(__FILE__))));

    prb_endTempMemory(temp);
}

function void
test_dirIsEmpty(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str dir = getTempPath(arena, __FUNCTION__);
    prb_assert(prb_clearDir(arena, dir) == prb_Success);
    prb_assert(prb_dirIsEmpty(arena, dir));
    prb_Str filepath = prb_pathJoin(arena, dir, prb_STR("file.txt"));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(!prb_dirIsEmpty(arena, dir));
    prb_assert(prb_removeFileIfExists(arena, filepath) == prb_Success);
    prb_assert(prb_dirIsEmpty(arena, dir));

    prb_Str dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_dirIsEmpty(arena, dirTrailingSlash));
    prb_assert(prb_dirIsEmpty(arena, dir));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(!prb_dirIsEmpty(arena, dirTrailingSlash));
    prb_assert(!prb_dirIsEmpty(arena, dir));
    prb_assert(prb_removeFileIfExists(arena, filepath) == prb_Success);
    prb_assert(prb_dirIsEmpty(arena, dirTrailingSlash));
    prb_assert(prb_dirIsEmpty(arena, dir));

    prb_Str dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_dirIsEmpty(arena, dirNotNull));
    prb_assert(prb_dirIsEmpty(arena, dir));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(!prb_dirIsEmpty(arena, dirNotNull));
    prb_assert(!prb_dirIsEmpty(arena, dir));
    prb_assert(prb_removeFileIfExists(arena, filepath) == prb_Success);
    prb_assert(prb_dirIsEmpty(arena, dirNotNull));
    prb_assert(prb_dirIsEmpty(arena, dir));

    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);
    prb_endTempMemory(temp);
}

function void
test_createDirIfNotExists(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str dir = getTempPath(arena, __FUNCTION__);
    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_isDir(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_isDir(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_isDir(arena, dir));
    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);

    prb_Str dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_createDirIfNotExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(prb_isDir(arena, dirTrailingSlash));
    prb_assert(prb_isDir(arena, dir));
    prb_assert(prb_removeFileOrDirIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_isDir(arena, dirTrailingSlash));
    prb_assert(!prb_isDir(arena, dir));

    prb_Str dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_createDirIfNotExists(arena, dirNotNull) == prb_Success);
    prb_assert(prb_isDir(arena, dirNotNull));
    prb_assert(prb_isDir(arena, dir));
    prb_assert(prb_removeFileOrDirIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_isDir(arena, dirNotNull));
    prb_assert(!prb_isDir(arena, dir));

    prb_endTempMemory(temp);
}

function void
test_removeFileOrDirIfExists(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str dir = getTempPath(arena, __FUNCTION__);
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);

    prb_Str filepath = prb_pathJoin(arena, dir, prb_STR("file.txt"));
    prb_Str filepathNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(filepath));
    filepathNotNull.len = filepath.len;
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);

    prb_assert(prb_isDir(arena, dir));
    prb_assert(prb_isFile(arena, filepath));

    prb_assert(prb_removeFileOrDirIfExists(arena, filepath) == prb_Success);
    prb_assert(prb_isDir(arena, dir));
    prb_assert(!prb_isFile(arena, filepath));

    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);

    prb_assert(prb_removeFileOrDirIfExists(arena, filepathNotNull) == prb_Success);
    prb_assert(prb_isDir(arena, dir));
    prb_assert(!prb_isFile(arena, filepath));

    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);

    prb_assert(prb_removeFileOrDirIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_isDir(arena, dir));
    prb_assert(!prb_isFile(arena, filepath));

    prb_Str dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_createDirIfNotExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(prb_isDir(arena, dirTrailingSlash));
    prb_assert(prb_isDir(arena, dir));
    prb_assert(prb_removeFileOrDirIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_isDir(arena, dirTrailingSlash));
    prb_assert(!prb_isDir(arena, dir));

    prb_Str dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_createDirIfNotExists(arena, dirNotNull) == prb_Success);
    prb_assert(prb_isDir(arena, dirNotNull));
    prb_assert(prb_isDir(arena, dir));
    prb_assert(prb_removeFileOrDirIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_isDir(arena, dirNotNull));
    prb_assert(!prb_isDir(arena, dir));

    prb_endTempMemory(temp);
}

function void
test_removeFileIfExists(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str dir = getTempPath(arena, __FUNCTION__);
    prb_assert(prb_clearDir(arena, dir) == prb_Success);

    prb_Str filepath = prb_pathJoin(arena, dir, prb_STR("file.txt"));
    prb_Str filepathNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(filepath));
    filepathNotNull.len = filepath.len;

    prb_assert(!prb_isFile(arena, filepath));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(prb_isFile(arena, filepath));
    prb_assert(prb_removeFileIfExists(arena, filepath) == prb_Success);
    prb_assert(!prb_isFile(arena, filepath));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(prb_isFile(arena, filepath));
    prb_assert(prb_removeFileIfExists(arena, filepathNotNull) == prb_Success);
    prb_assert(!prb_isFile(arena, filepath));

    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);

    prb_endTempMemory(temp);
}

function void
test_removeDirIfExists(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str dir = getTempPath(arena, __FUNCTION__);
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_isDir(arena, dir));
    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_isDir(arena, dir));

    prb_Str dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_createDirIfNotExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(prb_isDir(arena, dirTrailingSlash));
    prb_assert(prb_isDir(arena, dir));
    prb_assert(prb_removeDirIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_isDir(arena, dirTrailingSlash));
    prb_assert(!prb_isDir(arena, dir));

    prb_Str dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_createDirIfNotExists(arena, dirNotNull) == prb_Success);
    prb_assert(prb_isDir(arena, dirNotNull));
    prb_assert(prb_isDir(arena, dir));
    prb_assert(prb_removeDirIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_isDir(arena, dirNotNull));
    prb_assert(!prb_isDir(arena, dir));

    prb_endTempMemory(temp);
}

function void
test_clearDir(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str dir = getTempPath(arena, __FUNCTION__);
    prb_assert(prb_clearDir(arena, dir) == prb_Success);
    prb_assert(prb_dirIsEmpty(arena, dir));

    prb_Str filepath = prb_pathJoin(arena, dir, prb_STR("file.txt"));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);

    prb_assert(!prb_dirIsEmpty(arena, dir));
    prb_assert(prb_clearDir(arena, dir) == prb_Success);
    prb_assert(prb_dirIsEmpty(arena, dir));

    prb_Str dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(!prb_dirIsEmpty(arena, dir));
    prb_assert(!prb_dirIsEmpty(arena, dirTrailingSlash));
    prb_assert(prb_clearDir(arena, dirTrailingSlash) == prb_Success);
    prb_assert(prb_dirIsEmpty(arena, dirTrailingSlash));
    prb_assert(prb_dirIsEmpty(arena, dir));

    prb_Str dirNotNull = prb_fmt(arena, "%.*sabs", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(!prb_dirIsEmpty(arena, dir));
    prb_assert(!prb_dirIsEmpty(arena, dirNotNull));
    prb_assert(prb_clearDir(arena, dirNotNull) == prb_Success);
    prb_assert(prb_dirIsEmpty(arena, dirNotNull));
    prb_assert(prb_dirIsEmpty(arena, dir));

    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);

    prb_endTempMemory(temp);
}

function void
test_getWorkingDir(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str cwd = prb_getWorkingDir(arena);
    prb_assert(prb_isDir(arena, cwd));
    prb_Str filename = prb_getLastEntryInPath(getTempPath(arena, __FUNCTION__));
    prb_assert(prb_writeEntireFile(arena, filename, filename.ptr, filename.len) == prb_Success);
    prb_ReadEntireFileResult fileContent = prb_readEntireFile(arena, prb_pathJoin(arena, cwd, filename));
    prb_assert(fileContent.success);
    prb_assert(prb_streq((prb_Str) {(const char*)fileContent.content.data, fileContent.content.len}, filename));
    prb_assert(prb_removeFileIfExists(arena, filename) == prb_Success);

    prb_endTempMemory(temp);
}

function void
test_setWorkingDir(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str cwdInit = prb_getWorkingDir(arena);
    prb_Str newWd = getTempPath(arena, __FUNCTION__);
    prb_assert(prb_removeDirIfExists(arena, newWd) == prb_Success);
    prb_Str newWdAbsolute = prb_getAbsolutePath(arena, newWd);
    prb_assert(prb_setWorkingDir(arena, newWd) == prb_Failure);
    prb_assert(prb_createDirIfNotExists(arena, newWd) == prb_Success);
    prb_assert(prb_setWorkingDir(arena, newWd) == prb_Success);
    prb_assert(prb_streq(prb_getWorkingDir(arena), newWdAbsolute));
    prb_Str filename = prb_STR("testfile-setworkingdir.txt");
    prb_assert(prb_writeEntireFile(arena, filename, filename.ptr, filename.len) == prb_Success);
    prb_ReadEntireFileResult fileRead = prb_readEntireFile(arena, filename);
    prb_assert(fileRead.success);
    prb_assert(prb_streq(prb_strFromBytes(fileRead.content), filename));
    prb_assert(prb_setWorkingDir(arena, cwdInit) == prb_Success);
    fileRead = prb_readEntireFile(arena, filename);
    prb_assert(!fileRead.success);

    prb_removeDirIfExists(arena, newWd);
    prb_endTempMemory(temp);
}

function void
test_pathJoin(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_assert(prb_streq(prb_pathJoin(arena, prb_STR("a"), prb_STR("b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(arena, prb_STR("a/"), prb_STR("b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(arena, prb_STR("a"), prb_STR("/b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(arena, prb_STR("a/"), prb_STR("/b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(arena, prb_STR("a/"), prb_STR("/b/")), prb_STR("a/b/")));
    prb_assert(prb_streq(prb_pathJoin(arena, prb_STR("/a/"), prb_STR("/b/")), prb_STR("/a/b/")));

#if prb_PLATFORM_WINDOWS
    prb_assert(prb_streq(prb_pathJoin(arena, prb_STR("a\\"), prb_STR("b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(arena, prb_STR("a"), prb_STR("\\b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(arena, prb_STR("a\\"), prb_STR("\\b")), prb_STR("a/b")));
#elif prb_PLATFORM_LINUX
    prb_assert(prb_streq(prb_pathJoin(arena, prb_STR("a\\"), prb_STR("b")), prb_STR("a\\/b")));
    prb_assert(prb_streq(prb_pathJoin(arena, prb_STR("a"), prb_STR("\\b")), prb_STR("a/\\b")));
    prb_assert(prb_streq(prb_pathJoin(arena, prb_STR("a\\"), prb_STR("\\b")), prb_STR("a\\/\\b")));
#endif

    prb_endTempMemory(temp);
}

function void
test_charIsSep(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    prb_assert(prb_charIsSep('/'));

#if prb_PLATFORM_WINDOWS
    prb_assert(prb_charIsSep('\\'));
#elif prb_PLATFORM_LINUX
    prb_assert(!prb_charIsSep('\\'));
#endif
}

function void
test_getParentDir(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Str        cwd = prb_getWorkingDir(arena);

    prb_assert(prb_streq(prb_getParentDir(arena, prb_STR("test/run.sh")), prb_pathJoin(arena, cwd, prb_STR("test"))));
    prb_assert(prb_streq(prb_getParentDir(arena, prb_STR("test/path/")), prb_pathJoin(arena, cwd, prb_STR("test"))));
    prb_assert(prb_streq(prb_getParentDir(arena, prb_STR("test/path2/path")), prb_pathJoin(arena, cwd, prb_STR("test/path2"))));

    prb_assert(prb_streq(prb_getParentDir(arena, prb_STR("test")), cwd));

#if prb_PLATFORM_WINDOWS
    prb_assert(prb_streq(prb_getParentDir(arena, prb_STR("C:\\\\test")), prb_STR("C:\\\\")));
    prb_assert(prb_streq(prb_getParentDir(arena, prb_STR("C:\\\\test/")), prb_STR("C:\\\\")));
#elif prb_PLATFORM_LINUX
    prb_assert(prb_streq(prb_getParentDir(arena, prb_STR("/test")), prb_STR("/")));
    prb_assert(prb_streq(prb_getParentDir(arena, prb_STR("/test/")), prb_STR("/")));
#endif

    prb_endTempMemory(temp);
}

function void
test_getLastEntryInPath(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("path")), prb_STR("path")));
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("path/")), prb_STR("path")));
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("test/path")), prb_STR("path")));
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("test/path/")), prb_STR("path")));
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("test/path2/path")), prb_STR("path")));
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("test////path2////path")), prb_STR("path")));

    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("test")), prb_STR("test")));

#if prb_PLATFORM_WINDOWS
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("C:\\\\test")), prb_STR("C:\\\\")));
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("C:\\\\test/")), prb_STR("C:\\\\")));
#elif prb_PLATFORM_LINUX
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("/test")), prb_STR("test")));
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("/test/")), prb_STR("test")));
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("/")), prb_STR("/")));
#endif

    prb_endTempMemory(temp);
}

function void
test_replaceExt(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_assert(prb_streq(prb_replaceExt(arena, prb_STR("test"), prb_STR("txt")), prb_STR("test.txt")));
    prb_assert(prb_streq(prb_replaceExt(arena, prb_STR("test.md"), prb_STR("txt")), prb_STR("test.txt")));
    prb_assert(prb_streq(prb_replaceExt(arena, prb_STR("path/test.md"), prb_STR("txt")), prb_STR("path/test.txt")));
    prb_assert(prb_streq(prb_replaceExt(arena, prb_STR("path/test.txt.md"), prb_STR("txt")), prb_STR("path/test.txt.txt")));
    prb_assert(prb_streq(prb_replaceExt(arena, prb_STR("path.dot/test"), prb_STR("txt")), prb_STR("path.dot/test.txt")));

    prb_endTempMemory(temp);
}

function void
test_pathEntryIter(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);

    // TODO(khvorov) Test trailing slash
    // TODO(khvorov) Test multiple separators
    // TODO(khvorov) Test empty path

    prb_PathEntryIter iter = prb_createPathEntryIter(prb_STR("path/../to/./file"));
    prb_assert(prb_pathEntryIterNext(&iter) == prb_Success);
    prb_assert(prb_streq(iter.curEntryName, prb_STR("path")));
    prb_assert(prb_streq(iter.curEntryPath, prb_STR("path")));
    prb_assert(prb_pathEntryIterNext(&iter) == prb_Success);
    prb_assert(prb_streq(iter.curEntryName, prb_STR("..")));
    prb_assert(prb_streq(iter.curEntryPath, prb_STR("path/..")));
    prb_assert(prb_pathEntryIterNext(&iter) == prb_Success);
    prb_assert(prb_streq(iter.curEntryName, prb_STR("to")));
    prb_assert(prb_streq(iter.curEntryPath, prb_STR("path/../to")));
    prb_assert(prb_pathEntryIterNext(&iter) == prb_Success);
    prb_assert(prb_streq(iter.curEntryName, prb_STR(".")));
    prb_assert(prb_streq(iter.curEntryPath, prb_STR("path/../to/.")));
    prb_assert(prb_pathEntryIterNext(&iter) == prb_Success);
    prb_assert(prb_streq(iter.curEntryName, prb_STR("file")));
    prb_assert(prb_streq(iter.curEntryPath, prb_STR("path/../to/./file")));
    prb_assert(prb_pathEntryIterNext(&iter) == prb_Failure);

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    iter = prb_createPathEntryIter(prb_STR("/path/to/file"));
    prb_assert(prb_pathEntryIterNext(&iter) == prb_Success);
    prb_assert(prb_streq(iter.curEntryName, prb_STR("/")));
    prb_assert(prb_streq(iter.curEntryPath, prb_STR("/")));
    prb_assert(prb_pathEntryIterNext(&iter) == prb_Success);
    prb_assert(prb_streq(iter.curEntryName, prb_STR("path")));
    prb_assert(prb_streq(iter.curEntryPath, prb_STR("/path")));
    prb_assert(prb_pathEntryIterNext(&iter) == prb_Success);
    prb_assert(prb_streq(iter.curEntryName, prb_STR("to")));
    prb_assert(prb_streq(iter.curEntryPath, prb_STR("/path/to")));
    prb_assert(prb_pathEntryIterNext(&iter) == prb_Success);
    prb_assert(prb_streq(iter.curEntryName, prb_STR("file")));
    prb_assert(prb_streq(iter.curEntryPath, prb_STR("/path/to/file")));
    prb_assert(prb_pathEntryIterNext(&iter) == prb_Failure);

#else
#error unimplemented
#endif
}

function void
test_getAllDirEntries(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str dir = getTempPath(arena, __FUNCTION__);
    prb_Str dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_Str dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;

    prb_assert(prb_clearDir(arena, dir) == prb_Success);

    prb_Str       allDirs[] = {dir, dirNotNull, dirTrailingSlash};
    prb_Recursive modes[] = {prb_Recursive_Yes, prb_Recursive_No};

    for (i32 allDirsIndex = 0; allDirsIndex < prb_arrayCount(allDirs); allDirsIndex++) {
        prb_Str thisDir = allDirs[allDirsIndex];
        for (i32 modesIndex = 0; modesIndex < prb_arrayCount(modes); modesIndex++) {
            prb_Recursive thisMode = modes[modesIndex];
            prb_Str*      entries = prb_getAllDirEntries(arena, thisDir, thisMode);
            prb_assert(arrlen(entries) == 0);
            arrfree(entries);
        }
    }

    prb_Str files[] = {
        prb_pathJoin(arena, dir, prb_STR("f1.c")),
        prb_pathJoin(arena, dir, prb_STR("h2.h")),
        prb_pathJoin(arena, dir, prb_STR("f3.c")),
        prb_pathJoin(arena, dir, prb_STR("h4.h")),
    };

    for (usize fileIndex = 0; fileIndex < prb_arrayCount(files); fileIndex++) {
        prb_Str file = files[fileIndex];
        prb_assert(prb_writeEntireFile(arena, file, file.ptr, file.len) == prb_Success);
    }

    for (i32 allDirsIndex = 0; allDirsIndex < prb_arrayCount(allDirs); allDirsIndex++) {
        prb_Str thisDir = allDirs[allDirsIndex];
        for (i32 modesIndex = 0; modesIndex < prb_arrayCount(modes); modesIndex++) {
            prb_Recursive thisMode = modes[modesIndex];
            prb_Str*      entries = prb_getAllDirEntries(arena, thisDir, thisMode);
            prb_assert(arrlen(entries) == prb_arrayCount(files));
            for (i32 entryIndex = 0; entryIndex < arrlen(entries); entryIndex++) {
                prb_Str entry = entries[entryIndex];
                prb_assert(strIn(entry, files, prb_arrayCount(files)));
            }
            arrfree(entries);
        }
    }

    prb_Str nestedDir = prb_pathJoin(arena, dir, prb_STR("nested"));
    prb_assert(prb_createDirIfNotExists(arena, nestedDir) == prb_Success);
    prb_Str nestedFiles[] = {
        prb_pathJoin(arena, nestedDir, prb_STR("fn1.c")),
        prb_pathJoin(arena, nestedDir, prb_STR("hn2.h")),
        prb_pathJoin(arena, nestedDir, prb_STR("fn3.c")),
        prb_pathJoin(arena, nestedDir, prb_STR("hn4.h")),
    };
    prb_assert(prb_arrayCount(nestedFiles) == prb_arrayCount(files));

    prb_Str nestedNestedDir = prb_pathJoin(arena, nestedDir, prb_STR("nestednested"));
    prb_assert(prb_createDirIfNotExists(arena, nestedNestedDir) == prb_Success);
    prb_Str nestedNestedFiles[] = {
        prb_pathJoin(arena, nestedNestedDir, prb_STR("fnn1.c")),
        prb_pathJoin(arena, nestedNestedDir, prb_STR("hnn2.h")),
        prb_pathJoin(arena, nestedNestedDir, prb_STR("fnn3.c")),
        prb_pathJoin(arena, nestedNestedDir, prb_STR("hnn4.h")),
    };
    prb_assert(prb_arrayCount(nestedNestedFiles) == prb_arrayCount(files));

    prb_Str emptyNestedDir = prb_pathJoin(arena, dir, prb_STR("emptynested"));
    prb_assert(prb_createDirIfNotExists(arena, emptyNestedDir) == prb_Success);

    for (usize fileIndex = 0; fileIndex < prb_arrayCount(files); fileIndex++) {
        prb_Str file = files[fileIndex];
        prb_assert(prb_writeEntireFile(arena, file, file.ptr, file.len) == prb_Success);
        prb_Str nestedFile = nestedFiles[fileIndex];
        prb_assert(prb_writeEntireFile(arena, nestedFile, nestedFile.ptr, nestedFile.len) == prb_Success);
        prb_Str nestedNestedFile = nestedNestedFiles[fileIndex];
        prb_assert(prb_writeEntireFile(arena, nestedNestedFile, nestedNestedFile.ptr, nestedNestedFile.len) == prb_Success);
    }

    for (i32 allDirsIndex = 0; allDirsIndex < prb_arrayCount(allDirs); allDirsIndex++) {
        prb_Str  thisDir = allDirs[allDirsIndex];
        prb_Str* entries = prb_getAllDirEntries(arena, thisDir, prb_Recursive_No);
        prb_assert(arrlen(entries) == prb_arrayCount(files) + 2);
        for (i32 entryIndex = 0; entryIndex < arrlen(entries); entryIndex++) {
            prb_Str entry = entries[entryIndex];
            prb_assert(strIn(entry, files, prb_arrayCount(files)) || prb_streq(entry, nestedDir) || prb_streq(entry, emptyNestedDir));
        }
        arrfree(entries);
    }

    for (i32 allDirsIndex = 0; allDirsIndex < prb_arrayCount(allDirs); allDirsIndex++) {
        prb_Str  thisDir = allDirs[allDirsIndex];
        prb_Str* entries = prb_getAllDirEntries(arena, thisDir, prb_Recursive_Yes);
        prb_assert(arrlen(entries) == prb_arrayCount(files) + 2 + prb_arrayCount(nestedFiles) + 1 + prb_arrayCount(nestedNestedFiles));
        for (i32 entryIndex = 0; entryIndex < arrlen(entries); entryIndex++) {
            prb_Str entry = entries[entryIndex];
            bool    found = strIn(entry, files, prb_arrayCount(files))
                || prb_streq(entry, nestedDir)
                || prb_streq(entry, emptyNestedDir)
                || strIn(entry, nestedFiles, prb_arrayCount(nestedFiles))
                || prb_streq(entry, nestedNestedDir)
                || strIn(entry, nestedNestedFiles, prb_arrayCount(nestedFiles));
            prb_assert(found);
        }
        arrfree(entries);
    }

    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);
    prb_endTempMemory(temp);
}

function void
test_getLastModified(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Str        dir = getTempPath(arena, __FUNCTION__);
    prb_assert(prb_clearDir(arena, dir) == prb_Success);
    prb_Str file = prb_pathJoin(arena, dir, prb_STR("f1.c"));

    prb_FileTimestamp lastMod = prb_getLastModified(arena, file);
    prb_assert(!lastMod.valid && lastMod.timestamp == 0);
    prb_assert(prb_writeEntireFile(arena, file, file.ptr, file.len) == prb_Success);
    lastMod = prb_getLastModified(arena, file);
    prb_assert(lastMod.valid);

    u64 t1 = lastMod.timestamp;
    prb_sleep(10.0f);

    prb_assert(prb_writeEntireFile(arena, file, file.ptr, file.len) == prb_Success);
    lastMod = prb_getLastModified(arena, file);
    prb_assert(lastMod.valid);

    u64 t2 = lastMod.timestamp;
    prb_assert(t2 > t1);

    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);
    prb_endTempMemory(temp);
}

function void
test_createMultitime(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    prb_Multitime mt = prb_createMultitime();
    prb_assert(mt.invalidAddedTimestampsCount == 0);
    prb_assert(mt.validAddedTimestampsCount == 0);
    prb_assert(mt.timeEarliest == UINT64_MAX);
    prb_assert(mt.timeLatest == 0);
}

function void
test_multitimeAdd(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    prb_Multitime     mt = prb_createMultitime();
    prb_FileTimestamp t1 = {true, 100};
    prb_multitimeAdd(&mt, t1);
    prb_assert(mt.invalidAddedTimestampsCount == 0);
    prb_assert(mt.validAddedTimestampsCount == 1);
    prb_assert(mt.timeEarliest == t1.timestamp);
    prb_assert(mt.timeLatest == t1.timestamp);
    prb_FileTimestamp t2 = {true, 200};
    prb_multitimeAdd(&mt, t2);
    prb_assert(mt.invalidAddedTimestampsCount == 0);
    prb_assert(mt.validAddedTimestampsCount == 2);
    prb_assert(mt.timeEarliest == t1.timestamp);
    prb_assert(mt.timeLatest == t2.timestamp);
    prb_FileTimestamp t3 = {false, 300};
    prb_multitimeAdd(&mt, t3);
    prb_assert(mt.invalidAddedTimestampsCount == 1);
    prb_assert(mt.validAddedTimestampsCount == 2);
    prb_assert(mt.timeEarliest == t1.timestamp);
    prb_assert(mt.timeLatest == t2.timestamp);
}

function void
test_readEntireFile(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory           temp = prb_beginTempMemory(arena);
    prb_ReadEntireFileResult readResult = prb_readEntireFile(arena, prb_STR("nonexistant"));
    prb_assert(!readResult.success);
    readResult = prb_readEntireFile(arena, prb_STR(__FILE__));
    prb_assert(readResult.success);
    prb_assert(prb_strStartsWith(prb_strFromBytes(readResult.content), prb_STR("#include \"../cbuild.h\"")));
    prb_endTempMemory(temp);
}

function void
test_writeEntireFile(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Str        dir = getTempPath(arena, __FUNCTION__);
    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);
    prb_Str filepath = prb_pathJoin(arena, dir, prb_STR("filename.txt"));
    prb_assert(prb_writeEntireFile(arena, filepath, filepath.ptr, filepath.len) == prb_Failure);
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_writeEntireFile(arena, filepath, filepath.ptr, filepath.len) == prb_Success);
    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);
    prb_endTempMemory(temp);
}

function void
test_binaryToCArray(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    u8      bytes[] = {0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc};
    prb_Str carr = prb_binaryToCArray(arena, prb_STR("testarr"), bytes, prb_arrayCount(bytes));
    prb_assert(prb_streq(carr, prb_STR("unsigned char testarr[] = {\n    0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa,\n    0xb, 0xc\n};")));
}

function void
test_getFileHash(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Str        dir = getTempPath(arena, __FUNCTION__);
    prb_Str        filepath = prb_pathJoin(arena, dir, prb_STR("filename.txt"));
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_writeEntireFile(arena, filepath, filepath.ptr, filepath.len) == prb_Success);
    prb_FileHash hash1 = prb_getFileHash(arena, filepath);
    prb_assert(hash1.valid);
    prb_Str newContent = prb_STR("content");
    prb_assert(prb_writeEntireFile(arena, filepath, newContent.ptr, newContent.len) == prb_Success);
    prb_FileHash hash2 = prb_getFileHash(arena, filepath);
    prb_assert(hash2.valid);
    prb_assert(hash1.hash != hash2.hash);
    prb_assert(prb_writeEntireFile(arena, filepath, filepath.ptr, filepath.len) == prb_Success);
    prb_FileHash hash3 = prb_getFileHash(arena, filepath);
    prb_assert(hash3.valid);
    prb_assert(hash3.hash == hash1.hash);
    prb_assert(prb_removeDirIfExists(arena, dir) == prb_Success);
    prb_FileHash hash4 = prb_getFileHash(arena, filepath);
    prb_assert(!hash4.valid);
    prb_endTempMemory(temp);
}

//
// SECTION Strings
//

function void
test_streq(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_strSlice(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_strGetNullTerminated(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_strFromBytes(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_strTrimSide(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_strTrim(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_strFind(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    {
        prb_Str         str = prb_STR("p1at4pattern1 pattern2 pattern3p2a.t");
        prb_StrFindSpec spec = {};
        spec.pattern = prb_STR("pattern");
        spec.mode = prb_StrFindMode_Exact;
        prb_StrFindResult res = prb_strFind(str, spec);
        prb_assert(
            res.found
            && prb_streq(res.beforeMatch, prb_STR("p1at4"))
            && prb_streq(res.match, prb_STR("pattern"))
            && prb_streq(res.afterMatch, prb_STR("1 pattern2 pattern3p2a.t"))
        );

        spec.direction = prb_StrDirection_FromEnd;

        res = prb_strFind(str, spec);
        prb_assert(
            res.found
            && prb_streq(res.beforeMatch, prb_STR("p1at4pattern1 pattern2 "))
            && prb_streq(res.match, prb_STR("pattern"))
            && prb_streq(res.afterMatch, prb_STR("3p2a.t"))
        );
    }

    {
        prb_Str         str = prb_STR("p1at4pat1ern1 pat1ern2 pat1ern3p2a.p");
        prb_StrFindSpec spec = {};
        spec.pattern = prb_STR("pattern");
        spec.mode = prb_StrFindMode_Exact;
        prb_StrFindResult res = prb_strFind(str, spec);
        prb_assert(!res.found);

        spec.direction = prb_StrDirection_FromEnd;
        res = prb_strFind(str, spec);
        prb_assert(!res.found);
    }

    {
        prb_Str         str = prb_STR("中华人民共和国是目前世界上人口最多的国家");
        prb_StrFindSpec spec = {};
        spec.pattern = prb_STR("民共和国");
        ;
        spec.mode = prb_StrFindMode_Exact;
        prb_StrFindResult res = prb_strFind(str, spec);
        prb_assert(
            res.found
            && prb_streq(res.beforeMatch, prb_STR("中华人"))
            && prb_streq(res.match, prb_STR("民共和国"))
            && prb_streq(res.afterMatch, prb_STR("是目前世界上人口最多的国家"))
        );

        spec.direction = prb_StrDirection_FromEnd;
        res = prb_strFind(str, spec);
        prb_assert(
            res.found
            && prb_streq(res.beforeMatch, prb_STR("中华人"))
            && prb_streq(res.match, prb_STR("民共和国"))
            && prb_streq(res.afterMatch, prb_STR("是目前世界上人口最多的国家"))
        );
    }

    {
        prb_Str         str = prb_STR("中华人民共和国是目前世界上人口最多的国家");
        prb_StrFindSpec spec = {};
        spec.pattern = prb_STR("民共和国");
        ;
        spec.mode = prb_StrFindMode_AnyChar;
        prb_StrFindResult res = prb_strFind(str, spec);
        prb_assert(
            res.found
            && prb_streq(res.beforeMatch, prb_STR("中华人"))
            && prb_streq(res.match, prb_STR("民"))
            && prb_streq(res.afterMatch, prb_STR("共和国是目前世界上人口最多的国家"))
        );

        spec.direction = prb_StrDirection_FromEnd;
        res = prb_strFind(str, spec);
        prb_assert(
            res.found
            && prb_streq(res.beforeMatch, prb_STR("中华人民共和国是目前世界上人口最多的"))
            && prb_streq(res.match, prb_STR("国"))
            && prb_streq(res.afterMatch, prb_STR("家"))
        );
    }

    {
        prb_Str         line = prb_STR("line1\r\na");
        prb_StrFindSpec spec = {};
        spec.mode = prb_StrFindMode_LineBreak;
        prb_StrFindResult find = prb_strFind(line, spec);
        prb_assert(find.found);
        prb_assert(prb_streq(find.beforeMatch, prb_STR("line1")));
        prb_assert(prb_streq(find.match, prb_STR("\r\n")));
        prb_assert(prb_streq(find.afterMatch, prb_STR("a")));

        spec.direction = prb_StrDirection_FromEnd;
        find = prb_strFind(line, spec);
        prb_assert(find.found);
        prb_assert(prb_streq(find.beforeMatch, prb_STR("line1")));
        prb_assert(prb_streq(find.match, prb_STR("\r\n")));
        prb_assert(prb_streq(find.afterMatch, prb_STR("a")));
    }

    {
        prb_Str         line = prb_STR("line1\ra");
        prb_StrFindSpec spec = {};
        spec.mode = prb_StrFindMode_LineBreak;
        prb_StrFindResult find = prb_strFind(line, spec);
        prb_assert(find.found);
        prb_assert(prb_streq(find.beforeMatch, prb_STR("line1")));
        prb_assert(prb_streq(find.match, prb_STR("\r")));
        prb_assert(prb_streq(find.afterMatch, prb_STR("a")));

        spec.direction = prb_StrDirection_FromEnd;
        find = prb_strFind(line, spec);
        prb_assert(find.found);
        prb_assert(prb_streq(find.beforeMatch, prb_STR("line1")));
        prb_assert(prb_streq(find.match, prb_STR("\r")));
        prb_assert(prb_streq(find.afterMatch, prb_STR("a")));
    }

    {
        prb_Str         line = prb_STR("line1\na");
        prb_StrFindSpec spec = {};
        spec.mode = prb_StrFindMode_LineBreak;
        prb_StrFindResult find = prb_strFind(line, spec);
        prb_assert(find.found);
        prb_assert(prb_streq(find.beforeMatch, prb_STR("line1")));
        prb_assert(prb_streq(find.match, prb_STR("\n")));
        prb_assert(prb_streq(find.afterMatch, prb_STR("a")));

        spec.direction = prb_StrDirection_FromEnd;
        find = prb_strFind(line, spec);
        prb_assert(find.found);
        prb_assert(prb_streq(find.beforeMatch, prb_STR("line1")));
        prb_assert(prb_streq(find.match, prb_STR("\n")));
        prb_assert(prb_streq(find.afterMatch, prb_STR("a")));
    }

    {
        prb_Str         line = prb_STR("line1\na\nb");
        prb_StrFindSpec spec = {};
        spec.mode = prb_StrFindMode_LineBreak;
        prb_StrFindResult find = prb_strFind(line, spec);
        prb_assert(find.found);
        prb_assert(prb_streq(find.beforeMatch, prb_STR("line1")));
        prb_assert(prb_streq(find.match, prb_STR("\n")));
        prb_assert(prb_streq(find.afterMatch, prb_STR("a\nb")));

        spec.direction = prb_StrDirection_FromEnd;
        find = prb_strFind(line, spec);
        prb_assert(find.found);
        prb_assert(prb_streq(find.beforeMatch, prb_STR("line1\na")));
        prb_assert(prb_streq(find.match, prb_STR("\n")));
        prb_assert(prb_streq(find.afterMatch, prb_STR("b")));
    }

    {
        prb_Str         line = prb_STR("line1");
        prb_StrFindSpec spec = {};
        spec.mode = prb_StrFindMode_LineBreak;
        prb_StrFindResult find = prb_strFind(line, spec);
        prb_assert(find.found);
        prb_assert(prb_streq(find.beforeMatch, prb_STR("line1")));
        prb_assert(find.match.len == 0);
        prb_assert(find.afterMatch.len == 0);

        spec.direction = prb_StrDirection_FromEnd;
        find = prb_strFind(line, spec);
        prb_assert(find.found);
        prb_assert(find.beforeMatch.len == 0);
        prb_assert(find.match.len == 0);
        prb_assert(prb_streq(find.afterMatch, prb_STR("line1")));
    }

    prb_endTempMemory(temp);
}

function void
test_strStartsWith(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);
    prb_assert(prb_strStartsWith(prb_STR("123abc"), prb_STR("123")));
    prb_assert(!prb_strStartsWith(prb_STR("123abc"), prb_STR("abc")));
}

function void
test_strEndsWith(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);
    prb_assert(!prb_strEndsWith(prb_STR("123abc"), prb_STR("123")));
    prb_assert(prb_strEndsWith(prb_STR("123abc"), prb_STR("abc")));
}

function void
test_strReplace(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_stringsJoin(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_beginStr(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_addStrSegment(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_endStr(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_vfmtCustomBuffer(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_fmt(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_writeToStdout(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_writelnToStdout(
        arena,
        prb_fmt(
            arena,
            "%sblue%scyan%smagenta%syellow%sred%sgreen%sblack%swhite%s",
            prb_colorEsc(prb_ColorID_Blue).ptr,
            prb_colorEsc(prb_ColorID_Cyan).ptr,
            prb_colorEsc(prb_ColorID_Magenta).ptr,
            prb_colorEsc(prb_ColorID_Yellow).ptr,
            prb_colorEsc(prb_ColorID_Red).ptr,
            prb_colorEsc(prb_ColorID_Green).ptr,
            prb_colorEsc(prb_ColorID_Black).ptr,
            prb_colorEsc(prb_ColorID_White).ptr,
            prb_colorEsc(prb_ColorID_Reset).ptr
        )
    );

    prb_endTempMemory(temp);
}

function void
test_utf8CharIter(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);

    {
        prb_Str str = prb_STR("abcדזון是太متشاтипуκαι");
        u32     charsUtf32[] = {97, 98, 99, 1491, 1494, 1493, 1503, 26159, 22826, 1605, 1578, 1588, 1575, 1090, 1080, 1087, 1091, 954, 945, 953};
        i32     utf8Bytes[] = {1, 1, 1, 2, 2, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
        prb_assert(prb_arrayCount(charsUtf32) == prb_arrayCount(utf8Bytes));

        prb_Utf8CharIter iter = prb_createUtf8CharIter(str, prb_StrDirection_FromStart);
        prb_Utf8CharIter iterBackwards = prb_createUtf8CharIter(str, prb_StrDirection_FromEnd);
        prb_assert(iter.curCharCount == 0);
        prb_assert(iterBackwards.curCharCount == 0);
        i32 curTotalUtf8Bytes = 0;
        i32 curTotalUtf8BytesBackwards = 0;
        for (i32 charIndex = 0; charIndex < prb_arrayCount(charsUtf32); charIndex++) {
            i32 charIndexBackwards = prb_arrayCount(charsUtf32) - 1 - charIndex;
            i32 charUtf8Bytes = utf8Bytes[charIndex];
            i32 charUtf8BytesBackwards = utf8Bytes[charIndexBackwards];
            prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
            prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
            prb_assert(iter.curCharCount == charIndex + 1);
            prb_assert(iterBackwards.curCharCount == iter.curCharCount);
            prb_assert(iter.curByteOffset == curTotalUtf8Bytes);
            prb_assert(iterBackwards.curByteOffset == str.len - 1 - curTotalUtf8BytesBackwards - (charUtf8BytesBackwards - 1));
            prb_assert(iter.curUtf32Char == charsUtf32[charIndex]);
            prb_assert(iterBackwards.curUtf32Char == charsUtf32[charIndexBackwards]);
            prb_assert(iter.curUtf8Bytes == charUtf8Bytes);
            prb_assert(iterBackwards.curUtf8Bytes == charUtf8BytesBackwards);
            prb_assert(iter.curIsValid);
            prb_assert(iterBackwards.curIsValid);
            curTotalUtf8Bytes += charUtf8Bytes;
            curTotalUtf8BytesBackwards += charUtf8BytesBackwards;
        }

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Failure);
        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Failure);
        prb_assert(iter.curCharCount == prb_arrayCount(charsUtf32));
        prb_assert(iter.curByteOffset == str.len);
        prb_assert(iterBackwards.curCharCount == prb_arrayCount(charsUtf32));
        prb_assert(iterBackwards.curByteOffset == -1);
    }

    {
        uint8_t          borked[] = {'a', 0b10000000, 'b', '\0'};
        prb_Utf8CharIter iter = prb_createUtf8CharIter(prb_STR((char*)borked), prb_StrDirection_FromStart);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == 1);
        prb_assert(iter.curIsValid == true);
        prb_assert(iter.curUtf32Char == 'a');
        prb_assert(iter.curUtf8Bytes == 1);
        prb_assert(iter.curByteOffset == 0);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == 1);
        prb_assert(iter.curIsValid == false);
        prb_assert(iter.curUtf32Char == 0);
        prb_assert(iter.curUtf8Bytes == 0);
        prb_assert(iter.curByteOffset == 1);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == 2);
        prb_assert(iter.curIsValid == true);
        prb_assert(iter.curUtf32Char == 'b');
        prb_assert(iter.curUtf8Bytes == 1);
        prb_assert(iter.curByteOffset == 2);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Failure);

        prb_Utf8CharIter iterBackwards = prb_createUtf8CharIter(prb_STR((char*)borked), prb_StrDirection_FromEnd);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
        prb_assert(iterBackwards.curCharCount == 1);
        prb_assert(iterBackwards.curIsValid == true);
        prb_assert(iterBackwards.curUtf32Char == 'b');
        prb_assert(iterBackwards.curUtf8Bytes == 1);
        prb_assert(iterBackwards.curByteOffset == 2);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
        prb_assert(iterBackwards.curCharCount == 1);
        prb_assert(iterBackwards.curIsValid == false);
        prb_assert(iterBackwards.curUtf32Char == 0);
        prb_assert(iterBackwards.curUtf8Bytes == 0);
        prb_assert(iterBackwards.curByteOffset == 1);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
        prb_assert(iterBackwards.curCharCount == 2);
        prb_assert(iterBackwards.curIsValid == true);
        prb_assert(iterBackwards.curUtf32Char == 'a');
        prb_assert(iterBackwards.curUtf8Bytes == 1);
        prb_assert(iterBackwards.curByteOffset == 0);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Failure);
    }

    {
        uint8_t          borked[] = {0b10000000, 'a', 'b', '\0'};
        prb_Utf8CharIter iter = prb_createUtf8CharIter(prb_STR((char*)borked), prb_StrDirection_FromStart);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == 0);
        prb_assert(iter.curIsValid == false);
        prb_assert(iter.curUtf32Char == 0);
        prb_assert(iter.curUtf8Bytes == 0);
        prb_assert(iter.curByteOffset == 0);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == 1);
        prb_assert(iter.curIsValid == true);
        prb_assert(iter.curUtf32Char == 'a');
        prb_assert(iter.curUtf8Bytes == 1);
        prb_assert(iter.curByteOffset == 1);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == 2);
        prb_assert(iter.curIsValid == true);
        prb_assert(iter.curUtf32Char == 'b');
        prb_assert(iter.curUtf8Bytes == 1);
        prb_assert(iter.curByteOffset == 2);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Failure);

        prb_Utf8CharIter iterBackwards = prb_createUtf8CharIter(prb_STR((char*)borked), prb_StrDirection_FromEnd);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
        prb_assert(iterBackwards.curCharCount == 1);
        prb_assert(iterBackwards.curIsValid == true);
        prb_assert(iterBackwards.curUtf32Char == 'b');
        prb_assert(iterBackwards.curUtf8Bytes == 1);
        prb_assert(iterBackwards.curByteOffset == 2);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
        prb_assert(iterBackwards.curCharCount == 2);
        prb_assert(iterBackwards.curIsValid == true);
        prb_assert(iterBackwards.curUtf32Char == 'a');
        prb_assert(iterBackwards.curUtf8Bytes == 1);
        prb_assert(iterBackwards.curByteOffset == 1);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
        prb_assert(iterBackwards.curCharCount == 2);
        prb_assert(iterBackwards.curIsValid == false);
        prb_assert(iterBackwards.curUtf32Char == 0);
        prb_assert(iterBackwards.curUtf8Bytes == 0);
        prb_assert(iterBackwards.curByteOffset == 0);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Failure);
    }

    {
        uint8_t          borked[] = {'a', 'b', 0b10000000, '\0'};
        prb_Utf8CharIter iter = prb_createUtf8CharIter(prb_STR((char*)borked), prb_StrDirection_FromStart);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == 1);
        prb_assert(iter.curIsValid == true);
        prb_assert(iter.curUtf32Char == 'a');
        prb_assert(iter.curUtf8Bytes == 1);
        prb_assert(iter.curByteOffset == 0);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == 2);
        prb_assert(iter.curIsValid == true);
        prb_assert(iter.curUtf32Char == 'b');
        prb_assert(iter.curUtf8Bytes == 1);
        prb_assert(iter.curByteOffset == 1);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == 2);
        prb_assert(iter.curIsValid == false);
        prb_assert(iter.curUtf32Char == 0);
        prb_assert(iter.curUtf8Bytes == 0);
        prb_assert(iter.curByteOffset == 2);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Failure);

        prb_Utf8CharIter iterBackwards = prb_createUtf8CharIter(prb_STR((char*)borked), prb_StrDirection_FromEnd);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
        prb_assert(iterBackwards.curCharCount == 0);
        prb_assert(iterBackwards.curIsValid == false);
        prb_assert(iterBackwards.curUtf32Char == 0);
        prb_assert(iterBackwards.curUtf8Bytes == 0);
        prb_assert(iterBackwards.curByteOffset == 2);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
        prb_assert(iterBackwards.curCharCount == 1);
        prb_assert(iterBackwards.curIsValid == true);
        prb_assert(iterBackwards.curUtf32Char == 'b');
        prb_assert(iterBackwards.curUtf8Bytes == 1);
        prb_assert(iterBackwards.curByteOffset == 1);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
        prb_assert(iterBackwards.curCharCount == 2);
        prb_assert(iterBackwards.curIsValid == true);
        prb_assert(iterBackwards.curUtf32Char == 'a');
        prb_assert(iterBackwards.curUtf8Bytes == 1);
        prb_assert(iterBackwards.curByteOffset == 0);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Failure);
    }

    {
        prb_Str borked = prb_fmt(arena, "тип");
        ((u8*)borked.ptr)[1] = 0b11000000;
        prb_Utf8CharIter iter = prb_createUtf8CharIter(borked, prb_StrDirection_FromStart);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == 0);
        prb_assert(iter.curIsValid == false);
        prb_assert(iter.curUtf32Char == 0);
        prb_assert(iter.curUtf8Bytes == 0);
        prb_assert(iter.curByteOffset == 0);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == 0);
        prb_assert(iter.curIsValid == false);
        prb_assert(iter.curUtf32Char == 0);
        prb_assert(iter.curUtf8Bytes == 0);
        prb_assert(iter.curByteOffset == 1);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == 1);
        prb_assert(iter.curIsValid == true);
        prb_assert(iter.curUtf32Char == 1080);
        prb_assert(iter.curUtf8Bytes == 2);
        prb_assert(iter.curByteOffset == 2);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == 2);
        prb_assert(iter.curIsValid == true);
        prb_assert(iter.curUtf32Char == 1087);
        prb_assert(iter.curUtf8Bytes == 2);
        prb_assert(iter.curByteOffset == 4);

        prb_assert(prb_utf8CharIterNext(&iter) == prb_Failure);

        prb_Utf8CharIter iterBackwards = prb_createUtf8CharIter(borked, prb_StrDirection_FromEnd);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
        prb_assert(iterBackwards.curCharCount == 1);
        prb_assert(iterBackwards.curIsValid == true);
        prb_assert(iterBackwards.curUtf32Char == 1087);
        prb_assert(iterBackwards.curUtf8Bytes == 2);
        prb_assert(iterBackwards.curByteOffset == 4);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
        prb_assert(iterBackwards.curCharCount == 2);
        prb_assert(iterBackwards.curIsValid == true);
        prb_assert(iterBackwards.curUtf32Char == 1080);
        prb_assert(iterBackwards.curUtf8Bytes == 2);
        prb_assert(iterBackwards.curByteOffset == 2);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
        prb_assert(iterBackwards.curCharCount == 2);
        prb_assert(iterBackwards.curIsValid == false);
        prb_assert(iterBackwards.curUtf32Char == 0);
        prb_assert(iterBackwards.curUtf8Bytes == 0);
        prb_assert(iterBackwards.curByteOffset == 1);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Success);
        prb_assert(iterBackwards.curCharCount == 2);
        prb_assert(iterBackwards.curIsValid == false);
        prb_assert(iterBackwards.curUtf32Char == 0);
        prb_assert(iterBackwards.curUtf8Bytes == 0);
        prb_assert(iterBackwards.curByteOffset == 0);

        prb_assert(prb_utf8CharIterNext(&iterBackwards) == prb_Failure);
    }
}

function void
test_strScanner(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);

    {
        prb_Str        lines = prb_STR("line1\r\nline2\nline3\rline4\n\nline6\r\rline8\r\n\r\nline10\r\n\nline12\r\r\nline14");
        prb_StrScanner iter = prb_createStrScanner(lines);

        prb_StrFindSpec lineBreakSpec = {};
        lineBreakSpec.mode = prb_StrFindMode_LineBreak;

        prb_assert(iter.matchCount == 0);
        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(prb_streq(iter.betweenLastMatches, prb_STR("line1")));
        prb_assert(iter.match.len == 2);
        prb_assert(iter.matchCount == 1);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(prb_streq(iter.betweenLastMatches, prb_STR("line2")));
        prb_assert(iter.match.len == 1);
        prb_assert(iter.matchCount == 2);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(prb_streq(iter.betweenLastMatches, prb_STR("line3")));
        prb_assert(iter.betweenLastMatches.len == 5);
        prb_assert(iter.match.len == 1);
        prb_assert(iter.matchCount == 3);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(prb_streq(iter.betweenLastMatches, prb_STR("line4")));
        prb_assert(iter.match.len == 1);
        prb_assert(iter.matchCount == 4);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(iter.betweenLastMatches.len == 0);
        prb_assert(iter.match.len == 1);
        prb_assert(iter.matchCount == 5);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(prb_streq(iter.betweenLastMatches, prb_STR("line6")));
        prb_assert(iter.match.len == 1);
        prb_assert(iter.matchCount == 6);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(iter.betweenLastMatches.len == 0);
        prb_assert(iter.match.len == 1);
        prb_assert(iter.matchCount == 7);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(prb_streq(iter.betweenLastMatches, prb_STR("line8")));
        prb_assert(iter.match.len == 2);
        prb_assert(iter.matchCount == 8);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(iter.betweenLastMatches.len == 0);
        prb_assert(iter.match.len == 2);
        prb_assert(iter.matchCount == 9);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(prb_streq(iter.betweenLastMatches, prb_STR("line10")));
        prb_assert(iter.match.len == 2);
        prb_assert(iter.matchCount == 10);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(iter.betweenLastMatches.len == 0);
        prb_assert(iter.match.len == 1);
        prb_assert(iter.matchCount == 11);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(prb_streq(iter.betweenLastMatches, prb_STR("line12")));
        prb_assert(iter.match.len == 1);
        prb_assert(iter.matchCount == 12);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(iter.betweenLastMatches.len == 0);
        prb_assert(iter.match.len == 2);
        prb_assert(iter.matchCount == 13);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(prb_streq(iter.betweenLastMatches, prb_STR("line14")));
        prb_assert(iter.match.len == 0);
        prb_assert(iter.matchCount == 14);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Failure);
        prb_assert(iter.matchCount == 14);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_BeforeMatch));
        prb_assert(prb_streq(iter.betweenLastMatches, prb_STR("line2\nline3\rline4\n\nline6\r\rline8\r\n\r\nline10\r\n\nline12\r\r\nline14")));
        prb_assert(prb_streq(iter.beforeMatch, prb_STR("line1")));
        prb_assert(iter.match.len == 2);
        prb_assert(iter.matchCount == 15);

        lineBreakSpec.direction = prb_StrDirection_FromEnd;
        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch));
        prb_assert(prb_streq(iter.afterMatch, prb_STR("line14")));
        prb_assert(prb_streq(iter.betweenLastMatches, prb_STR("line2\nline3\rline4\n\nline6\r\rline8\r\n\r\nline10\r\n\nline12\r")));
    }

    {
        prb_Str        lines = prb_STR("\n");
        prb_StrScanner iter = prb_createStrScanner(lines);

        prb_StrFindSpec lineBreakSpec = {};
        lineBreakSpec.mode = prb_StrFindMode_LineBreak;

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
        prb_assert(iter.betweenLastMatches.len == 0);
        prb_assert(iter.match.len == 1);

        prb_assert(prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Failure);
    }
}

function void
test_parseNumber(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

//
// SECTION Processes
//

function void
test_terminate(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_getCmdline(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_getCmdArgs(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_getArgArrayFromStr(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str strings[] = {prb_STR("prg arg1 arg2 arg3"), prb_STR("  prg arg1 arg2  arg3 ")};

    for (i32 strIndex = 0; strIndex < prb_arrayCount(strings); strIndex++) {
        const char** args = prb_getArgArrayFromStr(arena, strings[strIndex]);
        prb_assert(arrlen(args) == 4);
        prb_assert(prb_streq(prb_STR(args[0]), prb_STR("prg")));
        prb_assert(prb_streq(prb_STR(args[1]), prb_STR("arg1")));
        prb_assert(prb_streq(prb_STR(args[2]), prb_STR("arg2")));
        prb_assert(prb_streq(prb_STR(args[3]), prb_STR("arg3")));
        prb_assert(args[4] == 0);
        arrfree(args);
    }

    {
        const char** args = prb_getArgArrayFromStr(arena, prb_STR("prg"));
        prb_assert(arrlen(args) == 1);
        prb_assert(prb_streq(prb_STR(args[0]), prb_STR("prg")));
        arrfree(args);
    }

    prb_endTempMemory(temp);
}

function void
test_preventExecutionOnCores(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_process(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str dir = getTempPath(arena, __FUNCTION__);
    prb_assert(prb_clearDir(arena, dir));

#if prb_PLATFORM_WINDOWS
    prb_Str exeExt = prb_STR("exe");
#elif prb_PLATFORM_LINUX
    prb_Str exeExt = prb_STR("bin");
#else
#error unimplemented
#endif

    {
        prb_Str helloWorldPath = prb_pathJoin(arena, dir, prb_STR("helloworld.c"));
        prb_Str helloWorld = prb_STR("#include <stdio.h>\nint main() {printf(\"hello world\\n\"); fflush(stdout); fprintf(stderr, \"stderrout\\n\"); return 0;}");
        prb_assert(prb_writeEntireFile(arena, helloWorldPath, helloWorld.ptr, helloWorld.len));

        prb_Str helloExe = prb_replaceExt(arena, helloWorldPath, exeExt);
        prb_Str compileCmd = prb_fmt(arena, "clang %.*s -o %.*s", prb_LIT(helloWorldPath), prb_LIT(helloExe));

        {
            prb_Process proc = prb_createProcess(compileCmd, (prb_ProcessSpec) {});
            prb_assert(prb_launchProcesses(arena, &proc, 1, prb_Background_No));
        }

        {
            prb_ProcessSpec spec = {};
            spec.redirectStdout = true;
            spec.stdoutFilepath = prb_pathJoin(arena, dir, prb_STR("stdout.txt"));
            spec.redirectStderr = true;
            prb_Process proc = prb_createProcess(helloExe, spec);
            prb_assert(prb_launchProcesses(arena, &proc, 1, prb_Background_No));
            prb_ReadEntireFileResult readRes = prb_readEntireFile(arena, spec.stdoutFilepath);
            prb_assert(readRes.success);
            prb_assert(prb_streq(prb_strFromBytes(readRes.content), prb_STR("hello world\n")));
        }

        {
            prb_ProcessSpec spec = {};
            spec.redirectStderr = true;
            spec.stderrFilepath = prb_pathJoin(arena, dir, prb_STR("stderr.txt"));
            spec.redirectStdout = true;
            prb_Process proc = prb_createProcess(helloExe, spec);
            prb_assert(prb_launchProcesses(arena, &proc, 1, prb_Background_No));
            prb_ReadEntireFileResult readRes = prb_readEntireFile(arena, spec.stderrFilepath);
            prb_assert(readRes.success);
            prb_assert(prb_streq(prb_strFromBytes(readRes.content), prb_STR("stderrout\n")));
        }

        {
            prb_ProcessSpec spec = {};
            spec.redirectStdout = true;
            spec.stdoutFilepath = prb_pathJoin(arena, dir, prb_STR("stdout.txt"));
            spec.redirectStderr = true;
            spec.stderrFilepath = prb_pathJoin(arena, dir, prb_STR("stderr.txt"));
            prb_Process proc = prb_createProcess(helloExe, spec);
            prb_assert(prb_launchProcesses(arena, &proc, 1, prb_Background_No));
            {
                prb_ReadEntireFileResult readRes = prb_readEntireFile(arena, spec.stdoutFilepath);
                prb_assert(readRes.success);
                prb_assert(prb_streq(prb_strFromBytes(readRes.content), prb_STR("hello world\n")));
            }
            {
                prb_ReadEntireFileResult readRes = prb_readEntireFile(arena, spec.stderrFilepath);
                prb_assert(readRes.success);
                prb_assert(prb_streq(prb_strFromBytes(readRes.content), prb_STR("stderrout\n")));
            }
        }

        {
            prb_ProcessSpec spec = {};
            spec.redirectStdout = true;
            spec.stdoutFilepath = prb_pathJoin(arena, dir, prb_STR("stdout.txt"));
            spec.redirectStderr = true;
            spec.stderrFilepath = spec.stdoutFilepath;
            prb_Process proc = prb_createProcess(helloExe, spec);
            prb_assert(prb_launchProcesses(arena, &proc, 1, prb_Background_No));
            prb_ReadEntireFileResult readRes = prb_readEntireFile(arena, spec.stdoutFilepath);
            prb_assert(readRes.success);
            prb_assert(prb_streq(prb_strFromBytes(readRes.content), prb_STR("hello world\nstderrout\n")));
        }
    }

    // TODO(khvorov) Run when we have concurrency limiter for processes
    if (false) {
        prb_Str programPath = prb_pathJoin(arena, dir, prb_STR("forever.c"));
#if prb_PLATFORM_WINDOWS
#error unimplemented
#elif prb_PLATFORM_LINUX
        prb_Str program = prb_STR("unsigned int sleep(unsigned int seconds);\nint main() {for (;;) {} return 0;}");
#else
#error unimplemented
#endif
        prb_assert(prb_writeEntireFile(arena, programPath, program.ptr, program.len));

        prb_Str programExe = prb_replaceExt(arena, programPath, exeExt);
        prb_Str compileCmd = prb_fmt(arena, "clang %.*s -o %.*s", prb_LIT(programPath), prb_LIT(programExe));

        {
            prb_Process proc = prb_createProcess(compileCmd, (prb_ProcessSpec) {});
            prb_assert(prb_launchProcesses(arena, &proc, 1, prb_Background_No));
        }

        {
            i32          procCount = 100;
            prb_Process* procs = 0;
            for (i32 procIndex = 0; procIndex < procCount; procIndex++) {
                arrput(procs, prb_createProcess(programExe, (prb_ProcessSpec) {}));
            }
            prb_assert(prb_launchProcesses(arena, procs, procCount, prb_Background_Yes));
            prb_assert(prb_waitForProcesses(procs, procCount));
        }
    }

    prb_removeDirIfExists(arena, dir);
    prb_endTempMemory(temp);
}

function void
test_sleep(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_debuggerPresent(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_env(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Str        name = prb_STR(__FUNCTION__);
    prb_Str        value = prb_STR("test");
    prb_Str        value2 = prb_STR("test2");
    prb_unsetenv(arena, name);
    prb_assert(!prb_getenv(arena, name).found);

    prb_assert(prb_setenv(arena, name, value));
    prb_GetenvResult getRes = prb_getenv(arena, name);
    prb_assert(getRes.found);
    prb_assert(prb_streq(getRes.str, value));

    prb_assert(prb_setenv(arena, name, value2));
    getRes = prb_getenv(arena, name);
    prb_assert(getRes.found);
    prb_assert(prb_streq(getRes.str, value2));

    prb_assert(prb_unsetenv(arena, name));
    prb_assert(!prb_getenv(arena, name).found);
    prb_endTempMemory(temp);
}

//
// SECTION Timing
//

function void
test_timeStart(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);
    // TODO(khvorov) Write
}

function void
test_getMsFrom(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);
    // TODO(khvorov) Write
}

//
// SECTION Multithreading
//

function void
randomJob(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    prb_Rng rng = prb_createRng(0);
    for (;;) {
        float num = prb_randomF3201(&rng);
        if (num > 0.95f) {
            break;
        }
    }
}

function void
test_jobs(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);

    {
        prb_Job* jobs = 0;
        i32      jobCount = 100;
        for (i32 jobIndex = 0; jobIndex < jobCount; jobIndex++) {
            arrput(jobs, prb_createJob(randomJob, data, arena, 0));
        }

        prb_assert(prb_launchJobs(jobs, jobCount, prb_Background_Yes));
        prb_assert(prb_waitForJobs(jobs, jobCount));
        arrfree(jobs);
    }
}

// SECTION Random numbers

function void
test_createRng(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);

    for (int32_t seed = 0; seed < 100; seed++) {
        prb_Rng rng = prb_createRng(seed);
        prb_assert((rng.inc & 1) != 0);
    }
}

function void
test_randomU32(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);
    for (int32_t seed = 0; seed < 100; seed++) {
        prb_Rng rng = prb_createRng(seed);
        int32_t odds = 0;
        for (int32_t ind = 0; ind < 1000; ind++) {
            u32 num = prb_randomU32(&rng);
            odds += num & 1;
        }
        // NOTE(khvorov) 48 is about 3 sd in this case
        int32_t oddsMin = 500 - 48;
        int32_t oddsMax = 500 + 48;
        prb_assert(odds >= oddsMin && odds <= oddsMax);
    }
}

function void
test_randomU32Bound(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);
    for (int32_t seed = 0; seed < 100; seed++) {
        prb_Rng rng = prb_createRng(seed);
        for (uint32_t bound = 1; bound < 20; bound++) {
            for (int32_t ind = 0; ind < 1000; ind++) {
                u32 num = prb_randomU32Bound(&rng, bound);
                prb_assert(num < bound);
            }
        }
    }
}

function void
test_randomF3201(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);
    for (int32_t seed = 0; seed < 100; seed++) {
        prb_Rng rng = prb_createRng(seed);
        for (uint32_t bound = 1; bound < 20; bound++) {
            for (int32_t ind = 0; ind < 1000; ind++) {
                float num = prb_randomF3201(&rng);
                prb_assert(num >= 0.0f && num < 1.0f);
            }
        }
    }
}

//
// SECTION Fileformat
//

function void
assertArrsAreTheSame(prb_Arena* arena, prb_Str* arr1, prb_Str* arr2) {
    prb_Str* arr1NotInArr2 = setdiff(arr1, arr2);
    if (arrlen(arr1NotInArr2) > 0) {
        prb_writelnToStdout(arena, prb_STR("in arr1 but not in arr2:"));
        for (i32 index = 0; index < arrlen(arr1NotInArr2); index++) {
            prb_Str str = arr1NotInArr2[index];
            prb_writeToStdout(str);
            prb_writeToStdout(prb_STR("\n"));
        }
    }
    prb_assert(arrlen(arr1NotInArr2) == 0);
    arrfree(arr1NotInArr2);

    prb_Str* implNotInHeader = setdiff(arr2, arr1);
    if (arrlen(implNotInHeader) > 0) {
        prb_writelnToStdout(arena, prb_STR("in arr2 but not in arr1:"));
        for (i32 index = 0; index < arrlen(implNotInHeader); index++) {
            prb_Str str = implNotInHeader[index];
            prb_writeToStdout(str);
            prb_writeToStdout(prb_STR("\n"));
        }
    }
    prb_assert(arrlen(implNotInHeader) == 0);
    arrfree(implNotInHeader);

    for (i32 index = 0; index < arrlen(arr1); index++) {
        prb_Str arr1Name = arr1[index];
        prb_Str implName = arr2[index];
        prb_assert(prb_streq(arr1Name, implName));
    }
}

function void
test_fileformat(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory           temp = prb_beginTempMemory(arena);
    prb_Str                  fileParent = prb_getParentDir(arena, prb_STR(__FILE__));
    prb_Str                  rootDir = prb_getParentDir(arena, fileParent);
    prb_Str                  prbFilepath = prb_pathJoin(arena, rootDir, prb_STR("cbuild.h"));
    prb_ReadEntireFileResult fileContents = prb_readEntireFile(arena, prbFilepath);
    prb_assert(fileContents.success);
    prb_StrScanner lineIter = prb_createStrScanner(prb_strFromBytes(fileContents.content));

    prb_Str*        headerNames = 0;
    prb_StrFindSpec lineBreakSpec = {};
    lineBreakSpec.mode = prb_StrFindMode_LineBreak;
    while (prb_strScannerMove(&lineIter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success) {
        prb_assert(!prb_strStartsWith(lineIter.betweenLastMatches, prb_STR("prb_PUBLICDEF")));

        if (prb_strStartsWith(lineIter.betweenLastMatches, prb_STR("// SECTION"))) {
            prb_Str name = prb_fmt(arena, "%.*s", prb_LIT(lineIter.betweenLastMatches));
            arrput(headerNames, name);
        } else if (prb_strStartsWith(lineIter.betweenLastMatches, prb_STR("prb_PUBLICDEC"))) {
            prb_StrScanner  scanner = prb_createStrScanner(lineIter.betweenLastMatches);
            prb_StrFindSpec bracket = {};
            bracket.pattern = prb_STR("(");
            prb_assert(prb_strScannerMove(&scanner, bracket, prb_StrScannerSide_AfterMatch));
            prb_StrFindSpec space = {};
            space.pattern = prb_STR(" ");
            space.direction = prb_StrDirection_FromEnd;
            prb_assert(prb_strScannerMove(&scanner, space, prb_StrScannerSide_BeforeMatch));
            arrput(headerNames, scanner.betweenLastMatches);
        } else if (prb_strStartsWith(lineIter.betweenLastMatches, prb_STR("#ifndef prb_NO_IMPLEMENTATION"))) {
            break;
        }
    }

    prb_Str* implNames = 0;
    while (prb_strScannerMove(&lineIter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success) {
        prb_assert(!prb_strStartsWith(lineIter.betweenLastMatches, prb_STR("prb_PUBLICDEC")));

        if (prb_strStartsWith(lineIter.betweenLastMatches, prb_STR("// SECTION"))) {
            prb_StrFindSpec spec = {};
            spec.pattern = prb_STR(" (implementation)");
            prb_StrFindResult implementationRes = prb_strFind(lineIter.betweenLastMatches, spec);
            prb_assert(implementationRes.found);
            arrput(implNames, implementationRes.beforeMatch);
        } else if (prb_strStartsWith(lineIter.betweenLastMatches, prb_STR("prb_PUBLICDEF"))) {
            prb_assert(prb_strScannerMove(&lineIter, lineBreakSpec, prb_StrScannerSide_AfterMatch) == prb_Success);
            prb_assert(prb_strStartsWith(lineIter.betweenLastMatches, prb_STR("prb_")));
            prb_StrFindSpec spec = {};
            spec.pattern = prb_STR("(");
            prb_StrFindResult nameLenRes = prb_strFind(lineIter.betweenLastMatches, spec);
            prb_assert(nameLenRes.found);
            arrput(implNames, nameLenRes.beforeMatch);
        }
    }

    assertArrsAreTheSame(arena, headerNames, implNames);

    prb_Str*                 testNames = 0;
    prb_ReadEntireFileResult testFileReadResut = prb_readEntireFile(arena, prb_STR(__FILE__));
    prb_assert(testFileReadResut.success);
    prb_Str        testFileContent = prb_strFromBytes(testFileReadResut.content);
    prb_StrScanner testFileLineIter = prb_createStrScanner(testFileContent);
    while (prb_strScannerMove(&testFileLineIter, lineBreakSpec, prb_StrScannerSide_AfterMatch)) {
        prb_Str testFunctionsPrefix = prb_STR("test_");
        if (prb_strStartsWith(testFileLineIter.betweenLastMatches, prb_STR("// SECTION"))) {
            if (prb_streq(testFileLineIter.betweenLastMatches, prb_STR("// SECTION Fileformat"))) {
                break;
            } else {
                arrput(testNames, testFileLineIter.betweenLastMatches);
            }
        } else if (prb_strStartsWith(testFileLineIter.betweenLastMatches, testFunctionsPrefix)) {
            prb_StrFindSpec bracketSpec = {};
            bracketSpec.pattern = prb_STR("(");
            prb_StrFindResult bracket = prb_strFind(testFileLineIter.betweenLastMatches, bracketSpec);
            prb_assert(bracket.found);
            testNameToPrbName(arena, bracket.beforeMatch, &testNames);
        }
    }

    arrput(testNames, prb_STR("// SECTION stb snprintf"));
    arrput(testNames, prb_STR("// SECTION stb ds"));
    assertArrsAreTheSame(arena, headerNames, testNames);

    prb_Str* testNamesInMain = 0;
    while (prb_strScannerMove(&testFileLineIter, lineBreakSpec, prb_StrScannerSide_AfterMatch)) {
        if (prb_strStartsWith(testFileLineIter.betweenLastMatches, prb_STR("main() {"))) {
            break;
        }
    }
    while (prb_strScannerMove(&testFileLineIter, lineBreakSpec, prb_StrScannerSide_AfterMatch)) {
        if (prb_strStartsWith(testFileLineIter.betweenLastMatches, prb_STR("    // SECTION"))) {
            if (prb_streq(testFileLineIter.betweenLastMatches, prb_STR("    // SECTION Fileformat"))) {
                break;
            } else {
                arrput(testNamesInMain, prb_strSlice(testFileLineIter.betweenLastMatches, 4, testFileLineIter.betweenLastMatches.len));
            }
        } else {
            prb_StrScanner scanner = prb_createStrScanner(testFileLineIter.betweenLastMatches);
            prb_StrFindSpec spec = {};
            spec.pattern = prb_STR("test_");
            if (prb_strScannerMove(&scanner, spec, prb_StrScannerSide_AfterMatch)) {
                spec.pattern = prb_STR("(,");
                spec.mode = prb_StrFindMode_AnyChar;
                if (prb_strScannerMove(&scanner, spec, prb_StrScannerSide_AfterMatch)) {
                    prb_Str name = prb_fmt(arena, "test_%.*s", prb_LIT(scanner.betweenLastMatches));
                    testNameToPrbName(arena, name, &testNamesInMain);
                }
            }            
        }
    }

    arrput(testNamesInMain, prb_STR("// SECTION stb snprintf"));
    arrput(testNamesInMain, prb_STR("// SECTION stb ds"));
    assertArrsAreTheSame(arena, headerNames, testNamesInMain);

    arrfree(headerNames);
    arrfree(implNames);
    arrfree(testNames);
    arrfree(testNamesInMain);
    prb_endTempMemory(temp);
}

int
main() {
    prb_TimeStart testStart = prb_timeStart();
    prb_Arena     arena_ = prb_createArenaFromVmem(1 * prb_GIGABYTE);
    prb_Arena*    arena = &arena_;
    void*         baseStart = arena->base;
    prb_assert(arena->tempCount == 0);

    prb_Str* args = prb_getCmdArgs(arena);
    if (arrlen(args) >= 2) {
        globalSuffix = args[1];
    }

    prb_Job* jobs = 0;

    arrput(jobs, prb_createJob(testMacros, 0, arena, 10 * prb_MEGABYTE));

    // SECTION Memory
    arrput(jobs, prb_createJob(test_memeq, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getOffsetForAlignment, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_vmemAlloc, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_createArenaFromVmem, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_createArenaFromArena, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_arenaAllocAndZero, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_arenaAlignFreePtr, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_arenaFreePtr, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_arenaFreeSize, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_arenaChangeUsed, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_beginTempMemory, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_endTempMemory, 0, arena, 10 * prb_MEGABYTE));

    // SECTION Filesystem
    arrput(jobs, prb_createJob(test_pathExists, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_pathIsAbsolute, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getAbsolutePath, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_isDir, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_isFile, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_dirIsEmpty, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_createDirIfNotExists, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_removeFileOrDirIfExists, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_removeFileIfExists, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_removeDirIfExists, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_clearDir, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getWorkingDir, 0, arena, 10 * prb_MEGABYTE));
    test_setWorkingDir(arena, 0);  // NOTE(khvorov) Changes global state
    arrput(jobs, prb_createJob(test_pathJoin, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_charIsSep, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getParentDir, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getLastEntryInPath, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_replaceExt, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_pathEntryIter, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getAllDirEntries, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getLastModified, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_createMultitime, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_multitimeAdd, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_readEntireFile, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_writeEntireFile, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_binaryToCArray, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getFileHash, 0, arena, 10 * prb_MEGABYTE));

    // SECTION Strings
    arrput(jobs, prb_createJob(test_streq, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strSlice, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strGetNullTerminated, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strFromBytes, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strTrimSide, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strTrim, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strFind, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strStartsWith, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strEndsWith, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strReplace, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_stringsJoin, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_beginStr, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_addStrSegment, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_endStr, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_vfmtCustomBuffer, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_fmt, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_writeToStdout, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_utf8CharIter, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strScanner, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_parseNumber, 0, arena, 10 * prb_MEGABYTE));

    // SECTION Processes
    arrput(jobs, prb_createJob(test_terminate, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getCmdline, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getCmdArgs, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getArgArrayFromStr, 0, arena, 10 * prb_MEGABYTE));
    test_preventExecutionOnCores(arena, 0);  // NOTE(khvorov) Changes global state
    arrput(jobs, prb_createJob(test_process, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_sleep, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_debuggerPresent, 0, arena, 10 * prb_MEGABYTE));
    test_env(arena, 0);  // NOTE(khvorov) Changes global state

    // SECTION Timing
    arrput(jobs, prb_createJob(test_timeStart, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getMsFrom, 0, arena, 10 * prb_MEGABYTE));

    // SECTION Multithreading
    arrput(jobs, prb_createJob(test_jobs, 0, arena, 10 * prb_MEGABYTE));

    // SECTION Random numbers
    arrput(jobs, prb_createJob(test_createRng, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_randomU32, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_randomU32Bound, 0, arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_randomF3201, 0, arena, 10 * prb_MEGABYTE));

    // SECTION Fileformat
    arrput(jobs, prb_createJob(test_fileformat, 0, arena, 10 * prb_MEGABYTE));

    // NOTE(khvorov) Running multithreaded is not necessarily faster here but it does test that codepath
    prb_Background threadMode = prb_Background_Yes;
    if (prb_debuggerPresent(arena)) {
        threadMode = prb_Background_No;
    }
    prb_assert(prb_launchJobs(jobs, arrlen(jobs), threadMode));
    prb_assert(prb_waitForJobs(jobs, arrlen(jobs)));

    prb_assert(arena->tempCount == 0);
    prb_assert(arena->base == baseStart);

    prb_writelnToStdout(arena, prb_fmt(arena, "%stests took %.2fms%s", prb_colorEsc(prb_ColorID_Green).ptr, prb_getMsFrom(testStart), prb_colorEsc(prb_ColorID_Reset).ptr));

    prb_terminate(0);
    prb_assert(!"unreachable");
    return 0;
}
