#include "../programmable_build.h"

#define function static

typedef uint8_t u8;
typedef int32_t i32;
typedef size_t  usize;

//
// SECTION Memory
//

function void
test_memeq(void) {
    const char* p1 = "test1";
    const char* p2 = "test12";
    prb_assert(prb_memeq(p1, p2, prb_strlen(p1)));
    prb_assert(!prb_memeq(p1, p2, prb_strlen(p2)));
}

function void
test_getOffsetForAlignment(void) {
    prb_TempMemory temp = prb_beginTempMemory();

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
test_globalArenaAlignFreePtr(void) {
    prb_TempMemory temp = prb_beginTempMemory();

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
test_allocAndZero(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    i32 arbitrarySize = 100;
    u8* ptr = (u8*)prb_allocAndZero(arbitrarySize, 1);
    u8  arbitraryValue = 12;
    ptr[0] = arbitraryValue;

    prb_endTempMemory(temp);
    temp = prb_beginTempMemory();

    prb_assert(ptr[0] == arbitraryValue);
    prb_assert(ptr == (u8*)prb_allocAndZero(1, 1));
    prb_assert(ptr[0] == 0);

    prb_endTempMemory(temp);
}

function void
test_realloc(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    i32  initLen = 10;
    i32* arr = prb_allocArray(i32, initLen);
    for (i32 index = 0; index < initLen; index++) {
        arr[index] = index;
    }

    i32 finalLen = 100;
    arr = (i32*)prb_realloc(arr, finalLen * sizeof(*arr));
    for (i32 index = 0; index < initLen; index++) {
        prb_assert(arr[index] == index);
    }
    for (i32 index = initLen; index < finalLen; index++) {
        prb_assert(arr[index] == 0);
    }

    prb_endTempMemory(temp);
}

function void
test_globalArenaCurrentFreePtr(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    uint8_t* ptrInit = (uint8_t*)prb_globalArenaCurrentFreePtr();
    i32      size = 1;
    prb_allocAndZero(size, 1);
    prb_assert(prb_globalArenaCurrentFreePtr() == ptrInit + size);

    prb_endTempMemory(temp);
}

function void
test_globalArenaCurrentFreeSize(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    i32 sizeInit = prb_globalArenaCurrentFreeSize();
    i32 size = 1;
    prb_allocAndZero(size, 1);
    prb_assert(prb_globalArenaCurrentFreeSize() == sizeInit - size);

    prb_endTempMemory(temp);
}

function void
test_globalArenaChangeUsed(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    i32 init = prb_globalArena.used;
    i32 delta = 100;
    prb_globalArenaChangeUsed(delta);
    prb_assert(prb_globalArena.used == init + delta);
    delta *= -1;
    prb_globalArenaChangeUsed(delta);
    prb_assert(prb_globalArena.used == init);

    prb_endTempMemory(temp);
}

function void
test_beginTempMemory(void) {
    prb_TempMemory temp = prb_beginTempMemory();
    prb_assert(temp.usedAtBegin == prb_globalArena.used);
    prb_assert(temp.tempCountAtBegin == prb_globalArena.tempCount - 1);
    prb_endTempMemory(temp);
}

function void
test_endTempMemory(void) {
    prb_TempMemory temp = prb_beginTempMemory();
    prb_allocAndZero(100, 1);
    prb_endTempMemory(temp);
    prb_assert(prb_globalArena.used == temp.usedAtBegin);
    prb_assert(prb_globalArena.tempCount == temp.tempCountAtBegin);
}

//
// SECTION Filesystem
//

function void
test_pathExists(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_String dir = prb_pathJoin(prb_getParentDir(prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_removeDirectoryIfExists(dir);
    prb_assert(!prb_pathExists(dir));
    prb_createDirIfNotExists(dir);
    prb_assert(prb_pathExists(dir));
    prb_removeDirectoryIfExists(dir);
    prb_assert(!prb_pathExists(dir));

    prb_String dirTrailingSlash = prb_fmt("%.*s/", prb_LIT(dir));
    prb_removeDirectoryIfExists(dirTrailingSlash);
    prb_assert(!prb_pathExists(dirTrailingSlash));
    prb_assert(!prb_pathExists(dir));
    prb_createDirIfNotExists(dirTrailingSlash);
    prb_assert(prb_pathExists(dirTrailingSlash));
    prb_assert(prb_pathExists(dir));
    prb_removeDirectoryIfExists(dirTrailingSlash);
    prb_assert(!prb_pathExists(dirTrailingSlash));
    prb_assert(!prb_pathExists(dir));

    prb_String dirNotNull = prb_fmt("%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_removeDirectoryIfExists(dirNotNull);
    prb_assert(!prb_pathExists(dirNotNull));
    prb_assert(!prb_pathExists(dir));
    prb_createDirIfNotExists(dirNotNull);
    prb_assert(prb_pathExists(dirNotNull));
    prb_assert(prb_pathExists(dir));
    prb_removeDirectoryIfExists(dirNotNull);
    prb_assert(!prb_pathExists(dirNotNull));
    prb_assert(!prb_pathExists(dir));

    prb_String filepath = prb_pathJoin(prb_getParentDir(prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_String filepathNotNull = prb_fmt("%.*sabc", prb_LIT(filepath));
    filepathNotNull.len = filepath.len;

    prb_removeFileIfExists(filepath);
    prb_assert(!prb_pathExists(filepath));
    prb_assert(!prb_pathExists(filepathNotNull));
    prb_writeEntireFile(filepath, (prb_Bytes) {(uint8_t*)"1", 1});
    prb_assert(prb_pathExists(filepath));
    prb_assert(prb_pathExists(filepathNotNull));
    prb_removeFileIfExists(filepath);
    prb_assert(!prb_pathExists(filepath));
    prb_assert(!prb_pathExists(filepathNotNull));

    prb_removeFileIfExists(filepathNotNull);
    prb_assert(!prb_pathExists(filepathNotNull));
    prb_assert(!prb_pathExists(filepath));
    prb_writeEntireFile(filepathNotNull, (prb_Bytes) {(uint8_t*)"1", 1});
    prb_assert(prb_pathExists(filepathNotNull));
    prb_assert(prb_pathExists(filepath));
    prb_removeFileIfExists(filepathNotNull);
    prb_assert(!prb_pathExists(filepathNotNull));
    prb_assert(!prb_pathExists(filepath));

    prb_assert(prb_pathExists(prb_STR(__FILE__)));

    prb_endTempMemory(temp);
}

function void
test_isDirectory(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_String dir = prb_pathJoin(prb_getParentDir(prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_removeDirectoryIfExists(dir);
    prb_assert(!prb_isDirectory(dir));
    prb_createDirIfNotExists(dir);
    prb_assert(prb_isDirectory(dir));
    prb_removeDirectoryIfExists(dir);
    prb_assert(!prb_isDirectory(dir));

    prb_String dirTrailingSlash = prb_fmt("%.*s/", prb_LIT(dir));
    prb_removeDirectoryIfExists(dirTrailingSlash);
    prb_assert(!prb_isDirectory(dirTrailingSlash));
    prb_assert(!prb_isDirectory(dir));
    prb_createDirIfNotExists(dirTrailingSlash);
    prb_assert(prb_isDirectory(dirTrailingSlash));
    prb_assert(prb_isDirectory(dir));
    prb_removeDirectoryIfExists(dirTrailingSlash);
    prb_assert(!prb_isDirectory(dirTrailingSlash));
    prb_assert(!prb_isDirectory(dir));

    prb_String dirNotNull = prb_fmt("%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_removeDirectoryIfExists(dirNotNull);
    prb_assert(!prb_isDirectory(dirNotNull));
    prb_assert(!prb_isDirectory(dir));
    prb_createDirIfNotExists(dirNotNull);
    prb_assert(prb_isDirectory(dirNotNull));
    prb_assert(prb_isDirectory(dir));
    prb_removeDirectoryIfExists(dirNotNull);
    prb_assert(!prb_isDirectory(dirNotNull));
    prb_assert(!prb_isDirectory(dir));

    prb_assert(!prb_isDirectory(prb_STR(__FILE__)));

    prb_endTempMemory(temp);
}

function void
test_isFile(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_String filepath = prb_pathJoin(prb_getParentDir(prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_String filepathNotNull = prb_fmt("%.*sabc", prb_LIT(filepath));
    filepathNotNull.len = filepath.len;

    prb_removeFileIfExists(filepath);
    prb_assert(!prb_isFile(filepath));
    prb_assert(!prb_isFile(filepathNotNull));
    prb_writeEntireFile(filepath, (prb_Bytes) {(uint8_t*)"1", 1});
    prb_assert(prb_isFile(filepath));
    prb_assert(prb_isFile(filepathNotNull));
    prb_removeFileIfExists(filepath);
    prb_assert(!prb_isFile(filepath));
    prb_assert(!prb_isFile(filepathNotNull));

    prb_removeFileIfExists(filepathNotNull);
    prb_assert(!prb_isFile(filepathNotNull));
    prb_assert(!prb_isFile(filepath));
    prb_writeEntireFile(filepathNotNull, (prb_Bytes) {(uint8_t*)"1", 1});
    prb_assert(prb_isFile(filepathNotNull));
    prb_assert(prb_isFile(filepath));
    prb_removeFileIfExists(filepathNotNull);
    prb_assert(!prb_isFile(filepathNotNull));
    prb_assert(!prb_isFile(filepath));

    prb_assert(prb_isFile(prb_STR(__FILE__)));
    prb_assert(!prb_isFile(prb_getParentDir(prb_STR(__FILE__))));

    prb_endTempMemory(temp);
}

function void
test_directoryIsEmpty(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_String dir = prb_pathJoin(prb_getParentDir(prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_clearDirectory(dir);
    prb_assert(prb_directoryIsEmpty(dir));
    prb_String filepath = prb_pathJoin(dir, prb_STR("file.txt"));
    prb_writeEntireFile(filepath, (prb_Bytes) {(uint8_t*)"1", 1});
    prb_assert(!prb_directoryIsEmpty(dir));
    prb_removeFileIfExists(filepath);
    prb_assert(prb_directoryIsEmpty(dir));

    prb_String dirTrailingSlash = prb_fmt("%.*s/", prb_LIT(dir));
    prb_assert(prb_directoryIsEmpty(dirTrailingSlash));
    prb_assert(prb_directoryIsEmpty(dir));
    prb_writeEntireFile(filepath, (prb_Bytes) {(uint8_t*)"1", 1});
    prb_assert(!prb_directoryIsEmpty(dirTrailingSlash));
    prb_assert(!prb_directoryIsEmpty(dir));
    prb_removeFileIfExists(filepath);
    prb_assert(prb_directoryIsEmpty(dirTrailingSlash));
    prb_assert(prb_directoryIsEmpty(dir));

    prb_String dirNotNull = prb_fmt("%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_assert(prb_directoryIsEmpty(dirNotNull));
    prb_assert(prb_directoryIsEmpty(dir));
    prb_writeEntireFile(filepath, (prb_Bytes) {(uint8_t*)"1", 1});
    prb_assert(!prb_directoryIsEmpty(dirNotNull));
    prb_assert(!prb_directoryIsEmpty(dir));
    prb_removeFileIfExists(filepath);
    prb_assert(prb_directoryIsEmpty(dirNotNull));
    prb_assert(prb_directoryIsEmpty(dir));

    prb_endTempMemory(temp);
}

function void
test_createDirIfNotExists(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_String dir = prb_pathJoin(prb_getParentDir(prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_removeDirectoryIfExists(dir);
    prb_assert(!prb_isDirectory(dir));
    prb_createDirIfNotExists(dir);
    prb_assert(prb_isDirectory(dir));
    prb_createDirIfNotExists(dir);
    prb_assert(prb_isDirectory(dir));
    prb_removeDirectoryIfExists(dir);

    prb_String dirTrailingSlash = prb_fmt("%.*s/", prb_LIT(dir));
    prb_createDirIfNotExists(dirTrailingSlash);
    prb_assert(prb_isDirectory(dirTrailingSlash));
    prb_assert(prb_isDirectory(dir));
    prb_removeFileOrDirectoryIfExists(dirTrailingSlash);
    prb_assert(!prb_isDirectory(dirTrailingSlash));
    prb_assert(!prb_isDirectory(dir));

    prb_String dirNotNull = prb_fmt("%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_createDirIfNotExists(dirNotNull);
    prb_assert(prb_isDirectory(dirNotNull));
    prb_assert(prb_isDirectory(dir));
    prb_removeFileOrDirectoryIfExists(dirNotNull);
    prb_assert(!prb_isDirectory(dirNotNull));
    prb_assert(!prb_isDirectory(dir));

    prb_endTempMemory(temp);
}

function void
test_removeFileOrDirectoryIfExists(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_String dir = prb_pathJoin(prb_getParentDir(prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_createDirIfNotExists(dir);

    prb_String filepath = prb_pathJoin(dir, prb_STR("file.txt"));
    prb_String filepathNotNull = prb_fmt("%.*sabc", prb_LIT(filepath));
    filepathNotNull.len = filepath.len;
    prb_writeEntireFile(filepath, (prb_Bytes) {(uint8_t*)"1", 1});

    prb_assert(prb_isDirectory(dir));
    prb_assert(prb_isFile(filepath));

    prb_removeFileOrDirectoryIfExists(filepath);
    prb_assert(prb_isDirectory(dir));
    prb_assert(!prb_isFile(filepath));

    prb_writeEntireFile(filepath, (prb_Bytes) {(uint8_t*)"1", 1});

    prb_removeFileOrDirectoryIfExists(filepathNotNull);
    prb_assert(prb_isDirectory(dir));
    prb_assert(!prb_isFile(filepath));

    prb_writeEntireFile(filepath, (prb_Bytes) {(uint8_t*)"1", 1});

    prb_removeFileOrDirectoryIfExists(dir);
    prb_assert(!prb_isDirectory(dir));
    prb_assert(!prb_isFile(filepath));

    prb_String dirTrailingSlash = prb_fmt("%.*s/", prb_LIT(dir));
    prb_createDirIfNotExists(dirTrailingSlash);
    prb_assert(prb_isDirectory(dirTrailingSlash));
    prb_assert(prb_isDirectory(dir));
    prb_removeFileOrDirectoryIfExists(dirTrailingSlash);
    prb_assert(!prb_isDirectory(dirTrailingSlash));
    prb_assert(!prb_isDirectory(dir));

    prb_String dirNotNull = prb_fmt("%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_createDirIfNotExists(dirNotNull);
    prb_assert(prb_isDirectory(dirNotNull));
    prb_assert(prb_isDirectory(dir));
    prb_removeFileOrDirectoryIfExists(dirNotNull);
    prb_assert(!prb_isDirectory(dirNotNull));
    prb_assert(!prb_isDirectory(dir));

    prb_endTempMemory(temp);
}

function void
test_removeFileIfExists(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_String dir = prb_pathJoin(prb_getParentDir(prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_clearDirectory(dir);

    prb_String filepath = prb_pathJoin(dir, prb_STR("file.txt"));
    prb_String filepathNotNull = prb_fmt("%.*sabc", prb_LIT(filepath));
    filepathNotNull.len = filepath.len;

    prb_assert(!prb_isFile(filepath));
    prb_writeEntireFile(filepath, (prb_Bytes) {(uint8_t*)"1", 1});
    prb_assert(prb_isFile(filepath));
    prb_removeFileIfExists(filepath);
    prb_assert(!prb_isFile(filepath));
    prb_writeEntireFile(filepath, (prb_Bytes) {(uint8_t*)"1", 1});
    prb_assert(prb_isFile(filepath));
    prb_removeFileIfExists(filepathNotNull);
    prb_assert(!prb_isFile(filepath));

    prb_removeDirectoryIfExists(dir);

    prb_endTempMemory(temp);
}

function void
test_removeDirectoryIfExists(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_String dir = prb_pathJoin(prb_getParentDir(prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_createDirIfNotExists(dir);
    prb_assert(prb_isDirectory(dir));
    prb_removeDirectoryIfExists(dir);
    prb_assert(!prb_isDirectory(dir));

    prb_String dirTrailingSlash = prb_fmt("%.*s/", prb_LIT(dir));
    prb_createDirIfNotExists(dirTrailingSlash);
    prb_assert(prb_isDirectory(dirTrailingSlash));
    prb_assert(prb_isDirectory(dir));
    prb_removeDirectoryIfExists(dirTrailingSlash);
    prb_assert(!prb_isDirectory(dirTrailingSlash));
    prb_assert(!prb_isDirectory(dir));

    prb_String dirNotNull = prb_fmt("%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_createDirIfNotExists(dirNotNull);
    prb_assert(prb_isDirectory(dirNotNull));
    prb_assert(prb_isDirectory(dir));
    prb_removeDirectoryIfExists(dirNotNull);
    prb_assert(!prb_isDirectory(dirNotNull));
    prb_assert(!prb_isDirectory(dir));

    prb_endTempMemory(temp);
}

function void
test_clearDirectory(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_String dir = prb_pathJoin(prb_getParentDir(prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_clearDirectory(dir);
    prb_assert(prb_directoryIsEmpty(dir));

    prb_String filepath = prb_pathJoin(dir, prb_STR("file.txt"));
    prb_writeEntireFile(filepath, (prb_Bytes) {(uint8_t*)"1", 1});

    prb_assert(!prb_directoryIsEmpty(dir));
    prb_clearDirectory(dir);
    prb_assert(prb_directoryIsEmpty(dir));

    prb_String dirTrailingSlash = prb_fmt("%.*s/", prb_LIT(dir));
    prb_writeEntireFile(filepath, (prb_Bytes) {(uint8_t*)"1", 1});
    prb_assert(!prb_directoryIsEmpty(dir));
    prb_assert(!prb_directoryIsEmpty(dirTrailingSlash));
    prb_clearDirectory(dirTrailingSlash);
    prb_assert(prb_directoryIsEmpty(dirTrailingSlash));
    prb_assert(prb_directoryIsEmpty(dir));

    prb_String dirNotNull = prb_fmt("%.*sabs", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_writeEntireFile(filepath, (prb_Bytes) {(uint8_t*)"1", 1});
    prb_assert(!prb_directoryIsEmpty(dir));
    prb_assert(!prb_directoryIsEmpty(dirNotNull));
    prb_clearDirectory(dirNotNull);
    prb_assert(prb_directoryIsEmpty(dirNotNull));
    prb_assert(prb_directoryIsEmpty(dir));

    prb_removeDirectoryIfExists(dir);

    prb_endTempMemory(temp);
}

function void
test_getCurrentWorkingDir(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_String cwd = prb_getCurrentWorkingDir();
    prb_assert(prb_isDirectory(cwd));
    prb_String filename = prb_STR(__FUNCTION__);
    prb_writeEntireFile(filename, (prb_Bytes) {(uint8_t*)filename.str, filename.len});
    prb_Bytes fileContent = prb_readEntireFile(prb_pathJoin(cwd, filename));
    prb_assert(prb_streq((prb_String) {(const char*)fileContent.data, fileContent.len}, filename));
    prb_removeFileIfExists(filename);

    prb_endTempMemory(temp);
}

function void
test_pathJoin(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_assert(prb_streq(prb_pathJoin(prb_STR("a"), prb_STR("b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(prb_STR("a/"), prb_STR("b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(prb_STR("a"), prb_STR("/b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(prb_STR("a/"), prb_STR("/b")), prb_STR("a/b")));
    prb_assert(prb_streq(prb_pathJoin(prb_STR("a/"), prb_STR("/b/")), prb_STR("a/b/")));
    prb_assert(prb_streq(prb_pathJoin(prb_STR("/a/"), prb_STR("/b/")), prb_STR("/a/b/")));

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
test_charIsSep(void) {
    prb_assert(prb_charIsSep('/'));

#if prb_PLATFORM_WINDOWS
    prb_assert(prb_charIsSep('\\'));
#elif prb_PLATFORM_LINUX
    prb_assert(!prb_charIsSep('\\'));
#endif
}

function void
test_findSepBeforeLastEntry(void) {
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
}

function void
test_getParentDir(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_assert(prb_streq(prb_getParentDir(prb_STR("test/path")), prb_STR("test/")));
    prb_assert(prb_streq(prb_getParentDir(prb_STR("test/path/")), prb_STR("test/")));
    prb_assert(prb_streq(prb_getParentDir(prb_STR("test/path2/path")), prb_STR("test/path2/")));

    prb_assert(prb_streq(prb_getParentDir(prb_STR("test")), prb_getCurrentWorkingDir()));

#if prb_PLATFORM_WINDOWS
    prb_assert(prb_streq(prb_getParentDir(prb_STR("C:\\\\test")), prb_STR("C:\\\\")));
    prb_assert(prb_streq(prb_getParentDir(prb_STR("C:\\\\test/")), prb_STR("C:\\\\")));
#elif prb_PLATFORM_LINUX
    prb_assert(prb_streq(prb_getParentDir(prb_STR("/test")), prb_STR("/")));
    prb_assert(prb_streq(prb_getParentDir(prb_STR("/test/")), prb_STR("/")));
#endif

    prb_endTempMemory(temp);
}

function void
test_getLastEntryInPath(void) {
    prb_TempMemory temp = prb_beginTempMemory();

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
test_replaceExt(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_assert(prb_streq(prb_replaceExt(prb_STR("test"), prb_STR("txt")), prb_STR("test.txt")));
    prb_assert(prb_streq(prb_replaceExt(prb_STR("test.md"), prb_STR("txt")), prb_STR("test.txt")));
    prb_assert(prb_streq(prb_replaceExt(prb_STR("path/test.md"), prb_STR("txt")), prb_STR("path/test.txt")));
    prb_assert(prb_streq(prb_replaceExt(prb_STR("path/test.txt.md"), prb_STR("txt")), prb_STR("path/test.txt.txt")));

    prb_endTempMemory(temp);
}

function void
test_pathFindIter(void) {
    prb_TempMemory temp = prb_beginTempMemory();

    prb_String dir = prb_pathJoin(prb_getParentDir(prb_STR(__FILE__)), prb_STR(__FUNCTION__));
    prb_String dirNotNull = prb_fmt("%.*sabc", prb_LIT(dir));
    dirNotNull.len = dir.len;
    prb_String pattern = prb_fmt("%.*s/*.h", prb_LIT(dir));
    prb_String patternNotNull = prb_fmt("%.*sabc", prb_LIT(pattern));
    patternNotNull.len = pattern.len;

    prb_clearDirectory(dir);

    prb_String files[] = {
        prb_pathJoin(dir, prb_STR("f1.c")),
        prb_pathJoin(dir, prb_STR("f2.h")),
        prb_pathJoin(dir, prb_STR("f3.c")),
        prb_pathJoin(dir, prb_STR("f4.h")),
    };

    for (usize fileIndex = 0; fileIndex < prb_arrayLength(files); fileIndex++) {
        prb_String file = files[fileIndex];
        prb_writeEntireFile(file, (prb_Bytes) {(u8*)file.str, file.len});
    }

    prb_PathFindIterator iter = prb_createPathFindIter(dir, prb_PathFindMode_AllFilesInDir);
    prb_PathFindIterator iterNotNull = prb_createPathFindIter(dirNotNull, prb_PathFindMode_AllFilesInDir);

    i32                  filesFound[] = {0, 0, 0, 0};
    i32                  totalEntries = 0;
    prb_assert(prb_arrayLength(filesFound) == prb_arrayLength(files));
    while (prb_pathFindIterNext(&iter) == prb_Success) {
        bool found = false;
        for (usize fileIndex = 0; fileIndex < prb_arrayLength(files) && !found; fileIndex++) {
            prb_String file = files[fileIndex];
            found = prb_streq(file, iter.curPath);
            if (found) {
                filesFound[fileIndex] += 1;
            }
        }
        prb_assert(found);
        totalEntries += 1;
        prb_assert(prb_pathFindIterNext(&iterNotNull) == prb_Success);
        prb_assert(prb_streq(iter.curPath, iterNotNull.curPath));
    }

    prb_assert(totalEntries == prb_arrayLength(files));
    for (usize fileIndex = 0; fileIndex < prb_arrayLength(files); fileIndex++) {
        prb_assert(filesFound[fileIndex] == 1);
    }

    prb_assert(prb_pathFindIterNext(&iter) == prb_Failure);
    prb_assert(prb_pathFindIterNext(&iterNotNull) == prb_Failure);

    prb_destroyPathFindIter(&iter);
    prb_PathFindIterator empty = {};
    prb_assert(prb_memeq(&iter, &empty, sizeof(prb_PathFindIterator)));
    prb_destroyPathFindIter(&iterNotNull);

    prb_PathFindIterator iterPattern = prb_createPathFindIter(pattern, prb_PathFindMode_Glob);
    prb_PathFindIterator iterPatternNotNull = prb_createPathFindIter(patternNotNull, prb_PathFindMode_Glob);
    i32 totalEntriesPattern = 0;
    while (prb_pathFindIterNext(&iterPattern) == prb_Success) {
        bool found = false;
        for (usize fileIndex = 0; fileIndex < prb_arrayLength(files) && !found; fileIndex++) {
            prb_String file = files[fileIndex];
            found = prb_streq(file, iterPattern.curPath);
            if (found) {
                filesFound[fileIndex] += 1;
            }
        }
        totalEntriesPattern += 1;
        prb_assert(prb_pathFindIterNext(&iterPatternNotNull) == prb_Success);
        prb_assert(prb_streq(iterPattern.curPath, iterPatternNotNull.curPath));
    }

    prb_assert(totalEntriesPattern == 2);
    prb_assert(filesFound[0] == 1);
    prb_assert(filesFound[1] == 2);
    prb_assert(filesFound[2] == 1);
    prb_assert(filesFound[3] == 2);

    prb_assert(prb_pathFindIterNext(&iterPattern) == prb_Failure);
    prb_assert(prb_pathFindIterNext(&iterPatternNotNull) == prb_Failure);

    prb_destroyPathFindIter(&iterPattern);
    prb_assert(prb_memeq(&iterPattern, &empty, sizeof(prb_PathFindIterator)));
    prb_destroyPathFindIter(&iterPatternNotNull);

    prb_clearDirectory(dir);
    iter = prb_createPathFindIter(dir, prb_PathFindMode_AllFilesInDir);
    prb_assert(prb_pathFindIterNext(&iter) == prb_Failure);
    iter = prb_createPathFindIter(pattern, prb_PathFindMode_Glob);
    prb_assert(prb_pathFindIterNext(&iter) == prb_Failure);

    prb_removeDirectoryIfExists(dir);
    prb_endTempMemory(temp);
}

//
//
//

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
    test_getOffsetForAlignment();
    test_globalArenaAlignFreePtr();
    test_allocAndZero();
    test_realloc();
    test_globalArenaCurrentFreePtr();
    test_globalArenaCurrentFreeSize();
    test_globalArenaChangeUsed();
    test_beginTempMemory();
    test_endTempMemory();

    test_pathExists();
    test_isDirectory();
    test_isFile();
    test_directoryIsEmpty();
    test_createDirIfNotExists();
    test_removeFileOrDirectoryIfExists();
    test_removeFileIfExists();
    test_removeDirectoryIfExists();
    test_clearDirectory();
    test_getCurrentWorkingDir();
    test_pathJoin();
    test_charIsSep();
    test_findSepBeforeLastEntry();
    test_getParentDir();
    test_getLastEntryInPath();
    test_replaceExt();
    test_pathFindIter();

    test_strings();
    test_fileformat();
    test_strFind();
    test_strFindIter();
    test_strStartsEnds();
    test_lineIter();

    test_printColor();

    prb_assert(prb_globalArena.tempCount == 0);
    prb_assert(prb_globalArena.base == baseStart);

    prb_fmtAndPrintln("tests took %.2fms", prb_getMsFrom(testStart));
    prb_terminate(0);
    prb_assert(!"unreachable");
    return 0;
}
