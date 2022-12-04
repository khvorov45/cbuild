#include "../programmable_build.h"

// TODO(khvorov) Check test function presence/order

#define function static

typedef uint8_t  u8;
typedef uint64_t u64;
typedef int32_t  i32;
typedef uint32_t u32;
typedef size_t   usize;

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
test_globalArenaCurrentFreePtr(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    uint8_t* ptrInit = (uint8_t*)prb_arenaFreePtr(arena);
    i32      size = 1;
    prb_arenaAllocAndZero(arena, size, 1);
    prb_assert(prb_arenaFreePtr(arena) == ptrInit + size);

    prb_endTempMemory(temp);
}

function void
test_globalArenaCurrentFreeSize(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    i32 sizeInit = prb_arenaFreeSize(arena);
    i32 size = 1;
    prb_arenaAllocAndZero(arena, size, 1);
    prb_assert(prb_arenaFreeSize(arena) == sizeInit - size);

    prb_endTempMemory(temp);
}

function void
test_globalArenaChangeUsed(prb_Arena* arena, void* data) {
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

    prb_String dir = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), prb_STR(__FUNCTION__));
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
test_isDirectory(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String dir = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), prb_STR(__FUNCTION__));
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

    prb_String dir = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), prb_STR(__FUNCTION__));
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

    prb_String dir = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), prb_STR(__FUNCTION__));
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

    prb_String dir = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), prb_STR(__FUNCTION__));
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

    prb_String dir = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), prb_STR(__FUNCTION__));
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

    prb_String dir = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), prb_STR(__FUNCTION__));
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

    prb_String dir = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), prb_STR(__FUNCTION__));
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
test_getCurrentWorkingDir(prb_Arena* arena, void* data) {
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

    prb_endTempMemory(temp);
}

function void
test_pathFindIter(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_String dir = prb_pathJoin(arena, prb_getParentDir(arena, prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_String dirTrailingSlash = prb_fmt(arena, "%.*s/", prb_LIT(dir));
    prb_String dirNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_String pattern = prb_STR("*.h");
    prb_String patternNotNull = prb_fmt(arena, "%.*sabc", prb_LIT(pattern));
    patternNotNull.len = pattern.len;

    prb_assert(prb_clearDirectory(arena, dir) == prb_Success);

    prb_String files[] = {
        prb_pathJoin(arena, dir, prb_STR("f1.c")),
        prb_pathJoin(arena, dir, prb_STR("f2.h")),
        prb_pathJoin(arena, dir, prb_STR("f3.c")),
        prb_pathJoin(arena, dir, prb_STR("f4.h")),
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

    prb_PathFindSpec iterPatternSpec = {};
    iterPatternSpec.arena = arena;
    iterPatternSpec.dir = dir;
    iterPatternSpec.mode = prb_PathFindMode_Glob;
    iterPatternSpec.recursive = false;
    iterPatternSpec.glob.pattern = pattern;
    prb_PathFindIterator iterPattern = prb_createPathFindIter(iterPatternSpec);
    iterPatternSpec.glob.pattern = patternNotNull;
    prb_PathFindIterator iterPatternNotNull = prb_createPathFindIter(iterPatternSpec);
    i32                  totalEntriesPattern = 0;
    for (; prb_pathFindIterNext(&iterPattern) == prb_Success; totalEntriesPattern++) {
        bool found = false;
        for (usize fileIndex = 0; fileIndex < prb_arrayLength(files) && !found; fileIndex++) {
            prb_String file = files[fileIndex];
            found = prb_streq(file, iterPattern.curPath);
            if (found) {
                filesFound[fileIndex] += 1;
            }
        }
        prb_assert(prb_pathFindIterNext(&iterPatternNotNull) == prb_Success);
        prb_assert(prb_streq(iterPattern.curPath, iterPatternNotNull.curPath));
        prb_assert(iterPattern.curMatchCount == totalEntriesPattern + 1);
    }

    prb_assert(totalEntriesPattern == 2);
    prb_assert(iterPattern.curMatchCount == 2);
    prb_assert(filesFound[0] == 1);
    prb_assert(filesFound[1] == 2);
    prb_assert(filesFound[2] == 1);
    prb_assert(filesFound[3] == 2);

    prb_assert(prb_pathFindIterNext(&iterPattern) == prb_Failure);
    prb_assert(prb_pathFindIterNext(&iterPatternNotNull) == prb_Failure);

    prb_destroyPathFindIter(&iterPattern);
    prb_assert(prb_memeq(&iterPattern, &empty, sizeof(prb_PathFindIterator)));
    prb_destroyPathFindIter(&iterPatternNotNull);

    prb_assert(prb_clearDirectory(arena, dir) == prb_Success);
    spec.dir = dir;
    iter = prb_createPathFindIter(spec);
    prb_assert(prb_pathFindIterNext(&iter) == prb_Failure);
    iterPatternSpec.glob.pattern = pattern;
    iter = prb_createPathFindIter(iterPatternSpec);
    prb_assert(prb_pathFindIterNext(&iter) == prb_Failure);

    // NOTE(khvorov) Recursive search

    prb_String nestedDir = prb_pathJoin(arena, dir, prb_STR("nested"));
    prb_assert(prb_createDirIfNotExists(arena, nestedDir) == prb_Success);
    prb_String nestedFiles[] = {
        prb_pathJoin(arena, nestedDir, prb_STR("nf1.c")),
        prb_pathJoin(arena, nestedDir, prb_STR("nf2.h")),
        prb_pathJoin(arena, nestedDir, prb_STR("nf3.c")),
        prb_pathJoin(arena, nestedDir, prb_STR("nf4.h")),
    };
    prb_assert(prb_arrayLength(nestedFiles) == prb_arrayLength(files));

    prb_String nestedNestedDir = prb_pathJoin(arena, nestedDir, prb_STR("nestednested"));
    prb_assert(prb_createDirIfNotExists(arena, nestedNestedDir) == prb_Success);
    prb_String nestedNestedFiles[] = {
        prb_pathJoin(arena, nestedNestedDir, prb_STR("nnf1.c")),
        prb_pathJoin(arena, nestedNestedDir, prb_STR("nnf2.h")),
        prb_pathJoin(arena, nestedNestedDir, prb_STR("nnf3.c")),
        prb_pathJoin(arena, nestedNestedDir, prb_STR("nnf4.h")),
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

    iterPatternSpec.recursive = true;
    prb_PathFindIterator iterRecursivePattern = prb_createPathFindIter(iterPatternSpec);
    while (prb_pathFindIterNext(&iterRecursivePattern) == prb_Success) {
        bool found = false;

        for (usize fileIndex = 0; fileIndex < prb_arrayLength(files) && !found; fileIndex++) {
            prb_String file = files[fileIndex];
            found = prb_streq(file, iterRecursivePattern.curPath);
            if (found) {
                filesFound[fileIndex] += 1;
            }
        }

        for (usize fileIndex = 0; fileIndex < prb_arrayLength(nestedFiles) && !found; fileIndex++) {
            prb_String file = nestedFiles[fileIndex];
            found = prb_streq(file, iterRecursivePattern.curPath);
            if (found) {
                nestedFilesFound[fileIndex] += 1;
            }
        }

        for (usize fileIndex = 0; fileIndex < prb_arrayLength(nestedFiles) && !found; fileIndex++) {
            prb_String file = nestedNestedFiles[fileIndex];
            found = prb_streq(file, iterRecursivePattern.curPath);
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

    prb_assert(iterRecursivePattern.curMatchCount == 6);

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

//
//
//

function void
test_printColor(prb_Arena* arena, void* data) {
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

    prb_endTempMemory(temp);
}

function void
test_strings(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_GrowingString gstr = prb_beginString(arena);
    prb_addStringSegment(&gstr, "%s", "one");
    prb_addStringSegment(&gstr, "%s", " two");
    prb_addStringSegment(&gstr, "%s", " three");
    prb_String str = prb_endString(&gstr);

    prb_String target = prb_STR("one two three");
    prb_assert(prb_streq(str, target));
    prb_assert(arena->used == temp.usedAtBegin + target.len + 1);

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
test_fileformat(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_ReadEntireFileResult fileContents = prb_readEntireFile(arena, prb_STR("programmable_build.h"));
    prb_assert(fileContents.success);
    prb_LineIterator lineIter = prb_createLineIter((prb_String) {(const char*)fileContents.content.data, fileContents.content.len});

    prb_String* headerNames = 0;
    while (prb_lineIterNext(&lineIter) == prb_Success) {
        prb_assert(!prb_strStartsWith(arena, lineIter.curLine, prb_STR("prb_PUBLICDEF"), prb_StringFindMode_Exact));

        if (prb_strStartsWith(arena, lineIter.curLine, prb_STR("// SECTION"), prb_StringFindMode_Exact)) {
            prb_String name = prb_fmt(arena, "%.*s", lineIter.curLine.len, lineIter.curLine.ptr);
            arrput(headerNames, name);
        } else if (prb_strStartsWith(arena, lineIter.curLine, prb_STR("prb_PUBLICDEC"), prb_StringFindMode_Exact)) {
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
        } else if (prb_strStartsWith(arena, lineIter.curLine, prb_STR("#ifndef prb_NO_IMPLEMENTATION"), prb_StringFindMode_Exact)) {
            break;
        }
    }

    prb_String* implNames = 0;
    while (prb_lineIterNext(&lineIter) == prb_Success) {
        prb_assert(!prb_strStartsWith(arena, lineIter.curLine, prb_STR("prb_PUBLICDEC"), prb_StringFindMode_Exact));

        if (prb_strStartsWith(arena, lineIter.curLine, prb_STR("// SECTION"), prb_StringFindMode_Exact)) {
            prb_StringFindSpec spec = {};
            spec.str = lineIter.curLine;
            spec.pattern = prb_STR(" (implementation)");
            spec.mode = prb_StringFindMode_Exact;
            spec.direction = prb_StringDirection_FromStart;
            prb_StringFindResult implementationRes = prb_strFind(spec);
            prb_assert(implementationRes.found);
            prb_String name = prb_fmt(arena, "%.*s", implementationRes.matchByteIndex, lineIter.curLine.ptr);
            arrput(implNames, name);
        } else if (prb_strStartsWith(arena, lineIter.curLine, prb_STR("prb_PUBLICDEF"), prb_StringFindMode_Exact)) {
            prb_assert(prb_lineIterNext(&lineIter) == prb_Success);
            prb_assert(prb_strStartsWith(arena, lineIter.curLine, prb_STR("prb_"), prb_StringFindMode_Exact));
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

    prb_String* headerNotInImpl = setdiff(headerNames, implNames);
    if (arrlen(headerNotInImpl) > 0) {
        prb_writelnToStdout(prb_STR("names in header but not in impl:"));
        for (i32 index = 0; index < arrlen(headerNotInImpl); index++) {
            prb_String str = headerNotInImpl[index];
            prb_writeToStdout(str);
            prb_writeToStdout(prb_STR("\n"));
        }
    }
    prb_assert(arrlen(headerNotInImpl) == 0);
    arrfree(headerNotInImpl);

    prb_String* implNotInHeader = setdiff(implNames, headerNames);
    if (arrlen(implNotInHeader) > 0) {
        prb_writelnToStdout(prb_STR("names in impl but not in header:"));
        for (i32 index = 0; index < arrlen(implNotInHeader); index++) {
            prb_String str = implNotInHeader[index];
            prb_writeToStdout(str);
            prb_writeToStdout(prb_STR("\n"));
        }
    }
    prb_assert(arrlen(implNotInHeader) == 0);
    arrfree(implNotInHeader);

    arrfree(headerNames);
    arrfree(implNames);

    for (i32 index = 0; index < arrlen(headerNames); index++) {
        prb_String headerName = headerNames[index];
        prb_String implName = implNames[index];
        prb_assert(prb_streq(headerName, implName));
    }

    prb_endTempMemory(temp);
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
test_strStartsEnds(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_assert(prb_strStartsWith(arena, prb_STR("123abc"), prb_STR("123"), prb_StringFindMode_Exact));
    prb_assert(!prb_strStartsWith(arena, prb_STR("123abc"), prb_STR("abc"), prb_StringFindMode_Exact));
    prb_assert(!prb_strEndsWith(arena, prb_STR("123abc"), prb_STR("123"), prb_StringFindMode_Exact));
    prb_assert(prb_strEndsWith(arena, prb_STR("123abc"), prb_STR("abc"), prb_StringFindMode_Exact));
}

function void
test_lineIter(prb_Arena* arena, void* data) {
    prb_unused(data);
    prb_String       lines = prb_STR("line1\r\nline2\nline3\rline4\n\nline6\r\rline8\r\n\r\nline10\r\n\nline12\r\r\nline14");
    prb_LineIterator iter = prb_createLineIter(lines);

    prb_assert(iter.curLineCount == 0);
    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(arena, iter.curLine, prb_STR("line1"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 2);
    prb_assert(iter.curLineCount == 1);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(arena, iter.curLine, prb_STR("line2"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 2);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(arena, iter.curLine, prb_STR("line3"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 3);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(arena, iter.curLine, prb_STR("line4"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 4);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 5);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(arena, iter.curLine, prb_STR("line6"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 6);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 7);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(arena, iter.curLine, prb_STR("line8"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 5);
    prb_assert(iter.curLineEndLen == 2);
    prb_assert(iter.curLineCount == 8);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 2);
    prb_assert(iter.curLineCount == 9);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(arena, iter.curLine, prb_STR("line10"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 6);
    prb_assert(iter.curLineEndLen == 2);
    prb_assert(iter.curLineCount == 10);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 11);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(arena, iter.curLine, prb_STR("line12"), prb_StringFindMode_Exact));
    prb_assert(iter.curLine.len == 6);
    prb_assert(iter.curLineEndLen == 1);
    prb_assert(iter.curLineCount == 12);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(iter.curLine.len == 0);
    prb_assert(iter.curLineEndLen == 2);
    prb_assert(iter.curLineCount == 13);

    prb_assert(prb_lineIterNext(&iter) == prb_Success);
    prb_assert(prb_strStartsWith(arena, iter.curLine, prb_STR("line14"), prb_StringFindMode_Exact));
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

int
main() {
    prb_TimeStart testStart = prb_timeStart();
    prb_Arena     arena = prb_createArenaFromVmem(1 * prb_GIGABYTE);
    void*         baseStart = arena.base;
    prb_assert(arena.tempCount == 0);

    prb_Job* jobs = 0;

    arrput(jobs, prb_createJob(test_memeq, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getOffsetForAlignment, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_arenaAlignFreePtr, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_arenaAllocAndZero, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_globalArenaCurrentFreePtr, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_globalArenaCurrentFreeSize, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_globalArenaChangeUsed, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_beginTempMemory, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_endTempMemory, 0, &arena, 10 * prb_MEGABYTE));

    arrput(jobs, prb_createJob(test_pathExists, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_isDirectory, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_isFile, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_directoryIsEmpty, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_createDirIfNotExists, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_removeFileOrDirectoryIfExists, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_removeFileIfExists, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_removeDirectoryIfExists, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_clearDirectory, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getCurrentWorkingDir, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_pathJoin, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_charIsSep, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_findSepBeforeLastEntry, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getParentDir, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getLastEntryInPath, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_replaceExt, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_pathFindIter, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getLastModified, 0, &arena, 10 * prb_MEGABYTE));

    arrput(jobs, prb_createJob(test_strings, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_fileformat, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strFind, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strFindIter, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_strStartsEnds, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_lineIter, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_utf8CharIter, 0, &arena, 10 * prb_MEGABYTE));
    arrput(jobs, prb_createJob(test_getArgArrayFromString, 0, &arena, 10 * prb_MEGABYTE));

    arrput(jobs, prb_createJob(test_printColor, 0, &arena, 10 * prb_MEGABYTE));

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
