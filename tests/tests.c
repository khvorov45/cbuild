#include "../programmable_build.h"

#define function static

typedef uint8_t  u8;
typedef uint64_t u64;
typedef int32_t  i32;
typedef uint32_t u32;
typedef size_t   usize;

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
testNameToPrbName(prb_Arena* arena, prb_String testName, prb_String** prbNames) {
    if (prb_streq(testName, prb_STR("pathFindIter"))) {
        arrput(*prbNames, prb_STR("prb_createPathFindIter"));
        arrput(*prbNames, prb_STR("prb_pathFindIterNext"));
        arrput(*prbNames, prb_STR("prb_destroyPathFindIter"));
    } else if (prb_streq(testName, prb_STR("strFindIter"))) {
        arrput(*prbNames, prb_STR("prb_createStrFindIter"));
        arrput(*prbNames, prb_STR("prb_strFindIterNext"));
    } else if (prb_streq(testName, prb_STR("utf8CharIter"))) {
        arrput(*prbNames, prb_STR("prb_createUtf8CharIter"));
        arrput(*prbNames, prb_STR("prb_utf8CharIterNext"));
    } else if (prb_streq(testName, prb_STR("lineIter"))) {
        arrput(*prbNames, prb_STR("prb_createLineIter"));
        arrput(*prbNames, prb_STR("prb_lineIterNext"));
    } else if (prb_streq(testName, prb_STR("wordIter"))) {
        arrput(*prbNames, prb_STR("prb_createWordIter"));
        arrput(*prbNames, prb_STR("prb_wordIterNext"));
    } else {
        prb_String nameWithPrefix = prb_fmt(arena, "prb_%.*s", prb_LIT(testName));
        arrput(*prbNames, nameWithPrefix);
    }
}

function prb_String
getTempDir(prb_Arena* arena, const char* funcName) {
    prb_String funcNameWithNonascii = prb_fmt(arena, "%s太阳😐", funcName);
    prb_String dir = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), funcNameWithNonascii);
    return dir;
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

    prb_assert(prb_getOffsetForAlignment((void*)0, 1) == 0);
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
    prb_String     arenaStr = prb_fmt(arena, "arena");
    prb_String     newArenaStr = prb_fmt(&newArena, "new");
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

    prb_String dir = getTempDir(arena, __FUNCTION__);
    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_pathExists(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_pathExists(arena, dir));
    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_pathExists(arena, dir));

    prb_String dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_removeDirectoryIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_pathExists(arena, dirTrailingSlash));
    prb_assert(!prb_pathExists(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(prb_pathExists(arena, dirTrailingSlash));
    prb_assert(prb_pathExists(arena, dir));
    prb_assert(prb_removeDirectoryIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_pathExists(arena, dirTrailingSlash));
    prb_assert(!prb_pathExists(arena, dir));

    prb_String dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_removeDirectoryIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_pathExists(arena, dirNotNull));
    prb_assert(!prb_pathExists(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dirNotNull) == prb_Success);
    prb_assert(prb_pathExists(arena, dirNotNull));
    prb_assert(prb_pathExists(arena, dir));
    prb_assert(prb_removeDirectoryIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_pathExists(arena, dirNotNull));
    prb_assert(!prb_pathExists(arena, dir));

    prb_String filepath = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_String filepathNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(filepath));
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
test_getAbsolutePath(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_String filename = prb_STR("test.txt");
    prb_String fileAbs = prb_getAbsolutePath(arena, filename);
    prb_String fileWithCwd = prb_pathJoin(arena, prb_getWorkingDir(arena), filename);
    prb_assert(prb_streq(fileAbs, fileWithCwd));
}

function void
test_isDirectory(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String dir = getTempDir(arena, __FUNCTION__);
    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dir));

    prb_String dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_removeDirectoryIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dirTrailingSlash));
    prb_assert(!prb_isDirectory(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(prb_isDirectory(arena, dirTrailingSlash));
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(prb_removeDirectoryIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dirTrailingSlash));
    prb_assert(!prb_isDirectory(arena, dir));

    prb_String dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_removeDirectoryIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dirNotNull));
    prb_assert(!prb_isDirectory(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dirNotNull) == prb_Success);
    prb_assert(prb_isDirectory(arena, dirNotNull));
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(prb_removeDirectoryIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dirNotNull));
    prb_assert(!prb_isDirectory(arena, dir));

    prb_assert(!prb_isDirectory(arena, prb_STR(__FILE__)));

    prb_endTempMemory(temp);
}

function void
test_isFile(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String filepath = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_String filepathNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(filepath));
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
test_directoryIsEmpty(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String dir = getTempDir(arena, __FUNCTION__);
    prb_assert(prb_clearDirectory(arena, dir) == prb_Success);
    prb_assert(prb_directoryIsEmpty(arena, dir));
    prb_String filepath = prb_pathJoin(arena, dir, prb_STR("file.txt"));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(!prb_directoryIsEmpty(arena, dir));
    prb_assert(prb_removeFileIfExists(arena, filepath) == prb_Success);
    prb_assert(prb_directoryIsEmpty(arena, dir));

    prb_String dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_directoryIsEmpty(arena, dirTrailingSlash));
    prb_assert(prb_directoryIsEmpty(arena, dir));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(!prb_directoryIsEmpty(arena, dirTrailingSlash));
    prb_assert(!prb_directoryIsEmpty(arena, dir));
    prb_assert(prb_removeFileIfExists(arena, filepath) == prb_Success);
    prb_assert(prb_directoryIsEmpty(arena, dirTrailingSlash));
    prb_assert(prb_directoryIsEmpty(arena, dir));

    prb_String dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_directoryIsEmpty(arena, dirNotNull));
    prb_assert(prb_directoryIsEmpty(arena, dir));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(!prb_directoryIsEmpty(arena, dirNotNull));
    prb_assert(!prb_directoryIsEmpty(arena, dir));
    prb_assert(prb_removeFileIfExists(arena, filepath) == prb_Success);
    prb_assert(prb_directoryIsEmpty(arena, dirNotNull));
    prb_assert(prb_directoryIsEmpty(arena, dir));

    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);
    prb_endTempMemory(temp);
}

function void
test_createDirIfNotExists(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String dir = getTempDir(arena, __FUNCTION__);
    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);

    prb_String dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_createDirIfNotExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(prb_isDirectory(arena, dirTrailingSlash));
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(prb_removeFileOrDirectoryIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dirTrailingSlash));
    prb_assert(!prb_isDirectory(arena, dir));

    prb_String dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_createDirIfNotExists(arena, dirNotNull) == prb_Success);
    prb_assert(prb_isDirectory(arena, dirNotNull));
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(prb_removeFileOrDirectoryIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dirNotNull));
    prb_assert(!prb_isDirectory(arena, dir));

    prb_endTempMemory(temp);
}

function void
test_removeFileOrDirectoryIfExists(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String dir = getTempDir(arena, __FUNCTION__);
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);

    prb_String filepath = prb_pathJoin(arena, dir, prb_STR("file.txt"));
    prb_String filepathNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(filepath));
    filepathNotNull.len = filepath.len;
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);

    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(prb_isFile(arena, filepath));

    prb_assert(prb_removeFileOrDirectoryIfExists(arena, filepath) == prb_Success);
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(!prb_isFile(arena, filepath));

    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);

    prb_assert(prb_removeFileOrDirectoryIfExists(arena, filepathNotNull) == prb_Success);
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(!prb_isFile(arena, filepath));

    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);

    prb_assert(prb_removeFileOrDirectoryIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dir));
    prb_assert(!prb_isFile(arena, filepath));

    prb_String dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_createDirIfNotExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(prb_isDirectory(arena, dirTrailingSlash));
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(prb_removeFileOrDirectoryIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dirTrailingSlash));
    prb_assert(!prb_isDirectory(arena, dir));

    prb_String dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_createDirIfNotExists(arena, dirNotNull) == prb_Success);
    prb_assert(prb_isDirectory(arena, dirNotNull));
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(prb_removeFileOrDirectoryIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dirNotNull));
    prb_assert(!prb_isDirectory(arena, dir));

    prb_endTempMemory(temp);
}

function void
test_removeFileIfExists(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String dir = getTempDir(arena, __FUNCTION__);
    prb_assert(prb_clearDirectory(arena, dir) == prb_Success);

    prb_String filepath = prb_pathJoin(arena, dir, prb_STR("file.txt"));
    prb_String filepathNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(filepath));
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

    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);

    prb_endTempMemory(temp);
}

function void
test_removeDirectoryIfExists(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String dir = getTempDir(arena, __FUNCTION__);
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dir));

    prb_String dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_createDirIfNotExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(prb_isDirectory(arena, dirTrailingSlash));
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(prb_removeDirectoryIfExists(arena, dirTrailingSlash) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dirTrailingSlash));
    prb_assert(!prb_isDirectory(arena, dir));

    prb_String dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_createDirIfNotExists(arena, dirNotNull) == prb_Success);
    prb_assert(prb_isDirectory(arena, dirNotNull));
    prb_assert(prb_isDirectory(arena, dir));
    prb_assert(prb_removeDirectoryIfExists(arena, dirNotNull) == prb_Success);
    prb_assert(!prb_isDirectory(arena, dirNotNull));
    prb_assert(!prb_isDirectory(arena, dir));

    prb_endTempMemory(temp);
}

function void
test_clearDirectory(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String dir = getTempDir(arena, __FUNCTION__);
    prb_assert(prb_clearDirectory(arena, dir) == prb_Success);
    prb_assert(prb_directoryIsEmpty(arena, dir));

    prb_String filepath = prb_pathJoin(arena, dir, prb_STR("file.txt"));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);

    prb_assert(!prb_directoryIsEmpty(arena, dir));
    prb_assert(prb_clearDirectory(arena, dir) == prb_Success);
    prb_assert(prb_directoryIsEmpty(arena, dir));

    prb_String dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(!prb_directoryIsEmpty(arena, dir));
    prb_assert(!prb_directoryIsEmpty(arena, dirTrailingSlash));
    prb_assert(prb_clearDirectory(arena, dirTrailingSlash) == prb_Success);
    prb_assert(prb_directoryIsEmpty(arena, dirTrailingSlash));
    prb_assert(prb_directoryIsEmpty(arena, dir));

    prb_String dirNotNull = prb_fmt(arena, "%.*sabs", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_writeEntireFile(arena, filepath, "1", 1) == prb_Success);
    prb_assert(!prb_directoryIsEmpty(arena, dir));
    prb_assert(!prb_directoryIsEmpty(arena, dirNotNull));
    prb_assert(prb_clearDirectory(arena, dirNotNull) == prb_Success);
    prb_assert(prb_directoryIsEmpty(arena, dirNotNull));
    prb_assert(prb_directoryIsEmpty(arena, dir));

    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);

    prb_endTempMemory(temp);
}

function void
test_getWorkingDir(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String cwd = prb_getWorkingDir(arena);
    prb_assert(prb_isDirectory(arena, cwd));
    prb_String filename = prb_STR(__FUNCTION__);
    prb_assert(prb_writeEntireFile(arena, filename, filename.ptr, filename.len) == prb_Success);
    prb_ReadEntireFileResult fileContent = prb_readEntireFile(arena, prb_pathJoin(arena, cwd, filename));
    prb_assert(fileContent.success);
    prb_assert(prb_streq((prb_String) {(const char*)fileContent.content.data, fileContent.content.len}, filename));
    prb_assert(prb_removeFileIfExists(arena, filename) == prb_Success);

    prb_endTempMemory(temp);
}

function void
test_setWorkingDir(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String cwdInit = prb_getWorkingDir(arena);
    prb_String newWd = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_String newWdAbsolute = prb_getAbsolutePath(arena, newWd);
    prb_assert(prb_setWorkingDir(arena, newWd) == prb_Failure);
    prb_assert(prb_createDirIfNotExists(arena, newWd) == prb_Success);
    prb_assert(prb_setWorkingDir(arena, newWd) == prb_Success);
    prb_assert(prb_streq(prb_getWorkingDir(arena), newWdAbsolute));
    prb_String filename = prb_STR("test.txt");
    prb_assert(prb_writeEntireFile(arena, filename, filename.ptr, filename.len) == prb_Success);
    prb_ReadEntireFileResult fileRead = prb_readEntireFile(arena, prb_pathJoin(arena, newWd, filename));
    prb_assert(!fileRead.success);
    prb_setWorkingDir(arena, cwdInit);
    fileRead = prb_readEntireFile(arena, prb_pathJoin(arena, newWd, filename));
    prb_assert(fileRead.success);
    prb_assert(prb_streq(prb_strFromBytes(fileRead.content), filename));

    prb_removeDirectoryIfExists(arena, newWd);
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
test_findSepBeforeLastEntry(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    {
        prb_StringFindResult res = prb_findSepBeforeLastEntry(prb_STR("test/path"));
        prb_assert(res.found);
        prb_assert(res.matchByteIndex == prb_strlen("test"));
        prb_assert(res.matchLen == 1);
    }

    {
        prb_StringFindResult res = prb_findSepBeforeLastEntry(prb_STR("test/path/"));
        prb_assert(res.found);
        prb_assert(res.matchByteIndex == prb_strlen("test"));
        prb_assert(res.matchLen == 1);
    }

    {
        prb_StringFindResult res = prb_findSepBeforeLastEntry(prb_STR("test/path2/path"));
        prb_assert(res.found);
        prb_assert(res.matchByteIndex == prb_strlen("test/path2"));
        prb_assert(res.matchLen == 1);
    }

    {
        prb_StringFindResult res = prb_findSepBeforeLastEntry(prb_STR("test"));
        prb_assert(!res.found);
    }

#if prb_PLATFORM_WINDOWS
    {
        prb_StringFindResult res = prb_findSepBeforeLastEntry(prb_STR("C:\\\\"));
        prb_assert(!res.found);
    }

    {
        prb_StringFindResult res = prb_findSepBeforeLastEntry(prb_STR("C:\\\\test"));
        prb_assert(res.found);
        prb_assert(res.matchByteIndex == 3);
        prb_assert(res.matchLen == 1);
    }

    {
        prb_StringFindResult res = prb_findSepBeforeLastEntry(prb_STR("C:\\\\test/"));
        prb_assert(res.found);
        prb_assert(res.matchByteIndex == 3);
        prb_assert(res.matchLen == 1);
    }

    {
        prb_StringFindResult res = prb_findSepBeforeLastEntry(prb_STR("//network"));
        prb_assert(!res.found);
    }
#elif prb_PLATFORM_LINUX
    {
        prb_StringFindResult res = prb_findSepBeforeLastEntry(prb_STR("/"));
        prb_assert(!res.found);
    }

    {
        prb_StringFindResult res = prb_findSepBeforeLastEntry(prb_STR("/test"));
        prb_assert(res.found);
        prb_assert(res.matchByteIndex == 0);
        prb_assert(res.matchLen == 1);
    }

    {
        prb_StringFindResult res = prb_findSepBeforeLastEntry(prb_STR("/test/"));
        prb_assert(res.found);
        prb_assert(res.matchByteIndex == 0);
        prb_assert(res.matchLen == 1);
    }
#endif

    prb_endTempMemory(temp);
}

function void
test_getParentDir(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_assert(prb_streq(prb_getParentDir(arena, prb_STR("test/path")), prb_STR("test/")));
    prb_assert(prb_streq(prb_getParentDir(arena, prb_STR("test/path/")), prb_STR("test/")));
    prb_assert(prb_streq(prb_getParentDir(arena, prb_STR("test/path2/path")), prb_STR("test/path2/")));

    prb_assert(prb_streq(prb_getParentDir(arena, prb_STR("test")), prb_getWorkingDir(arena)));

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

    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("test/path")), prb_STR("path")));
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("test/path/")), prb_STR("path/")));
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("test/path2/path")), prb_STR("path")));

    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("test")), prb_STR("test")));

#if prb_PLATFORM_WINDOWS
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("C:\\\\test")), prb_STR("C:\\\\")));
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("C:\\\\test/")), prb_STR("C:\\\\")));
#elif prb_PLATFORM_LINUX
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("/test")), prb_STR("test")));
    prb_assert(prb_streq(prb_getLastEntryInPath(prb_STR("/test/")), prb_STR("test/")));
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
test_pathFindIter(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String dir = getTempDir(arena, __FUNCTION__);
    prb_String dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_String dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_String globPattern = prb_STR("*.h");
    prb_String globPatternNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(globPattern));
    globPatternNotNull.len = globPattern.len;
    prb_String regexPattern = prb_STR("^f");
    prb_String regexPatternNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(regexPattern));
    regexPatternNotNull.len = regexPattern.len;

    prb_assert(prb_clearDirectory(arena, dir) == prb_Success);

    prb_String files[] = {
        prb_pathJoin(arena, dir, prb_STR("f1.c")),
        prb_pathJoin(arena, dir, prb_STR("h2.h")),
        prb_pathJoin(arena, dir, prb_STR("f3.c")),
        prb_pathJoin(arena, dir, prb_STR("h4.h")),
    };

    for (usize fileIndex = 0; fileIndex < prb_arrayLength(files); fileIndex++) {
        prb_String file = files[fileIndex];
        prb_assert(prb_writeEntireFile(arena, file, file.ptr, file.len) == prb_Success);
    }

    prb_PathFindSpec spec = {};
    spec.arena = arena;
    spec.dir = dir;
    spec.mode = prb_PathFindMode_AllEntriesInDir;
    prb_PathFindIterator iter = prb_createPathFindIter(spec);
    spec.dir = dirTrailingSlash;
    prb_PathFindIterator iterTrailingSlash = prb_createPathFindIter(spec);
    spec.dir = dirNotNull;
    prb_PathFindIterator iterNotNull = prb_createPathFindIter(spec);

    i32 filesFound[] = {0, 0, 0, 0};
    i32 totalEntries = 0;
    prb_assert(prb_arrayLength(filesFound) == prb_arrayLength(files));
    for (; prb_pathFindIterNext(&iter) == prb_Success; totalEntries++) {
        bool found = false;
        for (usize fileIndex = 0; fileIndex < prb_arrayLength(files) && !found; fileIndex++) {
            prb_String file = files[fileIndex];
            found = prb_streq(file, iter.curPath);
            if (found) {
                filesFound[fileIndex] += 1;
            }
        }
        prb_assert(found);
        prb_assert(prb_pathFindIterNext(&iterNotNull) == prb_Success);
        prb_assert(prb_streq(iter.curPath, iterNotNull.curPath));
        prb_assert(prb_pathFindIterNext(&iterTrailingSlash) == prb_Success);
        prb_assert(prb_streq(iter.curPath, iterTrailingSlash.curPath));
        prb_assert(iter.curMatchCount == totalEntries + 1);
    }

    prb_assert(totalEntries == prb_arrayLength(files));
    prb_assert(iter.curMatchCount == prb_arrayLength(files));
    for (usize fileIndex = 0; fileIndex < prb_arrayLength(files); fileIndex++) {
        prb_assert(filesFound[fileIndex] == 1);
    }

    prb_assert(prb_pathFindIterNext(&iter) == prb_Failure);
    prb_assert(prb_pathFindIterNext(&iterNotNull) == prb_Failure);

    prb_destroyPathFindIter(&iter);
    prb_PathFindIterator empty = {};
    prb_assert(prb_memeq(&iter, &empty, sizeof(prb_PathFindIterator)));
    prb_destroyPathFindIter(&iterNotNull);
    prb_destroyPathFindIter(&iterTrailingSlash);

    prb_PathFindSpec iterGlobSpec = {};
    iterGlobSpec.arena = arena;
    iterGlobSpec.dir = dir;
    iterGlobSpec.mode = prb_PathFindMode_Glob;
    iterGlobSpec.recursive = false;
    iterGlobSpec.pattern = globPattern;
    prb_PathFindIterator iterGlob = prb_createPathFindIter(iterGlobSpec);
    iterGlobSpec.pattern = globPatternNotNull;
    prb_PathFindIterator iterGlobNotNull = prb_createPathFindIter(iterGlobSpec);
    while (prb_pathFindIterNext(&iterGlob) == prb_Success) {
        bool found = false;
        for (usize fileIndex = 0; fileIndex < prb_arrayLength(files) && !found; fileIndex++) {
            prb_String file = files[fileIndex];
            found = prb_streq(file, iterGlob.curPath);
            if (found) {
                filesFound[fileIndex] += 1;
            }
        }
        prb_assert(prb_pathFindIterNext(&iterGlobNotNull) == prb_Success);
        prb_assert(prb_streq(iterGlob.curPath, iterGlobNotNull.curPath));
    }

    prb_assert(iterGlob.curMatchCount == 2);
    prb_assert(filesFound[0] == 1);
    prb_assert(filesFound[1] == 2);
    prb_assert(filesFound[2] == 1);
    prb_assert(filesFound[3] == 2);

    prb_assert(prb_pathFindIterNext(&iterGlob) == prb_Failure);
    prb_assert(prb_pathFindIterNext(&iterGlobNotNull) == prb_Failure);

    prb_destroyPathFindIter(&iterGlob);
    prb_assert(prb_memeq(&iterGlob, &empty, sizeof(prb_PathFindIterator)));
    prb_destroyPathFindIter(&iterGlobNotNull);

    prb_PathFindSpec iterRegexSpec = {};
    iterRegexSpec.arena = arena;
    iterRegexSpec.dir = dir;
    iterRegexSpec.mode = prb_PathFindMode_RegexPosix;
    iterRegexSpec.recursive = false;
    iterRegexSpec.pattern = regexPattern;
    prb_PathFindIterator iterRegex = prb_createPathFindIter(iterRegexSpec);
    iterRegexSpec.pattern = regexPatternNotNull;
    prb_PathFindIterator iterRegexNotNull = prb_createPathFindIter(iterRegexSpec);
    while (prb_pathFindIterNext(&iterRegex) == prb_Success) {
        bool found = false;
        for (usize fileIndex = 0; fileIndex < prb_arrayLength(files) && !found; fileIndex++) {
            prb_String file = files[fileIndex];
            found = prb_streq(file, iterRegex.curPath);
            if (found) {
                filesFound[fileIndex] += 1;
            }
        }
        prb_assert(prb_pathFindIterNext(&iterRegexNotNull) == prb_Success);
        prb_assert(prb_streq(iterRegex.curPath, iterRegexNotNull.curPath));
    }

    prb_assert(iterRegex.curMatchCount == 2);
    prb_assert(filesFound[0] == 2);
    prb_assert(filesFound[1] == 2);
    prb_assert(filesFound[2] == 2);
    prb_assert(filesFound[3] == 2);

    prb_assert(prb_pathFindIterNext(&iterRegex) == prb_Failure);
    prb_assert(prb_pathFindIterNext(&iterRegexNotNull) == prb_Failure);

    prb_destroyPathFindIter(&iterRegex);
    prb_assert(prb_memeq(&iterRegex, &empty, sizeof(prb_PathFindIterator)));
    prb_destroyPathFindIter(&iterRegexNotNull);

    prb_assert(prb_clearDirectory(arena, dir) == prb_Success);
    spec.dir = dir;
    iter = prb_createPathFindIter(spec);
    prb_assert(prb_pathFindIterNext(&iter) == prb_Failure);
    iterGlobSpec.pattern = globPattern;
    iter = prb_createPathFindIter(iterGlobSpec);
    prb_assert(prb_pathFindIterNext(&iter) == prb_Failure);

    // NOTE(khvorov) Recursive search

    prb_String nestedDir = prb_pathJoin(arena, dir, prb_STR("nested"));
    prb_assert(prb_createDirIfNotExists(arena, nestedDir) == prb_Success);
    prb_String nestedFiles[] = {
        prb_pathJoin(arena, nestedDir, prb_STR("fn1.c")),
        prb_pathJoin(arena, nestedDir, prb_STR("hn2.h")),
        prb_pathJoin(arena, nestedDir, prb_STR("fn3.c")),
        prb_pathJoin(arena, nestedDir, prb_STR("hn4.h")),
    };
    prb_assert(prb_arrayLength(nestedFiles) == prb_arrayLength(files));

    prb_String nestedNestedDir = prb_pathJoin(arena, nestedDir, prb_STR("nestednested"));
    prb_assert(prb_createDirIfNotExists(arena, nestedNestedDir) == prb_Success);
    prb_String nestedNestedFiles[] = {
        prb_pathJoin(arena, nestedNestedDir, prb_STR("fnn1.c")),
        prb_pathJoin(arena, nestedNestedDir, prb_STR("hnn2.h")),
        prb_pathJoin(arena, nestedNestedDir, prb_STR("fnn3.c")),
        prb_pathJoin(arena, nestedNestedDir, prb_STR("hnn4.h")),
    };
    prb_assert(prb_arrayLength(nestedNestedFiles) == prb_arrayLength(files));

    prb_String emptyNestedDir = prb_pathJoin(arena, dir, prb_STR("emptynested"));
    prb_assert(prb_createDirIfNotExists(arena, emptyNestedDir) == prb_Success);

    for (usize fileIndex = 0; fileIndex < prb_arrayLength(files); fileIndex++) {
        prb_String file = files[fileIndex];
        prb_assert(prb_writeEntireFile(arena, file, file.ptr, file.len) == prb_Success);
        prb_String nestedFile = nestedFiles[fileIndex];
        prb_assert(prb_writeEntireFile(arena, nestedFile, nestedFile.ptr, nestedFile.len) == prb_Success);
        prb_String nestedNestedFile = nestedNestedFiles[fileIndex];
        prb_assert(prb_writeEntireFile(arena, nestedNestedFile, nestedNestedFile.ptr, nestedNestedFile.len) == prb_Success);
    }

    filesFound[0] = 0;
    filesFound[1] = 0;
    filesFound[2] = 0;
    filesFound[3] = 0;

    i32  nestedFilesFound[] = {0, 0, 0, 0};
    bool nestedDirFound = false;

    i32  nestedNestedFilesFound[] = {0, 0, 0, 0};
    bool nestedNestedDirFound = false;

    bool emptyNestedDirFound = false;

    spec.dir = dir;
    spec.recursive = true;
    prb_PathFindIterator iterRecursive = prb_createPathFindIter(spec);
    while (prb_pathFindIterNext(&iterRecursive) == prb_Success) {
        bool found = false;

        for (usize fileIndex = 0; fileIndex < prb_arrayLength(files) && !found; fileIndex++) {
            prb_String file = files[fileIndex];
            found = prb_streq(file, iterRecursive.curPath);
            if (found) {
                filesFound[fileIndex] += 1;
            }
        }

        for (usize fileIndex = 0; fileIndex < prb_arrayLength(nestedFiles) && !found; fileIndex++) {
            prb_String file = nestedFiles[fileIndex];
            found = prb_streq(file, iterRecursive.curPath);
            if (found) {
                nestedFilesFound[fileIndex] += 1;
            }
        }

        for (usize fileIndex = 0; fileIndex < prb_arrayLength(nestedFiles) && !found; fileIndex++) {
            prb_String file = nestedNestedFiles[fileIndex];
            found = prb_streq(file, iterRecursive.curPath);
            if (found) {
                nestedNestedFilesFound[fileIndex] += 1;
            }
        }

        if (!found) {
            nestedDirFound = nestedDirFound || prb_streq(iterRecursive.curPath, nestedDir);
            nestedNestedDirFound = nestedNestedDirFound || prb_streq(iterRecursive.curPath, nestedNestedDir);
            emptyNestedDirFound = emptyNestedDirFound || prb_streq(iterRecursive.curPath, emptyNestedDir);
        }
    }

    prb_assert(filesFound[0] == 1);
    prb_assert(filesFound[1] == 1);
    prb_assert(filesFound[2] == 1);
    prb_assert(filesFound[3] == 1);

    prb_assert(nestedFilesFound[0] == 1);
    prb_assert(nestedFilesFound[1] == 1);
    prb_assert(nestedFilesFound[2] == 1);
    prb_assert(nestedFilesFound[3] == 1);

    prb_assert(nestedNestedFilesFound[0] == 1);
    prb_assert(nestedNestedFilesFound[1] == 1);
    prb_assert(nestedNestedFilesFound[2] == 1);
    prb_assert(nestedNestedFilesFound[3] == 1);

    prb_assert(nestedDirFound);
    prb_assert(nestedNestedDirFound);
    prb_assert(emptyNestedDirFound);

    prb_assert(iterRecursive.curMatchCount == prb_arrayLength(files) + prb_arrayLength(nestedFiles) + prb_arrayLength(nestedNestedFiles) + 3);

    iterGlobSpec.recursive = true;
    prb_PathFindIterator iterRecursiveGlob = prb_createPathFindIter(iterGlobSpec);
    while (prb_pathFindIterNext(&iterRecursiveGlob) == prb_Success) {
        bool found = false;

        for (usize fileIndex = 0; fileIndex < prb_arrayLength(files) && !found; fileIndex++) {
            prb_String file = files[fileIndex];
            found = prb_streq(file, iterRecursiveGlob.curPath);
            if (found) {
                filesFound[fileIndex] += 1;
            }
        }

        for (usize fileIndex = 0; fileIndex < prb_arrayLength(nestedFiles) && !found; fileIndex++) {
            prb_String file = nestedFiles[fileIndex];
            found = prb_streq(file, iterRecursiveGlob.curPath);
            if (found) {
                nestedFilesFound[fileIndex] += 1;
            }
        }

        for (usize fileIndex = 0; fileIndex < prb_arrayLength(nestedFiles) && !found; fileIndex++) {
            prb_String file = nestedNestedFiles[fileIndex];
            found = prb_streq(file, iterRecursiveGlob.curPath);
            if (found) {
                nestedNestedFilesFound[fileIndex] += 1;
            }
        }
    }

    prb_assert(filesFound[0] == 1);
    prb_assert(filesFound[1] == 2);
    prb_assert(filesFound[2] == 1);
    prb_assert(filesFound[3] == 2);

    prb_assert(nestedFilesFound[0] == 1);
    prb_assert(nestedFilesFound[1] == 2);
    prb_assert(nestedFilesFound[2] == 1);
    prb_assert(nestedFilesFound[3] == 2);

    prb_assert(nestedNestedFilesFound[0] == 1);
    prb_assert(nestedNestedFilesFound[1] == 2);
    prb_assert(nestedNestedFilesFound[2] == 1);
    prb_assert(nestedNestedFilesFound[3] == 2);

    prb_assert(iterRecursiveGlob.curMatchCount == 6);

    iterRegexSpec.recursive = true;
    prb_PathFindIterator iterRecursiveRegex = prb_createPathFindIter(iterRegexSpec);
    while (prb_pathFindIterNext(&iterRecursiveRegex) == prb_Success) {
        bool found = false;

        for (usize fileIndex = 0; fileIndex < prb_arrayLength(files) && !found; fileIndex++) {
            prb_String file = files[fileIndex];
            found = prb_streq(file, iterRecursiveRegex.curPath);
            if (found) {
                filesFound[fileIndex] += 1;
            }
        }

        for (usize fileIndex = 0; fileIndex < prb_arrayLength(nestedFiles) && !found; fileIndex++) {
            prb_String file = nestedFiles[fileIndex];
            found = prb_streq(file, iterRecursiveRegex.curPath);
            if (found) {
                nestedFilesFound[fileIndex] += 1;
            }
        }

        for (usize fileIndex = 0; fileIndex < prb_arrayLength(nestedFiles) && !found; fileIndex++) {
            prb_String file = nestedNestedFiles[fileIndex];
            found = prb_streq(file, iterRecursiveRegex.curPath);
            if (found) {
                nestedNestedFilesFound[fileIndex] += 1;
            }
        }
    }

    prb_assert(filesFound[0] == 2);
    prb_assert(filesFound[1] == 2);
    prb_assert(filesFound[2] == 2);
    prb_assert(filesFound[3] == 2);

    prb_assert(nestedFilesFound[0] == 2);
    prb_assert(nestedFilesFound[1] == 2);
    prb_assert(nestedFilesFound[2] == 2);
    prb_assert(nestedFilesFound[3] == 2);

    prb_assert(nestedNestedFilesFound[0] == 2);
    prb_assert(nestedNestedFilesFound[1] == 2);
    prb_assert(nestedNestedFilesFound[2] == 2);
    prb_assert(nestedNestedFilesFound[3] == 2);

    prb_assert(iterRecursiveRegex.curMatchCount == 6);

    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);
    prb_endTempMemory(temp);
}

function void
test_getLastModified(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_String     dir = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_assert(prb_clearDirectory(arena, dir) == prb_Success);
    prb_String file = prb_pathJoin(arena, dir, prb_STR("f1.c"));

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

    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);
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
    prb_assert(prb_strStartsWith(prb_strFromBytes(readResult.content), prb_STR("#include \"../programmable_build.h\"")));
    prb_endTempMemory(temp);
}

function void
test_writeEntireFile(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_String     dir = getTempDir(arena, __FUNCTION__);
    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);
    prb_String     filepath = prb_pathJoin(arena, dir, prb_STR("filename.txt"));
    prb_assert(prb_writeEntireFile(arena, filepath, filepath.ptr, filepath.len) == prb_Failure);
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_writeEntireFile(arena, filepath, filepath.ptr, filepath.len) == prb_Success);
    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);
    prb_endTempMemory(temp);
}

function void
test_binaryToCArray(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    u8         bytes[] = {0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc};
    prb_String carr = prb_binaryToCArray(arena, prb_STR("testarr"), bytes, prb_arrayLength(bytes));
    prb_assert(prb_streq(carr, prb_STR("unsigned char testarr[] = {\n    0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa,\n    0xb, 0xc\n};")));
}

function void
test_getFileHash(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_String     dir = getTempDir(arena, __FUNCTION__);
    prb_String     filepath = prb_pathJoin(arena, dir, prb_STR("filename.txt"));
    prb_assert(prb_createDirIfNotExists(arena, dir) == prb_Success);
    prb_assert(prb_writeEntireFile(arena, filepath, filepath.ptr, filepath.len) == prb_Success);
    prb_FileHash hash1 = prb_getFileHash(arena, filepath);
    prb_assert(hash1.valid);
    prb_String newContent = prb_STR("content");
    prb_assert(prb_writeEntireFile(arena, filepath, newContent.ptr, newContent.len) == prb_Success);
    prb_FileHash hash2 = prb_getFileHash(arena, filepath);
    prb_assert(hash2.valid);
    prb_assert(hash1.hash != hash2.hash);
    prb_assert(prb_writeEntireFile(arena, filepath, filepath.ptr, filepath.len) == prb_Success);
    prb_FileHash hash3 = prb_getFileHash(arena, filepath);
    prb_assert(hash3.valid);
    prb_assert(hash3.hash == hash1.hash);
    prb_assert(prb_removeDirectoryIfExists(arena, dir) == prb_Success);
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
test_strSliceForward(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_strSliceBetween(prb_Arena* arena, void* data) {
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
test_strMallocCopy(prb_Arena* arena, void* data) {
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

    prb_StringFindSpec spec = {};
    spec.str = prb_STR("p1at4pattern1 pattern2 pattern3p2a.t");
    spec.pattern = prb_STR("pattern");
    spec.mode = prb_StringFindMode_Exact;
    spec.direction = prb_StringDirection_FromStart;

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
    spec.regexPosix.arena = arena;
    {
        prb_StringFindResult res = prb_strFind(spec);
        prb_assert(
            res.found
            && res.matchByteIndex == prb_strlen("prb_PUBLICDEC prb_StringWindow ")
            && res.matchLen == prb_strlen("prb_createStringWindow(")
        );
    }

    spec.str.ptr = "prb_one() prb_2()";
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
test_strFindIter(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory     temp = prb_beginTempMemory(arena);
    prb_String         str = prb_STR("prog arg1:val1 arg2:val2 arg3:val3");
    prb_StringFindSpec spec = {};
    spec.str = str;
    spec.pattern = prb_STR(":");
    spec.mode = prb_StringFindMode_Exact;
    spec.direction = prb_StringDirection_FromStart;

    {
        prb_StrFindIterator iter = prb_createStrFindIter(spec);
        prb_assert(iter.curMatchCount == 0);

        prb_assert(prb_strFindIterNext(&iter) == prb_Success);
        prb_assert(iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == prb_strlen("prog arg1"));
        prb_assert(iter.curResult.matchLen == 1);
        prb_assert(iter.curMatchCount == 1);

        prb_assert(prb_strFindIterNext(&iter) == prb_Success);
        prb_assert(iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == prb_strlen("prog arg1:val1 arg2"));
        prb_assert(iter.curResult.matchLen == 1);
        prb_assert(iter.curMatchCount == 2);

        prb_assert(prb_strFindIterNext(&iter) == prb_Success);
        prb_assert(iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == prb_strlen("prog arg1:val1 arg2:val2 arg3"));
        prb_assert(iter.curResult.matchLen == 1);
        prb_assert(iter.curMatchCount == 3);

        prb_assert(prb_strFindIterNext(&iter) == prb_Failure);
        prb_assert(!iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == 0);
        prb_assert(iter.curResult.matchLen == 0);
        prb_assert(iter.curMatchCount == 3);
    }

    {
        spec.direction = prb_StringDirection_FromEnd;
        prb_StrFindIterator iter = prb_createStrFindIter(spec);
        prb_assert(iter.curMatchCount == 0);

        prb_assert(prb_strFindIterNext(&iter) == prb_Success);
        prb_assert(iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == prb_strlen("prog arg1:val1 arg2:val2 arg3"));
        prb_assert(iter.curResult.matchLen == 1);
        prb_assert(iter.curMatchCount == 1);

        prb_assert(prb_strFindIterNext(&iter) == prb_Success);
        prb_assert(iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == prb_strlen("prog arg1:val1 arg2"));
        prb_assert(iter.curResult.matchLen == 1);
        prb_assert(iter.curMatchCount == 2);

        prb_assert(prb_strFindIterNext(&iter) == prb_Success);
        prb_assert(iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == prb_strlen("prog arg1"));
        prb_assert(iter.curResult.matchLen == 1);
        prb_assert(iter.curMatchCount == 3);

        prb_assert(prb_strFindIterNext(&iter) == prb_Failure);
        prb_assert(!iter.curResult.found);
        prb_assert(iter.curResult.matchByteIndex == 0);
        prb_assert(iter.curResult.matchLen == 0);
        prb_assert(iter.curMatchCount == 3);

        spec.direction = prb_StringDirection_FromStart;
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
test_beginString(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_addStringSegment(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_endString(prb_Arena* arena, void* data) {
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
    prb_unused(arena);
    prb_unused(data);
    prb_assert(prb_writeToStdout(prb_STR("write to stdout test\n")) == prb_Success);
}

function void
test_writelnToStdout(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    prb_assert(prb_writelnToStdout(prb_STR("writeln to stdout test")) == prb_Success);
}

function void
test_setPrintColor(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_writelnToStdout(prb_STR("color printing:"));

    prb_setPrintColor(prb_ColorID_Blue);
    prb_writelnToStdout(prb_STR("blue"));

    prb_setPrintColor(prb_ColorID_Cyan);
    prb_writelnToStdout(prb_STR("cyan"));

    prb_setPrintColor(prb_ColorID_Magenta);
    prb_writelnToStdout(prb_STR("magenta"));

    prb_setPrintColor(prb_ColorID_Yellow);
    prb_writelnToStdout(prb_STR("yellow"));

    prb_setPrintColor(prb_ColorID_Red);
    prb_writelnToStdout(prb_STR("red"));

    prb_setPrintColor(prb_ColorID_Green);
    prb_writelnToStdout(prb_STR("green"));

    prb_setPrintColor(prb_ColorID_Black);
    prb_writelnToStdout(prb_STR("black"));

    prb_setPrintColor(prb_ColorID_White);
    prb_writelnToStdout(prb_STR("white"));

    prb_resetPrintColor();
    prb_writelnToStdout(prb_STR("reset"));

    prb_endTempMemory(temp);
}

function void
test_resetPrintColor(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_utf8CharIter(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    prb_String str = prb_STR("abcדזון是太متشاтипуκαι");
    u32        charsUtf32[] = {97, 98, 99, 1491, 1494, 1493, 1503, 26159, 22826, 1605, 1578, 1588, 1575, 1090, 1080, 1087, 1091, 954, 945, 953};
    i32        utf8Bytes[] = {1, 1, 1, 2, 2, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
    prb_assert(prb_arrayLength(charsUtf32) == prb_arrayLength(utf8Bytes));

    prb_Utf8CharIterator iter = prb_createUtf8CharIter(str, prb_StringDirection_FromStart);
    prb_assert(iter.curCharCount == 0);
    i32 curTotalUtf8Bytes = 0;
    for (i32 charIndex = 0; charIndex < prb_arrayLength(charsUtf32); charIndex++) {
        i32 charUtf8Bytes = utf8Bytes[charIndex];
        prb_assert(prb_utf8CharIterNext(&iter) == prb_Success);
        prb_assert(iter.curCharCount == charIndex + 1);
        prb_assert(iter.curByteOffset == curTotalUtf8Bytes);
        prb_assert(iter.curUtf32Char == charsUtf32[charIndex]);
        prb_assert(iter.curUtf8Bytes == charUtf8Bytes);
        prb_assert(iter.curIsValid);
        curTotalUtf8Bytes += charUtf8Bytes;
    }

    prb_assert(prb_utf8CharIterNext(&iter) == prb_Failure);
    prb_assert(iter.curCharCount == prb_arrayLength(charsUtf32));
}

function void
test_lineIter(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);
    prb_String       lines = prb_STR("line1\r\nline2\nline3\rline4\n\nline6\r\rline8\r\n\r\nline10\r\n\nline12\r\r\nline14");
    prb_LineIterator iter = prb_createLineIter(lines);

    prb_assert(iter.curLineCount == 0);
    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line1")));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 2);
    prb_assert(iter.curLineCount == 1);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line2")));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 2);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line3")));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 3);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line4")));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 4);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 5);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line6")));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 6);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 7);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line8")));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 2);
    prb_assert(iter.curLineCount == 8);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 2);
    prb_assert(iter.curLineCount == 9);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line10")));
    prb_assert(iter.curLine.len == 6);
    prb_assert(iter.curLineEndLen == 2);
    prb_assert(iter.curLineCount == 10);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 11);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line12")));
    prb_assert(iter.curLine.len == 6);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 12);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 2);
    prb_assert(iter.curLineCount == 13);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(iter.curLine, prb_STR("line14")));
    prb_assert(iter.curLine.len == 6);
    prb_assert(iter.curLineEndLen == 0);
    prb_assert(iter.curLineCount == 14);

    prb_assert(prb_lineIterNext(&iter) == prb_Failure);
    prb_assert(iter.curLineCount == 14);

    lines = prb_STR("\n");
    iter = prb_createLineIter(lines);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 1);

    prb_assert(prb_lineIterNext(&iter) == prb_Failure);
}

function void
test_wordIter(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
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
test_getArgArrayFromString(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String strings[] = {prb_STR("prg arg1 arg2 arg3"), prb_STR("  prg arg1 arg2  arg3 ")};

    for (i32 strIndex = 0; strIndex < prb_arrayLength(strings); strIndex++) {
        const char** args = prb_getArgArrayFromString(arena, strings[strIndex]);
        prb_assert(arrlen(args) == 4 + 1);
        prb_assert(prb_streq(prb_STR(args[0]), prb_STR("prg")));
        prb_assert(prb_streq(prb_STR(args[1]), prb_STR("arg1")));
        prb_assert(prb_streq(prb_STR(args[2]), prb_STR("arg2")));
        prb_assert(prb_streq(prb_STR(args[3]), prb_STR("arg3")));
    }

    prb_endTempMemory(temp);
}

function void
test_execCmd(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
}

function void
test_waitForProcesses(prb_Arena* arena, void* data) {
    prb_unused(arena);
    prb_unused(data);
    // TODO(khvorov) Write
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
test_createJob(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);
    // TODO(khvorov) Write
}

function void
test_execJobs(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_unused(arena);
    // TODO(khvorov) Write
}

//
// SECTION Fileformat
//

function void
assertArrsAreTheSame(prb_String* arr1, prb_String* arr2) {
    prb_String* arr1NotInArr2 = setdiff(arr1, arr2);
    if (arrlen(arr1NotInArr2) > 0) {
        prb_writelnToStdout(prb_STR("in arr1 but not in arr2:"));
        for (i32 index = 0; index < arrlen(arr1NotInArr2); index++) {
            prb_String str = arr1NotInArr2[index];
            prb_writeToStdout(str);
            prb_writeToStdout(prb_STR("\n"));
        }
    }
    prb_assert(arrlen(arr1NotInArr2) == 0);
    arrfree(arr1NotInArr2);

    prb_String* implNotInHeader = setdiff(arr2, arr1);
    if (arrlen(implNotInHeader) > 0) {
        prb_writelnToStdout(prb_STR("in arr2 but not in arr1:"));
        for (i32 index = 0; index < arrlen(implNotInHeader); index++) {
            prb_String str = implNotInHeader[index];
            prb_writeToStdout(str);
            prb_writeToStdout(prb_STR("\n"));
        }
    }
    prb_assert(arrlen(implNotInHeader) == 0);
    arrfree(implNotInHeader);

    for (i32 index = 0; index < arrlen(arr1); index++) {
        prb_String arr1Name = arr1[index];
        prb_String implName = arr2[index];
        prb_assert(prb_streq(arr1Name, implName));
    }
}

function void
test_fileformat(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_ReadEntireFileResult fileContents = prb_readEntireFile(arena, prb_STR("programmable_build.h"));
    prb_assert(fileContents.success);
    prb_LineIterator lineIter = prb_createLineIter((prb_String) {(const char*)fileContents.content.data, fileContents.content.len});

    prb_String* headerNames = 0;
    while (prb_lineIterNext(&lineIter) == prb_Success) {
        prb_assert(!prb_strStartsWith(lineIter.curLine, prb_STR("prb_PUBLICDEF")));

        if (prb_strStartsWith(lineIter.curLine, prb_STR("// SECTION"))) {
            prb_String name = prb_fmt(arena, "%.*s", prb_LIT(lineIter.curLine));
            arrput(headerNames, name);
        } else if (prb_strStartsWith(lineIter.curLine, prb_STR("prb_PUBLICDEC"))) {
            prb_StringFindSpec spec = {};
            spec.str = lineIter.curLine;
            spec.pattern = prb_STR("(");
            spec.mode = prb_StringFindMode_AnyChar;
            spec.direction = prb_StringDirection_FromStart;
            prb_StringFindResult onePastNameEndRes = prb_strFind(spec);
            prb_assert(onePastNameEndRes.found);
            prb_String win = {lineIter.curLine.ptr, onePastNameEndRes.matchByteIndex};
            spec.str = win;
            spec.pattern = prb_STR(" ");
            spec.direction = prb_StringDirection_FromEnd;
            prb_StringFindResult nameStartRes = prb_strFind(spec);
            prb_assert(nameStartRes.found);
            win = prb_strSliceForward(win, nameStartRes.matchByteIndex + 1);
            prb_String name = prb_fmt(arena, "%.*s", win.len, win.ptr);
            arrput(headerNames, name);
        } else if (prb_strStartsWith(lineIter.curLine, prb_STR("#ifndef prb_NO_IMPLEMENTATION"))) {
            break;
        }
    }

    prb_String* implNames = 0;
    while (prb_lineIterNext(&lineIter) == prb_Success) {
        prb_assert(!prb_strStartsWith(lineIter.curLine, prb_STR("prb_PUBLICDEC")));

        if (prb_strStartsWith(lineIter.curLine, prb_STR("// SECTION"))) {
            prb_StringFindSpec spec = {};
            spec.str = lineIter.curLine;
            spec.pattern = prb_STR(" (implementation)");
            spec.mode = prb_StringFindMode_Exact;
            spec.direction = prb_StringDirection_FromStart;
            prb_StringFindResult implementationRes = prb_strFind(spec);
            prb_assert(implementationRes.found);
            prb_String name = prb_fmt(arena, "%.*s", implementationRes.matchByteIndex, lineIter.curLine.ptr);
            arrput(implNames, name);
        } else if (prb_strStartsWith(lineIter.curLine, prb_STR("prb_PUBLICDEF"))) {
            prb_assert(prb_lineIterNext(&lineIter) == prb_Success);
            prb_assert(prb_strStartsWith(lineIter.curLine, prb_STR("prb_")));
            prb_StringFindSpec spec = {};
            spec.str = lineIter.curLine;
            spec.pattern = prb_STR("(");
            spec.mode = prb_StringFindMode_AnyChar;
            spec.direction = prb_StringDirection_FromStart;
            prb_StringFindResult nameLenRes = prb_strFind(spec);
            prb_assert(nameLenRes.found);
            prb_String name = prb_fmt(arena, "%.*s", nameLenRes.matchByteIndex, lineIter.curLine.ptr);
            arrput(implNames, name);
        }
    }

    assertArrsAreTheSame(headerNames, implNames);

    prb_String*              testNames = 0;
    prb_ReadEntireFileResult testFileReadResut = prb_readEntireFile(arena, prb_STR(__FILE__));
    prb_assert(testFileReadResut.success);
    prb_String       testFileContent = prb_strFromBytes(testFileReadResut.content);
    prb_LineIterator testFileLineIter = prb_createLineIter(testFileContent);
    while (prb_lineIterNext(&testFileLineIter)) {
        prb_String testFunctionsPrefix = prb_STR("test_");
        if (prb_strStartsWith(testFileLineIter.curLine, prb_STR("// SECTION"))) {
            if (prb_streq(testFileLineIter.curLine, prb_STR("// SECTION Fileformat"))) {
                break;
            } else {
                arrput(testNames, testFileLineIter.curLine);
            }
        } else if (prb_strStartsWith(testFileLineIter.curLine, testFunctionsPrefix)) {
            prb_StringFindSpec bracketSpec = {};
            bracketSpec.str = testFileLineIter.curLine;
            bracketSpec.pattern = prb_STR("(");
            bracketSpec.mode = prb_StringFindMode_AnyChar;
            prb_StringFindResult bracket = prb_strFind(bracketSpec);
            prb_assert(bracket.found);
            prb_String name = prb_strSliceBetween(testFileLineIter.curLine, testFunctionsPrefix.len, bracket.matchByteIndex);
            testNameToPrbName(arena, name, &testNames);
        }
    }

    arrput(testNames, prb_STR("// SECTION stb snprintf"));
    arrput(testNames, prb_STR("// SECTION stb ds"));
    assertArrsAreTheSame(headerNames, testNames);

    prb_String* testNamesInMain = 0;
    while (prb_lineIterNext(&testFileLineIter)) {
        prb_String testCall = prb_STR("    arrput(jobs, prb_createJob(test_");
        if (prb_strStartsWith(testFileLineIter.curLine, prb_STR("    // SECTION"))) {
            if (prb_streq(testFileLineIter.curLine, prb_STR("    // SECTION Fileformat"))) {
                break;
            } else {
                arrput(testNamesInMain, prb_strSliceForward(testFileLineIter.curLine, 4));
            }
        } else if (prb_strStartsWith(testFileLineIter.curLine, testCall)) {
            prb_String         nameOn = prb_strSliceForward(testFileLineIter.curLine, testCall.len);
            prb_StringFindSpec commaSpec = {};
            commaSpec.str = nameOn;
            commaSpec.pattern = prb_STR(",");
            commaSpec.mode = prb_StringFindMode_AnyChar;
            prb_StringFindResult comma = prb_strFind(commaSpec);
            prb_assert(comma.found);
            prb_String name = prb_strSliceBetween(nameOn, 0, comma.matchByteIndex);
            testNameToPrbName(arena, name, &testNamesInMain);
        } else if (prb_strStartsWith(testFileLineIter.curLine, prb_STR("    test_setWorkingDir"))) {
            arrput(testNamesInMain, prb_STR("prb_setWorkingDir"));
        }
    }

    arrput(testNamesInMain, prb_STR("// SECTION stb snprintf"));
    arrput(testNamesInMain, prb_STR("// SECTION stb ds"));
    assertArrsAreTheSame(headerNames, testNamesInMain);

    arrfree(headerNames);
    arrfree(implNames);
    arrfree(testNames);
    prb_endTempMemory(temp);
}

int
main() {
    prb_TimeStart testStart = prb_timeStart();
    prb_Arena     arena = prb_createArenaFromVmem(1 * prb_GIGABYTE);
    void*         baseStart = arena.base;
    prb_assert(arena.tempCount == 0);

    prb_Job* jobs = 0;

    // SECTION Memory
    arrput(jobs, prb_createJob(test_memeq, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getOffsetForAlignment, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_vmemAlloc, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_createArenaFromVmem, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_createArenaFromArena, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_arenaAllocAndZero, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_arenaAlignFreePtr, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_arenaFreePtr, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_arenaFreeSize, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_arenaChangeUsed, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_beginTempMemory, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_endTempMemory, 0, &arena, 10 * prb_MEGABYTE));

    // SECTION Filesystem
    arrput(jobs, prb_createJob(test_pathExists, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getAbsolutePath, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_isDirectory, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_isFile, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_directoryIsEmpty, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_createDirIfNotExists, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_removeFileOrDirectoryIfExists, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_removeFileIfExists, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_removeDirectoryIfExists, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_clearDirectory, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getWorkingDir, 0, &arena, 10 * prb_MEGABYTE));
    test_setWorkingDir(&arena, 0);  // NOTE(khvorov) Messes with global state of the process
    arrput(jobs, prb_createJob(test_pathJoin, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_charIsSep, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_findSepBeforeLastEntry, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getParentDir, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getLastEntryInPath, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_replaceExt, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_pathFindIter, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getLastModified, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_createMultitime, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_multitimeAdd, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_readEntireFile, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_writeEntireFile, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_binaryToCArray, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getFileHash, 0, &arena, 10 * prb_MEGABYTE));

    // SECTION Strings
    arrput(jobs, prb_createJob(test_streq, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strSliceForward, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strSliceBetween, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strGetNullTerminated, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strMallocCopy, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strFromBytes, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strTrimSide, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strTrim, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strFind, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strFindIter, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strStartsWith, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strEndsWith, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strReplace, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_stringsJoin, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_beginString, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_addStringSegment, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_endString, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_vfmtCustomBuffer, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_fmt, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_writeToStdout, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_writelnToStdout, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_setPrintColor, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_resetPrintColor, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_utf8CharIter, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_lineIter, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_wordIter, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_parseNumber, 0, &arena, 10 * prb_MEGABYTE));

    // SECTION Processes
    arrput(jobs, prb_createJob(test_terminate, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getCmdline, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getCmdArgs, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getArgArrayFromString, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_execCmd, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_waitForProcesses, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_sleep, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_debuggerPresent, 0, &arena, 10 * prb_MEGABYTE));

    // SECTION Timing
    arrput(jobs, prb_createJob(test_timeStart, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getMsFrom, 0, &arena, 10 * prb_MEGABYTE));

    // SECTION Multithreading
    arrput(jobs, prb_createJob(test_createJob, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_execJobs, 0, &arena, 10 * prb_MEGABYTE));

    // SECTION Fileformat
    arrput(jobs, prb_createJob(test_fileformat, 0, &arena, 10 * prb_MEGABYTE));

    prb_assert(prb_execJobs(jobs, arrlen(jobs), prb_ThreadMode_Multi) == prb_Success);

    prb_assert(arena.tempCount == 0);
    prb_assert(arena.base == baseStart);

    {
        prb_String msg = prb_fmt(&arena, "tests took %.2fms", prb_getMsFrom(testStart));
        prb_writelnToStdout(msg);
    }

    prb_terminate(0);
    prb_assert(!"unreachable");
    return 0;
}
