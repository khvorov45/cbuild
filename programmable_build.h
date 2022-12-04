/* A set of utilities for writing "build scripts" as small C (or C++) programs.

Repository: https://github.com/khvorov45/programmable_build
See example/build.c for an example build script.

If using in multiple translation units:
    - Define prb_NOT_STATIC in the translation unit that has the implementation
    - Define prb_NO_IMPLEMENTATION to use as a normal header

Note that arenas are not thread-safe, so don't pass the same arena to multiple threads.

All string formatting functions are wrappers around stb printf
https://github.com/nothings/stb/blob/master/stb_sprintf.h
The strings are allocated on the linear allocator everything else is using.
The original stb sprintf API is still exposed.

The library includes all of stb ds
https://github.com/nothings/stb/blob/master/stb_ds.h
There are no wrappers for it, use the original API.
All memory allocation calls in stb ds are using libc realloc/free.

If a prb_* function ever returns an array (pointer to multiple elements) then it's
an stb ds array, so get its length with arrlen()

All prb_* iterators are meant to be used in loops like this
for (prb_Iter iter = prb_createIter(); prb_iterNext(&iter) == prb_Success;) {
    // pull stuff you need off iter
}
prb_destroyIter() functions don't destroy actual entries, only system resources (e.g. directory handles).

CRC32 hashing implementation was adpted from this public domain implementation
http://home.thep.lu.se/~bjorn/crc/crc32_fast.c
*/

// TODO(khvorov) Make sure utf8 paths work on windows
// TODO(khvorov) File search by regex
// TODO(khvorov) Run sanitizers
// TODO(khvorov) prb_fmt should fail if locked for string
// TODO(khvorov) Better assert message
// TODO(khvorov) Automatic -lpthread probably
// TODO(khvorov) Avoid accidentally recursing in printing in assert
// TODO(khvorov) Test macros do the right thing
// TODO(khvorov) Consistent string/str iterator/iter naming
// TODO(khvorov) Access color escape codes as strings

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#ifndef PROGRAMMABLE_BUILD_H
#define PROGRAMMABLE_BUILD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdalign.h>

#include <string.h>
#include <regex.h>

#if defined(WIN32) || defined(_WIN32)
#define prb_PLATFORM_WINDOWS 1
#elif (defined(linux) || defined(__linux) || defined(__linux__))
#define prb_PLATFORM_LINUX 1
#else
#error unrecognized platform
#endif

#if prb_PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")

#elif prb_PLATFORM_LINUX

#include <linux/limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <spawn.h>
#include <dirent.h>
#include <glob.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>

#endif

#define prb_BYTE 1
#define prb_KILOBYTE 1024 * prb_BYTE
#define prb_MEGABYTE 1024 * prb_KILOBYTE
#define prb_GIGABYTE 1024 * prb_MEGABYTE

#define prb_memcpy memcpy
#define prb_memmove memmove
#define prb_memset memset
#define prb_memcmp memcmp
#define prb_strcmp strcmp
#define prb_strlen (int32_t) strlen
#define prb_malloc malloc
#define prb_realloc realloc
#define prb_free free

#define prb_max(a, b) (((a) > (b)) ? (a) : (b))
#define prb_min(a, b) (((a) < (b)) ? (a) : (b))
#define prb_clamp(x, a, b) (((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x)))
#define prb_arrayLength(arr) (int32_t)(sizeof(arr) / sizeof(arr[0]))
#define prb_arenaAllocArray(arena, type, len) (type*)prb_arenaAllocAndZero(arena, (len) * sizeof(type), alignof(type))
#define prb_arenaAllocStruct(type) (type*)prb_allocAndZero(arena, sizeof(type), alignof(type))
#define prb_isPowerOf2(x) (((x) > 0) && (((x) & ((x)-1)) == 0))
#define prb_unused(x) ((x) = (x))

#define prb_countLeading1sU32(x) __builtin_clz(~(x))
#define prb_countLeading1sU8(x) prb_countLeading1sU32((x) << 24)

// clang-format off

#define prb_STR(x) (prb_String) {x, prb_strlen(x)}
#define prb_LIT(x) (x).len, (x).ptr

// Debug break taken from SDL
// https://github.com/libsdl-org/SDL/blob/main/include/SDL_assert.h
#ifdef __has_builtin
    #define _prb_HAS_BUILTIN(x) __has_builtin(x)
#else
    #define _prb_HAS_BUILTIN(x) 0
#endif

#if defined(_MSC_VER)
    /* Don't include intrin.h here because it contains C++ code */
    extern void __cdecl __debugbreak(void);
    #define prb_debugbreak() __debugbreak()
#elif _prb_HAS_BUILTIN(__builtin_debugtrap)
    #define prb_debugbreak() __builtin_debugtrap()
#elif ((!defined(__NACL__)) && ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))))
    #define prb_debugbreak() __asm__ __volatile__("int $3\n\t")
#elif (defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))) /* this might work on other ARM targets, but this is a known quantity... */
    #define prb_debugbreak() __asm__ __volatile__("brk #22\n\t")
#elif defined(__APPLE__) && defined(__arm__)
    #define prb_debugbreak() __asm__ __volatile__("bkpt #22\n\t")
#elif defined(__386__) && defined(__WATCOMC__)
    #define prb_debugbreak() { _asm { int 0x03 } }
#elif defined(prb_HAVE_SIGNAL_H) && !defined(__WATCOMC__)
    #include <signal.h>
    #define prb_debugbreak() raise(SIGTRAP)
#else
    /* How do we trigger breakpoints on this platform? */
    #define prb_debugbreak()
#endif

// Taken from stb snprintf
// https://github.com/nothings/stb/blob/master/stb_sprintf.h
#if defined(__has_attribute)
    #if __has_attribute(format)
        #define prb_ATTRIBUTE_FORMAT(fmt, va) __attribute__((format(printf, fmt, va)))
    #endif
#endif

#ifndef prb_ATTRIBUTE_FORMAT
    #define prb_ATTRIBUTE_FORMAT(fmt, va)
#endif

#ifndef prb_assertAction
#define prb_assertAction() do {\
    prb_writelnToStdout(prb_STR("assertion failure"));\
    prb_debugbreak();\
    prb_terminate(1);\
} while (0)
#endif

#ifndef prb_assert
// NOTE(khvorov) Assign condition to a variable to catch prb_assert(x = y) instead of prb_assert(x == y)
#define prb_assert(condition) do { bool assertbool = condition; if (!(assertbool)) { prb_assertAction(); } } while (0)
#endif
// clang-format on

#ifdef prb_NO_IMPLEMENTATION
#define prb_NOT_STATIC
#endif

#ifdef prb_NOT_STATIC
#define prb_PUBLICDEC
#define prb_PUBLICDEF
#else
#define prb_PUBLICDEC static
#define prb_PUBLICDEF static
#endif

typedef struct prb_Arena {
    void*   base;
    int32_t size;
    int32_t used;
    bool    lockedForString;
    int32_t tempCount;
} prb_Arena;

typedef struct prb_TempMemory {
    prb_Arena* arena;
    int32_t    usedAtBegin;
    int32_t    tempCountAtBegin;
} prb_TempMemory;

// Assume: utf-8, immutable
typedef struct prb_String {
    const char* ptr;
    int32_t     len;
} prb_String;

typedef struct prb_GrowingString {
    prb_Arena* arena;
    prb_String string;
} prb_GrowingString;

typedef enum prb_Status {
    prb_Failure,
    prb_Success,
} prb_Status;

typedef struct prb_TimeStart {
    bool     valid;
    uint64_t nsec;
} prb_TimeStart;

typedef enum prb_ColorID {
    prb_ColorID_Black,
    prb_ColorID_Red,
    prb_ColorID_Green,
    prb_ColorID_Yellow,
    prb_ColorID_Blue,
    prb_ColorID_Magenta,
    prb_ColorID_Cyan,
    prb_ColorID_White,
} prb_ColorID;

typedef struct prb_Bytes {
    uint8_t* data;
    int32_t  len;
} prb_Bytes;

typedef enum prb_ProcessStatus {
    prb_ProcessStatus_NotLaunched,
    prb_ProcessStatus_Launched,
    prb_ProcessStatus_CompletedSuccess,
    prb_ProcessStatus_CompletedFailed,
} prb_ProcessStatus;

typedef struct prb_ProcessHandle {
    prb_ProcessStatus status;

#if prb_PLATFORM_WINDOWS
#error unimplemented
#elif prb_PLATFORM_LINUX
    pid_t     pid;
#else
#error unimplemented
#endif
} prb_ProcessHandle;

typedef enum prb_ProcessFlag {
    prb_ProcessFlag_DontWait = 1 << 0,
    prb_ProcessFlag_RedirectStdout = 1 << 1,
    prb_ProcessFlag_RedirectStderr = 1 << 2,
} prb_ProcessFlag;
typedef uint32_t prb_ProcessFlags;

typedef enum prb_StringFindMode {
    prb_StringFindMode_Exact,
    prb_StringFindMode_AnyChar,
    prb_StringFindMode_RegexPosix,
} prb_StringFindMode;

typedef enum prb_StringDirection {
    prb_StringDirection_FromStart,
    prb_StringDirection_FromEnd,
} prb_StringDirection;

typedef struct prb_StringFindSpec {
    prb_String          str;
    prb_String          pattern;
    prb_StringDirection direction;
    prb_StringFindMode  mode;
    union {
        struct {
            int32_t ignore;
        } exact;
        struct {
            int32_t ignore;
        } anyChar;
        struct {
            prb_Arena* arena;
        } regexPosix;
    };
} prb_StringFindSpec;

typedef struct prb_StringFindResult {
    bool    found;
    int32_t matchByteIndex;
    int32_t matchLen;
} prb_StringFindResult;

typedef struct prb_LineIterator {
    prb_String ogstr;
    int32_t    curLineCount;
    int32_t    curByteOffset;
    prb_String curLine;
    int32_t    curLineEndLen;
} prb_LineIterator;

typedef struct prb_WordIterator {
    prb_String ogstr;
    int32_t    curWordCount;
    int32_t    curByteOffset;
    prb_String curWord;
} prb_WordIterator;

typedef struct prb_Utf8CharIterator {
    prb_String          str;
    prb_StringDirection direction;
    int32_t             curCharCount;
    int32_t             curByteOffset;
    uint32_t            curUtf32Char;
    int32_t             curUtf8Bytes;
    bool                curIsValid;
} prb_Utf8CharIterator;

typedef struct prb_StrFindIterator {
    prb_StringFindSpec   spec;
    prb_StringFindResult curResult;
    int32_t              curMatchCount;
} prb_StrFindIterator;

typedef enum prb_PathFindMode {
    prb_PathFindMode_AllEntriesInDir,
    prb_PathFindMode_Glob,
} prb_PathFindMode;

typedef struct prb_PathFindSpec {
    prb_Arena*       arena;
    prb_String       dir;
    prb_PathFindMode mode;
    bool             recursive;
    union {
        struct {
            prb_String pattern;
        } glob;
        struct {
            int32_t ignore;
        } allEntriesInDir;
    };
} prb_PathFindSpec;

typedef struct prb_PathFindIterator_DirHandle {
    prb_String path;

#if prb_PLATFORM_WINDOWS
#error unimplemented
#elif prb_PLATFORM_LINUX
    DIR*      handle;
#else
#error unimplemented
#endif
} prb_PathFindIterator_DirHandle;

typedef struct prb_PathFindIterator {
    prb_PathFindSpec spec;
    prb_String       curPath;
    int32_t          curMatchCount;
    union {
        struct {
            prb_PathFindIterator_DirHandle* parents;
        } allEntriesInDir;
        struct {
            int     returnVal;
            int32_t currentIndex;
            glob_t  result;
        } glob;
    };
} prb_PathFindIterator;

typedef struct prb_FileTimestamp {
    bool     valid;
    uint64_t timestamp;
} prb_FileTimestamp;

typedef struct prb_Multitime {
    int32_t  validAddedTimestampsCount;
    int32_t  invalidAddedTimestampsCount;
    uint64_t timeLatest;
    uint64_t timeEarliest;
} prb_Multitime;

typedef void (*prb_JobProc)(prb_Arena* arena, void* data);

typedef struct prb_Job {
    prb_Arena         arena;
    prb_JobProc       proc;
    void*             data;
    prb_ProcessStatus status;

#if prb_PLATFORM_WINDOWS
#error unimplemented
#elif prb_PLATFORM_LINUX
    pthread_t threadid;
#else
#error unimplemented
#endif
} prb_Job;

typedef struct prb_ReadEntireFileResult {
    bool      success;
    prb_Bytes content;
} prb_ReadEntireFileResult;

// SECTION Memory
prb_PUBLICDEC bool           prb_memeq(const void* ptr1, const void* ptr2, int32_t bytes);
prb_PUBLICDEC int32_t        prb_getOffsetForAlignment(void* ptr, int32_t align);
prb_PUBLICDEC void*          prb_vmemAlloc(int32_t bytes);
prb_PUBLICDEC prb_Arena      prb_createArenaFromVmem(int32_t bytes);
prb_PUBLICDEC prb_Arena      prb_createArenaFromArena(prb_Arena* arena, int32_t bytes);
prb_PUBLICDEC void*          prb_arenaAllocAndZero(prb_Arena* arena, int32_t size, int32_t align);
prb_PUBLICDEC void           prb_arenaAlignFreePtr(prb_Arena* arena, int32_t align);
prb_PUBLICDEC void*          prb_arenaFreePtr(prb_Arena* arena);
prb_PUBLICDEC int32_t        prb_arenaFreeSize(prb_Arena* arena);
prb_PUBLICDEC void           prb_arenaChangeUsed(prb_Arena* arena, int32_t byteDelta);
prb_PUBLICDEC prb_TempMemory prb_beginTempMemory(prb_Arena* arena);
prb_PUBLICDEC void           prb_endTempMemory(prb_TempMemory temp);

// SECTION Filesystem
prb_PUBLICDEC bool                     prb_pathExists(prb_Arena* arena, prb_String path);
prb_PUBLICDEC bool                     prb_isDirectory(prb_Arena* arena, prb_String path);
prb_PUBLICDEC bool                     prb_isFile(prb_Arena* arena, prb_String path);
prb_PUBLICDEC bool                     prb_directoryIsEmpty(prb_Arena* arena, prb_String path);
prb_PUBLICDEC prb_Status               prb_createDirIfNotExists(prb_Arena* arena, prb_String path);
prb_PUBLICDEC prb_Status               prb_removeFileOrDirectoryIfExists(prb_Arena* arena, prb_String path);
prb_PUBLICDEC prb_Status               prb_removeFileIfExists(prb_Arena* arena, prb_String path);
prb_PUBLICDEC prb_Status               prb_removeDirectoryIfExists(prb_Arena* arena, prb_String path);
prb_PUBLICDEC prb_Status               prb_clearDirectory(prb_Arena* arena, prb_String path);
prb_PUBLICDEC prb_String               prb_getWorkingDir(prb_Arena* arena);
prb_PUBLICDEC prb_Status               prb_setWorkingDir(prb_Arena* arena, prb_String dir);
prb_PUBLICDEC prb_String               prb_pathJoin(prb_Arena* arena, prb_String path1, prb_String path2);
prb_PUBLICDEC bool                     prb_charIsSep(char ch);
prb_PUBLICDEC prb_StringFindResult     prb_findSepBeforeLastEntry(prb_String path);
prb_PUBLICDEC prb_String               prb_getParentDir(prb_Arena* arena, prb_String path);
prb_PUBLICDEC prb_String               prb_getLastEntryInPath(prb_String path);
prb_PUBLICDEC prb_String               prb_replaceExt(prb_Arena* arena, prb_String path, prb_String newExt);
prb_PUBLICDEC prb_PathFindIterator     prb_createPathFindIter(prb_PathFindSpec spec);
prb_PUBLICDEC prb_Status               prb_pathFindIterNext(prb_PathFindIterator* iter);
prb_PUBLICDEC void                     prb_destroyPathFindIter(prb_PathFindIterator* iter);
prb_PUBLICDEC prb_Multitime            prb_createMultitime(void);
prb_PUBLICDEC prb_FileTimestamp        prb_getLastModified(prb_Arena* arena, prb_String path);
prb_PUBLICDEC void                     prb_multitimeAdd(prb_Multitime* multitime, prb_FileTimestamp newTimestamp);
prb_PUBLICDEC prb_ReadEntireFileResult prb_readEntireFile(prb_Arena* arena, prb_String path);
prb_PUBLICDEC prb_Status               prb_writeEntireFile(prb_Arena* arena, prb_String path, const void* content, int32_t contentLen);
prb_PUBLICDEC prb_Status               prb_binaryToCArray(prb_Arena* arena, prb_String inPath, prb_String outPath, prb_String arrayName);

// SECTION Strings
prb_PUBLICDEC bool                 prb_streq(prb_String str1, prb_String str2);
prb_PUBLICDEC prb_String           prb_strSliceForward(prb_String str, int32_t bytes);
prb_PUBLICDEC const char*          prb_strGetNullTerminated(prb_Arena* arena, prb_String str);
prb_PUBLICDEC prb_String           prb_strTrimSide(prb_String str, prb_StringDirection dir);
prb_PUBLICDEC prb_String           prb_strTrim(prb_String str);
prb_PUBLICDEC prb_StringFindResult prb_strFind(prb_StringFindSpec spec);
prb_PUBLICDEC prb_StrFindIterator  prb_createStrFindIter(prb_StringFindSpec spec);
prb_PUBLICDEC prb_Status           prb_strFindIterNext(prb_StrFindIterator* iter);
prb_PUBLICDEC bool                 prb_strStartsWith(prb_Arena* arena, prb_String str, prb_String pattern, prb_StringFindMode mode);
prb_PUBLICDEC bool                 prb_strEndsWith(prb_Arena* arena, prb_String str, prb_String pattern, prb_StringFindMode mode);
prb_PUBLICDEC prb_String           prb_strReplace(prb_Arena* arena, prb_StringFindSpec spec, prb_String replacement);
prb_PUBLICDEC prb_String           prb_stringsJoin(prb_Arena* arena, prb_String* strings, int32_t stringsCount, prb_String sep);
prb_PUBLICDEC prb_GrowingString    prb_beginString(prb_Arena* arena);
prb_PUBLICDEC void                 prb_addStringSegment(prb_GrowingString* gstr, const char* fmt, ...) prb_ATTRIBUTE_FORMAT(2, 3);
prb_PUBLICDEC prb_String           prb_endString(prb_GrowingString* gstr);
prb_PUBLICDEC prb_String           prb_vfmtCustomBuffer(void* buf, int32_t bufSize, const char* fmt, va_list args);
prb_PUBLICDEC prb_String           prb_fmt(prb_Arena* arena, const char* fmt, ...) prb_ATTRIBUTE_FORMAT(2, 3);
prb_PUBLICDEC void                 prb_writeToStdout(prb_String str);
prb_PUBLICDEC void                 prb_writelnToStdout(prb_String str);
prb_PUBLICDEC void                 prb_setPrintColor(prb_ColorID color);
prb_PUBLICDEC void                 prb_resetPrintColor(void);
prb_PUBLICDEC prb_Utf8CharIterator prb_createUtf8CharIter(prb_String str, prb_StringDirection direction);
prb_PUBLICDEC prb_Status           prb_utf8CharIterNext(prb_Utf8CharIterator* iter);
prb_PUBLICDEC prb_LineIterator     prb_createLineIter(prb_String str);
prb_PUBLICDEC prb_Status           prb_lineIterNext(prb_LineIterator* iter);
prb_PUBLICDEC prb_WordIterator     prb_createWordIter(prb_String str);
prb_PUBLICDEC prb_Status           prb_wordIterNext(prb_WordIterator* iter);

// SECTION Processes
prb_PUBLICDEC void              prb_terminate(int32_t code);
prb_PUBLICDEC prb_String        prb_getCmdline(prb_Arena* arena);
prb_PUBLICDEC prb_String*       prb_getCmdArgs(prb_Arena* arena);
prb_PUBLICDEC const char**      prb_getArgArrayFromString(prb_Arena* arena, prb_String string);
prb_PUBLICDEC prb_ProcessHandle prb_execCmd(prb_Arena* arena, prb_String cmd, prb_ProcessFlags flags, prb_String redirectFilepath);
prb_PUBLICDEC prb_Status        prb_waitForProcesses(prb_ProcessHandle* handles, int32_t handleCount);
prb_PUBLICDEC void              prb_sleep(float ms);

// SECTION Timing
prb_PUBLICDEC prb_TimeStart prb_timeStart(void);
prb_PUBLICDEC float         prb_getMsFrom(prb_TimeStart timeStart);

// SECTION Multithreading
prb_PUBLICDEC prb_Job    prb_createJob(prb_JobProc proc, void* data, prb_Arena* arena, int32_t arenaBytes);
prb_PUBLICDEC prb_Status prb_execJobs(prb_Job* jobs, int32_t jobsCount);

// SECTION Hashing
prb_PUBLICDEC uint32_t prb_crc32(const void* data, size_t n_bytes);

//
// SECTION stb snprintf
//

#if defined(__clang__)
#if defined(__has_feature) && defined(__has_attribute)
#if __has_feature(address_sanitizer)
#if __has_attribute(__no_sanitize__)
#define STBSP__ASAN __attribute__((__no_sanitize__("address")))
#elif __has_attribute(__no_sanitize_address__)
#define STBSP__ASAN __attribute__((__no_sanitize_address__))
#elif __has_attribute(__no_address_safety_analysis__)
#define STBSP__ASAN __attribute__((__no_address_safety_analysis__))
#endif
#endif
#endif
#elif defined(__GNUC__) && (__GNUC__ >= 5 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8))
#if defined(__SANITIZE_ADDRESS__) && __SANITIZE_ADDRESS__
#define STBSP__ASAN __attribute__((__no_sanitize_address__))
#endif
#endif

#ifndef STBSP__ASAN
#define STBSP__ASAN
#endif

#ifndef prb_NOT_STATIC
#define STB_SPRINTF_STATIC
#endif

#ifdef STB_SPRINTF_STATIC
#define STBSP__PUBLICDEC static
#define STBSP__PUBLICDEF static STBSP__ASAN
#else
#ifdef __cplusplus
#define STBSP__PUBLICDEC extern "C"
#define STBSP__PUBLICDEF extern "C" STBSP__ASAN
#else
#define STBSP__PUBLICDEC extern
#define STBSP__PUBLICDEF STBSP__ASAN
#endif
#endif

#if defined(__has_attribute)
#if __has_attribute(format)
#define STBSP__ATTRIBUTE_FORMAT(fmt, va) __attribute__((format(printf, fmt, va)))
#endif
#endif

#ifndef STBSP__ATTRIBUTE_FORMAT
#define STBSP__ATTRIBUTE_FORMAT(fmt, va)
#endif

#ifdef _MSC_VER
#define STBSP__NOTUSED(v) (void)(v)
#else
#define STBSP__NOTUSED(v) (void)sizeof(v)
#endif

#ifndef STB_SPRINTF_MIN
#define STB_SPRINTF_MIN 512  // how many characters per callback
#endif
typedef char* STBSP_SPRINTFCB(const char* buf, void* user, int len);

#ifndef STB_SPRINTF_DECORATE
#define STB_SPRINTF_DECORATE(name) stbsp_##name  // define this before including if you want to change the names
#endif

STBSP__PUBLICDEC int STB_SPRINTF_DECORATE(vsprintf)(char* buf, char const* fmt, va_list va);
STBSP__PUBLICDEC int STB_SPRINTF_DECORATE(vsnprintf)(char* buf, int count, char const* fmt, va_list va);
STBSP__PUBLICDEC int STB_SPRINTF_DECORATE(sprintf)(char* buf, char const* fmt, ...) STBSP__ATTRIBUTE_FORMAT(2, 3);
STBSP__PUBLICDEC int STB_SPRINTF_DECORATE(snprintf)(char* buf, int count, char const* fmt, ...)
    STBSP__ATTRIBUTE_FORMAT(3, 4);

STBSP__PUBLICDEC int  STB_SPRINTF_DECORATE(vsprintfcb
)(STBSP_SPRINTFCB* callback, void* user, char* buf, char const* fmt, va_list va);
STBSP__PUBLICDEC void STB_SPRINTF_DECORATE(set_separators)(char comma, char period);

//
// SECTION stb ds
//

#ifndef STBDS_NO_SHORT_NAMES
#define arrlen stbds_arrlen
#define arrlenu stbds_arrlenu
#define arrput stbds_arrput
#define arrpush stbds_arrput
#define arrpop stbds_arrpop
#define arrfree stbds_arrfree
#define arraddn stbds_arraddn  // deprecated, use one of the following instead:
#define arraddnptr stbds_arraddnptr
#define arraddnindex stbds_arraddnindex
#define arrsetlen stbds_arrsetlen
#define arrlast stbds_arrlast
#define arrins stbds_arrins
#define arrinsn stbds_arrinsn
#define arrdel stbds_arrdel
#define arrdeln stbds_arrdeln
#define arrdelswap stbds_arrdelswap
#define arrcap stbds_arrcap
#define arrsetcap stbds_arrsetcap

#define hmput stbds_hmput
#define hmputs stbds_hmputs
#define hmget stbds_hmget
#define hmget_ts stbds_hmget_ts
#define hmgets stbds_hmgets
#define hmgetp stbds_hmgetp
#define hmgetp_ts stbds_hmgetp_ts
#define hmgetp_null stbds_hmgetp_null
#define hmgeti stbds_hmgeti
#define hmgeti_ts stbds_hmgeti_ts
#define hmdel stbds_hmdel
#define hmlen stbds_hmlen
#define hmlenu stbds_hmlenu
#define hmfree stbds_hmfree
#define hmdefault stbds_hmdefault
#define hmdefaults stbds_hmdefaults

#define shput stbds_shput
#define shputi stbds_shputi
#define shputs stbds_shputs
#define shget stbds_shget
#define shgeti stbds_shgeti
#define shgets stbds_shgets
#define shgetp stbds_shgetp
#define shgetp_null stbds_shgetp_null
#define shdel stbds_shdel
#define shlen stbds_shlen
#define shlenu stbds_shlenu
#define shfree stbds_shfree
#define shdefault stbds_shdefault
#define shdefaults stbds_shdefaults
#define sh_new_arena stbds_sh_new_arena
#define sh_new_strdup stbds_sh_new_strdup

#define stralloc stbds_stralloc
#define strreset stbds_strreset
#endif

#if !defined(STBDS_REALLOC) && !defined(STBDS_FREE)
#define STBDS_REALLOC(c, p, s) prb_realloc(p, s)
#define STBDS_FREE(c, p) prb_free(p)
#endif

#ifdef _MSC_VER
#define STBDS_NOTUSED(v) (void)(v)
#else
#define STBDS_NOTUSED(v) (void)sizeof(v)
#endif

#ifdef prb_NOT_STATIC
#define STBDS__PUBLICDEC
#define STBDS__PUBLICDEF
#else
#define STBDS__PUBLICDEC static
#define STBDS__PUBLICDEF static
#endif

#ifdef __cplusplus
extern "C" {
#endif

// for security against attackers, seed the library with a random number, at least time() but stronger is better
STBDS__PUBLICDEC void stbds_rand_seed(size_t seed);

// these are the hash functions used internally if you want to test them or use them for other purposes
STBDS__PUBLICDEC size_t stbds_hash_bytes(void* p, size_t len, size_t seed);
STBDS__PUBLICDEC size_t stbds_hash_string(char* str, size_t seed);

// this is a simple string arena allocator, initialize with e.g. 'stbds_string_arena my_arena={0}'.
typedef struct stbds_string_arena stbds_string_arena;
STBDS__PUBLICDEC char*            stbds_stralloc(stbds_string_arena* a, char* str);
STBDS__PUBLICDEC void             stbds_strreset(stbds_string_arena* a);

///////////////
//
// Everything below here is implementation details
//

STBDS__PUBLICDEC void* stbds_arrgrowf(void* a, size_t elemsize, size_t addlen, size_t min_cap);
STBDS__PUBLICDEC void  stbds_arrfreef(void* a);
STBDS__PUBLICDEC void  stbds_hmfree_func(void* p, size_t elemsize);
STBDS__PUBLICDEC void* stbds_hmget_key(void* a, size_t elemsize, void* key, size_t keysize, int mode);
STBDS__PUBLICDEC void* stbds_hmget_key_ts(void* a, size_t elemsize, void* key, size_t keysize, ptrdiff_t* temp, int mode);
STBDS__PUBLICDEC void* stbds_hmput_default(void* a, size_t elemsize);
STBDS__PUBLICDEC void* stbds_hmput_key(void* a, size_t elemsize, void* key, size_t keysize, int mode);
STBDS__PUBLICDEC void* stbds_hmdel_key(void* a, size_t elemsize, void* key, size_t keysize, size_t keyoffset, int mode);
STBDS__PUBLICDEC void* stbds_shmode_func(size_t elemsize, int mode);

#ifdef __cplusplus
}
#endif

#if defined(__GNUC__) || defined(__clang__)
#define STBDS_HAS_TYPEOF
#ifdef __cplusplus
//#define STBDS_HAS_LITERAL_ARRAY  // this is currently broken for clang
#endif
#endif

#if !defined(__cplusplus)
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define STBDS_HAS_LITERAL_ARRAY
#endif
#endif

// clang-format off
// this macro takes the address of the argument, but on gcc/clang can accept rvalues
#if defined(STBDS_HAS_LITERAL_ARRAY) && defined(STBDS_HAS_TYPEOF)
  #if __clang__
  #define STBDS_ADDRESSOF(typevar, value)     ((__typeof__(typevar)[1]){value}) // literal array decays to pointer to value
  #else
  #define STBDS_ADDRESSOF(typevar, value)     ((typeof(typevar)[1]){value}) // literal array decays to pointer to value
  #endif
#else
#define STBDS_ADDRESSOF(typevar, value)     &(value)
#endif

#define STBDS_OFFSETOF(var,field)           ((char *) &(var)->field - (char *) (var))

#define stbds_header(t)  ((stbds_array_header *) (t) - 1)
#define stbds_temp(t)    stbds_header(t)->temp
#define stbds_temp_key(t) (*(char **) stbds_header(t)->hash_table)

#define stbds_arrsetcap(a,n)   (stbds_arrgrow(a,0,n))
#define stbds_arrsetlen(a,n)   ((stbds_arrcap(a) < (size_t) (n) ? stbds_arrsetcap((a),(size_t)(n)),0 : 0), (a) ? stbds_header(a)->length = (size_t) (n) : 0)
#define stbds_arrcap(a)        ((a) ? stbds_header(a)->capacity : 0)
#define stbds_arrlen(a)        ((a) ? (ptrdiff_t) stbds_header(a)->length : 0)
#define stbds_arrlenu(a)       ((a) ?             stbds_header(a)->length : 0)
#define stbds_arrput(a,v)      (stbds_arrmaybegrow(a,1), (a)[stbds_header(a)->length++] = (v))
#define stbds_arrpush          stbds_arrput  // synonym
#define stbds_arrpop(a)        (stbds_header(a)->length--, (a)[stbds_header(a)->length])
#define stbds_arraddn(a,n)     ((void)(stbds_arraddnindex(a, n)))    // deprecated, use one of the following instead:
#define stbds_arraddnptr(a,n)  (stbds_arrmaybegrow(a,n), (n) ? (stbds_header(a)->length += (n), &(a)[stbds_header(a)->length-(n)]) : (a))
#define stbds_arraddnindex(a,n)(stbds_arrmaybegrow(a,n), (n) ? (stbds_header(a)->length += (n), stbds_header(a)->length-(n)) : stbds_arrlen(a))
#define stbds_arraddnoff       stbds_arraddnindex
#define stbds_arrlast(a)       ((a)[stbds_header(a)->length-1])
#define stbds_arrfree(a)       ((void) ((a) ? STBDS_FREE(NULL,stbds_header(a)) : (void)0), (a)=NULL)
#define stbds_arrdel(a,i)      stbds_arrdeln(a,i,1)
#define stbds_arrdeln(a,i,n)   (memmove(&(a)[i], &(a)[(i)+(n)], sizeof *(a) * (stbds_header(a)->length-(n)-(i))), stbds_header(a)->length -= (n))
#define stbds_arrdelswap(a,i)  ((a)[i] = stbds_arrlast(a), stbds_header(a)->length -= 1)
#define stbds_arrinsn(a,i,n)   (stbds_arraddn((a),(n)), memmove(&(a)[(i)+(n)], &(a)[i], sizeof *(a) * (stbds_header(a)->length-(n)-(i))))
#define stbds_arrins(a,i,v)    (stbds_arrinsn((a),(i),1), (a)[i]=(v))

#define stbds_arrmaybegrow(a,n)  ((!(a) || stbds_header(a)->length + (n) > stbds_header(a)->capacity) \
                                  ? (stbds_arrgrow(a,n,0),0) : 0)

#define stbds_arrgrow(a,b,c)   ((a) = stbds_arrgrowf_wrapper((a), sizeof *(a), (b), (c)))

#define stbds_hmput(t, k, v) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, 0),   \
     (t)[stbds_temp((t)-1)].key = (k),    \
     (t)[stbds_temp((t)-1)].value = (v))

#define stbds_hmputs(t, s) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), &(s).key, sizeof (s).key, STBDS_HM_BINARY), \
     (t)[stbds_temp((t)-1)] = (s))

#define stbds_hmgeti(t,k) \
    ((t) = stbds_hmget_key_wrapper((t), sizeof *(t), (void*) STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, STBDS_HM_BINARY), \
      stbds_temp((t)-1))

#define stbds_hmgeti_ts(t,k,temp) \
    ((t) = stbds_hmget_key_ts_wrapper((t), sizeof *(t), (void*) STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, &(temp), STBDS_HM_BINARY), \
      (temp))

#define stbds_hmgetp(t, k) \
    ((void) stbds_hmgeti(t,k), &(t)[stbds_temp((t)-1)])

#define stbds_hmgetp_ts(t, k, temp) \
    ((void) stbds_hmgeti_ts(t,k,temp), &(t)[temp])

#define stbds_hmdel(t,k) \
    (((t) = stbds_hmdel_key_wrapper((t),sizeof *(t), (void*) STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, STBDS_OFFSETOF((t),key), STBDS_HM_BINARY)),(t)?stbds_temp((t)-1):0)

#define stbds_hmdefault(t, v) \
    ((t) = stbds_hmput_default_wrapper((t), sizeof *(t)), (t)[-1].value = (v))

#define stbds_hmdefaults(t, s) \
    ((t) = stbds_hmput_default_wrapper((t), sizeof *(t)), (t)[-1] = (s))

#define stbds_hmfree(p)        \
    ((void) ((p) != NULL ? stbds_hmfree_func((p)-1,sizeof*(p)),0 : 0),(p)=NULL)

#define stbds_hmgets(t, k)    (*stbds_hmgetp(t,k))
#define stbds_hmget(t, k)     (stbds_hmgetp(t,k)->value)
#define stbds_hmget_ts(t, k, temp)  (stbds_hmgetp_ts(t,k,temp)->value)
#define stbds_hmlen(t)        ((t) ? (ptrdiff_t) stbds_header((t)-1)->length-1 : 0)
#define stbds_hmlenu(t)       ((t) ?             stbds_header((t)-1)->length-1 : 0)
#define stbds_hmgetp_null(t,k)  (stbds_hmgeti(t,k) == -1 ? NULL : &(t)[stbds_temp((t)-1)])

#define stbds_shput(t, k, v) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (t)->key, STBDS_HM_STRING),   \
     (t)[stbds_temp((t)-1)].value = (v))

#define stbds_shputi(t, k, v) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (t)->key, STBDS_HM_STRING),   \
     (t)[stbds_temp((t)-1)].value = (v), stbds_temp((t)-1))

#define stbds_shputs(t, s) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (s).key, sizeof (s).key, STBDS_HM_STRING), \
     (t)[stbds_temp((t)-1)] = (s), \
     (t)[stbds_temp((t)-1)].key = stbds_temp_key((t)-1)) // above line overwrites whole structure, so must rewrite key here if it was allocated internally

#define stbds_pshput(t, p) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (p)->key, sizeof (p)->key, STBDS_HM_PTR_TO_STRING), \
     (t)[stbds_temp((t)-1)] = (p))

#define stbds_shgeti(t,k) \
     ((t) = stbds_hmget_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (t)->key, STBDS_HM_STRING), \
      stbds_temp((t)-1))

#define stbds_pshgeti(t,k) \
     ((t) = stbds_hmget_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (*(t))->key, STBDS_HM_PTR_TO_STRING), \
      stbds_temp((t)-1))

#define stbds_shgetp(t, k) \
    ((void) stbds_shgeti(t,k), &(t)[stbds_temp((t)-1)])

#define stbds_pshget(t, k) \
    ((void) stbds_pshgeti(t,k), (t)[stbds_temp((t)-1)])

#define stbds_shdel(t,k) \
    (((t) = stbds_hmdel_key_wrapper((t),sizeof *(t), (void*) (k), sizeof (t)->key, STBDS_OFFSETOF((t),key), STBDS_HM_STRING)),(t)?stbds_temp((t)-1):0)
#define stbds_pshdel(t,k) \
    (((t) = stbds_hmdel_key_wrapper((t),sizeof *(t), (void*) (k), sizeof (*(t))->key, STBDS_OFFSETOF(*(t),key), STBDS_HM_PTR_TO_STRING)),(t)?stbds_temp((t)-1):0)

#define stbds_sh_new_arena(t)  \
    ((t) = stbds_shmode_func_wrapper(t, sizeof *(t), STBDS_SH_ARENA))
#define stbds_sh_new_strdup(t) \
    ((t) = stbds_shmode_func_wrapper(t, sizeof *(t), STBDS_SH_STRDUP))

#define stbds_shdefault(t, v)  stbds_hmdefault(t,v)
#define stbds_shdefaults(t, s) stbds_hmdefaults(t,s)

#define stbds_shfree       stbds_hmfree
#define stbds_shlenu       stbds_hmlenu

#define stbds_shgets(t, k) (*stbds_shgetp(t,k))
#define stbds_shget(t, k)  (stbds_shgetp(t,k)->value)
#define stbds_shgetp_null(t,k)  (stbds_shgeti(t,k) == -1 ? NULL : &(t)[stbds_temp((t)-1)])
#define stbds_shlen        stbds_hmlen

// clang-format on

typedef struct {
    size_t    length;
    size_t    capacity;
    void*     hash_table;
    ptrdiff_t temp;
} stbds_array_header;

typedef struct stbds_string_block {
    struct stbds_string_block* next;
    char                       storage[8];
} stbds_string_block;

struct stbds_string_arena {
    stbds_string_block* storage;
    size_t              remaining;
    unsigned char       block;
    unsigned char       mode;  // this isn't used by the string arena itself
};

#define STBDS_HM_BINARY 0
#define STBDS_HM_STRING 1

enum {
    STBDS_SH_NONE,
    STBDS_SH_DEFAULT,
    STBDS_SH_STRDUP,
    STBDS_SH_ARENA,
};

#ifdef __cplusplus
// in C we use implicit assignment from these void*-returning functions to T*.
// in C++ these templates make the same code work
template<class T>
static T*
stbds_arrgrowf_wrapper(T* a, size_t elemsize, size_t addlen, size_t min_cap) {
    return (T*)stbds_arrgrowf((void*)a, elemsize, addlen, min_cap);
}
template<class T>
static T*
stbds_hmget_key_wrapper(T* a, size_t elemsize, void* key, size_t keysize, int mode) {
    return (T*)stbds_hmget_key((void*)a, elemsize, key, keysize, mode);
}
template<class T>
static T*
stbds_hmget_key_ts_wrapper(T* a, size_t elemsize, void* key, size_t keysize, ptrdiff_t* temp, int mode) {
    return (T*)stbds_hmget_key_ts((void*)a, elemsize, key, keysize, temp, mode);
}
template<class T>
static T*
stbds_hmput_default_wrapper(T* a, size_t elemsize) {
    return (T*)stbds_hmput_default((void*)a, elemsize);
}
template<class T>
static T*
stbds_hmput_key_wrapper(T* a, size_t elemsize, void* key, size_t keysize, int mode) {
    return (T*)stbds_hmput_key((void*)a, elemsize, key, keysize, mode);
}
template<class T>
static T*
stbds_hmdel_key_wrapper(T* a, size_t elemsize, void* key, size_t keysize, size_t keyoffset, int mode) {
    return (T*)stbds_hmdel_key((void*)a, elemsize, key, keysize, keyoffset, mode);
}
template<class T>
static T*
stbds_shmode_func_wrapper(T*, size_t elemsize, int mode) {
    return (T*)stbds_shmode_func(elemsize, mode);
}
#else
#define stbds_arrgrowf_wrapper stbds_arrgrowf
#define stbds_hmget_key_wrapper stbds_hmget_key
#define stbds_hmget_key_ts_wrapper stbds_hmget_key_ts
#define stbds_hmput_default_wrapper stbds_hmput_default
#define stbds_hmput_key_wrapper stbds_hmput_key
#define stbds_hmdel_key_wrapper stbds_hmdel_key
#define stbds_shmode_func_wrapper(t, e, m) stbds_shmode_func(e, m)
#endif

#endif  // PROGRAMMABLE_BUILD_H

#ifndef prb_NO_IMPLEMENTATION

//
// SECTION Memory (implementation)
//

prb_PUBLICDEF bool
prb_memeq(const void* ptr1, const void* ptr2, int32_t bytes) {
    prb_assert(bytes >= 0);
    int  memcmpResult = prb_memcmp(ptr1, ptr2, bytes);
    bool result = memcmpResult == 0;
    return result;
}

prb_PUBLICDEF int32_t
prb_getOffsetForAlignment(void* ptr, int32_t align) {
    prb_assert(prb_isPowerOf2(align));
    uintptr_t ptrAligned = (uintptr_t)((uint8_t*)ptr + (align - 1)) & (uintptr_t)(~(align - 1));
    prb_assert(ptrAligned >= (uintptr_t)ptr);
    intptr_t diff = ptrAligned - (uintptr_t)ptr;
    prb_assert(diff < align && diff >= 0);
    return (int32_t)diff;
}

prb_PUBLICDEF void*
prb_vmemAlloc(int32_t bytes) {
#if prb_PLATFORM_WINDOWS

    void* ptr = VirtualAlloc(0, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    prb_assert(ptr);

#elif prb_PLATFORM_LINUX

    void* ptr = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    prb_assert(ptr != MAP_FAILED);

#else
#error unimplemented
#endif

    return ptr;
}

prb_PUBLICDEF prb_Arena
prb_createArenaFromVmem(int32_t bytes) {
    prb_Arena arena = {};
    arena.base = prb_vmemAlloc(bytes);
    arena.size = bytes;
    return arena;
}

prb_PUBLICDEF prb_Arena
prb_createArenaFromArena(prb_Arena* parent, int32_t bytes) {
    prb_Arena arena = {};
    arena.base = prb_arenaFreePtr(parent);
    arena.size = bytes;
    prb_arenaChangeUsed(parent, bytes);
    return arena;
}

prb_PUBLICDEF void*
prb_arenaAllocAndZero(prb_Arena* arena, int32_t size, int32_t align) {
    prb_assert(!arena->lockedForString);
    prb_arenaAlignFreePtr(arena, align);
    void* result = prb_arenaFreePtr(arena);
    prb_arenaChangeUsed(arena, size);
    prb_memset(result, 0, size);
    return result;
}

prb_PUBLICDEF void
prb_arenaAlignFreePtr(prb_Arena* arena, int32_t align) {
    int32_t offset = prb_getOffsetForAlignment(prb_arenaFreePtr(arena), align);
    prb_arenaChangeUsed(arena, offset);
}

prb_PUBLICDEF void*
prb_arenaFreePtr(prb_Arena* arena) {
    void* result = (uint8_t*)arena->base + arena->used;
    return result;
}

prb_PUBLICDEF int32_t
prb_arenaFreeSize(prb_Arena* arena) {
    int32_t result = arena->size - arena->used;
    return result;
}

prb_PUBLICDEF void
prb_arenaChangeUsed(prb_Arena* arena, int32_t byteDelta) {
    prb_assert(prb_arenaFreeSize(arena) >= byteDelta);
    arena->used += byteDelta;
}

prb_PUBLICDEF prb_TempMemory
prb_beginTempMemory(prb_Arena* arena) {
    prb_TempMemory temp = {.arena = arena, .usedAtBegin = arena->used, .tempCountAtBegin = arena->tempCount};
    arena->tempCount += 1;
    return temp;
}

prb_PUBLICDEF void
prb_endTempMemory(prb_TempMemory temp) {
    prb_assert(temp.arena->tempCount == temp.tempCountAtBegin + 1);
    temp.arena->used = temp.usedAtBegin;
    temp.arena->tempCount -= 1;
}

//
// SECTION Filesystem (implementation)
//

#if prb_PLATFORM_LINUX

typedef struct prb_linux_GetFileStatResult {
    bool        success;
    struct stat stat;
} prb_linux_GetFileStatResult;

static prb_linux_GetFileStatResult
prb_linux_getFileStat(prb_Arena* arena, prb_String path) {
    prb_TempMemory              temp = prb_beginTempMemory(arena);
    prb_linux_GetFileStatResult result = {};
    const char*                 pathNull = prb_strGetNullTerminated(arena, path);
    struct stat                 statBuf = {};
    if (stat(pathNull, &statBuf) == 0) {
        result = (prb_linux_GetFileStatResult) {.success = true, .stat = statBuf};
    }
    prb_endTempMemory(temp);
    return result;
}

typedef struct prb_linux_OpenResult {
    bool success;
    int  handle;
} prb_linux_OpenResult;

static prb_linux_OpenResult
prb_linux_open(prb_Arena* arena, prb_String path, int oflags, mode_t mode) {
    prb_TempMemory       temp = prb_beginTempMemory(arena);
    const char*          pathNull = prb_strGetNullTerminated(arena, path);
    prb_linux_OpenResult result = {};
    result.handle = open(pathNull, oflags, mode);
    result.success = result.handle != -1;
    prb_endTempMemory(temp);
    return result;
}

#endif

prb_PUBLICDEF bool
prb_pathExists(prb_Arena* arena, prb_String path) {
    bool result = false;

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    prb_linux_GetFileStatResult stat = prb_linux_getFileStat(arena, path);
    result = stat.success;

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF bool
prb_isDirectory(prb_Arena* arena, prb_String path) {
    bool result = false;

#if prb_PLATFORM_WINDOWS

    prb_String pathNoTrailingSlash = path;
    char       lastChar = path.ptr[path.len - 1];
    if (prb_charIsSep(lastChar)) {
        pathNoTrailingSlash = prb_stringCopy(path, 0, path.len - 2);
    }
    bool             result = false;
    WIN32_FIND_DATAA findData;
    HANDLE           findHandle = FindFirstFileA(pathNoTrailingSlash.ptr, &findData);
    if (findHandle != INVALID_HANDLE_VALUE) {
        result = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

#elif prb_PLATFORM_LINUX

    prb_linux_GetFileStatResult stat = prb_linux_getFileStat(arena, path);
    if (stat.success) {
        result = S_ISDIR(stat.stat.st_mode);
    }

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF bool
prb_isFile(prb_Arena* arena, prb_String path) {
    bool result = false;

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    prb_linux_GetFileStatResult stat = prb_linux_getFileStat(arena, path);
    if (stat.success) {
        result = S_ISREG(stat.stat.st_mode);
    }

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF bool
prb_directoryIsEmpty(prb_Arena* arena, prb_String path) {
    prb_TempMemory   temp = prb_beginTempMemory(arena);
    prb_PathFindSpec spec = {};
    spec.arena = arena;
    spec.dir = path;
    spec.mode = prb_PathFindMode_AllEntriesInDir;
    prb_PathFindIterator iter = prb_createPathFindIter(spec);
    bool                 result = prb_pathFindIterNext(&iter) == prb_Failure;
    prb_destroyPathFindIter(&iter);
    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Status
prb_createDirIfNotExists(prb_Arena* arena, prb_String path) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    const char*    pathNull = prb_strGetNullTerminated(arena, path);
    prb_Status     result = prb_Success;

#if prb_PLATFORM_WINDOWS

    // TODO(khvorov) Check error
    CreateDirectory(path.ptr, 0);

#elif prb_PLATFORM_LINUX

    if (!prb_isDirectory(arena, path)) {
        result = mkdir(pathNull, S_IRWXU | S_IRWXG | S_IRWXO) == 0 ? prb_Success : prb_Failure;
    }

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Status
prb_removeFileOrDirectoryIfExists(prb_Arena* arena, prb_String path) {
    prb_Status result = prb_Failure;
    if (prb_isDirectory(arena, path)) {
        result = prb_removeDirectoryIfExists(arena, path);
    } else {
        result = prb_removeFileIfExists(arena, path);
    }
    return result;
}

prb_PUBLICDEF prb_Status
prb_removeFileIfExists(prb_Arena* arena, prb_String path) {
    prb_Status     result = prb_Success;
    prb_TempMemory temp = prb_beginTempMemory(arena);
    const char*    pathNull = prb_strGetNullTerminated(arena, path);

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    if (prb_isFile(arena, path)) {
        result = unlink(pathNull) == 0 ? prb_Success : prb_Failure;
    }

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Status
prb_removeDirectoryIfExists(prb_Arena* arena, prb_String path) {
    prb_Status     result = prb_Success;
    prb_TempMemory temp = prb_beginTempMemory(arena);

#if prb_PLATFORM_WINDOWS

    const char*       pathNull = prb_strGetNullTerminated(arena, path);
    prb_StringBuilder doubleNullBuilder = prb_createStringBuilder(path.len + 2);
    prb_stringBuilderWrite(&doubleNullBuilder, path);
    SHFileOperationA(&(SHFILEOPSTRUCTA) {
        .wFunc = FO_DELETE,
        .pFrom = doubleNullBuilder.string.ptr,
        .fFlags = FOF_NO_UI,
    });

#elif prb_PLATFORM_LINUX

    if (prb_isDirectory(arena, path)) {
        prb_PathFindSpec spec = {};
        spec.arena = arena;
        spec.dir = path;
        spec.mode = prb_PathFindMode_AllEntriesInDir;
        prb_PathFindIterator iter = prb_createPathFindIter(spec);
        while (prb_pathFindIterNext(&iter) && result == prb_Success) {
            result = prb_removeFileOrDirectoryIfExists(arena, iter.curPath);
        }
        prb_destroyPathFindIter(&iter);
        if (result == prb_Success) {
            prb_assert(prb_directoryIsEmpty(arena, path));
            const char* pathNull = prb_strGetNullTerminated(arena, path);
            result = rmdir(pathNull) == 0 ? prb_Success : prb_Failure;
        }
    }

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Status
prb_clearDirectory(prb_Arena* arena, prb_String path) {
    prb_Status result = prb_removeFileOrDirectoryIfExists(arena, path);
    if (result == prb_Success) {
        result = prb_createDirIfNotExists(arena, path);
    }
    return result;
}

prb_PUBLICDEF prb_String
prb_getWorkingDir(prb_Arena* arena) {
#if prb_PLATFORM_WINDOWS

    // TODO(khvorov) Make sure long paths work
    // TODO(khvorov) Check error
    int32_t maxLen = MAX_PATH + 1;
    char*   ptr = (char*)prb_allocAndZero(maxLen, 1);
    GetCurrentDirectoryA(maxLen, ptr);
    return ptr;

#elif prb_PLATFORM_LINUX

    char* ptr = (char*)prb_arenaFreePtr(arena);
    prb_assert(getcwd(ptr, prb_arenaFreeSize(arena)));
    prb_String result = prb_STR(ptr);
    prb_arenaChangeUsed(arena, result.len);
    prb_arenaAllocAndZero(arena, 1, 1);  // NOTE(khvorov) Null terminator
    return result;

#else
#error unimplemented
#endif
}

prb_PUBLICDEF prb_Status
prb_setWorkingDir(prb_Arena* arena, prb_String dir) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    const char*    dirNull = prb_strGetNullTerminated(arena, dir);
    prb_Status     result = prb_Failure;

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    result = chdir(dirNull) == 0 ? prb_Success : prb_Failure;

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_String
prb_pathJoin(prb_Arena* arena, prb_String path1, prb_String path2) {
    prb_assert(path1.ptr && path2.ptr && path1.len > 0 && path2.len > 0);
    char path1LastChar = path1.ptr[path1.len - 1];
    bool path1EndsOnSep = prb_charIsSep(path1LastChar);
    if (path1EndsOnSep) {
        path1.len -= 1;
    }
    char path2FirstChar = path2.ptr[0];
    bool path2StartsOnSep = prb_charIsSep(path2FirstChar);
    if (path2StartsOnSep) {
        path2 = prb_strSliceForward(path2, 1);
    }
    prb_String result = prb_fmt(arena, "%.*s/%.*s", prb_LIT(path1), prb_LIT(path2));
    return result;
}

prb_PUBLICDEF bool
prb_charIsSep(char ch) {
#if prb_PLATFORM_WINDOWS

    bool result = ch == '/' || ch == '\\';
    return result;

#elif prb_PLATFORM_LINUX

    bool result = ch == '/';
    return result;

#else
#error unimplemented
#endif
}

prb_PUBLICDEF prb_StringFindResult
prb_findSepBeforeLastEntry(prb_String path) {
    prb_StringFindSpec spec = {};
    spec.str = path;
    spec.pattern = prb_STR("/\\");
    spec.mode = prb_StringFindMode_AnyChar;
    spec.direction = prb_StringDirection_FromEnd;
    prb_StringFindResult result = prb_strFind(spec);

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    // NOTE(khvorov) Ignore trailing slash. Root path '/' does not have a separator before it
    if (result.found && result.matchByteIndex == spec.str.len - 1) {
        spec.str.len -= 1;
        result = prb_strFind(spec);
    }

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF prb_String
prb_getParentDir(prb_Arena* arena, prb_String path) {
#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    prb_assert(!prb_streq(path, prb_STR("/")));

#endif

    prb_StringFindResult findResult = prb_findSepBeforeLastEntry(path);
    prb_String           result = findResult.found ? (prb_String) {path.ptr, findResult.matchByteIndex + 1} : prb_getWorkingDir(arena);
    return result;
}

prb_PUBLICDEF prb_String
prb_getLastEntryInPath(prb_String path) {
    prb_StringFindResult findResult = prb_findSepBeforeLastEntry(path);
    prb_String           result = path;

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    if (findResult.found) {
        prb_assert(path.len > 1);
        result = prb_strSliceForward(path, findResult.matchByteIndex + 1);
    }

#else
#error unimplemented
#endif
    return result;
}

prb_PUBLICDEF prb_String
prb_replaceExt(prb_Arena* arena, prb_String path, prb_String newExt) {
    prb_StringFindSpec spec = {};
    spec.str = path;
    spec.pattern = prb_STR(".");
    spec.mode = prb_StringFindMode_AnyChar;
    spec.direction = prb_StringDirection_FromEnd;
    prb_StringFindResult dotFind = prb_strFind(spec);
    prb_String           result = {};
    if (dotFind.found) {
        result = prb_fmt(arena, "%.*s.%.*s", dotFind.matchByteIndex, path.ptr, newExt.len, newExt.ptr);
    } else {
        result = prb_fmt(arena, "%.*s.%.*s", path.len, path.ptr, newExt.len, newExt.ptr);
    }
    return result;
}

prb_PUBLICDEF prb_PathFindIterator
prb_createPathFindIter(prb_PathFindSpec spec) {
    prb_TempMemory       temp = prb_beginTempMemory(spec.arena);
    prb_PathFindIterator iter = {};
    iter.spec = spec;

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    switch (spec.mode) {
        case prb_PathFindMode_AllEntriesInDir: {
            const char* dirNull = prb_strGetNullTerminated(spec.arena, spec.dir);
            DIR*        handle = opendir(dirNull);
            prb_assert(handle);
            prb_PathFindIterator_DirHandle firstParent = {spec.dir, handle};
            arrput(iter.allEntriesInDir.parents, firstParent);
        } break;

        case prb_PathFindMode_Glob: {
            iter.glob.currentIndex = -1;
            prb_String pattern = prb_pathJoin(spec.arena, spec.dir, spec.glob.pattern);
            iter.glob.returnVal = glob(pattern.ptr, GLOB_NOSORT, 0, &iter.glob.result);
            if (spec.recursive) {
                prb_PathFindSpec recursiveSpec = spec;
                recursiveSpec.mode = prb_PathFindMode_AllEntriesInDir;
                prb_PathFindIterator recursiveIter = prb_createPathFindIter(recursiveSpec);
                while (prb_pathFindIterNext(&recursiveIter)) {
                    if (prb_isDirectory(spec.arena, recursiveIter.curPath)) {
                        prb_String newPat = prb_pathJoin(spec.arena, recursiveIter.curPath, spec.glob.pattern);
                        int        newReturnVal = glob(newPat.ptr, GLOB_NOSORT | GLOB_APPEND, 0, &iter.glob.result);
                        if (newReturnVal == 0) {
                            iter.glob.returnVal = 0;
                        }
                    }
                }
                prb_destroyPathFindIter(&recursiveIter);
            }
        } break;
    }

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return iter;
}

prb_PUBLICDEF prb_Status
prb_pathFindIterNext(prb_PathFindIterator* iter) {
    prb_Status result = prb_Failure;
    iter->curPath = (prb_String) {};

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    switch (iter->spec.mode) {
        case prb_PathFindMode_AllEntriesInDir: {
            prb_PathFindIterator_DirHandle parent = iter->allEntriesInDir.parents[arrlen(iter->allEntriesInDir.parents) - 1];
            for (struct dirent* entry = readdir(parent.handle); entry; entry = readdir(parent.handle)) {
                bool isDot = entry->d_name[0] == '.' && entry->d_name[1] == '\0';
                bool isDoubleDot = entry->d_name[0] == '.' && entry->d_name[1] == '.' && entry->d_name[2] == '\0';
                if (!isDot && !isDoubleDot) {
                    result = prb_Success;
                    iter->curPath = prb_pathJoin(iter->spec.arena, parent.path, prb_STR(entry->d_name));
                    iter->curMatchCount += 1;
                    if (iter->spec.recursive && prb_isDirectory(iter->spec.arena, iter->curPath)) {
                        DIR* newHandle = opendir(iter->curPath.ptr);
                        prb_assert(newHandle);
                        prb_PathFindIterator_DirHandle newParent = {iter->curPath, newHandle};
                        arrput(iter->allEntriesInDir.parents, newParent);
                    }
                    break;
                }
            }

            if (result == prb_Failure && arrlen(iter->allEntriesInDir.parents) > 1) {
                prb_PathFindIterator_DirHandle doneParent = arrpop(iter->allEntriesInDir.parents);
                closedir(doneParent.handle);
                result = prb_pathFindIterNext(iter);
            }
        } break;

        case prb_PathFindMode_Glob: {
            iter->glob.currentIndex += 1;
            if (iter->glob.returnVal == 0 && (size_t)iter->glob.currentIndex < iter->glob.result.gl_pathc) {
                result = prb_Success;
                // NOTE(khvorov) Make a copy so that this is still usable after destoying the iterator
                iter->curPath = prb_fmt(iter->spec.arena, "%s", iter->glob.result.gl_pathv[iter->glob.currentIndex]);
                iter->curMatchCount += 1;
            }
        } break;
    }

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF void
prb_destroyPathFindIter(prb_PathFindIterator* iter) {
#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    switch (iter->spec.mode) {
        case prb_PathFindMode_AllEntriesInDir:
            for (int32_t parentIndex = 0; parentIndex < arrlen(iter->allEntriesInDir.parents); parentIndex++) {
                closedir(iter->allEntriesInDir.parents[parentIndex].handle);
            }
            arrfree(iter->allEntriesInDir.parents);
            break;
        case prb_PathFindMode_Glob: globfree(&iter->glob.result); break;
    }

#else
#error unimplemented
#endif

    *iter = (prb_PathFindIterator) {};
}

prb_PUBLICDEF prb_Multitime
prb_createMultitime(void) {
    prb_Multitime result = {};
    result.timeEarliest = UINT64_MAX;
    return result;
}

prb_PUBLICDEF prb_FileTimestamp
prb_getLastModified(prb_Arena* arena, prb_String path) {
    prb_FileTimestamp result = {};

#if prb_PLATFORM_WINDOWS

    WIN32_FIND_DATAA findData = {};
    HANDLE           firstHandle = FindFirstFileA(pattern, &findData);
    if (firstHandle != INVALID_HANDLE_VALUE) {
        uint64_t thisLastMod =
            ((uint64_t)findData.ftLastWriteTime.dwHighDateTime << 32) | findData.ftLastWriteTime.dwLowDateTime;
        result = prb_max(result, thisLastMod);
        while (FindNextFileA(firstHandle, &findData)) {
            thisLastMod =
                ((uint64_t)findData.ftLastWriteTime.dwHighDateTime << 32) | findData.ftLastWriteTime.dwLowDateTime;
            result = prb_max(result, thisLastMod);
        }
        FindClose(firstHandle);
    }

#elif prb_PLATFORM_LINUX

    prb_linux_GetFileStatResult statResult = prb_linux_getFileStat(arena, path);
    if (statResult.success) {
        result = (prb_FileTimestamp) {
            .valid = true,
            .timestamp = (uint64_t)statResult.stat.st_mtim.tv_sec * 1000 * 1000 * 1000 + (uint64_t)statResult.stat.st_mtim.tv_nsec,
        };
    }

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF void
prb_multitimeAdd(prb_Multitime* multitime, prb_FileTimestamp newTimestamp) {
    if (newTimestamp.valid) {
        multitime->validAddedTimestampsCount += 1;
        multitime->timeEarliest = prb_min(multitime->timeEarliest, newTimestamp.timestamp);
        multitime->timeLatest = prb_max(multitime->timeLatest, newTimestamp.timestamp);
    } else {
        multitime->invalidAddedTimestampsCount += 1;
    }
}

prb_PUBLICDEF prb_ReadEntireFileResult
prb_readEntireFile(prb_Arena* arena, prb_String path) {
    prb_ReadEntireFileResult result = {};

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    prb_linux_OpenResult handle = prb_linux_open(arena, path, O_RDONLY, 0);
    if (handle.success) {
        struct stat statBuf = {};
        if (fstat(handle.handle, &statBuf) == 0) {
            uint8_t* buf = (uint8_t*)prb_arenaAllocAndZero(arena, statBuf.st_size + 1, 1);  // NOTE(sen) Null terminator just in case
            int32_t  readResult = read(handle.handle, buf, statBuf.st_size);
            if (readResult == statBuf.st_size) {
                result.success = true;
                result.content = (prb_Bytes) {buf, readResult};
            }
        }
        close(handle.handle);
    }

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF prb_Status
prb_writeEntireFile(prb_Arena* arena, prb_String path, const void* content, int32_t contentLen) {
    prb_Status result = prb_Failure;

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    prb_linux_OpenResult handle = prb_linux_open(arena, path, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR);
    if (handle.success) {
        ssize_t writeResult = write(handle.handle, content, contentLen);
        result = writeResult == contentLen ? prb_Success : prb_Failure;
        close(handle.handle);
    }

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF prb_Status
prb_binaryToCArray(prb_Arena* arena, prb_String inPath, prb_String outPath, prb_String arrayName) {
    prb_Status               result = prb_Failure;
    prb_TempMemory           temp = prb_beginTempMemory(arena);
    prb_ReadEntireFileResult inContent = prb_readEntireFile(arena, inPath);
    if (inContent.success) {
        prb_GrowingString arrayGstr = prb_beginString(arena);
        prb_addStringSegment(&arrayGstr, "unsigned char %.*s[] = {", arrayName.len, arrayName.ptr);

        for (int32_t byteIndex = 0; byteIndex < inContent.content.len; byteIndex++) {
            uint8_t byte = inContent.content.data[byteIndex];
            prb_addStringSegment(&arrayGstr, "0x%x", byte);
            if (byteIndex != inContent.content.len - 1) {
                prb_addStringSegment(&arrayGstr, ", ");
            }
        }
        prb_addStringSegment(&arrayGstr, "};");
        prb_String arrayStr = prb_endString(&arrayGstr);

        result = prb_writeEntireFile(arena, outPath, arrayStr.ptr, arrayStr.len);
    }

    prb_endTempMemory(temp);
    return result;
}

//
// SECTION Strings (implementation)
//

prb_PUBLICDEF bool
prb_streq(prb_String str1, prb_String str2) {
    bool result = false;
    if (str1.len == str2.len) {
        result = prb_memeq(str1.ptr, str2.ptr, str1.len);
    }
    return result;
}

prb_PUBLICDEF prb_String
prb_strSliceForward(prb_String str, int32_t bytes) {
    prb_assert(bytes <= str.len);
    prb_String result = {str.ptr + bytes, str.len - bytes};
    return result;
}

prb_PUBLICDEF const char*
prb_strGetNullTerminated(prb_Arena* arena, prb_String str) {
    const char* result = prb_fmt(arena, "%.*s", prb_LIT(str)).ptr;
    return result;
}

prb_PUBLICDEF prb_String
prb_strTrimSide(prb_String str, prb_StringDirection dir) {
    prb_String result = str;

    bool    found = false;
    int32_t index = 0;
    int32_t byteStart = 0;
    int32_t onePastEnd = str.len;
    int32_t delta = 1;
    if (dir == prb_StringDirection_FromEnd) {
        byteStart = str.len - 1;
        onePastEnd = -1;
        delta = -1;
    }

    for (int32_t byteIndex = byteStart; byteIndex != onePastEnd && !found; byteIndex += delta) {
        char ch = str.ptr[byteIndex];
        if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t' && ch != '\v' && ch != '\f') {
            found = true;
            index = byteIndex;
        }
    }

    if (found) {
        switch (dir) {
            case prb_StringDirection_FromStart: result = prb_strSliceForward(str, index); break;
            case prb_StringDirection_FromEnd: result.len = index + 1; break;
        }
    } else {
        result.len = 0;
    }

    return result;
}

prb_PUBLICDEF prb_String
prb_strTrim(prb_String str) {
    prb_String result = prb_strTrimSide(str, prb_StringDirection_FromStart);
    result = prb_strTrimSide(result, prb_StringDirection_FromEnd);
    return result;
}

prb_PUBLICDEF prb_StringFindResult
prb_strFind(prb_StringFindSpec spec) {
    prb_assert(spec.str.ptr && spec.pattern.ptr && spec.str.len >= 0 && spec.pattern.len >= 0);
    prb_StringFindResult result = {};

    if (spec.pattern.len > 0) {
        prb_StringFindMode mode = spec.mode;
        if (spec.pattern.len == 1 && mode == prb_StringFindMode_Exact) {
            mode = prb_StringFindMode_AnyChar;
        }

        switch (mode) {
            case prb_StringFindMode_Exact: {
                // Raita string matching algorithm
                // https://en.wikipedia.org/wiki/Raita_algorithm

                if (spec.str.len >= spec.pattern.len) {
                    uint8_t* str = (uint8_t*)spec.str.ptr;
                    uint8_t* pat = (uint8_t*)spec.pattern.ptr;
                    int32_t  charOffsets[256];

                    for (int32_t i = 0; i < 256; ++i) {
                        charOffsets[i] = spec.pattern.len;
                    }

                    {
                        int32_t from = 0;
                        int32_t to = spec.pattern.len - 1;
                        int32_t delta = 1;
                        if (spec.direction == prb_StringDirection_FromEnd) {
                            from = spec.pattern.len - 1;
                            to = 0;
                            delta = -1;
                        }

                        int32_t count = 0;
                        for (int32_t i = from; i != to; i += delta) {
                            uint8_t patternChar = pat[i];
                            charOffsets[patternChar] = spec.pattern.len - count++ - 1;
                        }

                        if (spec.direction == prb_StringDirection_FromEnd) {
                            for (int32_t i = 0; i < 256; ++i) {
                                charOffsets[i] *= -1;
                            }
                        }
                    }

                    uint8_t patFirstCh = pat[0];
                    uint8_t patMiddleCh = pat[spec.pattern.len / 2];
                    uint8_t patLastCh = pat[spec.pattern.len - 1];

                    if (spec.direction == prb_StringDirection_FromEnd && spec.str.len < 0) {
                        spec.str.len = prb_strlen(spec.str.ptr);
                    }

                    int32_t off = 0;
                    if (spec.direction == prb_StringDirection_FromEnd) {
                        off = spec.str.len - spec.pattern.len;
                    }

                    for (;;) {
                        bool notEnoughStrLeft = false;
                        switch (spec.direction) {
                            case prb_StringDirection_FromStart: notEnoughStrLeft = off + spec.pattern.len > spec.str.len; break;
                            case prb_StringDirection_FromEnd: notEnoughStrLeft = off < 0; break;
                        }

                        if (notEnoughStrLeft) {
                            break;
                        }

                        uint8_t strFirstChar = str[off];
                        uint8_t strLastCh = str[off + spec.pattern.len - 1];
                        if (patLastCh == strLastCh && patMiddleCh == str[off + spec.pattern.len / 2] && patFirstCh == strFirstChar
                            && prb_memeq(pat + 1, str + off + 1, spec.pattern.len - 2)) {
                            result.found = true;
                            result.matchByteIndex = off;
                            result.matchLen = spec.pattern.len;
                            break;
                        }

                        uint8_t relChar = strLastCh;
                        if (spec.direction == prb_StringDirection_FromEnd) {
                            relChar = strFirstChar;
                        }
                        off += charOffsets[relChar];
                    }
                }
            } break;

            case prb_StringFindMode_AnyChar: {
                for (prb_Utf8CharIterator iter = prb_createUtf8CharIter(spec.str, spec.direction);
                     prb_utf8CharIterNext(&iter) == prb_Success && !result.found;) {
                    if (iter.curIsValid) {
                        for (prb_Utf8CharIterator patIter = prb_createUtf8CharIter(spec.pattern, prb_StringDirection_FromStart);
                             prb_utf8CharIterNext(&patIter) == prb_Success && !result.found;) {
                            if (patIter.curIsValid) {
                                if (iter.curUtf32Char == patIter.curUtf32Char) {
                                    result.found = true;
                                    result.matchByteIndex = iter.curByteOffset;
                                    result.matchLen = iter.curUtf8Bytes;
                                    break;
                                }
                            }
                        }
                    }
                }
            } break;

            case prb_StringFindMode_RegexPosix: {
                prb_TempMemory temp = prb_beginTempMemory(spec.regexPosix.arena);

                regex_t     regexCompiled = {};
                const char* pat = prb_strGetNullTerminated(spec.regexPosix.arena, spec.pattern);
                int         compResult = regcomp(&regexCompiled, pat, REG_EXTENDED);
                prb_assert(compResult == 0);
                regmatch_t  pos = {};
                const char* str = prb_strGetNullTerminated(spec.regexPosix.arena, spec.str);
                int         execResult = regexec(&regexCompiled, str, 1, &pos, 0);
                if (execResult == 0) {
                    result.found = true;
                    result.matchByteIndex = pos.rm_so;
                    result.matchLen = pos.rm_eo - pos.rm_so;

                    // NOTE(khvorov) Match forward and report last result.
                    // Janky, but I don't want to implement sane backwards regex matching myself.
                    if (spec.direction == prb_StringDirection_FromEnd) {
                        while (regexec(&regexCompiled, str + result.matchByteIndex + result.matchLen, 1, &pos, 0) == 0) {
                            result.matchByteIndex += result.matchLen + pos.rm_so;
                            result.matchLen = pos.rm_eo - pos.rm_so;
                        }
                    }
                }

                regfree(&regexCompiled);
                prb_endTempMemory(temp);
            } break;
        }
    }

    return result;
}

prb_PUBLICDEF prb_StrFindIterator
prb_createStrFindIter(prb_StringFindSpec spec) {
    prb_StrFindIterator iter = {.spec = spec, .curResult = (prb_StringFindResult) {}, .curMatchCount = 0};
    return iter;
}

prb_PUBLICDEF prb_Status
prb_strFindIterNext(prb_StrFindIterator* iter) {
    prb_StringFindSpec spec = iter->spec;
    int32_t            strOffset = 0;
    if (iter->curResult.found) {
        switch (spec.direction) {
            case prb_StringDirection_FromStart:
                strOffset = iter->curResult.matchByteIndex + iter->curResult.matchLen;
                spec.str = prb_strSliceForward(spec.str, strOffset);
                break;
            case prb_StringDirection_FromEnd:
                spec.str.len = iter->curResult.matchByteIndex;
        }
    }

    iter->curResult = prb_strFind(spec);
    prb_Status result = prb_Failure;
    if (iter->curResult.found) {
        result = prb_Success;
        if (spec.direction == prb_StringDirection_FromStart) {
            iter->curResult.matchByteIndex += strOffset;
        }
        iter->curMatchCount += 1;
    }

    return result;
}

prb_PUBLICDEF bool
prb_strStartsWith(prb_Arena* arena, prb_String str, prb_String pattern, prb_StringFindMode mode) {
    str.len = prb_min(str.len, pattern.len);
    prb_StringFindSpec spec = {};
    spec.str = str;
    spec.pattern = pattern;
    spec.mode = mode;
    spec.direction = prb_StringDirection_FromStart;
    if (mode == prb_StringFindMode_RegexPosix) {
        spec.regexPosix.arena = arena;
    }
    prb_StringFindResult find = prb_strFind(spec);
    bool                 result = find.found && find.matchByteIndex == 0;
    return result;
}

prb_PUBLICDEF bool
prb_strEndsWith(prb_Arena* arena, prb_String str, prb_String pattern, prb_StringFindMode mode) {
    if (str.len > pattern.len) {
        str = prb_strSliceForward(str, str.len - pattern.len);
    }
    bool result = prb_strStartsWith(arena, str, pattern, mode);
    return result;
}

prb_PUBLICDEF prb_String
prb_strReplace(prb_Arena* arena, prb_StringFindSpec spec, prb_String replacement) {
    prb_String           result = spec.str;
    prb_StringFindResult findResult = prb_strFind(spec);
    if (findResult.found) {
        prb_String strAfterMatch = prb_strSliceForward(spec.str, findResult.matchByteIndex + findResult.matchLen);
        result = prb_fmt(
            arena,
            "%.*s%.*s%.*s",
            findResult.matchByteIndex,
            spec.str.ptr,
            replacement.len,
            replacement.ptr,
            strAfterMatch.len,
            strAfterMatch.ptr
        );
    }
    return result;
}

prb_PUBLICDEF prb_String
prb_stringsJoin(prb_Arena* arena, prb_String* strings, int32_t stringsCount, prb_String sep) {
    prb_GrowingString gstr = prb_beginString(arena);
    for (int32_t strIndex = 0; strIndex < stringsCount; strIndex++) {
        prb_String str = strings[strIndex];
        prb_addStringSegment(&gstr, "%.*s", str.len, str.ptr);
        if (strIndex < stringsCount - 1) {
            prb_addStringSegment(&gstr, "%.*s", sep.len, sep.ptr);
        }
    }
    prb_String result = prb_endString(&gstr);
    return result;
}

prb_PUBLICDEF prb_GrowingString
prb_beginString(prb_Arena* arena) {
    prb_assert(!arena->lockedForString);
    arena->lockedForString = true;
    prb_String        str = {(const char*)prb_arenaFreePtr(arena), 0};
    prb_GrowingString result = {arena, str};
    return result;
}

prb_PUBLICDEF void
prb_addStringSegment(prb_GrowingString* gstr, const char* fmt, ...) {
    prb_assert(gstr->arena->lockedForString);
    va_list args;
    va_start(args, fmt);
    prb_String seg = prb_vfmtCustomBuffer((uint8_t*)prb_arenaFreePtr(gstr->arena), prb_arenaFreeSize(gstr->arena), fmt, args);
    prb_arenaChangeUsed(gstr->arena, seg.len);
    gstr->string.len += seg.len;
    va_end(args);
}

prb_PUBLICDEF prb_String
prb_endString(prb_GrowingString* gstr) {
    prb_assert(gstr->arena->lockedForString);
    gstr->arena->lockedForString = false;
    prb_arenaAllocAndZero(gstr->arena, 1, 1);  // NOTE(khvorov) Null terminator
    prb_String result = gstr->string;
    *gstr = (prb_GrowingString) {};
    return result;
}

prb_PUBLICDEF prb_String
prb_vfmtCustomBuffer(void* buf, int32_t bufSize, const char* fmt, va_list args) {
    int32_t    len = stbsp_vsnprintf((char*)buf, bufSize, fmt, args);
    prb_String result = {(const char*)buf, len};
    return result;
}

prb_PUBLICDEF prb_String
prb_fmt(prb_Arena* arena, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    prb_String result = prb_vfmtCustomBuffer(prb_arenaFreePtr(arena), prb_arenaFreeSize(arena), fmt, args);
    prb_arenaChangeUsed(arena, result.len);
    prb_arenaAllocAndZero(arena, 1, 1);  // NOTE(khvorov) Null terminator
    va_end(args);
    return result;
}

prb_PUBLICDEF void
prb_writeToStdout(prb_String msg) {
#if prb_PLATFORM_WINDOWS

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(out, msg.ptr, msg.len, 0, 0);

#elif prb_PLATFORM_LINUX

    ssize_t writeResult = write(STDOUT_FILENO, msg.ptr, msg.len);
    prb_assert(writeResult == msg.len);

#else
#error unimplemented
#endif
}

prb_PUBLICDEF void
prb_writelnToStdout(prb_String str) {
    prb_writeToStdout(str);
    prb_writeToStdout(prb_STR("\n"));
}

prb_PUBLICDEF void
prb_setPrintColor(prb_ColorID color) {
    prb_String str = prb_STR("BAD COLOR");
    switch (color) {
        case prb_ColorID_Black: str = prb_STR("\x1b[30m"); break;
        case prb_ColorID_Red: str = prb_STR("\x1b[31m"); break;
        case prb_ColorID_Green: str = prb_STR("\x1b[32m"); break;
        case prb_ColorID_Yellow: str = prb_STR("\x1b[33m"); break;
        case prb_ColorID_Blue: str = prb_STR("\x1b[34m"); break;
        case prb_ColorID_Magenta: str = prb_STR("\x1b[35m"); break;
        case prb_ColorID_Cyan: str = prb_STR("\x1b[36m"); break;
        case prb_ColorID_White: str = prb_STR("\x1b[37m"); break;
    }
    prb_writeToStdout(str);
}

prb_PUBLICDEF void
prb_resetPrintColor() {
    prb_writeToStdout(prb_STR("\x1b[0m"));
}

prb_PUBLICDEF prb_Utf8CharIterator
prb_createUtf8CharIter(prb_String str, prb_StringDirection direction) {
    prb_assert(str.ptr && str.len >= 0);
    int32_t curByteOffset = 0;
    if (direction == prb_StringDirection_FromEnd) {
        curByteOffset = str.len - 1;
    }

    prb_Utf8CharIterator iter = {
        .str = str,
        .direction = direction,
        .curCharCount = 0,
        .curByteOffset = curByteOffset,
        .curUtf32Char = 0,
        .curUtf8Bytes = 0,
        .curIsValid = false,
    };

    return iter;
}

prb_PUBLICDEF prb_Status
prb_utf8CharIterNext(prb_Utf8CharIterator* iter) {
    prb_Status result = prb_Failure;

    if (iter->curIsValid) {
        switch (iter->direction) {
            case prb_StringDirection_FromStart: iter->curByteOffset += iter->curUtf8Bytes; break;
            case prb_StringDirection_FromEnd: iter->curByteOffset -= 1; break;
        }
    }
    iter->curUtf8Bytes = 0;
    iter->curUtf32Char = 0;
    iter->curIsValid = false;

    bool more = iter->curByteOffset < iter->str.len;
    if (iter->direction == prb_StringDirection_FromEnd) {
        more = iter->curByteOffset >= 0;
    }

    if (more) {
        bool     chValid = false;
        uint32_t ch = 0;
        int32_t  chBytes = 0;

        result = prb_Success;

        uint8_t firstByte = iter->str.ptr[iter->curByteOffset];
        int32_t leading1s = prb_countLeading1sU8(firstByte);

        bool firstByteValid = false;
        switch (iter->direction) {
            case prb_StringDirection_FromStart:
                firstByteValid = (leading1s == 0 || leading1s == 2 || leading1s == 3 || leading1s == 4);
                break;
            case prb_StringDirection_FromEnd:
                firstByteValid = (leading1s == 0 || leading1s == 1);
                break;
        }

        if (firstByteValid) {
            if (leading1s == 0) {
                chValid = true;
                ch = firstByte;
                chBytes = 1;
            } else {
                uint8_t firstByteMask[] = {
                    0b01111111,
                    0b00011111,
                    0b00001111,
                    0b00000111,
                };

                switch (iter->direction) {
                    case prb_StringDirection_FromStart: {
                        chValid = true;
                        chBytes = leading1s;
                        prb_assert(chBytes == 2 || chBytes == 3 || chBytes == 4);
                        ch = firstByte & firstByteMask[chBytes - 1];
                        for (int32_t byteIndex = 1; byteIndex < chBytes; byteIndex++) {
                            uint8_t byte = iter->str.ptr[iter->curByteOffset + byteIndex];
                            if (prb_countLeading1sU8(byte) == 1) {
                                ch = (ch << 6) | (byte & 0b00111111);
                            } else {
                                iter->curByteOffset += byteIndex;
                                chValid = false;
                                break;
                            }
                        }
                    } break;

                    case prb_StringDirection_FromEnd: {
                        prb_assert(leading1s == 1);
                        ch = firstByte & 0b00111111;
                        int32_t maxExtraBytes = prb_min(3, iter->curByteOffset);
                        for (int32_t byteIndex = 0; byteIndex < maxExtraBytes; byteIndex++) {
                            uint8_t byte = iter->str.ptr[--iter->curByteOffset];
                            int32_t byteLeading1s = prb_countLeading1sU8(byte);
                            int32_t chBytesTaken = 6 * (byteIndex + 1);
                            if (byteLeading1s == 1) {
                                ch = ((byte & 0b00111111) << chBytesTaken) | ch;
                            } else if (byteLeading1s == byteIndex + 2) {
                                chValid = true;
                                chBytes = byteLeading1s;
                                ch = ((byte & firstByteMask[byteLeading1s - 1]) << chBytesTaken) | ch;
                                break;
                            } else {
                                break;
                            }
                        }
                    } break;
                }
            }
        } else {
            iter->curByteOffset += iter->direction == prb_StringDirection_FromStart ? 1 : -1;
        }

        if (chValid) {
            iter->curIsValid = true;
            iter->curUtf32Char = ch;
            iter->curUtf8Bytes = chBytes;
            iter->curCharCount += 1;
        }
    }

    return result;
}

prb_PUBLICDEF prb_LineIterator
prb_createLineIter(prb_String str) {
    prb_assert(str.ptr && str.len >= 0);
    prb_LineIterator iter = {
        .ogstr = str,
        .curLineCount = 0,
        .curByteOffset = 0,
        .curLine = (prb_String) {},
        .curLineEndLen = 0,
    };
    return iter;
}

prb_PUBLICDEF prb_Status
prb_lineIterNext(prb_LineIterator* iter) {
    prb_Status result = prb_Failure;

    iter->curByteOffset += iter->curLine.len + iter->curLineEndLen;
    iter->curLine = (prb_String) {};
    iter->curLineEndLen = 0;

    if (iter->curByteOffset < iter->ogstr.len) {
        iter->curLine = prb_strSliceForward(iter->ogstr, iter->curByteOffset);
        prb_StringFindSpec spec = {};
        spec.str = iter->curLine;
        spec.pattern = prb_STR("\r\n");
        spec.mode = prb_StringFindMode_AnyChar;
        spec.direction = prb_StringDirection_FromStart;
        prb_StringFindResult lineEndResult = prb_strFind(spec);
        if (lineEndResult.found) {
            iter->curLine.len = lineEndResult.matchByteIndex;
            iter->curLineEndLen = 1;
            if (iter->curLine.ptr[iter->curLine.len] == '\r'
                && iter->curByteOffset + iter->curLine.len + 1 < iter->ogstr.len
                && iter->curLine.ptr[iter->curLine.len + 1] == '\n') {
                iter->curLineEndLen += 1;
            }
        }
        iter->curLineCount += 1;
        result = prb_Success;
    }

    return result;
}

prb_PUBLICDEF prb_WordIterator
prb_createWordIter(prb_String str) {
    prb_WordIterator iter = {};
    iter.ogstr = str;
    return iter;
}

prb_PUBLICDEF prb_Status
prb_wordIterNext(prb_WordIterator* iter) {
    prb_unused(iter);
    prb_Status result = prb_Failure;
    // TODO(khvorov) Implement
    prb_assert(!"unimplemented");
    return result;
}

//
// SECTION Processes (implementation)
//

#if prb_PLATFORM_LINUX

static prb_Bytes
prb_linux_readFromProcSelf(prb_Arena* arena) {
    prb_linux_OpenResult handle = prb_linux_open(arena, prb_STR("/proc/self/cmdline"), O_RDONLY, 0);
    prb_assert(handle.success);
    prb_GrowingString gstr = prb_beginString(arena);
    gstr.string.len = read(handle.handle, (void*)gstr.string.ptr, prb_arenaFreeSize(arena));
    prb_assert(gstr.string.len > 0);
    prb_arenaChangeUsed(arena, gstr.string.len);
    prb_String str = prb_endString(&gstr);
    prb_Bytes  result = {(uint8_t*)str.ptr, str.len};
    return result;
}

#endif

prb_PUBLICDEF void
prb_terminate(int32_t code) {
#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    exit(code);

#else
#error unimplemented
#endif
}

prb_PUBLICDEF prb_String
prb_getCmdline(prb_Arena* arena) {
    prb_String result = {};

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    prb_Bytes procSelfContent = prb_linux_readFromProcSelf(arena);
    for (int32_t byteIndex = 0; byteIndex < procSelfContent.len; byteIndex++) {
        if (procSelfContent.data[byteIndex] == '\0') {
            procSelfContent.data[byteIndex] = ' ';
        }
    }

    result = (prb_String) {(const char*)procSelfContent.data, procSelfContent.len};

#elif
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF prb_String*
prb_getCmdArgs(prb_Arena* arena) {
    prb_String* result = 0;

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    prb_Bytes  procSelfContent = prb_linux_readFromProcSelf(arena);
    prb_String procSelfContentLeft = {(const char*)procSelfContent.data, procSelfContent.len};
    for (int32_t byteIndex = 0; byteIndex < procSelfContent.len; byteIndex++) {
        if (procSelfContent.data[byteIndex] == '\0') {
            int32_t    processed = procSelfContent.len - procSelfContentLeft.len;
            prb_String arg = {procSelfContentLeft.ptr, byteIndex - processed};
            arrput(result, arg);
            procSelfContentLeft = prb_strSliceForward(procSelfContentLeft, arg.len + 1);
        }
    }

#elif
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF const char**
prb_getArgArrayFromString(prb_Arena* arena, prb_String string) {
    const char**       args = 0;
    prb_StringFindSpec spec = {};
    spec.str = string;
    spec.pattern = prb_STR(" ");
    spec.mode = prb_StringFindMode_AnyChar;
    spec.direction = prb_StringDirection_FromStart;

    int32_t             prevSpaceIndex = -1;
    prb_StrFindIterator iter = prb_createStrFindIter(spec);
    for (;;) {
        int32_t spaceIndex = 0;
        if (prb_strFindIterNext(&iter) == prb_Success) {
            prb_assert(iter.curResult.found);
            spaceIndex = iter.curResult.matchByteIndex;
        } else {
            spaceIndex = string.len;
        }
        int32_t arglen = spaceIndex - prevSpaceIndex - 1;
        if (arglen > 0) {
            prb_String  arg = {string.ptr + prevSpaceIndex + 1, arglen};
            const char* argNull = prb_fmt(arena, "%.*s", prb_LIT(arg)).ptr;
            arrput(args, argNull);
        }
        prevSpaceIndex = spaceIndex;
        if (spaceIndex == string.len) {
            break;
        }
    }

    // NOTE(khvorov) Arg array needs a null at the end
    arrput(args, 0);

    return args;
}

prb_PUBLICDEF prb_ProcessHandle
prb_execCmd(prb_Arena* arena, prb_String cmd, prb_ProcessFlags flags, prb_String redirectFilepath) {
    prb_ProcessHandle result = {};
    prb_TempMemory    temp = prb_beginTempMemory(arena);
    const char*       redirectFilepathNull = 0;

    if ((flags & prb_ProcessFlag_RedirectStdout) || (flags & prb_ProcessFlag_RedirectStderr)) {
        prb_assert(redirectFilepath.ptr && redirectFilepath.len > 0);
        redirectFilepathNull = prb_strGetNullTerminated(arena, redirectFilepath);
    } else {
        prb_assert(redirectFilepath.ptr == 0);
    }

#if prb_PLATFORM_WINDOWS

    STARTUPINFOA        startupInfo = {.cb = sizeof(STARTUPINFOA)};
    PROCESS_INFORMATION processInfo;
    if (CreateProcessA(0, cmd.ptr, 0, 0, 0, 0, 0, 0, &startupInfo, &processInfo)) {
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        DWORD exitCode;
        if (GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
            if (exitCode == 0) {
                cmdStatus = prb_Success;
            }
        }
    }

#error unimplemented

#elif prb_PLATFORM_LINUX

    bool                        fileActionsSucceeded = true;
    posix_spawn_file_actions_t* fileActionsPtr = 0;
    posix_spawn_file_actions_t  fileActions = {};
    if ((flags & prb_ProcessFlag_RedirectStdout) || (flags & prb_ProcessFlag_RedirectStderr)) {
        fileActionsPtr = &fileActions;
        int initResult = posix_spawn_file_actions_init(&fileActions);
        fileActionsSucceeded = initResult == 0;
        if (fileActionsSucceeded) {
            if (flags & prb_ProcessFlag_RedirectStdout) {
                int stdoutRedirectResult = posix_spawn_file_actions_addopen(
                    &fileActions,
                    STDOUT_FILENO,
                    redirectFilepathNull,
                    O_WRONLY | O_CREAT | O_TRUNC,
                    S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR
                );
                fileActionsSucceeded = stdoutRedirectResult == 0;
                if (fileActionsSucceeded && (flags & prb_ProcessFlag_RedirectStderr)) {
                    int dupResult = posix_spawn_file_actions_adddup2(&fileActions, STDOUT_FILENO, STDERR_FILENO);
                    fileActionsSucceeded = dupResult == 0;
                }
            } else if (flags & prb_ProcessFlag_RedirectStderr) {
                int stderrRedirectResult = posix_spawn_file_actions_addopen(
                    &fileActions,
                    STDERR_FILENO,
                    redirectFilepathNull,
                    O_WRONLY | O_CREAT | O_TRUNC,
                    S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR
                );
                fileActionsSucceeded = stderrRedirectResult == 0;
            }
        }
    }

    if (fileActionsSucceeded) {
        const char** args = prb_getArgArrayFromString(arena, cmd);
        int          spawnResult = posix_spawnp(&result.pid, args[0], fileActionsPtr, 0, (char**)args, __environ);
        if (spawnResult == 0) {
            result.status = prb_ProcessStatus_Launched;
            if (!(flags & prb_ProcessFlag_DontWait)) {
                int   status = 0;
                pid_t waitResult = waitpid(result.pid, &status, 0);
                result.status = prb_ProcessStatus_CompletedFailed;
                if (waitResult == result.pid && status == 0) {
                    result.status = prb_ProcessStatus_CompletedSuccess;
                }
            }
        }
    }

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Status
prb_waitForProcesses(prb_ProcessHandle* handles, int32_t handleCount) {
    prb_Status result = prb_Success;

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    for (int32_t handleIndex = 0; handleIndex < handleCount; handleIndex++) {
        prb_ProcessHandle* handle = handles + handleIndex;
        if (handle->status == prb_ProcessStatus_Launched) {
            int32_t status = 0;
            pid_t   waitResult = waitpid(handle->pid, &status, 0);
            handle->status = prb_ProcessStatus_CompletedFailed;
            if (waitResult == handle->pid && status == 0) {
                handle->status = prb_ProcessStatus_CompletedSuccess;
            } else {
                result = prb_Failure;
            }
        }
    }

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF void
prb_sleep(float ms) {
#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    float           secf = ms * 0.001f;
    long int        sec = (long int)(secf);
    long int        nsec = (long int)((secf - (float)sec) * 1000.0f * 1000.0f * 1000.0f);
    struct timespec ts = {.tv_sec = sec, .tv_nsec = nsec};
    nanosleep(&ts, 0);

#else
#error unimplemented
#endif
}

//
// SECTION Timing (implementation)
//

prb_PUBLICDEF prb_TimeStart
prb_timeStart(void) {
    prb_TimeStart result = {};

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    struct timespec tp = {};
    if (clock_gettime(CLOCK_MONOTONIC, &tp) == 0) {
        prb_assert(tp.tv_nsec >= 0 && tp.tv_sec >= 0);
        result.nsec = (uint64_t)tp.tv_nsec + (uint64_t)tp.tv_sec * 1000 * 1000 * 1000;
        result.valid = true;
    }

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF float
prb_getMsFrom(prb_TimeStart timeStart) {
    prb_TimeStart now = prb_timeStart();
    float         result = 0.0f;
    if (now.valid && timeStart.valid) {
        uint64_t nsec = now.nsec - timeStart.nsec;
        result = (float)nsec / 1000.0f / 1000.0f;
    }
    return result;
}

//
// SECTION Multithreading (implementation)
//

#if prb_PLATFORM_LINUX

static void*
prb_linux_pthreadProc(void* data) {
    prb_Job* job = (prb_Job*)data;
    prb_assert(job->status == prb_ProcessStatus_NotLaunched);
    job->status = prb_ProcessStatus_Launched;
    job->proc(&job->arena, job->data);
    return 0;
}

#endif

prb_PUBLICDEF prb_Job
prb_createJob(prb_JobProc proc, void* data, prb_Arena* arena, int32_t arenaBytes) {
    prb_Job job = {};
    job.proc = proc;
    job.data = data;
    job.arena = prb_createArenaFromArena(arena, arenaBytes);
    return job;
}

prb_PUBLICDEF prb_Status
prb_execJobs(prb_Job* jobs, int32_t jobsCount) {
    prb_Status result = prb_Success;

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    for (int32_t jobIndex = 0; jobIndex < jobsCount && result == prb_Success; jobIndex++) {
        prb_Job* job = jobs + jobIndex;
        if (job->status == prb_ProcessStatus_NotLaunched) {
            if (pthread_create(&job->threadid, 0, prb_linux_pthreadProc, job) != 0) {
                result = prb_Failure;
            }
        }
    }

    for (int32_t jobIndex = 0; jobIndex < jobsCount; jobIndex++) {
        prb_Job* job = jobs + jobIndex;
        if (job->status == prb_ProcessStatus_Launched) {
            if (pthread_join(job->threadid, 0) == 0) {
                job->status = prb_ProcessStatus_CompletedSuccess;
            } else {
                result = prb_Failure;
            }
        }
    }

#else
#error unimplemented
#endif

    return result;
}

//
// SECTION Hashing (implementation)
//

// clang-format off
static uint32_t prb_globalCrc32Table[] = {
    0xd202ef8d, 0xa505df1b, 0x3c0c8ea1, 0x4b0bbe37, 0xd56f2b94, 0xa2681b02, 0x3b614ab8, 0x4c667a2e, 0xdcd967bf, 0xabde5729, 0x32d70693, 
    0x45d03605, 0xdbb4a3a6, 0xacb39330, 0x35bac28a, 0x42bdf21c, 0xcfb5ffe9, 0xb8b2cf7f, 0x21bb9ec5, 0x56bcae53, 0xc8d83bf0, 
    0xbfdf0b66, 0x26d65adc, 0x51d16a4a, 0xc16e77db, 0xb669474d, 0x2f6016f7, 0x58672661, 0xc603b3c2, 0xb1048354, 0x280dd2ee, 
    0x5f0ae278, 0xe96ccf45, 0x9e6bffd3, 0x762ae69, 0x70659eff, 0xee010b5c, 0x99063bca, 0xf6a70, 0x77085ae6, 0xe7b74777, 
    0x90b077e1, 0x9b9265b, 0x7ebe16cd, 0xe0da836e, 0x97ddb3f8, 0xed4e242, 0x79d3d2d4, 0xf4dbdf21, 0x83dcefb7, 0x1ad5be0d, 
    0x6dd28e9b, 0xf3b61b38, 0x84b12bae, 0x1db87a14, 0x6abf4a82, 0xfa005713, 0x8d076785, 0x140e363f, 0x630906a9, 0xfd6d930a, 
    0x8a6aa39c, 0x1363f226, 0x6464c2b0, 0xa4deae1d, 0xd3d99e8b, 0x4ad0cf31, 0x3dd7ffa7, 0xa3b36a04, 0xd4b45a92, 0x4dbd0b28, 
    0x3aba3bbe, 0xaa05262f, 0xdd0216b9, 0x440b4703, 0x330c7795, 0xad68e236, 0xda6fd2a0, 0x4366831a, 0x3461b38c, 0xb969be79, 
    0xce6e8eef, 0x5767df55, 0x2060efc3, 0xbe047a60, 0xc9034af6, 0x500a1b4c, 0x270d2bda, 0xb7b2364b, 0xc0b506dd, 0x59bc5767, 
    0x2ebb67f1, 0xb0dff252, 0xc7d8c2c4, 0x5ed1937e, 0x29d6a3e8, 0x9fb08ed5, 0xe8b7be43, 0x71beeff9, 0x6b9df6f, 0x98dd4acc, 
    0xefda7a5a, 0x76d32be0, 0x1d41b76, 0x916b06e7, 0xe66c3671, 0x7f6567cb, 0x862575d, 0x9606c2fe, 0xe101f268, 0x7808a3d2, 
    0xf0f9344, 0x82079eb1, 0xf500ae27, 0x6c09ff9d, 0x1b0ecf0b, 0x856a5aa8, 0xf26d6a3e, 0x6b643b84, 0x1c630b12, 0x8cdc1683, 
    0xfbdb2615, 0x62d277af, 0x15d54739, 0x8bb1d29a, 0xfcb6e20c, 0x65bfb3b6, 0x12b88320, 0x3fba6cad, 0x48bd5c3b, 0xd1b40d81, 
    0xa6b33d17, 0x38d7a8b4, 0x4fd09822, 0xd6d9c998, 0xa1def90e, 0x3161e49f, 0x4666d409, 0xdf6f85b3, 0xa868b525, 0x360c2086, 
    0x410b1010, 0xd80241aa, 0xaf05713c, 0x220d7cc9, 0x550a4c5f, 0xcc031de5, 0xbb042d73, 0x2560b8d0, 0x52678846, 0xcb6ed9fc, 
    0xbc69e96a, 0x2cd6f4fb, 0x5bd1c46d, 0xc2d895d7, 0xb5dfa541, 0x2bbb30e2, 0x5cbc0074, 0xc5b551ce, 0xb2b26158, 0x4d44c65, 
    0x73d37cf3, 0xeada2d49, 0x9ddd1ddf, 0x3b9887c, 0x74beb8ea, 0xedb7e950, 0x9ab0d9c6, 0xa0fc457, 0x7d08f4c1, 0xe401a57b, 
    0x930695ed, 0xd62004e, 0x7a6530d8, 0xe36c6162, 0x946b51f4, 0x19635c01, 0x6e646c97, 0xf76d3d2d, 0x806a0dbb, 0x1e0e9818, 
    0x6909a88e, 0xf000f934, 0x8707c9a2, 0x17b8d433, 0x60bfe4a5, 0xf9b6b51f, 0x8eb18589, 0x10d5102a, 0x67d220bc, 0xfedb7106, 
    0x89dc4190, 0x49662d3d, 0x3e611dab, 0xa7684c11, 0xd06f7c87, 0x4e0be924, 0x390cd9b2, 0xa0058808, 0xd702b89e, 0x47bda50f, 
    0x30ba9599, 0xa9b3c423, 0xdeb4f4b5, 0x40d06116, 0x37d75180, 0xaede003a, 0xd9d930ac, 0x54d13d59, 0x23d60dcf, 0xbadf5c75, 
    0xcdd86ce3, 0x53bcf940, 0x24bbc9d6, 0xbdb2986c, 0xcab5a8fa, 0x5a0ab56b, 0x2d0d85fd, 0xb404d447, 0xc303e4d1, 0x5d677172, 
    0x2a6041e4, 0xb369105e, 0xc46e20c8, 0x72080df5, 0x50f3d63, 0x9c066cd9, 0xeb015c4f, 0x7565c9ec, 0x262f97a, 0x9b6ba8c0, 
    0xec6c9856, 0x7cd385c7, 0xbd4b551, 0x92dde4eb, 0xe5dad47d, 0x7bbe41de, 0xcb97148, 0x95b020f2, 0xe2b71064, 0x6fbf1d91, 
    0x18b82d07, 0x81b17cbd, 0xf6b64c2b, 0x68d2d988, 0x1fd5e91e, 0x86dcb8a4, 0xf1db8832, 0x616495a3, 0x1663a535, 0x8f6af48f, 
    0xf86dc419, 0x660951ba, 0x110e612c, 0x88073096, 0xff000000
};

static uint64_t prb_globalCrc32Wtable[] = {
    0x6522df69, 0xa988dff7, 0x2707d814, 0xebadd88a, 0xe168d193, 0x2dc2d10d, 0xa34dd6ee, 0x6fe7d670, 0xb6c7c4dc, 0x7a6dc442, 0xf4e2c3a1, 
    0x3848c33f, 0x328dca26, 0xfe27cab8, 0x70a8cd5b, 0xbc02cdc5, 0x1999ee42, 0xd533eedc, 0x5bbce93f, 0x9716e9a1, 0x9dd3e0b8, 
    0x5179e026, 0xdff6e7c5, 0x135ce75b, 0xca7cf5f7, 0x6d6f569, 0x8859f28a, 0x44f3f214, 0x4e36fb0d, 0x829cfb93, 0xc13fc70, 
    0xc0b9fcee, 0x9c54bd3f, 0x50febda1, 0xde71ba42, 0x12dbbadc, 0x181eb3c5, 0xd4b4b35b, 0x5a3bb4b8, 0x9691b426, 0x4fb1a68a, 
    0x831ba614, 0xd94a1f7, 0xc13ea169, 0xcbfba870, 0x751a8ee, 0x89deaf0d, 0x4574af93, 0xe0ef8c14, 0x2c458c8a, 0xa2ca8b69, 
    0x6e608bf7, 0x64a582ee, 0xa80f8270, 0x26808593, 0xea2a850d, 0x330a97a1, 0xffa0973f, 0x712f90dc, 0xbd859042, 0xb740995b, 
    0x7bea99c5, 0xf5659e26, 0x39cf9eb8, 0x4cbf1d84, 0x80151d1a, 0xe9a1af9, 0xc2301a67, 0xc8f5137e, 0x45f13e0, 0x8ad01403, 
    0x467a149d, 0x9f5a0631, 0x53f006af, 0xdd7f014c, 0x11d501d2, 0x1b1008cb, 0xd7ba0855, 0x59350fb6, 0x959f0f28, 0x30042caf, 
    0xfcae2c31, 0x72212bd2, 0xbe8b2b4c, 0xb44e2255, 0x78e422cb, 0xf66b2528, 0x3ac125b6, 0xe3e1371a, 0x2f4b3784, 0xa1c43067, 
    0x6d6e30f9, 0x67ab39e0, 0xab01397e, 0x258e3e9d, 0xe9243e03, 0xb5c97fd2, 0x79637f4c, 0xf7ec78af, 0x3b467831, 0x31837128, 
    0xfd2971b6, 0x73a67655, 0xbf0c76cb, 0x662c6467, 0xaa8664f9, 0x2409631a, 0xe8a36384, 0xe2666a9d, 0x2ecc6a03, 0xa0436de0, 
    0x6ce96d7e, 0xc9724ef9, 0x5d84e67, 0x8b574984, 0x47fd491a, 0x4d384003, 0x8192409d, 0xf1d477e, 0xc3b747e0, 0x1a97554c, 
    0xd63d55d2, 0x58b25231, 0x941852af, 0x9edd5bb6, 0x52775b28, 0xdcf85ccb, 0x10525c55, 0x36195ab3, 0xfab35a2d, 0x743c5dce, 
    0xb8965d50, 0xb2535449, 0x7ef954d7, 0xf0765334, 0x3cdc53aa, 0xe5fc4106, 0x29564198, 0xa7d9467b, 0x6b7346e5, 0x61b64ffc, 
    0xad1c4f62, 0x23934881, 0xef39481f, 0x4aa26b98, 0x86086b06, 0x8876ce5, 0xc42d6c7b, 0xcee86562, 0x24265fc, 0x8ccd621f, 
    0x40676281, 0x9947702d, 0x55ed70b3, 0xdb627750, 0x17c877ce, 0x1d0d7ed7, 0xd1a77e49, 0x5f2879aa, 0x93827934, 0xcf6f38e5, 
    0x3c5387b, 0x8d4a3f98, 0x41e03f06, 0x4b25361f, 0x878f3681, 0x9003162, 0xc5aa31fc, 0x1c8a2350, 0xd02023ce, 0x5eaf242d, 
    0x920524b3, 0x98c02daa, 0x546a2d34, 0xdae52ad7, 0x164f2a49, 0xb3d409ce, 0x7f7e0950, 0xf1f10eb3, 0x3d5b0e2d, 0x379e0734, 
    0xfb3407aa, 0x75bb0049, 0xb91100d7, 0x6031127b, 0xac9b12e5, 0x22141506, 0xeebe1598, 0xe47b1c81, 0x28d11c1f, 0xa65e1bfc, 
    0x6af41b62, 0x1f84985e, 0xd32e98c0, 0x5da19f23, 0x910b9fbd, 0x9bce96a4, 0x5764963a, 0xd9eb91d9, 0x15419147, 0xcc6183eb, 
    0xcb8375, 0x8e448496, 0x42ee8408, 0x482b8d11, 0x84818d8f, 0xa0e8a6c, 0xc6a48af2, 0x633fa975, 0xaf95a9eb, 0x211aae08, 
    0xedb0ae96, 0xe775a78f, 0x2bdfa711, 0xa550a0f2, 0x69faa06c, 0xb0dab2c0, 0x7c70b25e, 0xf2ffb5bd, 0x3e55b523, 0x3490bc3a, 
    0xf83abca4, 0x76b5bb47, 0xba1fbbd9, 0xe6f2fa08, 0x2a58fa96, 0xa4d7fd75, 0x687dfdeb, 0x62b8f4f2, 0xae12f46c, 0x209df38f, 
    0xec37f311, 0x3517e1bd, 0xf9bde123, 0x7732e6c0, 0xbb98e65e, 0xb15def47, 0x7df7efd9, 0xf378e83a, 0x3fd2e8a4, 0x9a49cb23, 
    0x56e3cbbd, 0xd86ccc5e, 0x14c6ccc0, 0x1e03c5d9, 0xd2a9c547, 0x5c26c2a4, 0x908cc23a, 0x49acd096, 0x8506d008, 0xb89d7eb, 
    0xc723d775, 0xcde6de6c, 0x14cdef2, 0x8fc3d911, 0x4369d98f, 0x0, 0xa6770bb4, 0x979f1129, 0x31e81a9d, 0xf44f2413, 
    0x52382fa7, 0x63d0353a, 0xc5a73e8e, 0x33ef4e67, 0x959845d3, 0xa4705f4e, 0x20754fa, 0xc7a06a74, 0x61d761c0, 0x503f7b5d, 
    0xf64870e9, 0x67de9cce, 0xc1a9977a, 0xf0418de7, 0x56368653, 0x9391b8dd, 0x35e6b369, 0x40ea9f4, 0xa279a240, 0x5431d2a9, 
    0xf246d91d, 0xc3aec380, 0x65d9c834, 0xa07ef6ba, 0x609fd0e, 0x37e1e793, 0x9196ec27, 0xcfbd399c, 0x69ca3228, 0x582228b5, 
    0xfe552301, 0x3bf21d8f, 0x9d85163b, 0xac6d0ca6, 0xa1a0712, 0xfc5277fb, 0x5a257c4f, 0x6bcd66d2, 0xcdba6d66, 0x81d53e8, 
    0xae6a585c, 0x9f8242c1, 0x39f54975, 0xa863a552, 0xe14aee6, 0x3ffcb47b, 0x998bbfcf, 0x5c2c8141, 0xfa5b8af5, 0xcbb39068, 
    0x6dc49bdc, 0x9b8ceb35, 0x3dfbe081, 0xc13fa1c, 0xaa64f1a8, 0x6fc3cf26, 0xc9b4c492, 0xf85cde0f, 0x5e2bd5bb, 0x440b7579, 
    0xe27c7ecd, 0xd3946450, 0x75e36fe4, 0xb044516a, 0x16335ade, 0x27db4043, 0x81ac4bf7, 0x77e43b1e, 0xd19330aa, 0xe07b2a37, 
    0x460c2183, 0x83ab1f0d, 0x25dc14b9, 0x14340e24, 0xb2430590, 0x23d5e9b7, 0x85a2e203, 0xb44af89e, 0x123df32a, 0xd79acda4, 
    0x71edc610, 0x4005dc8d, 0xe672d739, 0x103aa7d0, 0xb64dac64, 0x87a5b6f9, 0x21d2bd4d, 0xe47583c3, 0x42028877, 0x73ea92ea, 
    0xd59d995e, 0x8bb64ce5, 0x2dc14751, 0x1c295dcc, 0xba5e5678, 0x7ff968f6, 0xd98e6342, 0xe86679df, 0x4e11726b, 0xb8590282, 
    0x1e2e0936, 0x2fc613ab, 0x89b1181f, 0x4c162691, 0xea612d25, 0xdb8937b8, 0x7dfe3c0c, 0xec68d02b, 0x4a1fdb9f, 0x7bf7c102, 
    0xdd80cab6, 0x1827f438, 0xbe50ff8c, 0x8fb8e511, 0x29cfeea5, 0xdf879e4c, 0x79f095f8, 0x48188f65, 0xee6f84d1, 0x2bc8ba5f, 
    0x8dbfb1eb, 0xbc57ab76, 0x1a20a0c2, 0x8816eaf2, 0x2e61e146, 0x1f89fbdb, 0xb9fef06f, 0x7c59cee1, 0xda2ec555, 0xebc6dfc8, 
    0x4db1d47c, 0xbbf9a495, 0x1d8eaf21, 0x2c66b5bc, 0x8a11be08, 0x4fb68086, 0xe9c18b32, 0xd82991af, 0x7e5e9a1b, 0xefc8763c, 
    0x49bf7d88, 0x78576715, 0xde206ca1, 0x1b87522f, 0xbdf0599b, 0x8c184306, 0x2a6f48b2, 0xdc27385b, 0x7a5033ef, 0x4bb82972, 
    0xedcf22c6, 0x28681c48, 0x8e1f17fc, 0xbff70d61, 0x198006d5, 0x47abd36e, 0xe1dcd8da, 0xd034c247, 0x7643c9f3, 0xb3e4f77d, 
    0x1593fcc9, 0x247be654, 0x820cede0, 0x74449d09, 0xd23396bd, 0xe3db8c20, 0x45ac8794, 0x800bb91a, 0x267cb2ae, 0x1794a833, 
    0xb1e3a387, 0x20754fa0, 0x86024414, 0xb7ea5e89, 0x119d553d, 0xd43a6bb3, 0x724d6007, 0x43a57a9a, 0xe5d2712e, 0x139a01c7, 
    0xb5ed0a73, 0x840510ee, 0x22721b5a, 0xe7d525d4, 0x41a22e60, 0x704a34fd, 0xd63d3f49, 0xcc1d9f8b, 0x6a6a943f, 0x5b828ea2, 
    0xfdf58516, 0x3852bb98, 0x9e25b02c, 0xafcdaab1, 0x9baa105, 0xfff2d1ec, 0x5985da58, 0x686dc0c5, 0xce1acb71, 0xbbdf5ff, 
    0xadcafe4b, 0x9c22e4d6, 0x3a55ef62, 0xabc30345, 0xdb408f1, 0x3c5c126c, 0x9a2b19d8, 0x5f8c2756, 0xf9fb2ce2, 0xc813367f, 
    0x6e643dcb, 0x982c4d22, 0x3e5b4696, 0xfb35c0b, 0xa9c457bf, 0x6c636931, 0xca146285, 0xfbfc7818, 0x5d8b73ac, 0x3a0a617, 
    0xa5d7ada3, 0x943fb73e, 0x3248bc8a, 0xf7ef8204, 0x519889b0, 0x6070932d, 0xc6079899, 0x304fe870, 0x9638e3c4, 0xa7d0f959, 
    0x1a7f2ed, 0xc400cc63, 0x6277c7d7, 0x539fdd4a, 0xf5e8d6fe, 0x647e3ad9, 0xc209316d, 0xf3e12bf0, 0x55962044, 0x90311eca, 
    0x3646157e, 0x7ae0fe3, 0xa1d90457, 0x579174be, 0xf1e67f0a, 0xc00e6597, 0x66796e23, 0xa3de50ad, 0x5a95b19, 0x34414184, 
    0x92364a30, 0x0, 0xcb5cd3a5, 0x4dc8a10b, 0x869472ae, 0x9b914216, 0x50cd91b3, 0xd659e31d, 0x1d0530b8, 0xec53826d, 
    0x270f51c8, 0xa19b2366, 0x6ac7f0c3, 0x77c2c07b, 0xbc9e13de, 0x3a0a6170, 0xf156b2d5, 0x3d6029b, 0xc88ad13e, 0x4e1ea390, 
    0x85427035, 0x9847408d, 0x531b9328, 0xd58fe186, 0x1ed33223, 0xef8580f6, 0x24d95353, 0xa24d21fd, 0x6911f258, 0x7414c2e0, 
    0xbf481145, 0x39dc63eb, 0xf280b04e, 0x7ac0536, 0xccf0d693, 0x4a64a43d, 0x81387798, 0x9c3d4720, 0x57619485, 0xd1f5e62b, 
    0x1aa9358e, 0xebff875b, 0x20a354fe, 0xa6372650, 0x6d6bf5f5, 0x706ec54d, 0xbb3216e8, 0x3da66446, 0xf6fab7e3, 0x47a07ad, 
    0xcf26d408, 0x49b2a6a6, 0x82ee7503, 0x9feb45bb, 0x54b7961e, 0xd223e4b0, 0x197f3715, 0xe82985c0, 0x23755665, 0xa5e124cb, 
    0x6ebdf76e, 0x73b8c7d6, 0xb8e41473, 0x3e7066dd, 0xf52cb578, 0xf580a6c, 0xc404d9c9, 0x4290ab67, 0x89cc78c2, 0x94c9487a, 
    0x5f959bdf, 0xd901e971, 0x125d3ad4, 0xe30b8801, 0x28575ba4, 0xaec3290a, 0x659ffaaf, 0x789aca17, 0xb3c619b2, 0x35526b1c, 
    0xfe0eb8b9, 0xc8e08f7, 0xc7d2db52, 0x4146a9fc, 0x8a1a7a59, 0x971f4ae1, 0x5c439944, 0xdad7ebea, 0x118b384f, 0xe0dd8a9a, 
    0x2b81593f, 0xad152b91, 0x6649f834, 0x7b4cc88c, 0xb0101b29, 0x36846987, 0xfdd8ba22, 0x8f40f5a, 0xc3a8dcff, 0x453cae51, 
    0x8e607df4, 0x93654d4c, 0x58399ee9, 0xdeadec47, 0x15f13fe2, 0xe4a78d37, 0x2ffb5e92, 0xa96f2c3c, 0x6233ff99, 0x7f36cf21, 
    0xb46a1c84, 0x32fe6e2a, 0xf9a2bd8f, 0xb220dc1, 0xc07ede64, 0x46eaacca, 0x8db67f6f, 0x90b34fd7, 0x5bef9c72, 0xdd7beedc, 
    0x16273d79, 0xe7718fac, 0x2c2d5c09, 0xaab92ea7, 0x61e5fd02, 0x7ce0cdba, 0xb7bc1e1f, 0x31286cb1, 0xfa74bf14, 0x1eb014d8, 
    0xd5ecc77d, 0x5378b5d3, 0x98246676, 0x852156ce, 0x4e7d856b, 0xc8e9f7c5, 0x3b52460, 0xf2e396b5, 0x39bf4510, 0xbf2b37be, 
    0x7477e41b, 0x6972d4a3, 0xa22e0706, 0x24ba75a8, 0xefe6a60d, 0x1d661643, 0xd63ac5e6, 0x50aeb748, 0x9bf264ed, 0x86f75455, 
    0x4dab87f0, 0xcb3ff55e, 0x6326fb, 0xf135942e, 0x3a69478b, 0xbcfd3525, 0x77a1e680, 0x6aa4d638, 0xa1f8059d, 0x276c7733, 
    0xec30a496, 0x191c11ee, 0xd240c24b, 0x54d4b0e5, 0x9f886340, 0x828d53f8, 0x49d1805d, 0xcf45f2f3, 0x4192156, 0xf54f9383, 
    0x3e134026, 0xb8873288, 0x73dbe12d, 0x6eded195, 0xa5820230, 0x2316709e, 0xe84aa33b, 0x1aca1375, 0xd196c0d0, 0x5702b27e, 
    0x9c5e61db, 0x815b5163, 0x4a0782c6, 0xcc93f068, 0x7cf23cd, 0xf6999118, 0x3dc542bd, 0xbb513013, 0x700de3b6, 0x6d08d30e, 
    0xa65400ab, 0x20c07205, 0xeb9ca1a0, 0x11e81eb4, 0xdab4cd11, 0x5c20bfbf, 0x977c6c1a, 0x8a795ca2, 0x41258f07, 0xc7b1fda9, 
    0xced2e0c, 0xfdbb9cd9, 0x36e74f7c, 0xb0733dd2, 0x7b2fee77, 0x662adecf, 0xad760d6a, 0x2be27fc4, 0xe0beac61, 0x123e1c2f, 
    0xd962cf8a, 0x5ff6bd24, 0x94aa6e81, 0x89af5e39, 0x42f38d9c, 0xc467ff32, 0xf3b2c97, 0xfe6d9e42, 0x35314de7, 0xb3a53f49, 
    0x78f9ecec, 0x65fcdc54, 0xaea00ff1, 0x28347d5f, 0xe368aefa, 0x16441b82, 0xdd18c827, 0x5b8cba89, 0x90d0692c, 0x8dd55994, 
    0x46898a31, 0xc01df89f, 0xb412b3a, 0xfa1799ef, 0x314b4a4a, 0xb7df38e4, 0x7c83eb41, 0x6186dbf9, 0xaada085c, 0x2c4e7af2, 
    0xe712a957, 0x15921919, 0xdececabc, 0x585ab812, 0x93066bb7, 0x8e035b0f, 0x455f88aa, 0xc3cbfa04, 0x89729a1, 0xf9c19b74, 
    0x329d48d1, 0xb4093a7f, 0x7f55e9da, 0x6250d962, 0xa90c0ac7, 0x2f987869, 0xe4c4abcc, 0x0, 0x3d6029b0, 0x7ac05360, 
    0x47a07ad0, 0xf580a6c0, 0xc8e08f70, 0x8f40f5a0, 0xb220dc10, 0x30704bc1, 0xd106271, 0x4ab018a1, 0x77d03111, 0xc5f0ed01, 
    0xf890c4b1, 0xbf30be61, 0x825097d1, 0x60e09782, 0x5d80be32, 0x1a20c4e2, 0x2740ed52, 0x95603142, 0xa80018f2, 0xefa06222, 
    0xd2c04b92, 0x5090dc43, 0x6df0f5f3, 0x2a508f23, 0x1730a693, 0xa5107a83, 0x98705333, 0xdfd029e3, 0xe2b00053, 0xc1c12f04, 
    0xfca106b4, 0xbb017c64, 0x866155d4, 0x344189c4, 0x921a074, 0x4e81daa4, 0x73e1f314, 0xf1b164c5, 0xccd14d75, 0x8b7137a5, 
    0xb6111e15, 0x431c205, 0x3951ebb5, 0x7ef19165, 0x4391b8d5, 0xa121b886, 0x9c419136, 0xdbe1ebe6, 0xe681c256, 0x54a11e46, 
    0x69c137f6, 0x2e614d26, 0x13016496, 0x9151f347, 0xac31daf7, 0xeb91a027, 0xd6f18997, 0x64d15587, 0x59b17c37, 0x1e1106e7, 
    0x23712f57, 0x58f35849, 0x659371f9, 0x22330b29, 0x1f532299, 0xad73fe89, 0x9013d739, 0xd7b3ade9, 0xead38459, 0x68831388, 
    0x55e33a38, 0x124340e8, 0x2f236958, 0x9d03b548, 0xa0639cf8, 0xe7c3e628, 0xdaa3cf98, 0x3813cfcb, 0x573e67b, 0x42d39cab, 
    0x7fb3b51b, 0xcd93690b, 0xf0f340bb, 0xb7533a6b, 0x8a3313db, 0x863840a, 0x3503adba, 0x72a3d76a, 0x4fc3feda, 0xfde322ca, 
    0xc0830b7a, 0x872371aa, 0xba43581a, 0x9932774d, 0xa4525efd, 0xe3f2242d, 0xde920d9d, 0x6cb2d18d, 0x51d2f83d, 0x167282ed, 
    0x2b12ab5d, 0xa9423c8c, 0x9422153c, 0xd3826fec, 0xeee2465c, 0x5cc29a4c, 0x61a2b3fc, 0x2602c92c, 0x1b62e09c, 0xf9d2e0cf, 
    0xc4b2c97f, 0x8312b3af, 0xbe729a1f, 0xc52460f, 0x31326fbf, 0x7692156f, 0x4bf23cdf, 0xc9a2ab0e, 0xf4c282be, 0xb362f86e, 
    0x8e02d1de, 0x3c220dce, 0x142247e, 0x46e25eae, 0x7b82771e, 0xb1e6b092, 0x8c869922, 0xcb26e3f2, 0xf646ca42, 0x44661652, 
    0x79063fe2, 0x3ea64532, 0x3c66c82, 0x8196fb53, 0xbcf6d2e3, 0xfb56a833, 0xc6368183, 0x74165d93, 0x49767423, 0xed60ef3, 
    0x33b62743, 0xd1062710, 0xec660ea0, 0xabc67470, 0x96a65dc0, 0x248681d0, 0x19e6a860, 0x5e46d2b0, 0x6326fb00, 0xe1766cd1, 
    0xdc164561, 0x9bb63fb1, 0xa6d61601, 0x14f6ca11, 0x2996e3a1, 0x6e369971, 0x5356b0c1, 0x70279f96, 0x4d47b626, 0xae7ccf6, 
    0x3787e546, 0x85a73956, 0xb8c710e6, 0xff676a36, 0xc2074386, 0x4057d457, 0x7d37fde7, 0x3a978737, 0x7f7ae87, 0xb5d77297, 
    0x88b75b27, 0xcf1721f7, 0xf2770847, 0x10c70814, 0x2da721a4, 0x6a075b74, 0x576772c4, 0xe547aed4, 0xd8278764, 0x9f87fdb4, 
    0xa2e7d404, 0x20b743d5, 0x1dd76a65, 0x5a7710b5, 0x67173905, 0xd537e515, 0xe857cca5, 0xaff7b675, 0x92979fc5, 0xe915e8db, 
    0xd475c16b, 0x93d5bbbb, 0xaeb5920b, 0x1c954e1b, 0x21f567ab, 0x66551d7b, 0x5b3534cb, 0xd965a31a, 0xe4058aaa, 0xa3a5f07a, 
    0x9ec5d9ca, 0x2ce505da, 0x11852c6a, 0x562556ba, 0x6b457f0a, 0x89f57f59, 0xb49556e9, 0xf3352c39, 0xce550589, 0x7c75d999, 
    0x4115f029, 0x6b58af9, 0x3bd5a349, 0xb9853498, 0x84e51d28, 0xc34567f8, 0xfe254e48, 0x4c059258, 0x7165bbe8, 0x36c5c138, 
    0xba5e888, 0x28d4c7df, 0x15b4ee6f, 0x521494bf, 0x6f74bd0f, 0xdd54611f, 0xe03448af, 0xa794327f, 0x9af41bcf, 0x18a48c1e, 
    0x25c4a5ae, 0x6264df7e, 0x5f04f6ce, 0xed242ade, 0xd044036e, 0x97e479be, 0xaa84500e, 0x4834505d, 0x755479ed, 0x32f4033d, 
    0xf942a8d, 0xbdb4f69d, 0x80d4df2d, 0xc774a5fd, 0xfa148c4d, 0x78441b9c, 0x4524322c, 0x28448fc, 0x3fe4614c, 0x8dc4bd5c, 
    0xb0a494ec, 0xf704ee3c, 0xca64c78c, 0x0, 0xb8bc6765, 0xaa09c88b, 0x12b5afee, 0x8f629757, 0x37def032, 0x256b5fdc, 
    0x9dd738b9, 0xc5b428ef, 0x7d084f8a, 0x6fbde064, 0xd7018701, 0x4ad6bfb8, 0xf26ad8dd, 0xe0df7733, 0x58631056, 0x5019579f, 
    0xe8a530fa, 0xfa109f14, 0x42acf871, 0xdf7bc0c8, 0x67c7a7ad, 0x75720843, 0xcdce6f26, 0x95ad7f70, 0x2d111815, 0x3fa4b7fb, 
    0x8718d09e, 0x1acfe827, 0xa2738f42, 0xb0c620ac, 0x87a47c9, 0xa032af3e, 0x188ec85b, 0xa3b67b5, 0xb28700d0, 0x2f503869, 
    0x97ec5f0c, 0x8559f0e2, 0x3de59787, 0x658687d1, 0xdd3ae0b4, 0xcf8f4f5a, 0x7733283f, 0xeae41086, 0x525877e3, 0x40edd80d, 
    0xf851bf68, 0xf02bf8a1, 0x48979fc4, 0x5a22302a, 0xe29e574f, 0x7f496ff6, 0xc7f50893, 0xd540a77d, 0x6dfcc018, 0x359fd04e, 
    0x8d23b72b, 0x9f9618c5, 0x272a7fa0, 0xbafd4719, 0x241207c, 0x10f48f92, 0xa848e8f7, 0x9b14583d, 0x23a83f58, 0x311d90b6, 
    0x89a1f7d3, 0x1476cf6a, 0xaccaa80f, 0xbe7f07e1, 0x6c36084, 0x5ea070d2, 0xe61c17b7, 0xf4a9b859, 0x4c15df3c, 0xd1c2e785, 
    0x697e80e0, 0x7bcb2f0e, 0xc377486b, 0xcb0d0fa2, 0x73b168c7, 0x6104c729, 0xd9b8a04c, 0x446f98f5, 0xfcd3ff90, 0xee66507e, 
    0x56da371b, 0xeb9274d, 0xb6054028, 0xa4b0efc6, 0x1c0c88a3, 0x81dbb01a, 0x3967d77f, 0x2bd27891, 0x936e1ff4, 0x3b26f703, 
    0x839a9066, 0x912f3f88, 0x299358ed, 0xb4446054, 0xcf80731, 0x1e4da8df, 0xa6f1cfba, 0xfe92dfec, 0x462eb889, 0x549b1767, 
    0xec277002, 0x71f048bb, 0xc94c2fde, 0xdbf98030, 0x6345e755, 0x6b3fa09c, 0xd383c7f9, 0xc1366817, 0x798a0f72, 0xe45d37cb, 
    0x5ce150ae, 0x4e54ff40, 0xf6e89825, 0xae8b8873, 0x1637ef16, 0x48240f8, 0xbc3e279d, 0x21e91f24, 0x99557841, 0x8be0d7af, 
    0x335cb0ca, 0xed59b63b, 0x55e5d15e, 0x47507eb0, 0xffec19d5, 0x623b216c, 0xda874609, 0xc832e9e7, 0x708e8e82, 0x28ed9ed4, 
    0x9051f9b1, 0x82e4565f, 0x3a58313a, 0xa78f0983, 0x1f336ee6, 0xd86c108, 0xb53aa66d, 0xbd40e1a4, 0x5fc86c1, 0x1749292f, 
    0xaff54e4a, 0x322276f3, 0x8a9e1196, 0x982bbe78, 0x2097d91d, 0x78f4c94b, 0xc048ae2e, 0xd2fd01c0, 0x6a4166a5, 0xf7965e1c, 
    0x4f2a3979, 0x5d9f9697, 0xe523f1f2, 0x4d6b1905, 0xf5d77e60, 0xe762d18e, 0x5fdeb6eb, 0xc2098e52, 0x7ab5e937, 0x680046d9, 
    0xd0bc21bc, 0x88df31ea, 0x3063568f, 0x22d6f961, 0x9a6a9e04, 0x7bda6bd, 0xbf01c1d8, 0xadb46e36, 0x15080953, 0x1d724e9a, 
    0xa5ce29ff, 0xb77b8611, 0xfc7e174, 0x9210d9cd, 0x2aacbea8, 0x38191146, 0x80a57623, 0xd8c66675, 0x607a0110, 0x72cfaefe, 
    0xca73c99b, 0x57a4f122, 0xef189647, 0xfdad39a9, 0x45115ecc, 0x764dee06, 0xcef18963, 0xdc44268d, 0x64f841e8, 0xf92f7951, 
    0x41931e34, 0x5326b1da, 0xeb9ad6bf, 0xb3f9c6e9, 0xb45a18c, 0x19f00e62, 0xa14c6907, 0x3c9b51be, 0x842736db, 0x96929935, 
    0x2e2efe50, 0x2654b999, 0x9ee8defc, 0x8c5d7112, 0x34e11677, 0xa9362ece, 0x118a49ab, 0x33fe645, 0xbb838120, 0xe3e09176, 
    0x5b5cf613, 0x49e959fd, 0xf1553e98, 0x6c820621, 0xd43e6144, 0xc68bceaa, 0x7e37a9cf, 0xd67f4138, 0x6ec3265d, 0x7c7689b3, 
    0xc4caeed6, 0x591dd66f, 0xe1a1b10a, 0xf3141ee4, 0x4ba87981, 0x13cb69d7, 0xab770eb2, 0xb9c2a15c, 0x17ec639, 0x9ca9fe80, 
    0x241599e5, 0x36a0360b, 0x8e1c516e, 0x866616a7, 0x3eda71c2, 0x2c6fde2c, 0x94d3b949, 0x90481f0, 0xb1b8e695, 0xa30d497b, 
    0x1bb12e1e, 0x43d23e48, 0xfb6e592d, 0xe9dbf6c3, 0x516791a6, 0xccb0a91f, 0x740cce7a, 0x66b96194, 0xde0506f1, 0x0, 
    0x1c26a37, 0x384d46e, 0x246be59, 0x709a8dc, 0x6cbc2eb, 0x48d7cb2, 0x54f1685, 0xe1351b8, 0xfd13b8f, 0xd9785d6, 
    0xc55efe1, 0x91af964, 0x8d89353, 0xa9e2d0a, 0xb5c473d, 0x1c26a370, 0x1de4c947, 0x1fa2771e, 0x1e601d29, 0x1b2f0bac, 
    0x1aed619b, 0x18abdfc2, 0x1969b5f5, 0x1235f2c8, 0x13f798ff, 0x11b126a6, 0x10734c91, 0x153c5a14, 0x14fe3023, 0x16b88e7a, 
    0x177ae44d, 0x384d46e0, 0x398f2cd7, 0x3bc9928e, 0x3a0bf8b9, 0x3f44ee3c, 0x3e86840b, 0x3cc03a52, 0x3d025065, 0x365e1758, 
    0x379c7d6f, 0x35dac336, 0x3418a901, 0x3157bf84, 0x3095d5b3, 0x32d36bea, 0x331101dd, 0x246be590, 0x25a98fa7, 0x27ef31fe, 
    0x262d5bc9, 0x23624d4c, 0x22a0277b, 0x20e69922, 0x2124f315, 0x2a78b428, 0x2bbade1f, 0x29fc6046, 0x283e0a71, 0x2d711cf4, 
    0x2cb376c3, 0x2ef5c89a, 0x2f37a2ad, 0x709a8dc0, 0x7158e7f7, 0x731e59ae, 0x72dc3399, 0x7793251c, 0x76514f2b, 0x7417f172, 
    0x75d59b45, 0x7e89dc78, 0x7f4bb64f, 0x7d0d0816, 0x7ccf6221, 0x798074a4, 0x78421e93, 0x7a04a0ca, 0x7bc6cafd, 0x6cbc2eb0, 
    0x6d7e4487, 0x6f38fade, 0x6efa90e9, 0x6bb5866c, 0x6a77ec5b, 0x68315202, 0x69f33835, 0x62af7f08, 0x636d153f, 0x612bab66, 
    0x60e9c151, 0x65a6d7d4, 0x6464bde3, 0x662203ba, 0x67e0698d, 0x48d7cb20, 0x4915a117, 0x4b531f4e, 0x4a917579, 0x4fde63fc, 
    0x4e1c09cb, 0x4c5ab792, 0x4d98dda5, 0x46c49a98, 0x4706f0af, 0x45404ef6, 0x448224c1, 0x41cd3244, 0x400f5873, 0x4249e62a, 
    0x438b8c1d, 0x54f16850, 0x55330267, 0x5775bc3e, 0x56b7d609, 0x53f8c08c, 0x523aaabb, 0x507c14e2, 0x51be7ed5, 0x5ae239e8, 
    0x5b2053df, 0x5966ed86, 0x58a487b1, 0x5deb9134, 0x5c29fb03, 0x5e6f455a, 0x5fad2f6d, 0xe1351b80, 0xe0f771b7, 0xe2b1cfee, 
    0xe373a5d9, 0xe63cb35c, 0xe7fed96b, 0xe5b86732, 0xe47a0d05, 0xef264a38, 0xeee4200f, 0xeca29e56, 0xed60f461, 0xe82fe2e4, 
    0xe9ed88d3, 0xebab368a, 0xea695cbd, 0xfd13b8f0, 0xfcd1d2c7, 0xfe976c9e, 0xff5506a9, 0xfa1a102c, 0xfbd87a1b, 0xf99ec442, 
    0xf85cae75, 0xf300e948, 0xf2c2837f, 0xf0843d26, 0xf1465711, 0xf4094194, 0xf5cb2ba3, 0xf78d95fa, 0xf64fffcd, 0xd9785d60, 
    0xd8ba3757, 0xdafc890e, 0xdb3ee339, 0xde71f5bc, 0xdfb39f8b, 0xddf521d2, 0xdc374be5, 0xd76b0cd8, 0xd6a966ef, 0xd4efd8b6, 
    0xd52db281, 0xd062a404, 0xd1a0ce33, 0xd3e6706a, 0xd2241a5d, 0xc55efe10, 0xc49c9427, 0xc6da2a7e, 0xc7184049, 0xc25756cc, 
    0xc3953cfb, 0xc1d382a2, 0xc011e895, 0xcb4dafa8, 0xca8fc59f, 0xc8c97bc6, 0xc90b11f1, 0xcc440774, 0xcd866d43, 0xcfc0d31a, 
    0xce02b92d, 0x91af9640, 0x906dfc77, 0x922b422e, 0x93e92819, 0x96a63e9c, 0x976454ab, 0x9522eaf2, 0x94e080c5, 0x9fbcc7f8, 
    0x9e7eadcf, 0x9c381396, 0x9dfa79a1, 0x98b56f24, 0x99770513, 0x9b31bb4a, 0x9af3d17d, 0x8d893530, 0x8c4b5f07, 0x8e0de15e, 
    0x8fcf8b69, 0x8a809dec, 0x8b42f7db, 0x89044982, 0x88c623b5, 0x839a6488, 0x82580ebf, 0x801eb0e6, 0x81dcdad1, 0x8493cc54, 
    0x8551a663, 0x8717183a, 0x86d5720d, 0xa9e2d0a0, 0xa820ba97, 0xaa6604ce, 0xaba46ef9, 0xaeeb787c, 0xaf29124b, 0xad6fac12, 
    0xacadc625, 0xa7f18118, 0xa633eb2f, 0xa4755576, 0xa5b73f41, 0xa0f829c4, 0xa13a43f3, 0xa37cfdaa, 0xa2be979d, 0xb5c473d0, 
    0xb40619e7, 0xb640a7be, 0xb782cd89, 0xb2cddb0c, 0xb30fb13b, 0xb1490f62, 0xb08b6555, 0xbbd72268, 0xba15485f, 0xb853f606, 
    0xb9919c31, 0xbcde8ab4, 0xbd1ce083, 0xbf5a5eda, 0xbe9834ed, 0x0, 0x191b3141, 0x32366282, 0x2b2d53c3, 0x646cc504, 
    0x7d77f445, 0x565aa786, 0x4f4196c7, 0xc8d98a08, 0xd1c2bb49, 0xfaefe88a, 0xe3f4d9cb, 0xacb54f0c, 0xb5ae7e4d, 0x9e832d8e, 
    0x87981ccf, 0x4ac21251, 0x53d92310, 0x78f470d3, 0x61ef4192, 0x2eaed755, 0x37b5e614, 0x1c98b5d7, 0x5838496, 0x821b9859, 
    0x9b00a918, 0xb02dfadb, 0xa936cb9a, 0xe6775d5d, 0xff6c6c1c, 0xd4413fdf, 0xcd5a0e9e, 0x958424a2, 0x8c9f15e3, 0xa7b24620, 
    0xbea97761, 0xf1e8e1a6, 0xe8f3d0e7, 0xc3de8324, 0xdac5b265, 0x5d5daeaa, 0x44469feb, 0x6f6bcc28, 0x7670fd69, 0x39316bae, 
    0x202a5aef, 0xb07092c, 0x121c386d, 0xdf4636f3, 0xc65d07b2, 0xed705471, 0xf46b6530, 0xbb2af3f7, 0xa231c2b6, 0x891c9175, 
    0x9007a034, 0x179fbcfb, 0xe848dba, 0x25a9de79, 0x3cb2ef38, 0x73f379ff, 0x6ae848be, 0x41c51b7d, 0x58de2a3c, 0xf0794f05, 
    0xe9627e44, 0xc24f2d87, 0xdb541cc6, 0x94158a01, 0x8d0ebb40, 0xa623e883, 0xbf38d9c2, 0x38a0c50d, 0x21bbf44c, 0xa96a78f, 
    0x138d96ce, 0x5ccc0009, 0x45d73148, 0x6efa628b, 0x77e153ca, 0xbabb5d54, 0xa3a06c15, 0x888d3fd6, 0x91960e97, 0xded79850, 
    0xc7cca911, 0xece1fad2, 0xf5facb93, 0x7262d75c, 0x6b79e61d, 0x4054b5de, 0x594f849f, 0x160e1258, 0xf152319, 0x243870da, 
    0x3d23419b, 0x65fd6ba7, 0x7ce65ae6, 0x57cb0925, 0x4ed03864, 0x191aea3, 0x188a9fe2, 0x33a7cc21, 0x2abcfd60, 0xad24e1af, 
    0xb43fd0ee, 0x9f12832d, 0x8609b26c, 0xc94824ab, 0xd05315ea, 0xfb7e4629, 0xe2657768, 0x2f3f79f6, 0x362448b7, 0x1d091b74, 
    0x4122a35, 0x4b53bcf2, 0x52488db3, 0x7965de70, 0x607eef31, 0xe7e6f3fe, 0xfefdc2bf, 0xd5d0917c, 0xcccba03d, 0x838a36fa, 
    0x9a9107bb, 0xb1bc5478, 0xa8a76539, 0x3b83984b, 0x2298a90a, 0x9b5fac9, 0x10aecb88, 0x5fef5d4f, 0x46f46c0e, 0x6dd93fcd, 
    0x74c20e8c, 0xf35a1243, 0xea412302, 0xc16c70c1, 0xd8774180, 0x9736d747, 0x8e2de606, 0xa500b5c5, 0xbc1b8484, 0x71418a1a, 
    0x685abb5b, 0x4377e898, 0x5a6cd9d9, 0x152d4f1e, 0xc367e5f, 0x271b2d9c, 0x3e001cdd, 0xb9980012, 0xa0833153, 0x8bae6290, 
    0x92b553d1, 0xddf4c516, 0xc4eff457, 0xefc2a794, 0xf6d996d5, 0xae07bce9, 0xb71c8da8, 0x9c31de6b, 0x852aef2a, 0xca6b79ed, 
    0xd37048ac, 0xf85d1b6f, 0xe1462a2e, 0x66de36e1, 0x7fc507a0, 0x54e85463, 0x4df36522, 0x2b2f3e5, 0x1ba9c2a4, 0x30849167, 
    0x299fa026, 0xe4c5aeb8, 0xfdde9ff9, 0xd6f3cc3a, 0xcfe8fd7b, 0x80a96bbc, 0x99b25afd, 0xb29f093e, 0xab84387f, 0x2c1c24b0, 
    0x350715f1, 0x1e2a4632, 0x7317773, 0x4870e1b4, 0x516bd0f5, 0x7a468336, 0x635db277, 0xcbfad74e, 0xd2e1e60f, 0xf9ccb5cc, 
    0xe0d7848d, 0xaf96124a, 0xb68d230b, 0x9da070c8, 0x84bb4189, 0x3235d46, 0x1a386c07, 0x31153fc4, 0x280e0e85, 0x674f9842, 
    0x7e54a903, 0x5579fac0, 0x4c62cb81, 0x8138c51f, 0x9823f45e, 0xb30ea79d, 0xaa1596dc, 0xe554001b, 0xfc4f315a, 0xd7626299, 
    0xce7953d8, 0x49e14f17, 0x50fa7e56, 0x7bd72d95, 0x62cc1cd4, 0x2d8d8a13, 0x3496bb52, 0x1fbbe891, 0x6a0d9d0, 0x5e7ef3ec, 
    0x4765c2ad, 0x6c48916e, 0x7553a02f, 0x3a1236e8, 0x230907a9, 0x824546a, 0x113f652b, 0x96a779e4, 0x8fbc48a5, 0xa4911b66, 
    0xbd8a2a27, 0xf2cbbce0, 0xebd08da1, 0xc0fdde62, 0xd9e6ef23, 0x14bce1bd, 0xda7d0fc, 0x268a833f, 0x3f91b27e, 0x70d024b9, 
    0x69cb15f8, 0x42e6463b, 0x5bfd777a, 0xdc656bb5, 0xc57e5af4, 0xee530937, 0xf7483876, 0xb809aeb1, 0xa1129ff0, 0x8a3fcc33, 
    0x9324fd72, 0x0, 0x77073096, 0xee0e612c, 0x990951ba, 0x76dc419, 0x706af48f, 0xe963a535, 0x9e6495a3, 0xedb8832, 
    0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x9b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 
    0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 
    0x63066cd9, 0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 
    0xa50ab56b, 0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 
    0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 
    0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x1db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 
    0x6b6b51f, 0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0xf00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x86d3d2d, 0x91646c97, 
    0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 
    0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074, 
    0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 
    0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 
    0xce61e49f, 0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 
    0x9abfb3b6, 0x3b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x4db2615, 0x73dc1683, 0xe3630b12, 0x94643b84, 0xd6d6a3e, 
    0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0xa00ae27, 0x7d079eb1, 0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 
    0x806567cb, 0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 
    0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 
    0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 
    0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 
    0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x26d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 
    0x5005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0xcb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0xbdbdf21, 0x86d3d2d4, 
    0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777, 0x88085ae6, 0xff0f6a70, 0x66063bca, 
    0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 
    0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 
    0x30b5ffe9, 0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 
    0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};
// clang-format on

prb_PUBLICDEF uint32_t
prb_crc32(const void* data, size_t n_bytes) {
    size_t   n_accum = n_bytes / sizeof(uint64_t);
    uint32_t crc = 0;
    for (size_t i = 0; i < n_accum; ++i) {
        uint64_t a = crc ^ ((uint64_t*)data)[i];
        for (size_t j = crc = 0; j < sizeof(uint64_t); ++j) {
            crc ^= prb_globalCrc32Wtable[(j << 8) + (uint8_t)(a >> 8 * j)];
        }
    }
    for (size_t i = n_accum * sizeof(uint64_t); i < n_bytes; ++i) {
        crc = prb_globalCrc32Table[(uint8_t)crc ^ ((uint8_t*)data)[i]] ^ crc >> 8;
    }
    return crc;
}

//
// SECTION stb snprintf (implementation)
//

#define stbsp__uint32 unsigned int
#define stbsp__int32 signed int

#ifdef _MSC_VER
#define stbsp__uint64 unsigned __int64
#define stbsp__int64 signed __int64
#else
#define stbsp__uint64 unsigned long long
#define stbsp__int64 signed long long
#endif
#define stbsp__uint16 unsigned short

#ifndef stbsp__uintptr
#if defined(__ppc64__) || defined(__powerpc64__) || defined(__aarch64__) || defined(_M_X64) || defined(__x86_64__) \
    || defined(__x86_64) || defined(__s390x__)
#define stbsp__uintptr stbsp__uint64
#else
#define stbsp__uintptr stbsp__uint32
#endif
#endif

#ifndef STB_SPRINTF_MSVC_MODE  // used for MSVC2013 and earlier (MSVC2015 matches GCC)
#if defined(_MSC_VER) && (_MSC_VER < 1900)
#define STB_SPRINTF_MSVC_MODE
#endif
#endif

#ifdef STB_SPRINTF_NOUNALIGNED  // define this before inclusion to force stbsp_sprintf to always use aligned accesses
#define STBSP__UNALIGNED(code)
#else
#define STBSP__UNALIGNED(code) code
#endif

#ifndef STB_SPRINTF_NOFLOAT
// internal float utility functions
static stbsp__int32 stbsp__real_to_str(
    char const**   start,
    stbsp__uint32* len,
    char*          out,
    stbsp__int32*  decimal_pos,
    double         value,
    stbsp__uint32  frac_digits
);
static stbsp__int32 stbsp__real_to_parts(stbsp__int64* bits, stbsp__int32* expo, double value);
#define STBSP__SPECIAL 0x7000
#endif

static char stbsp__period = '.';
static char stbsp__comma = ',';
static struct {
    short temp;  // force next field to be 2-byte aligned
    char  pair[201];
} stbsp__digitpair = {
    0,
    "00010203040506070809101112131415161718192021222324"
    "25262728293031323334353637383940414243444546474849"
    "50515253545556575859606162636465666768697071727374"
    "75767778798081828384858687888990919293949596979899"};

STBSP__PUBLICDEF void
STB_SPRINTF_DECORATE(set_separators)(char pcomma, char pperiod) {
    stbsp__period = pperiod;
    stbsp__comma = pcomma;
}

#define STBSP__LEFTJUST 1
#define STBSP__LEADINGPLUS 2
#define STBSP__LEADINGSPACE 4
#define STBSP__LEADING_0X 8
#define STBSP__LEADINGZERO 16
#define STBSP__INTMAX 32
#define STBSP__TRIPLET_COMMA 64
#define STBSP__NEGATIVE 128
#define STBSP__METRIC_SUFFIX 256
#define STBSP__HALFWIDTH 512
#define STBSP__METRIC_NOSPACE 1024
#define STBSP__METRIC_1024 2048
#define STBSP__METRIC_JEDEC 4096

static void
stbsp__lead_sign(stbsp__uint32 fl, char* sign) {
    sign[0] = 0;
    if (fl & STBSP__NEGATIVE) {
        sign[0] = 1;
        sign[1] = '-';
    } else if (fl & STBSP__LEADINGSPACE) {
        sign[0] = 1;
        sign[1] = ' ';
    } else if (fl & STBSP__LEADINGPLUS) {
        sign[0] = 1;
        sign[1] = '+';
    }
}

static STBSP__ASAN stbsp__uint32
stbsp__strlen_limited(char const* s, stbsp__uint32 limit) {
    char const* sn = s;

    // get up to 4-byte alignment
    for (;;) {
        if (((stbsp__uintptr)sn & 3) == 0)
            break;

        if (!limit || *sn == 0)
            return (stbsp__uint32)(sn - s);

        ++sn;
        --limit;
    }

    // scan over 4 bytes at a time to find terminating 0
    // this will intentionally scan up to 3 bytes past the end of buffers,
    // but becase it works 4B aligned, it will never cross page boundaries
    // (hence the STBSP__ASAN markup; the over-read here is intentional
    // and harmless)
    while (limit >= 4) {
        stbsp__uint32 v = *(stbsp__uint32*)sn;
        // bit hack to find if there's a 0 byte in there
        if ((v - 0x01010101) & (~v) & 0x80808080UL)
            break;

        sn += 4;
        limit -= 4;
    }

    // handle the last few characters to find actual size
    while (limit && *sn) {
        ++sn;
        --limit;
    }

    return (stbsp__uint32)(sn - s);
}

STBSP__PUBLICDEF int
STB_SPRINTF_DECORATE(vsprintfcb)(STBSP_SPRINTFCB* callback, void* user, char* buf, char const* fmt, va_list va) {
    static char hex[] = "0123456789abcdefxp";
    static char hexu[] = "0123456789ABCDEFXP";
    char*       bf;
    char const* f;
    int         tlen = 0;

    bf = buf;
    f = fmt;
    for (;;) {
        stbsp__int32  fw, pr, tz;
        stbsp__uint32 fl;

// macros for the callback buffer stuff
#define stbsp__chk_cb_bufL(bytes) \
    { \
        int len = (int)(bf - buf); \
        if ((len + (bytes)) >= STB_SPRINTF_MIN) { \
            tlen += len; \
            if (0 == (bf = buf = callback(buf, user, len))) \
                goto done; \
        } \
    }
#define stbsp__chk_cb_buf(bytes) \
    { \
        if (callback) { \
            stbsp__chk_cb_bufL(bytes); \
        } \
    }
#define stbsp__flush_cb() \
    { stbsp__chk_cb_bufL(STB_SPRINTF_MIN - 1); }  // flush if there is even one byte in the buffer
#define stbsp__cb_buf_clamp(cl, v) \
    cl = v; \
    if (callback) { \
        int lg = STB_SPRINTF_MIN - (int)(bf - buf); \
        if (cl > lg) \
            cl = lg; \
    }

        // fast copy everything up to the next % (or end of string)
        for (;;) {
            while (((stbsp__uintptr)f) & 3) {
            schk1:
                if (f[0] == '%')
                    goto scandd;
            schk2:
                if (f[0] == 0)
                    goto endfmt;
                stbsp__chk_cb_buf(1);
                *bf++ = f[0];
                ++f;
            }
            for (;;) {
                // Check if the next 4 bytes contain %(0x25) or end of string.
                // Using the 'hasless' trick:
                // https://graphics.stanford.edu/~seander/bithacks.html#HasLessInWord
                stbsp__uint32 v, c;
                v = *(stbsp__uint32*)f;
                c = (~v) & 0x80808080;
                if (((v ^ 0x25252525) - 0x01010101) & c)
                    goto schk1;
                if ((v - 0x01010101) & c)
                    goto schk2;
                if (callback)
                    if ((STB_SPRINTF_MIN - (int)(bf - buf)) < 4)
                        goto schk1;
#ifdef STB_SPRINTF_NOUNALIGNED
                if (((stbsp__uintptr)bf) & 3) {
                    bf[0] = f[0];
                    bf[1] = f[1];
                    bf[2] = f[2];
                    bf[3] = f[3];
                } else
#endif
                {
                    *(stbsp__uint32*)bf = v;
                }
                bf += 4;
                f += 4;
            }
        }
    scandd:

        ++f;

        // ok, we have a percent, read the modifiers first
        fw = 0;
        pr = -1;
        fl = 0;
        tz = 0;

        // flags
        for (;;) {
            switch (f[0]) {
                // if we have left justify
                case '-':
                    fl |= STBSP__LEFTJUST;
                    ++f;
                    continue;
                // if we have leading plus
                case '+':
                    fl |= STBSP__LEADINGPLUS;
                    ++f;
                    continue;
                // if we have leading space
                case ' ':
                    fl |= STBSP__LEADINGSPACE;
                    ++f;
                    continue;
                // if we have leading 0x
                case '#':
                    fl |= STBSP__LEADING_0X;
                    ++f;
                    continue;
                // if we have thousand commas
                case '\'':
                    fl |= STBSP__TRIPLET_COMMA;
                    ++f;
                    continue;
                // if we have kilo marker (none->kilo->kibi->jedec)
                case '$':
                    if (fl & STBSP__METRIC_SUFFIX) {
                        if (fl & STBSP__METRIC_1024) {
                            fl |= STBSP__METRIC_JEDEC;
                        } else {
                            fl |= STBSP__METRIC_1024;
                        }
                    } else {
                        fl |= STBSP__METRIC_SUFFIX;
                    }
                    ++f;
                    continue;
                // if we don't want space between metric suffix and number
                case '_':
                    fl |= STBSP__METRIC_NOSPACE;
                    ++f;
                    continue;
                // if we have leading zero
                case '0':
                    fl |= STBSP__LEADINGZERO;
                    ++f;
                    goto flags_done;
                default: goto flags_done;
            }
        }
    flags_done:

        // get the field width
        if (f[0] == '*') {
            fw = va_arg(va, stbsp__uint32);
            ++f;
        } else {
            while ((f[0] >= '0') && (f[0] <= '9')) {
                fw = fw * 10 + f[0] - '0';
                f++;
            }
        }
        // get the precision
        if (f[0] == '.') {
            ++f;
            if (f[0] == '*') {
                pr = va_arg(va, stbsp__uint32);
                ++f;
            } else {
                pr = 0;
                while ((f[0] >= '0') && (f[0] <= '9')) {
                    pr = pr * 10 + f[0] - '0';
                    f++;
                }
            }
        }

        // handle integer size overrides
        switch (f[0]) {
            // are we halfwidth?
            case 'h':
                fl |= STBSP__HALFWIDTH;
                ++f;
                if (f[0] == 'h')
                    ++f;  // QUARTERWIDTH
                break;
            // are we 64-bit (unix style)
            case 'l':
                fl |= ((sizeof(long) == 8) ? STBSP__INTMAX : 0);
                ++f;
                if (f[0] == 'l') {
                    fl |= STBSP__INTMAX;
                    ++f;
                }
                break;
            // are we 64-bit on intmax? (c99)
            case 'j':
                fl |= (sizeof(size_t) == 8) ? STBSP__INTMAX : 0;
                ++f;
                break;
            // are we 64-bit on size_t or ptrdiff_t? (c99)
            case 'z':
                fl |= (sizeof(ptrdiff_t) == 8) ? STBSP__INTMAX : 0;
                ++f;
                break;
            case 't':
                fl |= (sizeof(ptrdiff_t) == 8) ? STBSP__INTMAX : 0;
                ++f;
                break;
            // are we 64-bit (msft style)
            case 'I':
                if ((f[1] == '6') && (f[2] == '4')) {
                    fl |= STBSP__INTMAX;
                    f += 3;
                } else if ((f[1] == '3') && (f[2] == '2')) {
                    f += 3;
                } else {
                    fl |= ((sizeof(void*) == 8) ? STBSP__INTMAX : 0);
                    ++f;
                }
                break;
            default: break;
        }

        // handle each replacement
        switch (f[0]) {
#define STBSP__NUMSZ 512  // big enough for e308 (with commas) or e-307
            char          num[STBSP__NUMSZ];
            char          lead[8];
            char          tail[8];
            char*         s;
            char const*   h;
            stbsp__uint32 l, n, cs;
            stbsp__uint64 n64;
#ifndef STB_SPRINTF_NOFLOAT
            double fv;
#endif
            stbsp__int32 dp;
            char const*  sn;

            case 's':
                // get the string
                s = va_arg(va, char*);
                if (s == 0)
                    s = (char*)"null";
                // get the length, limited to desired precision
                // always limit to ~0u chars since our counts are 32b
                l = stbsp__strlen_limited(s, (pr >= 0) ? (stbsp__uint32)pr : ~0u);
                lead[0] = 0;
                tail[0] = 0;
                pr = 0;
                dp = 0;
                cs = 0;
                // copy the string in
                goto scopy;

            case 'c':  // char
                // get the character
                s = num + STBSP__NUMSZ - 1;
                *s = (char)va_arg(va, int);
                l = 1;
                lead[0] = 0;
                tail[0] = 0;
                pr = 0;
                dp = 0;
                cs = 0;
                goto scopy;

            case 'n':  // weird write-bytes specifier
            {
                int* d = va_arg(va, int*);
                *d = tlen + (int)(bf - buf);
            } break;

#ifdef STB_SPRINTF_NOFLOAT
            case 'A':  // float
            case 'a':  // hex float
            case 'G':  // float
            case 'g':  // float
            case 'E':  // float
            case 'e':  // float
            case 'f':  // float
                va_arg(va, double);  // eat it
                s = (char*)"No float";
                l = 8;
                lead[0] = 0;
                tail[0] = 0;
                pr = 0;
                cs = 0;
                STBSP__NOTUSED(dp);
                goto scopy;
#else
            case 'A':  // hex float
            case 'a':  // hex float
                h = (f[0] == 'A') ? hexu : hex;
                fv = va_arg(va, double);
                if (pr == -1)
                    pr = 6;  // default is 6
                // read the double into a string
                if (stbsp__real_to_parts((stbsp__int64*)&n64, &dp, fv))
                    fl |= STBSP__NEGATIVE;

                s = num + 64;

                stbsp__lead_sign(fl, lead);

                if (dp == -1023)
                    dp = (n64) ? -1022 : 0;
                else
                    n64 |= (((stbsp__uint64)1) << 52);
                n64 <<= (64 - 56);
                if (pr < 15)
                    n64 += ((((stbsp__uint64)8) << 56) >> (pr * 4));
                    // add leading chars

#ifdef STB_SPRINTF_MSVC_MODE
                *s++ = '0';
                *s++ = 'x';
#else
                lead[1 + lead[0]] = '0';
                lead[2 + lead[0]] = 'x';
                lead[0] += 2;
#endif
                *s++ = h[(n64 >> 60) & 15];
                n64 <<= 4;
                if (pr)
                    *s++ = stbsp__period;
                sn = s;

                // print the bits
                n = pr;
                if (n > 13)
                    n = 13;
                if (pr > (stbsp__int32)n)
                    tz = pr - n;
                pr = 0;
                while (n--) {
                    *s++ = h[(n64 >> 60) & 15];
                    n64 <<= 4;
                }

                // print the expo
                tail[1] = h[17];
                if (dp < 0) {
                    tail[2] = '-';
                    dp = -dp;
                } else
                    tail[2] = '+';
                n = (dp >= 1000) ? 6 : ((dp >= 100) ? 5 : ((dp >= 10) ? 4 : 3));
                tail[0] = (char)n;
                for (;;) {
                    tail[n] = '0' + dp % 10;
                    if (n <= 3)
                        break;
                    --n;
                    dp /= 10;
                }

                dp = (int)(s - sn);
                l = (int)(s - (num + 64));
                s = num + 64;
                cs = 1 + (3 << 24);
                goto scopy;

            case 'G':  // float
            case 'g':  // float
                h = (f[0] == 'G') ? hexu : hex;
                fv = va_arg(va, double);
                if (pr == -1)
                    pr = 6;
                else if (pr == 0)
                    pr = 1;  // default is 6
                // read the double into a string
                if (stbsp__real_to_str(&sn, &l, num, &dp, fv, (pr - 1) | 0x80000000))
                    fl |= STBSP__NEGATIVE;

                // clamp the precision and delete extra zeros after clamp
                n = pr;
                if (l > (stbsp__uint32)pr)
                    l = pr;
                while ((l > 1) && (pr) && (sn[l - 1] == '0')) {
                    --pr;
                    --l;
                }

                // should we use %e
                if ((dp <= -4) || (dp > (stbsp__int32)n)) {
                    if (pr > (stbsp__int32)l)
                        pr = l - 1;
                    else if (pr)
                        --pr;  // when using %e, there is one digit before the decimal
                    goto doexpfromg;
                }
                // this is the insane action to get the pr to match %g semantics for %f
                if (dp > 0) {
                    pr = (dp < (stbsp__int32)l) ? l - dp : 0;
                } else {
                    pr = -dp + ((pr > (stbsp__int32)l) ? (stbsp__int32)l : pr);
                }
                goto dofloatfromg;

            case 'E':  // float
            case 'e':  // float
                h = (f[0] == 'E') ? hexu : hex;
                fv = va_arg(va, double);
                if (pr == -1)
                    pr = 6;  // default is 6
                // read the double into a string
                if (stbsp__real_to_str(&sn, &l, num, &dp, fv, pr | 0x80000000))
                    fl |= STBSP__NEGATIVE;
            doexpfromg:
                tail[0] = 0;
                stbsp__lead_sign(fl, lead);
                if (dp == STBSP__SPECIAL) {
                    s = (char*)sn;
                    cs = 0;
                    pr = 0;
                    goto scopy;
                }
                s = num + 64;
                // handle leading chars
                *s++ = sn[0];

                if (pr)
                    *s++ = stbsp__period;

                // handle after decimal
                if ((l - 1) > (stbsp__uint32)pr)
                    l = pr + 1;
                for (n = 1; n < l; n++)
                    *s++ = sn[n];
                // trailing zeros
                tz = pr - (l - 1);
                pr = 0;
                // dump expo
                tail[1] = h[0xe];
                dp -= 1;
                if (dp < 0) {
                    tail[2] = '-';
                    dp = -dp;
                } else
                    tail[2] = '+';
#ifdef STB_SPRINTF_MSVC_MODE
                n = 5;
#else
                n = (dp >= 100) ? 5 : 4;
#endif
                tail[0] = (char)n;
                for (;;) {
                    tail[n] = '0' + dp % 10;
                    if (n <= 3)
                        break;
                    --n;
                    dp /= 10;
                }
                cs = 1 + (3 << 24);  // how many tens
                goto flt_lead;

            case 'f':  // float
                fv = va_arg(va, double);
            doafloat:
                // do kilos
                if (fl & STBSP__METRIC_SUFFIX) {
                    double divisor;
                    divisor = 1000.0f;
                    if (fl & STBSP__METRIC_1024)
                        divisor = 1024.0;
                    while (fl < 0x4000000) {
                        if ((fv < divisor) && (fv > -divisor))
                            break;
                        fv /= divisor;
                        fl += 0x1000000;
                    }
                }
                if (pr == -1)
                    pr = 6;  // default is 6
                // read the double into a string
                if (stbsp__real_to_str(&sn, &l, num, &dp, fv, pr))
                    fl |= STBSP__NEGATIVE;
            dofloatfromg:
                tail[0] = 0;
                stbsp__lead_sign(fl, lead);
                if (dp == STBSP__SPECIAL) {
                    s = (char*)sn;
                    cs = 0;
                    pr = 0;
                    goto scopy;
                }
                s = num + 64;

                // handle the three decimal varieties
                if (dp <= 0) {
                    stbsp__int32 i;
                    // handle 0.000*000xxxx
                    *s++ = '0';
                    if (pr)
                        *s++ = stbsp__period;
                    n = -dp;
                    if ((stbsp__int32)n > pr)
                        n = pr;
                    i = n;
                    while (i) {
                        if ((((stbsp__uintptr)s) & 3) == 0)
                            break;
                        *s++ = '0';
                        --i;
                    }
                    while (i >= 4) {
                        *(stbsp__uint32*)s = 0x30303030;
                        s += 4;
                        i -= 4;
                    }
                    while (i) {
                        *s++ = '0';
                        --i;
                    }
                    if ((stbsp__int32)(l + n) > pr)
                        l = pr - n;
                    i = l;
                    while (i) {
                        *s++ = *sn++;
                        --i;
                    }
                    tz = pr - (n + l);
                    cs = 1 + (3 << 24);  // how many tens did we write (for commas below)
                } else {
                    cs = (fl & STBSP__TRIPLET_COMMA) ? ((600 - (stbsp__uint32)dp) % 3) : 0;
                    if ((stbsp__uint32)dp >= l) {
                        // handle xxxx000*000.0
                        n = 0;
                        for (;;) {
                            if ((fl & STBSP__TRIPLET_COMMA) && (++cs == 4)) {
                                cs = 0;
                                *s++ = stbsp__comma;
                            } else {
                                *s++ = sn[n];
                                ++n;
                                if (n >= l)
                                    break;
                            }
                        }
                        if (n < (stbsp__uint32)dp) {
                            n = dp - n;
                            if ((fl & STBSP__TRIPLET_COMMA) == 0) {
                                while (n) {
                                    if ((((stbsp__uintptr)s) & 3) == 0)
                                        break;
                                    *s++ = '0';
                                    --n;
                                }
                                while (n >= 4) {
                                    *(stbsp__uint32*)s = 0x30303030;
                                    s += 4;
                                    n -= 4;
                                }
                            }
                            while (n) {
                                if ((fl & STBSP__TRIPLET_COMMA) && (++cs == 4)) {
                                    cs = 0;
                                    *s++ = stbsp__comma;
                                } else {
                                    *s++ = '0';
                                    --n;
                                }
                            }
                        }
                        cs = (int)(s - (num + 64)) + (3 << 24);  // cs is how many tens
                        if (pr) {
                            *s++ = stbsp__period;
                            tz = pr;
                        }
                    } else {
                        // handle xxxxx.xxxx000*000
                        n = 0;
                        for (;;) {
                            if ((fl & STBSP__TRIPLET_COMMA) && (++cs == 4)) {
                                cs = 0;
                                *s++ = stbsp__comma;
                            } else {
                                *s++ = sn[n];
                                ++n;
                                if (n >= (stbsp__uint32)dp)
                                    break;
                            }
                        }
                        cs = (int)(s - (num + 64)) + (3 << 24);  // cs is how many tens
                        if (pr)
                            *s++ = stbsp__period;
                        if ((l - dp) > (stbsp__uint32)pr)
                            l = pr + dp;
                        while (n < l) {
                            *s++ = sn[n];
                            ++n;
                        }
                        tz = pr - (l - dp);
                    }
                }
                pr = 0;

                // handle k,m,g,t
                if (fl & STBSP__METRIC_SUFFIX) {
                    char idx;
                    idx = 1;
                    if (fl & STBSP__METRIC_NOSPACE)
                        idx = 0;
                    tail[0] = idx;
                    tail[1] = ' ';
                    {
                        if (fl >> 24) {  // SI kilo is 'k', JEDEC and SI kibits are 'K'.
                            if (fl & STBSP__METRIC_1024)
                                tail[idx + 1] = "_KMGT"[fl >> 24];
                            else
                                tail[idx + 1] = "_kMGT"[fl >> 24];
                            idx++;
                            // If printing kibits and not in jedec, add the 'i'.
                            if (fl & STBSP__METRIC_1024 && !(fl & STBSP__METRIC_JEDEC)) {
                                tail[idx + 1] = 'i';
                                idx++;
                            }
                            tail[0] = idx;
                        }
                    }
                };

            flt_lead:
                // get the length that we copied
                l = (stbsp__uint32)(s - (num + 64));
                s = num + 64;
                goto scopy;
#endif

            case 'B':  // upper binary
            case 'b':  // lower binary
                h = (f[0] == 'B') ? hexu : hex;
                lead[0] = 0;
                if (fl & STBSP__LEADING_0X) {
                    lead[0] = 2;
                    lead[1] = '0';
                    lead[2] = h[0xb];
                }
                l = (8 << 4) | (1 << 8);
                goto radixnum;

            case 'o':  // octal
                h = hexu;
                lead[0] = 0;
                if (fl & STBSP__LEADING_0X) {
                    lead[0] = 1;
                    lead[1] = '0';
                }
                l = (3 << 4) | (3 << 8);
                goto radixnum;

            case 'p':  // pointer
                fl |= (sizeof(void*) == 8) ? STBSP__INTMAX : 0;
                pr = sizeof(void*) * 2;
                fl &= ~STBSP__LEADINGZERO;  // 'p' only prints the pointer with zeros
                    // fall through - to X

            case 'X':  // upper hex
            case 'x':  // lower hex
                h = (f[0] == 'X') ? hexu : hex;
                l = (4 << 4) | (4 << 8);
                lead[0] = 0;
                if (fl & STBSP__LEADING_0X) {
                    lead[0] = 2;
                    lead[1] = '0';
                    lead[2] = h[16];
                }
            radixnum:
                // get the number
                if (fl & STBSP__INTMAX)
                    n64 = va_arg(va, stbsp__uint64);
                else
                    n64 = va_arg(va, stbsp__uint32);

                s = num + STBSP__NUMSZ;
                dp = 0;
                // clear tail, and clear leading if value is zero
                tail[0] = 0;
                if (n64 == 0) {
                    lead[0] = 0;
                    if (pr == 0) {
                        l = 0;
                        cs = 0;
                        goto scopy;
                    }
                }
                // convert to string
                for (;;) {
                    *--s = h[n64 & ((1 << (l >> 8)) - 1)];
                    n64 >>= (l >> 8);
                    if (!((n64) || ((stbsp__int32)((num + STBSP__NUMSZ) - s) < pr)))
                        break;
                    if (fl & STBSP__TRIPLET_COMMA) {
                        ++l;
                        if ((l & 15) == ((l >> 4) & 15)) {
                            l &= ~15;
                            *--s = stbsp__comma;
                        }
                    }
                };
                // get the tens and the comma pos
                cs = (stbsp__uint32)((num + STBSP__NUMSZ) - s) + ((((l >> 4) & 15)) << 24);
                // get the length that we copied
                l = (stbsp__uint32)((num + STBSP__NUMSZ) - s);
                // copy it
                goto scopy;

            case 'u':  // unsigned
            case 'i':
            case 'd':  // integer
                // get the integer and abs it
                if (fl & STBSP__INTMAX) {
                    stbsp__int64 i64 = va_arg(va, stbsp__int64);
                    n64 = (stbsp__uint64)i64;
                    if ((f[0] != 'u') && (i64 < 0)) {
                        n64 = (stbsp__uint64)-i64;
                        fl |= STBSP__NEGATIVE;
                    }
                } else {
                    stbsp__int32 i = va_arg(va, stbsp__int32);
                    n64 = (stbsp__uint32)i;
                    if ((f[0] != 'u') && (i < 0)) {
                        n64 = (stbsp__uint32)-i;
                        fl |= STBSP__NEGATIVE;
                    }
                }

#ifndef STB_SPRINTF_NOFLOAT
                if (fl & STBSP__METRIC_SUFFIX) {
                    if (n64 < 1024)
                        pr = 0;
                    else if (pr == -1)
                        pr = 1;
                    fv = (double)(stbsp__int64)n64;
                    goto doafloat;
                }
#endif

                // convert to string
                s = num + STBSP__NUMSZ;
                l = 0;

                for (;;) {
                    // do in 32-bit chunks (avoid lots of 64-bit divides even with constant denominators)
                    char* o = s - 8;
                    if (n64 >= 100000000) {
                        n = (stbsp__uint32)(n64 % 100000000);
                        n64 /= 100000000;
                    } else {
                        n = (stbsp__uint32)n64;
                        n64 = 0;
                    }
                    if ((fl & STBSP__TRIPLET_COMMA) == 0) {
                        do {
                            s -= 2;
                            *(stbsp__uint16*)s = *(stbsp__uint16*)&stbsp__digitpair.pair[(n % 100) * 2];
                            n /= 100;
                        } while (n);
                    }
                    while (n) {
                        if ((fl & STBSP__TRIPLET_COMMA) && (l++ == 3)) {
                            l = 0;
                            *--s = stbsp__comma;
                            --o;
                        } else {
                            *--s = (char)(n % 10) + '0';
                            n /= 10;
                        }
                    }
                    if (n64 == 0) {
                        if ((s[0] == '0') && (s != (num + STBSP__NUMSZ)))
                            ++s;
                        break;
                    }
                    while (s != o)
                        if ((fl & STBSP__TRIPLET_COMMA) && (l++ == 3)) {
                            l = 0;
                            *--s = stbsp__comma;
                            --o;
                        } else {
                            *--s = '0';
                        }
                }

                tail[0] = 0;
                stbsp__lead_sign(fl, lead);

                // get the length that we copied
                l = (stbsp__uint32)((num + STBSP__NUMSZ) - s);
                if (l == 0) {
                    *--s = '0';
                    l = 1;
                }
                cs = l + (3 << 24);
                if (pr < 0)
                    pr = 0;

            scopy:
                // get fw=leading/trailing space, pr=leading zeros
                if (pr < (stbsp__int32)l)
                    pr = l;
                n = pr + lead[0] + tail[0] + tz;
                if (fw < (stbsp__int32)n)
                    fw = n;
                fw -= n;
                pr -= l;

                // handle right justify and leading zeros
                if ((fl & STBSP__LEFTJUST) == 0) {
                    if (fl & STBSP__LEADINGZERO)  // if leading zeros, everything is in pr
                    {
                        pr = (fw > pr) ? fw : pr;
                        fw = 0;
                    } else {
                        fl &= ~STBSP__TRIPLET_COMMA;  // if no leading zeros, then no commas
                    }
                }

                // copy the spaces and/or zeros
                if (fw + pr) {
                    stbsp__int32  i;
                    stbsp__uint32 c;

                    // copy leading spaces (or when doing %8.4d stuff)
                    if ((fl & STBSP__LEFTJUST) == 0)
                        while (fw > 0) {
                            stbsp__cb_buf_clamp(i, fw);
                            fw -= i;
                            while (i) {
                                if ((((stbsp__uintptr)bf) & 3) == 0)
                                    break;
                                *bf++ = ' ';
                                --i;
                            }
                            while (i >= 4) {
                                *(stbsp__uint32*)bf = 0x20202020;
                                bf += 4;
                                i -= 4;
                            }
                            while (i) {
                                *bf++ = ' ';
                                --i;
                            }
                            stbsp__chk_cb_buf(1);
                        }

                    // copy leader
                    sn = lead + 1;
                    while (lead[0]) {
                        stbsp__cb_buf_clamp(i, lead[0]);
                        lead[0] -= (char)i;
                        while (i) {
                            *bf++ = *sn++;
                            --i;
                        }
                        stbsp__chk_cb_buf(1);
                    }

                    // copy leading zeros
                    c = cs >> 24;
                    cs &= 0xffffff;
                    cs = (fl & STBSP__TRIPLET_COMMA) ? ((stbsp__uint32)(c - ((pr + cs) % (c + 1)))) : 0;
                    while (pr > 0) {
                        stbsp__cb_buf_clamp(i, pr);
                        pr -= i;
                        if ((fl & STBSP__TRIPLET_COMMA) == 0) {
                            while (i) {
                                if ((((stbsp__uintptr)bf) & 3) == 0)
                                    break;
                                *bf++ = '0';
                                --i;
                            }
                            while (i >= 4) {
                                *(stbsp__uint32*)bf = 0x30303030;
                                bf += 4;
                                i -= 4;
                            }
                        }
                        while (i) {
                            if ((fl & STBSP__TRIPLET_COMMA) && (cs++ == c)) {
                                cs = 0;
                                *bf++ = stbsp__comma;
                            } else
                                *bf++ = '0';
                            --i;
                        }
                        stbsp__chk_cb_buf(1);
                    }
                }

                // copy leader if there is still one
                sn = lead + 1;
                while (lead[0]) {
                    stbsp__int32 i;
                    stbsp__cb_buf_clamp(i, lead[0]);
                    lead[0] -= (char)i;
                    while (i) {
                        *bf++ = *sn++;
                        --i;
                    }
                    stbsp__chk_cb_buf(1);
                }

                // copy the string
                n = l;
                while (n) {
                    stbsp__int32 i;
                    stbsp__cb_buf_clamp(i, n);
                    n -= i;
                    STBSP__UNALIGNED(while (i >= 4) {
                        *(stbsp__uint32 volatile*)bf = *(stbsp__uint32 volatile*)s;
                        bf += 4;
                        s += 4;
                        i -= 4;
                    })
                    while (i) {
                        *bf++ = *s++;
                        --i;
                    }
                    stbsp__chk_cb_buf(1);
                }

                // copy trailing zeros
                while (tz) {
                    stbsp__int32 i;
                    stbsp__cb_buf_clamp(i, tz);
                    tz -= i;
                    while (i) {
                        if ((((stbsp__uintptr)bf) & 3) == 0)
                            break;
                        *bf++ = '0';
                        --i;
                    }
                    while (i >= 4) {
                        *(stbsp__uint32*)bf = 0x30303030;
                        bf += 4;
                        i -= 4;
                    }
                    while (i) {
                        *bf++ = '0';
                        --i;
                    }
                    stbsp__chk_cb_buf(1);
                }

                // copy tail if there is one
                sn = tail + 1;
                while (tail[0]) {
                    stbsp__int32 i;
                    stbsp__cb_buf_clamp(i, tail[0]);
                    tail[0] -= (char)i;
                    while (i) {
                        *bf++ = *sn++;
                        --i;
                    }
                    stbsp__chk_cb_buf(1);
                }

                // handle the left justify
                if (fl & STBSP__LEFTJUST)
                    if (fw > 0) {
                        while (fw) {
                            stbsp__int32 i;
                            stbsp__cb_buf_clamp(i, fw);
                            fw -= i;
                            while (i) {
                                if ((((stbsp__uintptr)bf) & 3) == 0)
                                    break;
                                *bf++ = ' ';
                                --i;
                            }
                            while (i >= 4) {
                                *(stbsp__uint32*)bf = 0x20202020;
                                bf += 4;
                                i -= 4;
                            }
                            while (i--)
                                *bf++ = ' ';
                            stbsp__chk_cb_buf(1);
                        }
                    }
                break;

            default:  // unknown, just copy code
                s = num + STBSP__NUMSZ - 1;
                *s = f[0];
                l = 1;
                fw = fl = 0;
                lead[0] = 0;
                tail[0] = 0;
                pr = 0;
                dp = 0;
                cs = 0;
                goto scopy;
        }
        ++f;
    }
endfmt:

    if (!callback)
        *bf = 0;
    else
        stbsp__flush_cb();

done:
    return tlen + (int)(bf - buf);
}

// cleanup
#undef STBSP__LEFTJUST
#undef STBSP__LEADINGPLUS
#undef STBSP__LEADINGSPACE
#undef STBSP__LEADING_0X
#undef STBSP__LEADINGZERO
#undef STBSP__INTMAX
#undef STBSP__TRIPLET_COMMA
#undef STBSP__NEGATIVE
#undef STBSP__METRIC_SUFFIX
#undef STBSP__NUMSZ
#undef stbsp__chk_cb_bufL
#undef stbsp__chk_cb_buf
#undef stbsp__flush_cb
#undef stbsp__cb_buf_clamp

// ============================================================================
//   wrapper functions

STBSP__PUBLICDEF int
STB_SPRINTF_DECORATE(sprintf)(char* buf, char const* fmt, ...) {
    int     result;
    va_list va;
    va_start(va, fmt);
    result = STB_SPRINTF_DECORATE(vsprintfcb)(0, 0, buf, fmt, va);
    va_end(va);
    return result;
}

typedef struct stbsp__context {
    char* buf;
    int   count;
    int   length;
    char  tmp[STB_SPRINTF_MIN];
} stbsp__context;

static char*
stbsp__clamp_callback(const char* buf, void* user, int len) {
    stbsp__context* c = (stbsp__context*)user;
    c->length += len;

    if (len > c->count)
        len = c->count;

    if (len) {
        if (buf != c->buf) {
            const char *s, *se;
            char*       d;
            d = c->buf;
            s = buf;
            se = buf + len;
            do {
                *d++ = *s++;
            } while (s < se);
        }
        c->buf += len;
        c->count -= len;
    }

    if (c->count <= 0)
        return c->tmp;
    return (c->count >= STB_SPRINTF_MIN) ? c->buf : c->tmp;  // go direct into buffer if you can
}

static char*
stbsp__count_clamp_callback(const char* buf, void* user, int len) {
    stbsp__context* c = (stbsp__context*)user;
    (void)sizeof(buf);

    c->length += len;
    return c->tmp;  // go direct into buffer if you can
}

STBSP__PUBLICDEF int
STB_SPRINTF_DECORATE(vsnprintf)(char* buf, int count, char const* fmt, va_list va) {
    prb_assert(count >= 0);
    stbsp__context c;

    if ((count == 0) && !buf) {
        c.length = 0;

        STB_SPRINTF_DECORATE(vsprintfcb)
        (stbsp__count_clamp_callback, &c, c.tmp, fmt, va);
    } else {
        int l;

        c.buf = buf;
        c.count = count;
        c.length = 0;

        STB_SPRINTF_DECORATE(vsprintfcb)
        (stbsp__clamp_callback, &c, stbsp__clamp_callback(0, &c, 0), fmt, va);

        // zero-terminate
        l = (int)(c.buf - buf);
        if (l >= count)  // should never be greater, only equal (or less) than count
            l = count - 1;
        buf[l] = 0;
    }

    return c.length;
}

STBSP__PUBLICDEF int
STB_SPRINTF_DECORATE(snprintf)(char* buf, int count, char const* fmt, ...) {
    int     result;
    va_list va;
    va_start(va, fmt);

    result = STB_SPRINTF_DECORATE(vsnprintf)(buf, count, fmt, va);
    va_end(va);

    return result;
}

STBSP__PUBLICDEF int
STB_SPRINTF_DECORATE(vsprintf)(char* buf, char const* fmt, va_list va) {
    return STB_SPRINTF_DECORATE(vsprintfcb)(0, 0, buf, fmt, va);
}

// =======================================================================
//   low level float utility functions

#ifndef STB_SPRINTF_NOFLOAT

// copies d to bits w/ strict aliasing (this compiles to nothing on /Ox)
#define STBSP__COPYFP(dest, src) \
    { \
        int cn; \
        for (cn = 0; cn < 8; cn++) \
            ((char*)&dest)[cn] = ((char*)&src)[cn]; \
    }

// get float info
static stbsp__int32
stbsp__real_to_parts(stbsp__int64* bits, stbsp__int32* expo, double value) {
    double       d;
    stbsp__int64 b = 0;

    // load value and round at the frac_digits
    d = value;

    STBSP__COPYFP(b, d);

    *bits = b & ((((stbsp__uint64)1) << 52) - 1);
    *expo = (stbsp__int32)(((b >> 52) & 2047) - 1023);

    return (stbsp__int32)((stbsp__uint64)b >> 63);
}

// clang-format off
static double const stbsp__bot[23] = {1e+000, 1e+001, 1e+002, 1e+003, 1e+004, 1e+005, 1e+006, 1e+007,
                                      1e+008, 1e+009, 1e+010, 1e+011, 1e+012, 1e+013, 1e+014, 1e+015,
                                      1e+016, 1e+017, 1e+018, 1e+019, 1e+020, 1e+021, 1e+022};
static double const stbsp__negbot[22] = {1e-001, 1e-002, 1e-003, 1e-004, 1e-005, 1e-006, 1e-007, 1e-008,
                                         1e-009, 1e-010, 1e-011, 1e-012, 1e-013, 1e-014, 1e-015, 1e-016,
                                         1e-017, 1e-018, 1e-019, 1e-020, 1e-021, 1e-022};
static double const stbsp__negboterr[22] = {
    -5.551115123125783e-018,  -2.0816681711721684e-019, -2.0816681711721686e-020, -4.7921736023859299e-021,
    -8.1803053914031305e-022, 4.5251888174113741e-023,  4.5251888174113739e-024,  -2.0922560830128471e-025,
    -6.2281591457779853e-026, -3.6432197315497743e-027, 6.0503030718060191e-028,  2.0113352370744385e-029,
    -3.0373745563400371e-030, 1.1806906454401013e-032,  -7.7705399876661076e-032, 2.0902213275965398e-033,
    -7.1542424054621921e-034, -7.1542424054621926e-035, 2.4754073164739869e-036,  5.4846728545790429e-037,
    9.2462547772103625e-038,  -4.8596774326570872e-039};
static double const stbsp__top[13] =
    {1e+023, 1e+046, 1e+069, 1e+092, 1e+115, 1e+138, 1e+161, 1e+184, 1e+207, 1e+230, 1e+253, 1e+276, 1e+299};
static double const stbsp__negtop[13] =
    {1e-023, 1e-046, 1e-069, 1e-092, 1e-115, 1e-138, 1e-161, 1e-184, 1e-207, 1e-230, 1e-253, 1e-276, 1e-299};
static double const stbsp__toperr[13] = {
    8388608, 6.8601809640529717e+028, -7.253143638152921e+052,
    -4.3377296974619174e+075, -1.5559416129466825e+098, -3.2841562489204913e+121, -3.7745893248228135e+144,
    -1.7356668416969134e+167, -3.8893577551088374e+190, -9.9566444326005119e+213, 6.3641293062232429e+236,
    -5.2069140800249813e+259, -5.2504760255204387e+282};
static double const stbsp__negtoperr[13] = {
   3.9565301985100693e-040,  -2.299904345391321e-063,  3.6506201437945798e-086,  1.1875228833981544e-109,
   -5.0644902316928607e-132, -6.7156837247865426e-155, -2.812077463003139e-178,  -5.7778912386589953e-201,
   7.4997100559334532e-224,  -4.6439668915134491e-247, -6.3691100762962136e-270, -9.436808465446358e-293,
   8.0970921678014997e-317};
// clang-format on

#if defined(_MSC_VER) && (_MSC_VER <= 1200)
static stbsp__uint64 const stbsp__powten[20] = {
    1,
    10,
    100,
    1000,
    10000,
    100000,
    1000000,
    10000000,
    100000000,
    1000000000,
    10000000000,
    100000000000,
    1000000000000,
    10000000000000,
    100000000000000,
    1000000000000000,
    10000000000000000,
    100000000000000000,
    1000000000000000000,
    10000000000000000000U};
#define stbsp__tento19th ((stbsp__uint64)1000000000000000000)
#else
static stbsp__uint64 const stbsp__powten[20] = {
    1,
    10,
    100,
    1000,
    10000,
    100000,
    1000000,
    10000000,
    100000000,
    1000000000,
    10000000000ULL,
    100000000000ULL,
    1000000000000ULL,
    10000000000000ULL,
    100000000000000ULL,
    1000000000000000ULL,
    10000000000000000ULL,
    100000000000000000ULL,
    1000000000000000000ULL,
    10000000000000000000ULL};
#define stbsp__tento19th (1000000000000000000ULL)
#endif

#define stbsp__ddmulthi(oh, ol, xh, yh) \
    { \
        double       ahi = 0, alo, bhi = 0, blo; \
        stbsp__int64 bt; \
        oh = xh * yh; \
        STBSP__COPYFP(bt, xh); \
        bt &= ((~(stbsp__uint64)0) << 27); \
        STBSP__COPYFP(ahi, bt); \
        alo = xh - ahi; \
        STBSP__COPYFP(bt, yh); \
        bt &= ((~(stbsp__uint64)0) << 27); \
        STBSP__COPYFP(bhi, bt); \
        blo = yh - bhi; \
        ol = ((ahi * bhi - oh) + ahi * blo + alo * bhi) + alo * blo; \
    }

#define stbsp__ddtoS64(ob, xh, xl) \
    { \
        double ahi = 0, alo, vh, t; \
        ob = (stbsp__int64)xh; \
        vh = (double)ob; \
        ahi = (xh - vh); \
        t = (ahi - xh); \
        alo = (xh - (ahi - t)) - (vh + t); \
        ob += (stbsp__int64)(ahi + alo + xl); \
    }

#define stbsp__ddrenorm(oh, ol) \
    { \
        double s; \
        s = oh + ol; \
        ol = ol - (s - oh); \
        oh = s; \
    }

#define stbsp__ddmultlo(oh, ol, xh, xl, yh, yl) ol = ol + (xh * yl + xl * yh);

#define stbsp__ddmultlos(oh, ol, xh, yl) ol = ol + (xh * yl);

static void
stbsp__raise_to_power10(double* ohi, double* olo, double d, stbsp__int32 power)  // power can be -323 to +350
{
    double ph, pl;
    if ((power >= 0) && (power <= 22)) {
        stbsp__ddmulthi(ph, pl, d, stbsp__bot[power]);
    } else {
        stbsp__int32 e, et, eb;
        double       p2h, p2l;

        e = power;
        if (power < 0)
            e = -e;
        et = (e * 0x2c9) >> 14; /* %23 */
        if (et > 13)
            et = 13;
        eb = e - (et * 23);

        ph = d;
        pl = 0.0;
        if (power < 0) {
            if (eb) {
                --eb;
                stbsp__ddmulthi(ph, pl, d, stbsp__negbot[eb]);
                stbsp__ddmultlos(ph, pl, d, stbsp__negboterr[eb]);
            }
            if (et) {
                stbsp__ddrenorm(ph, pl);
                --et;
                stbsp__ddmulthi(p2h, p2l, ph, stbsp__negtop[et]);
                stbsp__ddmultlo(p2h, p2l, ph, pl, stbsp__negtop[et], stbsp__negtoperr[et]);
                ph = p2h;
                pl = p2l;
            }
        } else {
            if (eb) {
                e = eb;
                if (eb > 22)
                    eb = 22;
                e -= eb;
                stbsp__ddmulthi(ph, pl, d, stbsp__bot[eb]);
                if (e) {
                    stbsp__ddrenorm(ph, pl);
                    stbsp__ddmulthi(p2h, p2l, ph, stbsp__bot[e]);
                    stbsp__ddmultlos(p2h, p2l, stbsp__bot[e], pl);
                    ph = p2h;
                    pl = p2l;
                }
            }
            if (et) {
                stbsp__ddrenorm(ph, pl);
                --et;
                stbsp__ddmulthi(p2h, p2l, ph, stbsp__top[et]);
                stbsp__ddmultlo(p2h, p2l, ph, pl, stbsp__top[et], stbsp__toperr[et]);
                ph = p2h;
                pl = p2l;
            }
        }
    }
    stbsp__ddrenorm(ph, pl);
    *ohi = ph;
    *olo = pl;
}

// given a float value, returns the significant bits in bits, and the position of the
//   decimal point in decimal_pos.  +/-INF and NAN are specified by special values
//   returned in the decimal_pos parameter.
// frac_digits is absolute normally, but if you want from first significant digits (got %g and %e), or in 0x80000000
static stbsp__int32
stbsp__real_to_str(
    char const**   start,
    stbsp__uint32* len,
    char*          out,
    stbsp__int32*  decimal_pos,
    double         value,
    stbsp__uint32  frac_digits
) {
    double       d;
    stbsp__int64 bits = 0;
    stbsp__int32 expo, e, ng, tens;

    d = value;
    STBSP__COPYFP(bits, d);
    expo = (stbsp__int32)((bits >> 52) & 2047);
    ng = (stbsp__int32)((stbsp__uint64)bits >> 63);
    if (ng)
        d = -d;

    if (expo == 2047)  // is nan or inf?
    {
        *start = (bits & ((((stbsp__uint64)1) << 52) - 1)) ? "NaN" : "Inf";
        *decimal_pos = STBSP__SPECIAL;
        *len = 3;
        return ng;
    }

    if (expo == 0)  // is zero or denormal
    {
        if (((stbsp__uint64)bits << 1) == 0)  // do zero
        {
            *decimal_pos = 1;
            *start = out;
            out[0] = '0';
            *len = 1;
            return ng;
        }
        // find the right expo for denormals
        {
            stbsp__int64 v = ((stbsp__uint64)1) << 51;
            while ((bits & v) == 0) {
                --expo;
                v >>= 1;
            }
        }
    }

    // find the decimal exponent as well as the decimal bits of the value
    {
        double ph, pl;

        // log10 estimate - very specifically tweaked to hit or undershoot by no more than 1 of log10 of all expos 1..2046
        tens = expo - 1023;
        tens = (tens < 0) ? ((tens * 617) / 2048) : (((tens * 1233) / 4096) + 1);

        // move the significant bits into position and stick them into an int
        stbsp__raise_to_power10(&ph, &pl, d, 18 - tens);

        // get full as much precision from double-double as possible
        stbsp__ddtoS64(bits, ph, pl);

        // check if we undershot
        if (((stbsp__uint64)bits) >= stbsp__tento19th)
            ++tens;
    }

    // now do the rounding in integer land
    frac_digits = (frac_digits & 0x80000000) ? ((frac_digits & 0x7ffffff) + 1) : (tens + frac_digits);
    if ((frac_digits < 24)) {
        stbsp__uint32 dg = 1;
        if ((stbsp__uint64)bits >= stbsp__powten[9])
            dg = 10;
        while ((stbsp__uint64)bits >= stbsp__powten[dg]) {
            ++dg;
            if (dg == 20)
                goto noround;
        }
        if (frac_digits < dg) {
            stbsp__uint64 r;
            // add 0.5 at the right position and round
            e = dg - frac_digits;
            if ((stbsp__uint32)e >= 24)
                goto noround;
            r = stbsp__powten[e];
            bits = bits + (r / 2);
            if ((stbsp__uint64)bits >= stbsp__powten[dg])
                ++tens;
            bits /= r;
        }
    noround:;
    }

    // kill long trailing runs of zeros
    if (bits) {
        stbsp__uint32 n;
        for (;;) {
            if (bits <= 0xffffffff)
                break;
            if (bits % 1000)
                goto donez;
            bits /= 1000;
        }
        n = (stbsp__uint32)bits;
        while ((n % 1000) == 0)
            n /= 1000;
        bits = n;
    donez:;
    }

    // convert to string
    out += 64;
    e = 0;
    for (;;) {
        stbsp__uint32 n;
        char*         o = out - 8;
        // do the conversion in chunks of U32s (avoid most 64-bit divides, worth it, constant denomiators be damned)
        if (bits >= 100000000) {
            n = (stbsp__uint32)(bits % 100000000);
            bits /= 100000000;
        } else {
            n = (stbsp__uint32)bits;
            bits = 0;
        }
        while (n) {
            out -= 2;
            *(stbsp__uint16*)out = *(stbsp__uint16*)&stbsp__digitpair.pair[(n % 100) * 2];
            n /= 100;
            e += 2;
        }
        if (bits == 0) {
            if ((e) && (out[0] == '0')) {
                ++out;
                --e;
            }
            break;
        }
        while (out != o) {
            *--out = '0';
            ++e;
        }
    }

    *decimal_pos = tens;
    *start = out;
    *len = e;
    return ng;
}

#undef stbsp__ddmulthi
#undef stbsp__ddrenorm
#undef stbsp__ddmultlo
#undef stbsp__ddmultlos
#undef STBSP__SPECIAL
#undef STBSP__COPYFP

#endif  // STB_SPRINTF_NOFLOAT

// clean up
#undef stbsp__uint16
#undef stbsp__uint32
#undef stbsp__int32
#undef stbsp__uint64
#undef stbsp__int64
#undef STBSP__UNALIGNED

//
// SECTION stb ds (implementation)
//

#define STBDS_ASSERT(x) prb_assert(x)

#ifdef STBDS_STATISTICS
#define STBDS_STATS(x) x
size_t stbds_array_grow;
size_t stbds_hash_grow;
size_t stbds_hash_shrink;
size_t stbds_hash_rebuild;
size_t stbds_hash_probes;
size_t stbds_hash_alloc;
size_t stbds_rehash_probes;
size_t stbds_rehash_items;
#else
#define STBDS_STATS(x)
#endif

//
// stbds_arr implementation
//

//int *prev_allocs[65536];
//int num_prev;

STBDS__PUBLICDEF void*
stbds_arrgrowf(void* a, size_t elemsize, size_t addlen, size_t min_cap) {
    stbds_array_header temp = {};  // force debugging
    void*              b;
    size_t             min_len = stbds_arrlen(a) + addlen;
    (void)sizeof(temp);

    // compute the minimum capacity needed
    if (min_len > min_cap)
        min_cap = min_len;

    if (min_cap <= stbds_arrcap(a))
        return a;

    // increase needed capacity to guarantee O(1) amortized
    if (min_cap < 2 * stbds_arrcap(a))
        min_cap = 2 * stbds_arrcap(a);
    else if (min_cap < 4)
        min_cap = 4;

    //if (num_prev < 65536) if (a) prev_allocs[num_prev++] = (int *) ((char *) a+1);
    //if (num_prev == 2201)
    //  num_prev = num_prev;
    b = STBDS_REALLOC(NULL, (a) ? stbds_header(a) : 0, elemsize * min_cap + sizeof(stbds_array_header));
    //if (num_prev < 65536) prev_allocs[num_prev++] = (int *) (char *) b;
    b = (char*)b + sizeof(stbds_array_header);
    if (a == NULL) {
        stbds_header(b)->length = 0;
        stbds_header(b)->hash_table = 0;
        stbds_header(b)->temp = 0;
    } else {
        STBDS_STATS(++stbds_array_grow);
    }
    stbds_header(b)->capacity = min_cap;

    return b;
}

STBDS__PUBLICDEF void
stbds_arrfreef(void* a) {
    STBDS_FREE(NULL, stbds_header(a));
}

//
// stbds_hm hash table implementation
//

#ifdef STBDS_INTERNAL_SMALL_BUCKET
#define STBDS_BUCKET_LENGTH 4
#else
#define STBDS_BUCKET_LENGTH 8
#endif

#define STBDS_BUCKET_SHIFT (STBDS_BUCKET_LENGTH == 8 ? 3 : 2)
#define STBDS_BUCKET_MASK (STBDS_BUCKET_LENGTH - 1)
#define STBDS_CACHE_LINE_SIZE 64

#define STBDS_ALIGN_FWD(n, a) (((n) + (a)-1) & ~((a)-1))

typedef struct {
    size_t    hash[STBDS_BUCKET_LENGTH];
    ptrdiff_t index[STBDS_BUCKET_LENGTH];
} stbds_hash_bucket;  // in 32-bit, this is one 64-byte cache line; in 64-bit, each array is one 64-byte cache line

typedef struct {
    char*              temp_key;  // this MUST be the first field of the hash table
    size_t             slot_count;
    size_t             used_count;
    size_t             used_count_threshold;
    size_t             used_count_shrink_threshold;
    size_t             tombstone_count;
    size_t             tombstone_count_threshold;
    size_t             seed;
    size_t             slot_count_log2;
    stbds_string_arena string;
    stbds_hash_bucket* storage;  // not a separate allocation, just 64-byte aligned storage after this struct
} stbds_hash_index;

#define STBDS_INDEX_EMPTY -1
#define STBDS_INDEX_DELETED -2
#define STBDS_INDEX_IN_USE(x) ((x) >= 0)

#define STBDS_HASH_EMPTY 0
#define STBDS_HASH_DELETED 1

static size_t stbds_hash_seed = 0x31415926;

STBDS__PUBLICDEF void
stbds_rand_seed(size_t seed) {
    stbds_hash_seed = seed;
}

#define stbds_load_32_or_64(var, temp, v32, v64_hi, v64_lo) \
    temp = v64_lo ^ v32, temp <<= 16, temp <<= 16, temp >>= 16, temp >>= 16, /* discard if 32-bit */ \
        var = v64_hi, var <<= 16, var <<= 16, /* discard if 32-bit */ \
        var ^= temp ^ v32

#define STBDS_SIZE_T_BITS ((sizeof(size_t)) * 8)

static size_t
stbds_probe_position(size_t hash, size_t slot_count, size_t slot_log2) {
    size_t pos;
    STBDS_NOTUSED(slot_log2);
    pos = hash & (slot_count - 1);
#ifdef STBDS_INTERNAL_BUCKET_START
    pos &= ~STBDS_BUCKET_MASK;
#endif
    return pos;
}

static size_t
stbds_log2(size_t slot_count) {
    size_t n = 0;
    while (slot_count > 1) {
        slot_count >>= 1;
        ++n;
    }
    return n;
}

static stbds_hash_index*
stbds_make_hash_index(size_t slot_count, stbds_hash_index* ot) {
    stbds_hash_index* t;
    t = (stbds_hash_index*)STBDS_REALLOC(
        NULL,
        0,
        (slot_count >> STBDS_BUCKET_SHIFT) * sizeof(stbds_hash_bucket) + sizeof(stbds_hash_index)
            + STBDS_CACHE_LINE_SIZE - 1
    );
    t->storage = (stbds_hash_bucket*)STBDS_ALIGN_FWD((size_t)(t + 1), STBDS_CACHE_LINE_SIZE);
    t->slot_count = slot_count;
    t->slot_count_log2 = stbds_log2(slot_count);
    t->tombstone_count = 0;
    t->used_count = 0;

#if 0  // A1
  t->used_count_threshold        = slot_count*12/16; // if 12/16th of table is occupied, grow
  t->tombstone_count_threshold   = slot_count* 2/16; // if tombstones are 2/16th of table, rebuild
  t->used_count_shrink_threshold = slot_count* 4/16; // if table is only 4/16th full, shrink
#elif 1  // A2
    //t->used_count_threshold        = slot_count*12/16; // if 12/16th of table is occupied, grow
    //t->tombstone_count_threshold   = slot_count* 3/16; // if tombstones are 3/16th of table, rebuild
    //t->used_count_shrink_threshold = slot_count* 4/16; // if table is only 4/16th full, shrink

    // compute without overflowing
    t->used_count_threshold = slot_count - (slot_count >> 2);
    t->tombstone_count_threshold = (slot_count >> 3) + (slot_count >> 4);
    t->used_count_shrink_threshold = slot_count >> 2;

#elif 0  // B1
    t->used_count_threshold = slot_count * 13 / 16;  // if 13/16th of table is occupied, grow
    t->tombstone_count_threshold = slot_count * 2 / 16;  // if tombstones are 2/16th of table, rebuild
    t->used_count_shrink_threshold = slot_count * 5 / 16;  // if table is only 5/16th full, shrink
#else  // C1
    t->used_count_threshold = slot_count * 14 / 16;  // if 14/16th of table is occupied, grow
    t->tombstone_count_threshold = slot_count * 2 / 16;  // if tombstones are 2/16th of table, rebuild
    t->used_count_shrink_threshold = slot_count * 6 / 16;  // if table is only 6/16th full, shrink
#endif
    // Following statistics were measured on a Core i7-6700 @ 4.00Ghz, compiled with clang 7.0.1 -O2
    // Note that the larger tables have high variance as they were run fewer times
    //     A1            A2          B1           C1
    //    0.10ms :     0.10ms :     0.10ms :     0.11ms :      2,000 inserts creating 2K table
    //    0.96ms :     0.95ms :     0.97ms :     1.04ms :     20,000 inserts creating 20K table
    //   14.48ms :    14.46ms :    10.63ms :    11.00ms :    200,000 inserts creating 200K table
    //  195.74ms :   196.35ms :   203.69ms :   214.92ms :  2,000,000 inserts creating 2M table
    // 2193.88ms :  2209.22ms :  2285.54ms :  2437.17ms : 20,000,000 inserts creating 20M table
    //   65.27ms :    53.77ms :    65.33ms :    65.47ms : 500,000 inserts & deletes in 2K table
    //   72.78ms :    62.45ms :    71.95ms :    72.85ms : 500,000 inserts & deletes in 20K table
    //   89.47ms :    77.72ms :    96.49ms :    96.75ms : 500,000 inserts & deletes in 200K table
    //   97.58ms :    98.14ms :    97.18ms :    97.53ms : 500,000 inserts & deletes in 2M table
    //  118.61ms :   119.62ms :   120.16ms :   118.86ms : 500,000 inserts & deletes in 20M table
    //  192.11ms :   194.39ms :   196.38ms :   195.73ms : 500,000 inserts & deletes in 200M table

    if (slot_count <= STBDS_BUCKET_LENGTH)
        t->used_count_shrink_threshold = 0;
    // to avoid infinite loop, we need to guarantee that at least one slot is empty and will terminate probes
    STBDS_ASSERT(t->used_count_threshold + t->tombstone_count_threshold < t->slot_count);
    STBDS_STATS(++stbds_hash_alloc);
    if (ot) {
        t->string = ot->string;
        // reuse old seed so we can reuse old hashes so below "copy out old data" doesn't do any hashing
        t->seed = ot->seed;
    } else {
        size_t a, b, temp;
        prb_memset(&t->string, 0, sizeof(t->string));
        t->seed = stbds_hash_seed;
        // LCG
        // in 32-bit, a =          2147001325   b =  715136305
        // in 64-bit, a = 2862933555777941757   b = 3037000493
        stbds_load_32_or_64(a, temp, 2147001325, 0x27bb2ee6, 0x87b0b0fd);
        stbds_load_32_or_64(b, temp, 715136305, 0, 0xb504f32d);
        stbds_hash_seed = stbds_hash_seed * a + b;
    }

    {
        size_t i, j;
        for (i = 0; i < slot_count >> STBDS_BUCKET_SHIFT; ++i) {
            stbds_hash_bucket* b = &t->storage[i];
            for (j = 0; j < STBDS_BUCKET_LENGTH; ++j)
                b->hash[j] = STBDS_HASH_EMPTY;
            for (j = 0; j < STBDS_BUCKET_LENGTH; ++j)
                b->index[j] = STBDS_INDEX_EMPTY;
        }
    }

    // copy out the old data, if any
    if (ot) {
        size_t i, j;
        t->used_count = ot->used_count;
        for (i = 0; i < ot->slot_count >> STBDS_BUCKET_SHIFT; ++i) {
            stbds_hash_bucket* ob = &ot->storage[i];
            for (j = 0; j < STBDS_BUCKET_LENGTH; ++j) {
                if (STBDS_INDEX_IN_USE(ob->index[j])) {
                    size_t hash = ob->hash[j];
                    size_t pos = stbds_probe_position(hash, t->slot_count, t->slot_count_log2);
                    size_t step = STBDS_BUCKET_LENGTH;
                    STBDS_STATS(++stbds_rehash_items);
                    for (;;) {
                        size_t             limit, z;
                        stbds_hash_bucket* bucket;
                        bucket = &t->storage[pos >> STBDS_BUCKET_SHIFT];
                        STBDS_STATS(++stbds_rehash_probes);

                        for (z = pos & STBDS_BUCKET_MASK; z < STBDS_BUCKET_LENGTH; ++z) {
                            if (bucket->hash[z] == 0) {
                                bucket->hash[z] = hash;
                                bucket->index[z] = ob->index[j];
                                goto done;
                            }
                        }

                        limit = pos & STBDS_BUCKET_MASK;
                        for (z = 0; z < limit; ++z) {
                            if (bucket->hash[z] == 0) {
                                bucket->hash[z] = hash;
                                bucket->index[z] = ob->index[j];
                                goto done;
                            }
                        }

                        pos += step;  // quadratic probing
                        step += STBDS_BUCKET_LENGTH;
                        pos &= (t->slot_count - 1);
                    }
                }
            done:;
            }
        }
    }

    return t;
}

#define STBDS_ROTATE_LEFT(val, n) (((val) << (n)) | ((val) >> (STBDS_SIZE_T_BITS - (n))))
#define STBDS_ROTATE_RIGHT(val, n) (((val) >> (n)) | ((val) << (STBDS_SIZE_T_BITS - (n))))

STBDS__PUBLICDEF size_t
stbds_hash_string(char* str, size_t seed) {
    size_t hash = seed;
    while (*str)
        hash = STBDS_ROTATE_LEFT(hash, 9) + (unsigned char)*str++;

    // Thomas Wang 64-to-32 bit mix function, hopefully also works in 32 bits
    hash ^= seed;
    hash = (~hash) + (hash << 18);
    hash ^= hash ^ STBDS_ROTATE_RIGHT(hash, 31);
    hash = hash * 21;
    hash ^= hash ^ STBDS_ROTATE_RIGHT(hash, 11);
    hash += (hash << 6);
    hash ^= STBDS_ROTATE_RIGHT(hash, 22);
    return hash + seed;
}

#ifdef STBDS_SIPHASH_2_4
#define STBDS_SIPHASH_C_ROUNDS 2
#define STBDS_SIPHASH_D_ROUNDS 4
typedef int STBDS_SIPHASH_2_4_can_only_be_used_in_64_bit_builds[sizeof(size_t) == 8 ? 1 : -1];
#endif

#ifndef STBDS_SIPHASH_C_ROUNDS
#define STBDS_SIPHASH_C_ROUNDS 1
#endif
#ifndef STBDS_SIPHASH_D_ROUNDS
#define STBDS_SIPHASH_D_ROUNDS 1
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127)  // conditional expression is constant, for do..while(0) and sizeof()==
#endif

static size_t
stbds_siphash_bytes(void* p, size_t len, size_t seed) {
    unsigned char* d = (unsigned char*)p;
    size_t         i, j;
    size_t         v0, v1, v2, v3, data;

    // hash that works on 32- or 64-bit registers without knowing which we have
    // (computes different results on 32-bit and 64-bit platform)
    // derived from siphash, but on 32-bit platforms very different as it uses 4 32-bit state not 4 64-bit
    v0 = ((((size_t)0x736f6d65 << 16) << 16) + 0x70736575) ^ seed;
    v1 = ((((size_t)0x646f7261 << 16) << 16) + 0x6e646f6d) ^ ~seed;
    v2 = ((((size_t)0x6c796765 << 16) << 16) + 0x6e657261) ^ seed;
    v3 = ((((size_t)0x74656462 << 16) << 16) + 0x79746573) ^ ~seed;

#ifdef STBDS_TEST_SIPHASH_2_4
    // hardcoded with key material in the siphash test vectors
    v0 ^= 0x0706050403020100ull ^ seed;
    v1 ^= 0x0f0e0d0c0b0a0908ull ^ ~seed;
    v2 ^= 0x0706050403020100ull ^ seed;
    v3 ^= 0x0f0e0d0c0b0a0908ull ^ ~seed;
#endif

#define STBDS_SIPROUND() \
    do { \
        v0 += v1; \
        v1 = STBDS_ROTATE_LEFT(v1, 13); \
        v1 ^= v0; \
        v0 = STBDS_ROTATE_LEFT(v0, STBDS_SIZE_T_BITS / 2); \
        v2 += v3; \
        v3 = STBDS_ROTATE_LEFT(v3, 16); \
        v3 ^= v2; \
        v2 += v1; \
        v1 = STBDS_ROTATE_LEFT(v1, 17); \
        v1 ^= v2; \
        v2 = STBDS_ROTATE_LEFT(v2, STBDS_SIZE_T_BITS / 2); \
        v0 += v3; \
        v3 = STBDS_ROTATE_LEFT(v3, 21); \
        v3 ^= v0; \
    } while (0)

    for (i = 0; i + sizeof(size_t) <= len; i += sizeof(size_t), d += sizeof(size_t)) {
        data = d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
        data |= (size_t)(d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24)) << 16 << 16;  // discarded if size_t == 4

        v3 ^= data;
        for (j = 0; j < STBDS_SIPHASH_C_ROUNDS; ++j)
            STBDS_SIPROUND();
        v0 ^= data;
    }
    data = len << (STBDS_SIZE_T_BITS - 8);
    switch (len - i) {
        case 7: data |= ((size_t)d[6] << 24) << 24;  // fall through
        case 6: data |= ((size_t)d[5] << 20) << 20;  // fall through
        case 5: data |= ((size_t)d[4] << 16) << 16;  // fall through
        case 4: data |= (d[3] << 24);  // fall through
        case 3: data |= (d[2] << 16);  // fall through
        case 2: data |= (d[1] << 8);  // fall through
        case 1: data |= d[0];  // fall through
        case 0: break;
    }
    v3 ^= data;
    for (j = 0; j < STBDS_SIPHASH_C_ROUNDS; ++j)
        STBDS_SIPROUND();
    v0 ^= data;
    v2 ^= 0xff;
    for (j = 0; j < STBDS_SIPHASH_D_ROUNDS; ++j)
        STBDS_SIPROUND();

#ifdef STBDS_SIPHASH_2_4
    return v0 ^ v1 ^ v2 ^ v3;
#else
    return v1 ^ v2
        ^ v3;  // slightly stronger since v0^v3 in above cancels out final round operation? I tweeted at the authors of SipHash about this but they didn't reply
#endif
}

STBDS__PUBLICDEF size_t
stbds_hash_bytes(void* p, size_t len, size_t seed) {
#ifdef STBDS_SIPHASH_2_4
    return stbds_siphash_bytes(p, len, seed);
#else
    unsigned char* d = (unsigned char*)p;

    if (len == 4) {
        unsigned int hash = d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
#if 0
    // HASH32-A  Bob Jenkin's hash function w/o large constants
    hash ^= seed;
    hash -= (hash<<6);
    hash ^= (hash>>17);
    hash -= (hash<<9);
    hash ^= seed;
    hash ^= (hash<<4);
    hash -= (hash<<3);
    hash ^= (hash<<10);
    hash ^= (hash>>15);
#elif 1
        // HASH32-BB  Bob Jenkin's presumably-accidental version of Thomas Wang hash with rotates turned into shifts.
        // Note that converting these back to rotates makes it run a lot slower, presumably due to collisions, so I'm
        // not really sure what's going on.
        hash ^= seed;
        hash = (hash ^ 61) ^ (hash >> 16);
        hash = hash + (hash << 3);
        hash = hash ^ (hash >> 4);
        hash = hash * 0x27d4eb2d;
        hash ^= seed;
        hash = hash ^ (hash >> 15);
#else  // HASH32-C   -  Murmur3
        hash ^= seed;
        hash *= 0xcc9e2d51;
        hash = (hash << 17) | (hash >> 15);
        hash *= 0x1b873593;
        hash ^= seed;
        hash = (hash << 19) | (hash >> 13);
        hash = hash * 5 + 0xe6546b64;
        hash ^= hash >> 16;
        hash *= 0x85ebca6b;
        hash ^= seed;
        hash ^= hash >> 13;
        hash *= 0xc2b2ae35;
        hash ^= hash >> 16;
#endif
        // Following statistics were measured on a Core i7-6700 @ 4.00Ghz, compiled with clang 7.0.1 -O2
        // Note that the larger tables have high variance as they were run fewer times
        //  HASH32-A   //  HASH32-BB  //  HASH32-C
        //    0.10ms   //    0.10ms   //    0.10ms :      2,000 inserts creating 2K table
        //    0.96ms   //    0.95ms   //    0.99ms :     20,000 inserts creating 20K table
        //   14.69ms   //   14.43ms   //   14.97ms :    200,000 inserts creating 200K table
        //  199.99ms   //  195.36ms   //  202.05ms :  2,000,000 inserts creating 2M table
        // 2234.84ms   // 2187.74ms   // 2240.38ms : 20,000,000 inserts creating 20M table
        //   55.68ms   //   53.72ms   //   57.31ms : 500,000 inserts & deletes in 2K table
        //   63.43ms   //   61.99ms   //   65.73ms : 500,000 inserts & deletes in 20K table
        //   80.04ms   //   77.96ms   //   81.83ms : 500,000 inserts & deletes in 200K table
        //  100.42ms   //   97.40ms   //  102.39ms : 500,000 inserts & deletes in 2M table
        //  119.71ms   //  120.59ms   //  121.63ms : 500,000 inserts & deletes in 20M table
        //  185.28ms   //  195.15ms   //  187.74ms : 500,000 inserts & deletes in 200M table
        //   15.58ms   //   14.79ms   //   15.52ms : 200,000 inserts creating 200K table with varying key spacing

        return (((size_t)hash << 16 << 16) | hash) ^ seed;
    } else if (len == 8 && sizeof(size_t) == 8) {
        size_t hash = d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
        hash |= (size_t)(d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24)) << 16 << 16;  // avoid warning if size_t == 4
        hash ^= seed;
        hash = (~hash) + (hash << 21);
        hash ^= STBDS_ROTATE_RIGHT(hash, 24);
        hash *= 265;
        hash ^= STBDS_ROTATE_RIGHT(hash, 14);
        hash ^= seed;
        hash *= 21;
        hash ^= STBDS_ROTATE_RIGHT(hash, 28);
        hash += (hash << 31);
        hash = (~hash) + (hash << 18);
        return hash;
    } else {
        return stbds_siphash_bytes(p, len, seed);
    }
#endif
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

static int
stbds_is_key_equal(void* a, size_t elemsize, void* key, size_t keysize, size_t keyoffset, int mode, size_t i) {
    if (mode >= STBDS_HM_STRING)
        return 0 == prb_strcmp((char*)key, *(char**)((char*)a + elemsize * i + keyoffset));
    else
        return prb_memeq(key, (char*)a + elemsize * i + keyoffset, keysize);
}

#define STBDS_HASH_TO_ARR(x, elemsize) ((char*)(x) - (elemsize))
#define STBDS_ARR_TO_HASH(x, elemsize) ((char*)(x) + (elemsize))

#define stbds_hash_table(a) ((stbds_hash_index*)stbds_header(a)->hash_table)

STBDS__PUBLICDEF void
stbds_hmfree_func(void* a, size_t elemsize) {
    STBDS_NOTUSED(elemsize);
    if (a == NULL)
        return;
    if (stbds_hash_table(a) != NULL) {
        if (stbds_hash_table(a)->string.mode == STBDS_SH_STRDUP) {
            size_t i;
            // skip 0th element, which is default
            for (i = 1; i < stbds_header(a)->length; ++i)
                STBDS_FREE(NULL, *(char**)((char*)a + elemsize * i));
        }
        stbds_strreset(&stbds_hash_table(a)->string);
    }
    STBDS_FREE(NULL, stbds_header(a)->hash_table);
    STBDS_FREE(NULL, stbds_header(a));
}

static ptrdiff_t
stbds_hm_find_slot(void* a, size_t elemsize, void* key, size_t keysize, size_t keyoffset, int mode) {
    void*              raw_a = STBDS_HASH_TO_ARR(a, elemsize);
    stbds_hash_index*  table = stbds_hash_table(raw_a);
    size_t             hash = mode >= STBDS_HM_STRING ? stbds_hash_string((char*)key, table->seed)
                                                      : stbds_hash_bytes(key, keysize, table->seed);
    size_t             step = STBDS_BUCKET_LENGTH;
    size_t             limit, i;
    size_t             pos;
    stbds_hash_bucket* bucket;

    if (hash < 2)
        hash += 2;  // stored hash values are forbidden from being 0, so we can detect empty slots

    pos = stbds_probe_position(hash, table->slot_count, table->slot_count_log2);

    for (;;) {
        STBDS_STATS(++stbds_hash_probes);
        bucket = &table->storage[pos >> STBDS_BUCKET_SHIFT];

        // start searching from pos to end of bucket, this should help performance on small hash tables that fit in cache
        for (i = pos & STBDS_BUCKET_MASK; i < STBDS_BUCKET_LENGTH; ++i) {
            if (bucket->hash[i] == hash) {
                if (stbds_is_key_equal(a, elemsize, key, keysize, keyoffset, mode, bucket->index[i])) {
                    return (pos & ~STBDS_BUCKET_MASK) + i;
                }
            } else if (bucket->hash[i] == STBDS_HASH_EMPTY) {
                return -1;
            }
        }

        // search from beginning of bucket to pos
        limit = pos & STBDS_BUCKET_MASK;
        for (i = 0; i < limit; ++i) {
            if (bucket->hash[i] == hash) {
                if (stbds_is_key_equal(a, elemsize, key, keysize, keyoffset, mode, bucket->index[i])) {
                    return (pos & ~STBDS_BUCKET_MASK) + i;
                }
            } else if (bucket->hash[i] == STBDS_HASH_EMPTY) {
                return -1;
            }
        }

        // quadratic probing
        pos += step;
        step += STBDS_BUCKET_LENGTH;
        pos &= (table->slot_count - 1);
    }
    /* NOTREACHED */
}

STBDS__PUBLICDEF void*
stbds_hmget_key_ts(void* a, size_t elemsize, void* key, size_t keysize, ptrdiff_t* temp, int mode) {
    size_t keyoffset = 0;
    if (a == NULL) {
        // make it non-empty so we can return a temp
        a = stbds_arrgrowf(0, elemsize, 0, 1);
        stbds_header(a)->length += 1;
        prb_memset(a, 0, elemsize);
        *temp = STBDS_INDEX_EMPTY;
        // adjust a to point after the default element
        return STBDS_ARR_TO_HASH(a, elemsize);
    } else {
        stbds_hash_index* table;
        void*             raw_a = STBDS_HASH_TO_ARR(a, elemsize);
        // adjust a to point to the default element
        table = (stbds_hash_index*)stbds_header(raw_a)->hash_table;
        if (table == 0) {
            *temp = -1;
        } else {
            ptrdiff_t slot = stbds_hm_find_slot(a, elemsize, key, keysize, keyoffset, mode);
            if (slot < 0) {
                *temp = STBDS_INDEX_EMPTY;
            } else {
                stbds_hash_bucket* b = &table->storage[slot >> STBDS_BUCKET_SHIFT];
                *temp = b->index[slot & STBDS_BUCKET_MASK];
            }
        }
        return a;
    }
}

STBDS__PUBLICDEF void*
stbds_hmget_key(void* a, size_t elemsize, void* key, size_t keysize, int mode) {
    ptrdiff_t temp;
    void*     p = stbds_hmget_key_ts(a, elemsize, key, keysize, &temp, mode);
    stbds_temp(STBDS_HASH_TO_ARR(p, elemsize)) = temp;
    return p;
}

STBDS__PUBLICDEF void*
stbds_hmput_default(void* a, size_t elemsize) {
    // three cases:
    //   a is NULL <- allocate
    //   a has a hash table but no entries, because of shmode <- grow
    //   a has entries <- do nothing
    if (a == NULL || stbds_header(STBDS_HASH_TO_ARR(a, elemsize))->length == 0) {
        a = stbds_arrgrowf(a ? STBDS_HASH_TO_ARR(a, elemsize) : NULL, elemsize, 0, 1);
        stbds_header(a)->length += 1;
        prb_memset(a, 0, elemsize);
        a = STBDS_ARR_TO_HASH(a, elemsize);
    }
    return a;
}

static char* stbds_strdup(char* str);

STBDS__PUBLICDEF void*
stbds_hmput_key(void* a, size_t elemsize, void* key, size_t keysize, int mode) {
    size_t            keyoffset = 0;
    void*             raw_a;
    stbds_hash_index* table;

    if (a == NULL) {
        a = stbds_arrgrowf(0, elemsize, 0, 1);
        prb_memset(a, 0, elemsize);
        stbds_header(a)->length += 1;
        // adjust a to point AFTER the default element
        a = STBDS_ARR_TO_HASH(a, elemsize);
    }

    // adjust a to point to the default element
    raw_a = a;
    a = STBDS_HASH_TO_ARR(a, elemsize);

    table = (stbds_hash_index*)stbds_header(a)->hash_table;

    if (table == NULL || table->used_count >= table->used_count_threshold) {
        stbds_hash_index* nt;
        size_t            slot_count;

        slot_count = (table == NULL) ? STBDS_BUCKET_LENGTH : table->slot_count * 2;
        nt = stbds_make_hash_index(slot_count, table);
        if (table)
            STBDS_FREE(NULL, table);
        else
            nt->string.mode = mode >= STBDS_HM_STRING ? STBDS_SH_DEFAULT : 0;
        stbds_header(a)->hash_table = table = nt;
        STBDS_STATS(++stbds_hash_grow);
    }

    // we iterate hash table explicitly because we want to track if we saw a tombstone
    {
        size_t             hash = mode >= STBDS_HM_STRING ? stbds_hash_string((char*)key, table->seed)
                                                          : stbds_hash_bytes(key, keysize, table->seed);
        size_t             step = STBDS_BUCKET_LENGTH;
        size_t             pos;
        ptrdiff_t          tombstone = -1;
        stbds_hash_bucket* bucket;

        // stored hash values are forbidden from being 0, so we can detect empty slots to early out quickly
        if (hash < 2)
            hash += 2;

        pos = stbds_probe_position(hash, table->slot_count, table->slot_count_log2);

        for (;;) {
            size_t limit, i;
            STBDS_STATS(++stbds_hash_probes);
            bucket = &table->storage[pos >> STBDS_BUCKET_SHIFT];

            // start searching from pos to end of bucket
            for (i = pos & STBDS_BUCKET_MASK; i < STBDS_BUCKET_LENGTH; ++i) {
                if (bucket->hash[i] == hash) {
                    if (stbds_is_key_equal(raw_a, elemsize, key, keysize, keyoffset, mode, bucket->index[i])) {
                        stbds_temp(a) = bucket->index[i];
                        if (mode >= STBDS_HM_STRING)
                            stbds_temp_key(a) = *(char**)((char*)raw_a + elemsize * bucket->index[i] + keyoffset);
                        return STBDS_ARR_TO_HASH(a, elemsize);
                    }
                } else if (bucket->hash[i] == 0) {
                    pos = (pos & ~STBDS_BUCKET_MASK) + i;
                    goto found_empty_slot;
                } else if (tombstone < 0) {
                    if (bucket->index[i] == STBDS_INDEX_DELETED)
                        tombstone = (ptrdiff_t)((pos & ~STBDS_BUCKET_MASK) + i);
                }
            }

            // search from beginning of bucket to pos
            limit = pos & STBDS_BUCKET_MASK;
            for (i = 0; i < limit; ++i) {
                if (bucket->hash[i] == hash) {
                    if (stbds_is_key_equal(raw_a, elemsize, key, keysize, keyoffset, mode, bucket->index[i])) {
                        stbds_temp(a) = bucket->index[i];
                        return STBDS_ARR_TO_HASH(a, elemsize);
                    }
                } else if (bucket->hash[i] == 0) {
                    pos = (pos & ~STBDS_BUCKET_MASK) + i;
                    goto found_empty_slot;
                } else if (tombstone < 0) {
                    if (bucket->index[i] == STBDS_INDEX_DELETED)
                        tombstone = (ptrdiff_t)((pos & ~STBDS_BUCKET_MASK) + i);
                }
            }

            // quadratic probing
            pos += step;
            step += STBDS_BUCKET_LENGTH;
            pos &= (table->slot_count - 1);
        }
    found_empty_slot:
        if (tombstone >= 0) {
            pos = tombstone;
            --table->tombstone_count;
        }
        ++table->used_count;

        {
            ptrdiff_t i = (ptrdiff_t)stbds_arrlen(a);
            // we want to do stbds_arraddn(1), but we can't use the macros since we don't have something of the right type
            if ((size_t)i + 1 > stbds_arrcap(a))
                *(void**)&a = stbds_arrgrowf(a, elemsize, 1, 0);
            raw_a = STBDS_ARR_TO_HASH(a, elemsize);
            prb_unused(raw_a);

            STBDS_ASSERT((size_t)i + 1 <= stbds_arrcap(a));
            stbds_header(a)->length = i + 1;
            bucket = &table->storage[pos >> STBDS_BUCKET_SHIFT];
            bucket->hash[pos & STBDS_BUCKET_MASK] = hash;
            bucket->index[pos & STBDS_BUCKET_MASK] = i - 1;
            stbds_temp(a) = i - 1;

            switch (table->string.mode) {
                case STBDS_SH_STRDUP:
                    stbds_temp_key(a) = *(char**)((char*)a + elemsize * i) = stbds_strdup((char*)key);
                    break;
                case STBDS_SH_ARENA:
                    stbds_temp_key(a) = *(char**)((char*)a + elemsize * i) = stbds_stralloc(&table->string, (char*)key);
                    break;
                case STBDS_SH_DEFAULT: stbds_temp_key(a) = *(char**)((char*)a + elemsize * i) = (char*)key; break;
                default: prb_memcpy((char*)a + elemsize * i, key, keysize); break;
            }
        }
        return STBDS_ARR_TO_HASH(a, elemsize);
    }
}

STBDS__PUBLICDEF void*
stbds_shmode_func(size_t elemsize, int mode) {
    void*             a = stbds_arrgrowf(0, elemsize, 0, 1);
    stbds_hash_index* h;
    prb_memset(a, 0, elemsize);
    stbds_header(a)->length = 1;
    stbds_header(a)->hash_table = h = (stbds_hash_index*)stbds_make_hash_index(STBDS_BUCKET_LENGTH, NULL);
    h->string.mode = (unsigned char)mode;
    return STBDS_ARR_TO_HASH(a, elemsize);
}

STBDS__PUBLICDEF void*
stbds_hmdel_key(void* a, size_t elemsize, void* key, size_t keysize, size_t keyoffset, int mode) {
    if (a == NULL) {
        return 0;
    } else {
        stbds_hash_index* table;
        void*             raw_a = STBDS_HASH_TO_ARR(a, elemsize);
        table = (stbds_hash_index*)stbds_header(raw_a)->hash_table;
        stbds_temp(raw_a) = 0;
        if (table == 0) {
            return a;
        } else {
            ptrdiff_t slot;
            slot = stbds_hm_find_slot(a, elemsize, key, keysize, keyoffset, mode);
            if (slot < 0)
                return a;
            else {
                stbds_hash_bucket* b = &table->storage[slot >> STBDS_BUCKET_SHIFT];
                int                i = slot & STBDS_BUCKET_MASK;
                ptrdiff_t          old_index = b->index[i];
                ptrdiff_t          final_index =
                    (ptrdiff_t)stbds_arrlen(raw_a) - 1 - 1;  // minus one for the raw_a vs a, and minus one for 'last'
                STBDS_ASSERT(slot < (ptrdiff_t)table->slot_count);
                --table->used_count;
                ++table->tombstone_count;
                stbds_temp(raw_a) = 1;
                // STBDS_ASSERT(table->used_count >= 0);
                //STBDS_ASSERT(table->tombstone_count < table->slot_count/4);
                b->hash[i] = STBDS_HASH_DELETED;
                b->index[i] = STBDS_INDEX_DELETED;

                if (mode == STBDS_HM_STRING && table->string.mode == STBDS_SH_STRDUP) {
                    STBDS_FREE(NULL, *(char**)((char*)a + elemsize * old_index));
                }

                // if indices are the same, memcpy is a no-op, but back-pointer-fixup will fail, so skip
                if (old_index != final_index) {
                    // swap delete
                    prb_memmove((char*)a + elemsize * old_index, (char*)a + elemsize * final_index, elemsize);

                    // now find the slot for the last element
                    if (mode == STBDS_HM_STRING)
                        slot = stbds_hm_find_slot(
                            a,
                            elemsize,
                            *(char**)((char*)a + elemsize * old_index + keyoffset),
                            keysize,
                            keyoffset,
                            mode
                        );
                    else
                        slot = stbds_hm_find_slot(
                            a,
                            elemsize,
                            (char*)a + elemsize * old_index + keyoffset,
                            keysize,
                            keyoffset,
                            mode
                        );
                    STBDS_ASSERT(slot >= 0);
                    b = &table->storage[slot >> STBDS_BUCKET_SHIFT];
                    i = slot & STBDS_BUCKET_MASK;
                    STBDS_ASSERT(b->index[i] == final_index);
                    b->index[i] = old_index;
                }
                stbds_header(raw_a)->length -= 1;

                if (table->used_count < table->used_count_shrink_threshold && table->slot_count > STBDS_BUCKET_LENGTH) {
                    stbds_header(raw_a)->hash_table = stbds_make_hash_index(table->slot_count >> 1, table);
                    STBDS_FREE(NULL, table);
                    STBDS_STATS(++stbds_hash_shrink);
                } else if (table->tombstone_count > table->tombstone_count_threshold) {
                    stbds_header(raw_a)->hash_table = stbds_make_hash_index(table->slot_count, table);
                    STBDS_FREE(NULL, table);
                    STBDS_STATS(++stbds_hash_rebuild);
                }

                return a;
            }
        }
    }
    /* NOTREACHED */
}

static char*
stbds_strdup(char* str) {
    // to keep replaceable allocator simple, we don't want to use strdup.
    // rolling our own also avoids problem of strdup vs _strdup
    size_t len = prb_strlen(str) + 1;
    char*  p = (char*)STBDS_REALLOC(NULL, 0, len);
    prb_memmove(p, str, len);
    return p;
}

#ifndef STBDS_STRING_ARENA_BLOCKSIZE_MIN
#define STBDS_STRING_ARENA_BLOCKSIZE_MIN 512u
#endif
#ifndef STBDS_STRING_ARENA_BLOCKSIZE_MAX
#define STBDS_STRING_ARENA_BLOCKSIZE_MAX (1u << 20)
#endif

STBDS__PUBLICDEF char*
stbds_stralloc(stbds_string_arena* a, char* str) {
    char*  p;
    size_t len = prb_strlen(str) + 1;
    if (len > a->remaining) {
        // compute the next blocksize
        size_t blocksize = a->block;

        // size is 512, 512, 1024, 1024, 2048, 2048, 4096, 4096, etc., so that
        // there are log(SIZE) allocations to free when we destroy the table
        blocksize = (size_t)(STBDS_STRING_ARENA_BLOCKSIZE_MIN) << (blocksize >> 1);

        // if size is under 1M, advance to next blocktype
        if (blocksize < (size_t)(STBDS_STRING_ARENA_BLOCKSIZE_MAX))
            ++a->block;

        if (len > blocksize) {
            // if string is larger than blocksize, then just allocate the full size.
            // note that we still advance string_block so block size will continue
            // increasing, so e.g. if somebody only calls this with 1000-long strings,
            // eventually the arena will start doubling and handling those as well
            stbds_string_block* sb = (stbds_string_block*)STBDS_REALLOC(NULL, 0, sizeof(*sb) - 8 + len);
            prb_memmove(sb->storage, str, len);
            if (a->storage) {
                // insert it after the first element, so that we don't waste the space there
                sb->next = a->storage->next;
                a->storage->next = sb;
            } else {
                sb->next = 0;
                a->storage = sb;
                a->remaining = 0;  // this is redundant, but good for clarity
            }
            return sb->storage;
        } else {
            stbds_string_block* sb = (stbds_string_block*)STBDS_REALLOC(NULL, 0, sizeof(*sb) - 8 + blocksize);
            sb->next = a->storage;
            a->storage = sb;
            a->remaining = blocksize;
        }
    }

    STBDS_ASSERT(len <= a->remaining);
    p = a->storage->storage + a->remaining - len;
    a->remaining -= len;
    prb_memmove(p, str, len);
    return p;
}

STBDS__PUBLICDEF void
stbds_strreset(stbds_string_arena* a) {
    stbds_string_block *x, *y;
    x = a->storage;
    while (x) {
        y = x->next;
        STBDS_FREE(NULL, x);
        x = y;
    }
    prb_memset(a, 0, sizeof(*a));
}

#endif  // prb_NO_IMPLEMENTATION

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

// NOLINTEND(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
