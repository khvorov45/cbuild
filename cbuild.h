/* A set of utilities for writing "build scripts" as small C (or C++) programs.

Works on windows and linux.

License: MIT or public domain. See end of file.

Repository: https://github.com/khvorov45/cbuild
See example/build.c for a non-trivial example build program.

A simple build program might look like this

File structure

project-directory
|
|-cbuild.h
|-build.c
|-program.c

Contents of build.c:

```c
#include "cbuild.h"

int main() {
    prb_Arena arena_ = prb_createArenaFromVmem(1 * prb_GIGABYTE);
    prb_Arena* arena = &arena_;
    prb_Str rootDir = prb_getParentDir(arena, prb_STR(__FILE__));
    prb_Str mainFile = prb_pathJoin(arena, rootDir, prb_STR("program.c"));
    prb_Str mainOut = prb_replaceExt(arena, mainFile, prb_STR("exe"));
    prb_Str compileCmd = prb_fmt(arena, "clang %.*s -o %.*s", prb_LIT(mainFile), prb_LIT(mainOut));
    prb_Process proc = prb_createProcess(compileCmd, (prb_ProcessSpec) {});
    prb_assert(prb_launchProcesses(arena, &proc, 1, prb_Background_No));
}
```

Compile and run `build.c` to compile the main program.
On linux you'll have to link the build program to pthread `-lpthread` if the compiler doesn't do it by default.

If using in multiple translation units:
    - Define prb_NOT_STATIC in the translation unit that has the implementation
    - Define prb_NO_IMPLEMENTATION to use as a normal header

Note that arenas are not thread-safe, so don't pass the same arena to multiple threads.

All string formatting functions are wrappers around stb printf
https://github.com/nothings/stb/blob/master/stb_sprintf.h
The strings are allocated on the linear allocator everything else is using.
The original stb sprintf API is still exposed but with the additional prb_ prefix

The library includes all of stb ds
https://github.com/nothings/stb/blob/master/stb_ds.h
There are no wrappers for it, use the original API (note the additional prb_ prefix)
Note that by default short macro names (like arrlen) are exposed, define prb_STBDS_NO_SHORT_NAMES to disable them.
All memory allocation calls in stb ds are using libc realloc/free.

If a prb_* function ever returns an array (pointer to multiple elements) then it's
an stb ds array, so get its length with arrlen() (or prb_stbds_arrlen() without short names)
Note that arrlen() is different from prb_arrayCount() which counts the length of a static array on the stack.

All prb_* iterators are meant to be used in loops like this
for (prb_Iter iter = prb_createIter(); prb_iterNext(&iter) == prb_Success;) {
    // pull stuff you need off iter
}
*/

// NOLINTBEGIN(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4146)  // unary minus operator applied to unsigned type, result still unsigned
#pragma warning(disable : 4201)  // nonstandard extension used: nameless struct/union
#pragma warning(disable : 4505)  // unreferenced function with internal linkage has been removed
#pragma warning(disable : 4820)  // padding
#pragma warning(disable : 5045)  // Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
#endif

#ifndef prb_HEADER_FILE
#define prb_HEADER_FILE

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdalign.h>

#include <string.h>
#include <stdlib.h>

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

#elif prb_PLATFORM_LINUX

#include <sys/syscall.h>
#include <linux/limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
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

#if defined(_MSC_VER) && !defined(__cplusplus)
#define prb_alignof _Alignof
#else
#define prb_alignof alignof
#endif

#if defined(__cplusplus)
#define prb_FALLTHROUGH [[fallthrough]]
#else
#define prb_FALLTHROUGH
#endif

#define prb_max(a, b) (((a) > (b)) ? (a) : (b))
#define prb_min(a, b) (((a) < (b)) ? (a) : (b))
#define prb_clamp(x, a, b) (((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x)))
#define prb_arrayCount(arr) (int32_t)(sizeof(arr) / sizeof(arr[0]))
#define prb_arenaAllocArray(arena, type, len) (type*)prb_arenaAllocAndZero(arena, (len) * (int32_t)sizeof(type), prb_alignof(type))
#define prb_arenaAllocStruct(arena, type) (type*)prb_arenaAllocAndZero(arena, sizeof(type), prb_alignof(type))
#define prb_isPowerOf2(x) (((x) > 0) && (((x) & ((x)-1)) == 0))
#define prb_unused(x) ((x) = (x))

// clang-format off

#define prb_STR(x) (prb_Str) {x, prb_strlen(x)}
#define prb_LIT(x) (x).len, (x).ptr

// Taken from portable snippets
// https://github.com/nemequ/portable-snippets/blob/master/debug-trap/debug-trap.h
#if defined(__has_builtin) && !defined(__ibmxl__)
#if __has_builtin(__builtin_debugtrap)
#define prb_debugbreak() __builtin_debugtrap()
#elif __has_builtin(__debugbreak)
#define prb_debugbreak() __debugbreak()
#endif
#endif
#if !defined(prb_debugbreak)
#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
#define prb_debugbreak() __debugbreak()
#elif defined(__ARMCC_VERSION)
#define prb_debugbreak() __breakpoint(42)
#elif defined(__ibmxl__) || defined(__xlC__)
#include <builtins.h>
#define prb_debugbreak() __trap(42)
#elif defined(__DMC__) && defined(_M_IX86)
#define prb_debugbreak() __asm int 3h;
#elif defined(__i386__) || defined(__x86_64__)
#define prb_debugbreak() __asm__ __volatile__("int3")
#elif defined(__thumb__)
#define prb_debugbreak() __asm__ __volatile__(".inst 0xde01")
#elif defined(__aarch64__)
#define prb_debugbreak() __asm__ __volatile__(".inst 0xd4200000")
#elif defined(__arm__)
#define prb_debugbreak() __asm__ __volatile__(".inst 0xe7f001f0")
#elif defined (__alpha__) && !defined(__osf__)
#define prb_debugbreak() __asm__ __volatile__("bpt")
#elif defined(_54_)
#define prb_debugbreak() __asm__ __volatile__("ESTOP")
#elif defined(_55_)
#define prb_debugbreak() __asm__ __volatile__(";\n .if (.MNEMONIC)\n ESTOP_1\n .else\n ESTOP_1()\n .endif\n NOP")
#elif defined(_64P_)
#define prb_debugbreak() __asm__ __volatile__("SWBP 0")
#elif defined(_6x_)
#define prb_debugbreak() __asm__ __volatile__("NOP\n .word 0x10000000")
#elif defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 0) && defined(__GNUC__)
#define prb_debugbreak() __builtin_trap()
#else
#include <signal.h>
#if defined(SIGTRAP)
#define prb_debugbreak() raise(SIGTRAP)
#else
#define prb_debugbreak() raise(SIGABRT)
#endif
#endif
#endif

// Taken from prb_stb snprintf
// https://github.com/nothings/prb_stb/blob/master/prb_stb_sprintf.h
#if defined(__has_attribute)
    #if __has_attribute(format)
        #define prb_ATTRIBUTE_FORMAT(fmt, va) __attribute__((format(printf, fmt, va)))
    #endif
#endif

#ifndef prb_ATTRIBUTE_FORMAT
    #define prb_ATTRIBUTE_FORMAT(fmt, va)
#endif

#define prb_STRINGIZE(x) prb_STRINGIZE2(x)
#define prb_STRINGIZE2(x) #x
#define prb_LINE_STRING prb_STRINGIZE(__LINE__)

#ifndef prb_assertAction
#define prb_assertAction() do {\
    prb_writeToStdout(prb_STR("assertion failure at "));\
    prb_writeToStdout(prb_STR(__FILE__));\
    prb_writeToStdout(prb_STR(":"));\
    prb_writeToStdout(prb_STR(prb_LINE_STRING));\
    prb_writeToStdout(prb_STR("\n"));\
    prb_debugbreak();\
    prb_terminate(1);\
} while (0)
#endif

#ifndef prb_assert
#define prb_assert(condition) do { if (condition) {} else { prb_assertAction(); } } while (0)
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
    void*    base;
    intptr_t size;
    intptr_t used;
    bool     lockedForStr;
    int32_t  tempCount;
} prb_Arena;

typedef struct prb_TempMemory {
    prb_Arena* arena;
    intptr_t   usedAtBegin;
    int32_t    tempCountAtBegin;
} prb_TempMemory;

// Assume: utf-8, immutable
typedef struct prb_Str {
    const char* ptr;
    int32_t     len;
} prb_Str;

typedef struct prb_GrowingStr {
    prb_Arena* arena;
    prb_Str    str;
} prb_GrowingStr;

typedef enum prb_Status {
    prb_Failure,
    prb_Success,
} prb_Status;

typedef struct prb_TimeStart {
    bool valid;
#if prb_PLATFORM_WINDOWS
    LONGLONG ticks;
#elif prb_PLATFORM_LINUX
    uint64_t  nsec;
#endif
} prb_TimeStart;

typedef enum prb_ColorID {
    prb_ColorID_Reset,
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

typedef struct prb_ProcessSpec {
    bool    redirectStdout;
    prb_Str stdoutFilepath;
    bool    redirectStderr;
    prb_Str stderrFilepath;
    // Additional environment variables that look like this "var1=val1 var2=val2"
    prb_Str addEnv;
} prb_ProcessSpec;

typedef enum prb_ProcessStatus {
    prb_ProcessStatus_NotLaunched,
    prb_ProcessStatus_Launched,
    prb_ProcessStatus_CompletedSuccess,
    prb_ProcessStatus_CompletedFailed,
} prb_ProcessStatus;

typedef struct prb_Process {
    prb_Str           cmd;
    prb_ProcessSpec   spec;
    prb_ProcessStatus status;

#if prb_PLATFORM_WINDOWS
    PROCESS_INFORMATION processInfo;
#elif prb_PLATFORM_LINUX
    pid_t     pid;
#endif
} prb_Process;

typedef enum prb_StrFindMode {
    prb_StrFindMode_Exact,
    prb_StrFindMode_AnyChar,
    prb_StrFindMode_LineBreak,
} prb_StrFindMode;

typedef enum prb_StrDirection {
    prb_StrDirection_FromStart,
    prb_StrDirection_FromEnd,
} prb_StrDirection;

typedef enum prb_StrScannerSide {
    prb_StrScannerSide_AfterMatch,
    prb_StrScannerSide_BeforeMatch,
} prb_StrScannerSide;

typedef struct prb_StrFindSpec {
    prb_StrFindMode  mode;
    prb_StrDirection direction;
    // Not necessary if mode is LineBreak
    prb_Str pattern;
    // Only for mode AnyChar. When true, encountering end of string will result in a match.
    bool alwaysMatchEnd;
} prb_StrFindSpec;

typedef struct prb_StrFindResult {
    bool found;
    // In a left-to-right system `beforeMatch` is to the left of match
    prb_Str beforeMatch;
    prb_Str match;
    // In a left-to-right system `afterMatch` is to the right of match
    prb_Str afterMatch;
} prb_StrFindResult;

typedef struct prb_StrScanner {
    prb_Str ogstr;
    prb_Str beforeMatch;
    prb_Str match;
    prb_Str afterMatch;
    int32_t matchCount;
    prb_Str betweenLastMatches;
} prb_StrScanner;

typedef struct prb_Utf8CharIter {
    prb_Str          str;
    prb_StrDirection direction;
    int32_t          curCharCount;
    int32_t          curByteOffset;
    uint32_t         curUtf32Char;
    int32_t          curUtf8Bytes;
    bool             curIsValid;
} prb_Utf8CharIter;

typedef struct prb_PathEntryIter {
    prb_Str ogstr;
    int32_t curOffset;
    prb_Str curEntryName;
    prb_Str curEntryPath;
} prb_PathEntryIter;

typedef enum prb_Recursive {
    prb_Recursive_No,
    prb_Recursive_Yes,
} prb_Recursive;

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

typedef struct prb_FileHash {
    bool     valid;
    uint64_t hash;
} prb_FileHash;

typedef enum prb_JobStatus {
    prb_JobStatus_NotLaunched,
    prb_JobStatus_Launched,
    prb_JobStatus_Completed,
} prb_JobStatus;

typedef void (*prb_JobProc)(prb_Arena* arena, void* data);

typedef struct prb_Job {
    prb_Arena     arena;
    prb_JobProc   proc;
    void*         data;
    prb_JobStatus status;

#if prb_PLATFORM_WINDOWS
    HANDLE threadhandle;
    DWORD  threadid;
#elif prb_PLATFORM_LINUX
    pthread_t threadid;
#else
#error unimplemented
#endif
} prb_Job;

typedef enum prb_Background {
    prb_Background_No,
    prb_Background_Yes,
} prb_Background;

typedef struct prb_ParseUintResult {
    bool     success;
    uint64_t number;
} prb_ParseUintResult;

typedef enum prb_ParsedNumberKind {
    prb_ParsedNumberKind_None,
    prb_ParsedNumberKind_U64,
    prb_ParsedNumberKind_I64,
    prb_ParsedNumberKind_F64,
} prb_ParsedNumberKind;

typedef struct prb_ParsedNumber {
    prb_ParsedNumberKind kind;
    union {
        uint64_t parsedU64;
        int64_t  parsedI64;
        double   parsedF64;
    };
} prb_ParsedNumber;

typedef struct prb_Rng {
    uint64_t state;
    // Must be odd
    uint64_t inc;
} prb_Rng;

typedef struct prb_ReadEntireFileResult {
    bool      success;
    prb_Bytes content;
} prb_ReadEntireFileResult;

typedef struct prb_GetenvResult {
    bool    found;
    prb_Str str;
} prb_GetenvResult;

typedef struct prb_CoreCountResult {
    bool    success;
    int32_t cores;
} prb_CoreCountResult;

// SECTION Memory
prb_PUBLICDEC bool           prb_memeq(const void* ptr1, const void* ptr2, int32_t bytes);
prb_PUBLICDEC int32_t        prb_getOffsetForAlignment(void* ptr, int32_t align);
prb_PUBLICDEC void*          prb_vmemAlloc(intptr_t bytes);
prb_PUBLICDEC prb_Arena      prb_createArenaFromVmem(intptr_t bytes);
prb_PUBLICDEC prb_Arena      prb_createArenaFromArena(prb_Arena* arena, intptr_t bytes);
prb_PUBLICDEC void*          prb_arenaAllocAndZero(prb_Arena* arena, int32_t size, int32_t align);
prb_PUBLICDEC void           prb_arenaAlignFreePtr(prb_Arena* arena, int32_t align);
prb_PUBLICDEC void*          prb_arenaFreePtr(prb_Arena* arena);
prb_PUBLICDEC intptr_t       prb_arenaFreeSize(prb_Arena* arena);
prb_PUBLICDEC void           prb_arenaChangeUsed(prb_Arena* arena, intptr_t byteDelta);
prb_PUBLICDEC prb_TempMemory prb_beginTempMemory(prb_Arena* arena);
prb_PUBLICDEC void           prb_endTempMemory(prb_TempMemory temp);

// SECTION Filesystem
prb_PUBLICDEC bool                     prb_pathExists(prb_Arena* arena, prb_Str path);
prb_PUBLICDEC bool                     prb_pathIsAbsolute(prb_Str path);
prb_PUBLICDEC prb_Str                  prb_getAbsolutePath(prb_Arena* arena, prb_Str path);
prb_PUBLICDEC bool                     prb_isDir(prb_Arena* arena, prb_Str path);
prb_PUBLICDEC bool                     prb_isFile(prb_Arena* arena, prb_Str path);
prb_PUBLICDEC bool                     prb_dirIsEmpty(prb_Arena* arena, prb_Str path);
prb_PUBLICDEC prb_Status               prb_createDirIfNotExists(prb_Arena* arena, prb_Str path);
prb_PUBLICDEC prb_Status               prb_removePathIfExists(prb_Arena* arena, prb_Str path);
prb_PUBLICDEC prb_Status               prb_clearDir(prb_Arena* arena, prb_Str path);
prb_PUBLICDEC prb_Str                  prb_getWorkingDir(prb_Arena* arena);
prb_PUBLICDEC prb_Status               prb_setWorkingDir(prb_Arena* arena, prb_Str dir);
prb_PUBLICDEC prb_Str                  prb_pathJoin(prb_Arena* arena, prb_Str path1, prb_Str path2);
prb_PUBLICDEC bool                     prb_charIsSep(char ch);
prb_PUBLICDEC prb_Str                  prb_getParentDir(prb_Arena* arena, prb_Str path);
prb_PUBLICDEC prb_Str                  prb_getLastEntryInPath(prb_Str path);
prb_PUBLICDEC prb_Str                  prb_replaceExt(prb_Arena* arena, prb_Str path, prb_Str newExt);
prb_PUBLICDEC prb_PathEntryIter        prb_createPathEntryIter(prb_Str path);
prb_PUBLICDEC prb_Status               prb_pathEntryIterNext(prb_PathEntryIter* iter);
prb_PUBLICDEC void                     prb_getAllDirEntriesCustomBuffer(prb_Arena* arena, prb_Str dir, prb_Recursive mode, prb_Str** storage);
prb_PUBLICDEC prb_Str*                 prb_getAllDirEntries(prb_Arena* arena, prb_Str dir, prb_Recursive mode);
prb_PUBLICDEC prb_FileTimestamp        prb_getLastModified(prb_Arena* arena, prb_Str path);
prb_PUBLICDEC prb_Multitime            prb_createMultitime(void);
prb_PUBLICDEC void                     prb_multitimeAdd(prb_Multitime* multitime, prb_FileTimestamp newTimestamp);
prb_PUBLICDEC prb_ReadEntireFileResult prb_readEntireFile(prb_Arena* arena, prb_Str path);
prb_PUBLICDEC prb_Status               prb_writeEntireFile(prb_Arena* arena, prb_Str path, const void* content, int32_t contentLen);
prb_PUBLICDEC prb_FileHash             prb_getFileHash(prb_Arena* arena, prb_Str filepath);

// SECTION Strings
prb_PUBLICDEC bool                prb_streq(prb_Str str1, prb_Str str2);
prb_PUBLICDEC prb_Str             prb_strSlice(prb_Str str, int32_t start, int32_t onePastEnd);
prb_PUBLICDEC const char*         prb_strGetNullTerminated(prb_Arena* arena, prb_Str str);
prb_PUBLICDEC prb_Str             prb_strFromBytes(prb_Bytes bytes);
prb_PUBLICDEC prb_Str             prb_strTrimSide(prb_Str str, prb_StrDirection dir);
prb_PUBLICDEC prb_Str             prb_strTrim(prb_Str str);
prb_PUBLICDEC prb_StrFindResult   prb_strFind(prb_Str str, prb_StrFindSpec spec);
prb_PUBLICDEC bool                prb_strStartsWith(prb_Str str, prb_Str pattern);
prb_PUBLICDEC bool                prb_strEndsWith(prb_Str str, prb_Str pattern);
prb_PUBLICDEC prb_Str             prb_stringsJoin(prb_Arena* arena, prb_Str* strings, int32_t stringsCount, prb_Str sep);
prb_PUBLICDEC prb_GrowingStr      prb_beginStr(prb_Arena* arena);
prb_PUBLICDEC void                prb_addStrSegment(prb_GrowingStr* gstr, const char* fmt, ...) prb_ATTRIBUTE_FORMAT(2, 3);
prb_PUBLICDEC prb_Str             prb_endStr(prb_GrowingStr* gstr);
prb_PUBLICDEC prb_Str             prb_vfmtCustomBuffer(void* buf, int32_t bufSize, const char* fmt, va_list args);
prb_PUBLICDEC prb_Str             prb_fmt(prb_Arena* arena, const char* fmt, ...) prb_ATTRIBUTE_FORMAT(2, 3);
prb_PUBLICDEC prb_Status          prb_writeToStdout(prb_Str str);
prb_PUBLICDEC prb_Status          prb_writelnToStdout(prb_Arena* arena, prb_Str str);
prb_PUBLICDEC prb_Str             prb_colorEsc(prb_ColorID color);
prb_PUBLICDEC prb_Utf8CharIter    prb_createUtf8CharIter(prb_Str str, prb_StrDirection direction);
prb_PUBLICDEC prb_Status          prb_utf8CharIterNext(prb_Utf8CharIter* iter);
prb_PUBLICDEC prb_StrScanner      prb_createStrScanner(prb_Str str);
prb_PUBLICDEC prb_Status          prb_strScannerMove(prb_StrScanner* scanner, prb_StrFindSpec spec, prb_StrScannerSide side);
prb_PUBLICDEC prb_ParseUintResult prb_parseUint(prb_Str digits, uint64_t base);
prb_PUBLICDEC prb_ParsedNumber    prb_parseNumber(prb_Str str);
prb_PUBLICDEC prb_Str             prb_binaryToCArray(prb_Arena* arena, prb_Str arrayName, void* data, int32_t dataLen);

// SECTION Processes
prb_PUBLICDEC void                prb_terminate(int32_t code);
prb_PUBLICDEC prb_Str             prb_getCmdline(prb_Arena* arena);
prb_PUBLICDEC prb_Str*            prb_getCmdArgs(prb_Arena* arena);
prb_PUBLICDEC const char**        prb_getArgArrayFromStr(prb_Arena* arena, prb_Str str);
prb_PUBLICDEC prb_CoreCountResult prb_getCoreCount(prb_Arena* arena);
prb_PUBLICDEC prb_CoreCountResult prb_getAllowExecutionCoreCount(prb_Arena* arena);
prb_PUBLICDEC prb_Status          prb_allowExecutionOnCores(prb_Arena* arena, int32_t coreCount);
prb_PUBLICDEC prb_Process         prb_createProcess(prb_Str cmd, prb_ProcessSpec spec);
prb_PUBLICDEC prb_Status          prb_launchProcesses(prb_Arena* arena, prb_Process* procs, int32_t procCount, prb_Background mode);
prb_PUBLICDEC prb_Status          prb_waitForProcesses(prb_Process* handles, int32_t handleCount);
prb_PUBLICDEC prb_Status          prb_killProcesses(prb_Process* handles, int32_t handleCount);
prb_PUBLICDEC void                prb_sleep(float ms);
prb_PUBLICDEC bool                prb_debuggerPresent(prb_Arena* arena);
prb_PUBLICDEC prb_Status          prb_setenv(prb_Arena* arena, prb_Str name, prb_Str value);
prb_PUBLICDEC prb_GetenvResult    prb_getenv(prb_Arena* arena, prb_Str name);
prb_PUBLICDEC prb_Status          prb_unsetenv(prb_Arena* arena, prb_Str name);

// SECTION Timing
prb_PUBLICDEC prb_TimeStart prb_timeStart(void);
prb_PUBLICDEC float         prb_getMsFrom(prb_TimeStart timeStart);

// SECTION Multithreading
prb_PUBLICDEC prb_Job    prb_createJob(prb_JobProc proc, void* data, prb_Arena* arena, int32_t arenaBytes);
prb_PUBLICDEC prb_Status prb_launchJobs(prb_Job* jobs, int32_t jobsCount, prb_Background mode);
prb_PUBLICDEC prb_Status prb_waitForJobs(prb_Job* jobs, int32_t jobsCount);

// SECTION Random numbers
prb_PUBLICDEC prb_Rng  prb_createRng(uint32_t seed);
prb_PUBLICDEC uint32_t prb_randomU32(prb_Rng* rng);
prb_PUBLICDEC uint32_t prb_randomU32Bound(prb_Rng* rng, uint32_t max);
prb_PUBLICDEC float    prb_randomF3201(prb_Rng* rng);

//
// SECTION stb snprintf
//

#if defined(__clang__)
#if defined(__has_feature) && defined(__has_attribute)
#if __has_feature(address_sanitizer)
#if __has_attribute(__no_sanitize__)
#define prb_STBSP__ASAN __attribute__((__no_sanitize__("address")))
#elif __has_attribute(__no_sanitize_address__)
#define prb_STBSP__ASAN __attribute__((__no_sanitize_address__))
#elif __has_attribute(__no_address_safety_analysis__)
#define prb_STBSP__ASAN __attribute__((__no_address_safety_analysis__))
#endif
#endif
#endif
#elif defined(__GNUC__) && (__GNUC__ >= 5 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8))
#if defined(__SANITIZE_ADDRESS__) && __SANITIZE_ADDRESS__
#define prb_STBSP__ASAN __attribute__((__no_sanitize_address__))
#endif
#endif

#ifndef prb_STBSP__ASAN
#define prb_STBSP__ASAN
#endif

#ifndef prb_NOT_STATIC
#define prb_STB_SPRINTF_STATIC
#endif

#ifdef prb_STB_SPRINTF_STATIC
#define prb_STBSP__PUBLICDEC static
#define prb_STBSP__PUBLICDEF static prb_STBSP__ASAN
#else
#ifdef __cplusplus
#define prb_STBSP__PUBLICDEC extern "C"
#define prb_STBSP__PUBLICDEF extern "C" prb_STBSP__ASAN
#else
#define prb_STBSP__PUBLICDEC extern
#define prb_STBSP__PUBLICDEF prb_STBSP__ASAN
#endif
#endif

#if defined(__has_attribute)
#if __has_attribute(format)
#define prb_STBSP__ATTRIBUTE_FORMAT(fmt, va) __attribute__((format(printf, fmt, va)))
#endif
#endif

#ifndef prb_STBSP__ATTRIBUTE_FORMAT
#define prb_STBSP__ATTRIBUTE_FORMAT(fmt, va)
#endif

#ifdef _MSC_VER
#define prb_STBSP__NOTUSED(v) (void)(v)
#else
#define prb_STBSP__NOTUSED(v) (void)sizeof(v)
#endif

#ifndef prb_STB_SPRINTF_MIN
#define prb_STB_SPRINTF_MIN 512  // how many characters per callback
#endif
typedef char* prb_STBSP_SPRINTFCB(const char* buf, void* user, int len);

#ifndef prb_STB_SPRINTF_DECORATE
#define prb_STB_SPRINTF_DECORATE(name) prb_stbsp_##name  // define this before including if you want to change the names
#endif

prb_STBSP__PUBLICDEC int prb_STB_SPRINTF_DECORATE(vsprintf)(char* buf, char const* fmt, va_list va);
prb_STBSP__PUBLICDEC int prb_STB_SPRINTF_DECORATE(vsnprintf)(char* buf, int count, char const* fmt, va_list va);
prb_STBSP__PUBLICDEC int prb_STB_SPRINTF_DECORATE(sprintf)(char* buf, char const* fmt, ...) prb_STBSP__ATTRIBUTE_FORMAT(2, 3);
prb_STBSP__PUBLICDEC int prb_STB_SPRINTF_DECORATE(snprintf)(char* buf, int count, char const* fmt, ...)
    prb_STBSP__ATTRIBUTE_FORMAT(3, 4);

prb_STBSP__PUBLICDEC int  prb_STB_SPRINTF_DECORATE(vsprintfcb)(prb_STBSP_SPRINTFCB* callback, void* user, char* buf, char const* fmt, va_list va);
prb_STBSP__PUBLICDEC void prb_STB_SPRINTF_DECORATE(set_separators)(char comma, char period);

//
// SECTION stb ds
//

#ifndef prb_STBDS_NO_SHORT_NAMES
#define arrlen prb_stbds_arrlen
#define arrlenu prb_stbds_arrlenu
#define arrput prb_stbds_arrput
#define arrpush prb_stbds_arrput
#define arrpop prb_stbds_arrpop
#define arrfree prb_stbds_arrfree
#define arraddn prb_stbds_arraddn  // deprecated, use one of the following instead:
#define arraddnptr prb_stbds_arraddnptr
#define arraddnindex prb_stbds_arraddnindex
#define arrsetlen prb_stbds_arrsetlen
#define arrlast prb_stbds_arrlast
#define arrins prb_stbds_arrins
#define arrinsn prb_stbds_arrinsn
#define arrdel prb_stbds_arrdel
#define arrdeln prb_stbds_arrdeln
#define arrdelswap prb_stbds_arrdelswap
#define arrcap prb_stbds_arrcap
#define arrsetcap prb_stbds_arrsetcap

#define hmput prb_stbds_hmput
#define hmputs prb_stbds_hmputs
#define hmget prb_stbds_hmget
#define hmget_ts prb_stbds_hmget_ts
#define hmgets prb_stbds_hmgets
#define hmgetp prb_stbds_hmgetp
#define hmgetp_ts prb_stbds_hmgetp_ts
#define hmgetp_null prb_stbds_hmgetp_null
#define hmgeti prb_stbds_hmgeti
#define hmgeti_ts prb_stbds_hmgeti_ts
#define hmdel prb_stbds_hmdel
#define hmlen prb_stbds_hmlen
#define hmlenu prb_stbds_hmlenu
#define hmfree prb_stbds_hmfree
#define hmdefault prb_stbds_hmdefault
#define hmdefaults prb_stbds_hmdefaults

#define shput prb_stbds_shput
#define shputi prb_stbds_shputi
#define shputs prb_stbds_shputs
#define shget prb_stbds_shget
#define shgeti prb_stbds_shgeti
#define shgets prb_stbds_shgets
#define shgetp prb_stbds_shgetp
#define shgetp_null prb_stbds_shgetp_null
#define shdel prb_stbds_shdel
#define shlen prb_stbds_shlen
#define shlenu prb_stbds_shlenu
#define shfree prb_stbds_shfree
#define shdefault prb_stbds_shdefault
#define shdefaults prb_stbds_shdefaults
#define sh_new_arena prb_stbds_sh_new_arena
#define sh_new_strdup prb_stbds_sh_new_strdup

#define stralloc prb_stbds_stralloc
#define strreset prb_stbds_strreset
#endif

#if !defined(prb_STBDS_REALLOC) && !defined(prb_STBDS_FREE)
#define prb_STBDS_REALLOC(c, p, s) prb_realloc(p, s)
#define prb_STBDS_FREE(c, p) prb_free(p)
#endif

#ifdef _MSC_VER
#define prb_STBDS_NOTUSED(v) (void)(v)
#else
#define prb_STBDS_NOTUSED(v) (void)sizeof(v)
#endif

#ifdef prb_NOT_STATIC
#define prb_STBDS__PUBLICDEC
#define prb_STBDS__PUBLICDEF
#else
#define prb_STBDS__PUBLICDEC static
#define prb_STBDS__PUBLICDEF static
#endif

#ifdef __cplusplus
extern "C" {
#endif

// for security against attackers, seed the library with a random number, at least time() but stronger is better
prb_STBDS__PUBLICDEC void prb_stbds_rand_seed(size_t seed);

// these are the hash functions used internally if you want to test them or use them for other purposes
prb_STBDS__PUBLICDEC size_t prb_stbds_hash_bytes(void* p, size_t len, size_t seed);
prb_STBDS__PUBLICDEC size_t prb_stbds_hash_string(char* str, size_t seed);

// this is a simple string arena allocator, initialize with e.g. 'prb_stbds_string_arena my_arena={0}'.
typedef struct prb_stbds_string_arena prb_stbds_string_arena;
prb_STBDS__PUBLICDEC char*            prb_stbds_stralloc(prb_stbds_string_arena* a, char* str);
prb_STBDS__PUBLICDEC void             prb_stbds_strreset(prb_stbds_string_arena* a);

///////////////
//
// Everything below here is implementation details
//

prb_STBDS__PUBLICDEC void* prb_stbds_arrgrowf(void* a, size_t elemsize, size_t addlen, size_t min_cap);
prb_STBDS__PUBLICDEC void  prb_stbds_arrfreef(void* a);
prb_STBDS__PUBLICDEC void  prb_stbds_hmfree_func(void* p, size_t elemsize);
prb_STBDS__PUBLICDEC void* prb_stbds_hmget_key(void* a, size_t elemsize, void* key, size_t keysize, int mode);
prb_STBDS__PUBLICDEC void* prb_stbds_hmget_key_ts(void* a, size_t elemsize, void* key, size_t keysize, ptrdiff_t* temp, int mode);
prb_STBDS__PUBLICDEC void* prb_stbds_hmput_default(void* a, size_t elemsize);
prb_STBDS__PUBLICDEC void* prb_stbds_hmput_key(void* a, size_t elemsize, void* key, size_t keysize, int mode);
prb_STBDS__PUBLICDEC void* prb_stbds_hmdel_key(void* a, size_t elemsize, void* key, size_t keysize, size_t keyoffset, int mode);
prb_STBDS__PUBLICDEC void* prb_stbds_shmode_func(size_t elemsize, int mode);

#ifdef __cplusplus
}
#endif

#if defined(__GNUC__) || defined(__clang__)
#define prb_STBDS_HAS_TYPEOF
#ifdef __cplusplus
//#define prb_STBDS_HAS_LITERAL_ARRAY  // this is currently broken for clang
#endif
#endif

#if !defined(__cplusplus)
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define prb_STBDS_HAS_LITERAL_ARRAY
#endif
#endif

// clang-format off
// this macro takes the address of the argument, but on gcc/clang can accept rvalues
#if defined(prb_STBDS_HAS_LITERAL_ARRAY) && defined(prb_STBDS_HAS_TYPEOF)
  #if __clang__
  #define prb_STBDS_ADDRESSOF(typevar, value)     ((__typeof__(typevar)[1]){value}) // literal array decays to pointer to value
  #else
  #define prb_STBDS_ADDRESSOF(typevar, value)     ((typeof(typevar)[1]){value}) // literal array decays to pointer to value
  #endif
#else
#define prb_STBDS_ADDRESSOF(typevar, value)     &(value)
#endif

#define prb_STBDS_OFFSETOF(var,field)           ((char *) &(var)->field - (char *) (var))

#define prb_stbds_header(t)  ((prb_stbds_array_header *) (t) - 1)
#define prb_stbds_temp(t)    prb_stbds_header(t)->temp
#define prb_stbds_temp_key(t) (*(char **) prb_stbds_header(t)->hash_table)

#define prb_stbds_arrsetcap(a,n)   (prb_stbds_arrgrow(a,0,n))
#define prb_stbds_arrsetlen(a,n)   ((prb_stbds_arrcap(a) < (size_t) (n) ? prb_stbds_arrsetcap((a),(size_t)(n)),0 : 0), (a) ? prb_stbds_header(a)->length = (size_t) (n) : 0)
#define prb_stbds_arrcap(a)        ((a) ? prb_stbds_header(a)->capacity : 0)
#define prb_stbds_arrlen(a)        ((a) ? (ptrdiff_t) prb_stbds_header(a)->length : 0)
#define prb_stbds_arrlenu(a)       ((a) ?             prb_stbds_header(a)->length : 0)
#define prb_stbds_arrput(a,v)      (prb_stbds_arrmaybegrow(a,1), (a)[prb_stbds_header(a)->length++] = (v))
#define prb_stbds_arrpush          prb_stbds_arrput  // synonym
#define prb_stbds_arrpop(a)        (prb_stbds_header(a)->length--, (a)[prb_stbds_header(a)->length])
#define prb_stbds_arraddn(a,n)     ((void)(prb_stbds_arraddnindex(a, n)))    // deprecated, use one of the following instead:
#define prb_stbds_arraddnptr(a,n)  (prb_stbds_arrmaybegrow(a,n), (n) ? (prb_stbds_header(a)->length += (n), &(a)[prb_stbds_header(a)->length-(n)]) : (a))
#define prb_stbds_arraddnindex(a,n)(prb_stbds_arrmaybegrow(a,n), (n) ? (prb_stbds_header(a)->length += (n), prb_stbds_header(a)->length-(n)) : prb_stbds_arrlen(a))
#define prb_stbds_arraddnoff       prb_stbds_arraddnindex
#define prb_stbds_arrlast(a)       ((a)[prb_stbds_header(a)->length-1])
#define prb_stbds_arrfree(a)       ((void) ((a) ? prb_STBDS_FREE(NULL,prb_stbds_header(a)) : (void)0), (a)=NULL)
#define prb_stbds_arrdel(a,i)      prb_stbds_arrdeln(a,i,1)
#define prb_stbds_arrdeln(a,i,n)   (memmove(&(a)[i], &(a)[(i)+(n)], sizeof *(a) * (prb_stbds_header(a)->length-(n)-(i))), prb_stbds_header(a)->length -= (n))
#define prb_stbds_arrdelswap(a,i)  ((a)[i] = prb_stbds_arrlast(a), prb_stbds_header(a)->length -= 1)
#define prb_stbds_arrinsn(a,i,n)   (prb_stbds_arraddn((a),(n)), memmove(&(a)[(i)+(n)], &(a)[i], sizeof *(a) * (prb_stbds_header(a)->length-(n)-(i))))
#define prb_stbds_arrins(a,i,v)    (prb_stbds_arrinsn((a),(i),1), (a)[i]=(v))

#define prb_stbds_arrmaybegrow(a,n)  ((!(a) || prb_stbds_header(a)->length + (n) > prb_stbds_header(a)->capacity) \
                                  ? (prb_stbds_arrgrow(a,n,0),0) : 0)

#define prb_stbds_arrgrow(a,b,c)   ((a) = prb_stbds_arrgrowf_wrapper((a), sizeof *(a), (b), (c)))

#define prb_stbds_hmput(t, k, v) \
    ((t) = prb_stbds_hmput_key_wrapper((t), sizeof *(t), (void*) prb_STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, 0),   \
     (t)[prb_stbds_temp((t)-1)].key = (k),    \
     (t)[prb_stbds_temp((t)-1)].value = (v))

#define prb_stbds_hmputs(t, s) \
    ((t) = prb_stbds_hmput_key_wrapper((t), sizeof *(t), &(s).key, sizeof (s).key, prb_STBDS_HM_BINARY), \
     (t)[prb_stbds_temp((t)-1)] = (s))

#define prb_stbds_hmgeti(t,k) \
    ((t) = prb_stbds_hmget_key_wrapper((t), sizeof *(t), (void*) prb_STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, prb_STBDS_HM_BINARY), \
      prb_stbds_temp((t)-1))

#define prb_stbds_hmgeti_ts(t,k,temp) \
    ((t) = prb_stbds_hmget_key_ts_wrapper((t), sizeof *(t), (void*) prb_STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, &(temp), prb_STBDS_HM_BINARY), \
      (temp))

#define prb_stbds_hmgetp(t, k) \
    ((void) prb_stbds_hmgeti(t,k), &(t)[prb_stbds_temp((t)-1)])

#define prb_stbds_hmgetp_ts(t, k, temp) \
    ((void) prb_stbds_hmgeti_ts(t,k,temp), &(t)[temp])

#define prb_stbds_hmdel(t,k) \
    (((t) = prb_stbds_hmdel_key_wrapper((t),sizeof *(t), (void*) prb_STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, prb_STBDS_OFFSETOF((t),key), prb_STBDS_HM_BINARY)),(t)?prb_stbds_temp((t)-1):0)

#define prb_stbds_hmdefault(t, v) \
    ((t) = prb_stbds_hmput_default_wrapper((t), sizeof *(t)), (t)[-1].value = (v))

#define prb_stbds_hmdefaults(t, s) \
    ((t) = prb_stbds_hmput_default_wrapper((t), sizeof *(t)), (t)[-1] = (s))

#define prb_stbds_hmfree(p)        \
    ((void) ((p) != NULL ? prb_stbds_hmfree_func((p)-1,sizeof*(p)),0 : 0),(p)=NULL)

#define prb_stbds_hmgets(t, k)    (*prb_stbds_hmgetp(t,k))
#define prb_stbds_hmget(t, k)     (prb_stbds_hmgetp(t,k)->value)
#define prb_stbds_hmget_ts(t, k, temp)  (prb_stbds_hmgetp_ts(t,k,temp)->value)
#define prb_stbds_hmlen(t)        ((t) ? (ptrdiff_t) prb_stbds_header((t)-1)->length-1 : 0)
#define prb_stbds_hmlenu(t)       ((t) ?             prb_stbds_header((t)-1)->length-1 : 0)
#define prb_stbds_hmgetp_null(t,k)  (prb_stbds_hmgeti(t,k) == -1 ? NULL : &(t)[prb_stbds_temp((t)-1)])

#define prb_stbds_shput(t, k, v) \
    ((t) = prb_stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (t)->key, prb_STBDS_HM_STRING),   \
     (t)[prb_stbds_temp((t)-1)].value = (v))

#define prb_stbds_shputi(t, k, v) \
    ((t) = prb_stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (t)->key, prb_STBDS_HM_STRING),   \
     (t)[prb_stbds_temp((t)-1)].value = (v), prb_stbds_temp((t)-1))

#define prb_stbds_shputs(t, s) \
    ((t) = prb_stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (s).key, sizeof (s).key, prb_STBDS_HM_STRING), \
     (t)[prb_stbds_temp((t)-1)] = (s), \
     (t)[prb_stbds_temp((t)-1)].key = prb_stbds_temp_key((t)-1)) // above line overwrites whole structure, so must rewrite key here if it was allocated internally

#define prb_stbds_pshput(t, p) \
    ((t) = prb_stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (p)->key, sizeof (p)->key, prb_STBDS_HM_PTR_TO_STRING), \
     (t)[prb_stbds_temp((t)-1)] = (p))

#define prb_stbds_shgeti(t,k) \
     ((t) = prb_stbds_hmget_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (t)->key, prb_STBDS_HM_STRING), \
      prb_stbds_temp((t)-1))

#define prb_stbds_pshgeti(t,k) \
     ((t) = prb_stbds_hmget_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (*(t))->key, prb_STBDS_HM_PTR_TO_STRING), \
      prb_stbds_temp((t)-1))

#define prb_stbds_shgetp(t, k) \
    ((void) prb_stbds_shgeti(t,k), &(t)[prb_stbds_temp((t)-1)])

#define prb_stbds_pshget(t, k) \
    ((void) prb_stbds_pshgeti(t,k), (t)[prb_stbds_temp((t)-1)])

#define prb_stbds_shdel(t,k) \
    (((t) = prb_stbds_hmdel_key_wrapper((t),sizeof *(t), (void*) (k), sizeof (t)->key, prb_STBDS_OFFSETOF((t),key), prb_STBDS_HM_STRING)),(t)?prb_stbds_temp((t)-1):0)
#define prb_stbds_pshdel(t,k) \
    (((t) = prb_stbds_hmdel_key_wrapper((t),sizeof *(t), (void*) (k), sizeof (*(t))->key, prb_STBDS_OFFSETOF(*(t),key), prb_STBDS_HM_PTR_TO_STRING)),(t)?prb_stbds_temp((t)-1):0)

#define prb_stbds_sh_new_arena(t)  \
    ((t) = prb_stbds_shmode_func_wrapper(t, sizeof *(t), prb_STBDS_SH_ARENA))
#define prb_stbds_sh_new_strdup(t) \
    ((t) = prb_stbds_shmode_func_wrapper(t, sizeof *(t), prb_STBDS_SH_STRDUP))

#define prb_stbds_shdefault(t, v)  prb_stbds_hmdefault(t,v)
#define prb_stbds_shdefaults(t, s) prb_stbds_hmdefaults(t,s)

#define prb_stbds_shfree       prb_stbds_hmfree
#define prb_stbds_shlenu       prb_stbds_hmlenu

#define prb_stbds_shgets(t, k) (*prb_stbds_shgetp(t,k))
#define prb_stbds_shget(t, k)  (prb_stbds_shgetp(t,k)->value)
#define prb_stbds_shgetp_null(t,k)  (prb_stbds_shgeti(t,k) == -1 ? NULL : &(t)[prb_stbds_temp((t)-1)])
#define prb_stbds_shlen        prb_stbds_hmlen

// clang-format on

typedef struct {
    size_t    length;
    size_t    capacity;
    void*     hash_table;
    ptrdiff_t temp;
} prb_stbds_array_header;

typedef struct prb_stbds_string_block {
    struct prb_stbds_string_block* next;
    char                           storage[8];
} prb_stbds_string_block;

struct prb_stbds_string_arena {
    prb_stbds_string_block* storage;
    size_t                  remaining;
    unsigned char           block;
    unsigned char           mode;  // this isn't used by the string arena itself
};

#define prb_STBDS_HM_BINARY 0
#define prb_STBDS_HM_STRING 1

enum {
    prb_STBDS_SH_NONE,
    prb_STBDS_SH_DEFAULT,
    prb_STBDS_SH_STRDUP,
    prb_STBDS_SH_ARENA,
};

#ifdef __cplusplus
// in C we use implicit assignment from these void*-returning functions to T*.
// in C++ these templates make the same code work
template<class T>
static T*
prb_stbds_arrgrowf_wrapper(T* a, size_t elemsize, size_t addlen, size_t min_cap) {
    return (T*)prb_stbds_arrgrowf((void*)a, elemsize, addlen, min_cap);
}
template<class T>
static T*
prb_stbds_hmget_key_wrapper(T* a, size_t elemsize, void* key, size_t keysize, int mode) {
    return (T*)prb_stbds_hmget_key((void*)a, elemsize, key, keysize, mode);
}
template<class T>
static T*
prb_stbds_hmget_key_ts_wrapper(T* a, size_t elemsize, void* key, size_t keysize, ptrdiff_t* temp, int mode) {
    return (T*)prb_stbds_hmget_key_ts((void*)a, elemsize, key, keysize, temp, mode);
}
template<class T>
static T*
prb_stbds_hmput_default_wrapper(T* a, size_t elemsize) {
    return (T*)prb_stbds_hmput_default((void*)a, elemsize);
}
template<class T>
static T*
prb_stbds_hmput_key_wrapper(T* a, size_t elemsize, void* key, size_t keysize, int mode) {
    return (T*)prb_stbds_hmput_key((void*)a, elemsize, key, keysize, mode);
}
template<class T>
static T*
prb_stbds_hmdel_key_wrapper(T* a, size_t elemsize, void* key, size_t keysize, size_t keyoffset, int mode) {
    return (T*)prb_stbds_hmdel_key((void*)a, elemsize, key, keysize, keyoffset, mode);
}
template<class T>
static T*
prb_stbds_shmode_func_wrapper(T*, size_t elemsize, int mode) {
    return (T*)prb_stbds_shmode_func(elemsize, mode);
}
#else
#define prb_stbds_arrgrowf_wrapper prb_stbds_arrgrowf
#define prb_stbds_hmget_key_wrapper prb_stbds_hmget_key
#define prb_stbds_hmget_key_ts_wrapper prb_stbds_hmget_key_ts
#define prb_stbds_hmput_default_wrapper prb_stbds_hmput_default
#define prb_stbds_hmput_key_wrapper prb_stbds_hmput_key
#define prb_stbds_hmdel_key_wrapper prb_stbds_hmdel_key
#define prb_stbds_shmode_func_wrapper(t, e, m) prb_stbds_shmode_func(e, m)
#endif

#endif  // prb_HEADER_FILE

#ifndef prb_NO_IMPLEMENTATION

//
// SECTION Memory (implementation)
//

prb_PUBLICDEF bool
prb_memeq(const void* ptr1, const void* ptr2, int32_t bytes) {
    prb_assert(bytes >= 0);
    int  memcmpResult = prb_memcmp(ptr1, ptr2, (size_t)bytes);
    bool result = memcmpResult == 0;
    return result;
}

prb_PUBLICDEF int32_t
prb_getOffsetForAlignment(void* ptr, int32_t align) {
    prb_assert(prb_isPowerOf2(align));
    uintptr_t ptrAligned = (uintptr_t)((uint8_t*)ptr + (align - 1)) & (uintptr_t)(~(align - 1));
    prb_assert(ptrAligned >= (uintptr_t)ptr);
    intptr_t diff = (intptr_t)(ptrAligned - (uintptr_t)ptr);
    prb_assert(diff < align && diff >= 0);
    return (int32_t)diff;
}

prb_PUBLICDEF void*
prb_vmemAlloc(intptr_t bytes) {
#if prb_PLATFORM_WINDOWS

    void* ptr = VirtualAlloc(0, (SIZE_T)bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    prb_assert(ptr != 0);

#elif prb_PLATFORM_LINUX

    void* ptr = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    prb_assert(ptr != MAP_FAILED);

#else
#error unimplemented
#endif

    return ptr;
}

prb_PUBLICDEF prb_Arena
prb_createArenaFromVmem(intptr_t bytes) {
    prb_Arena arena = {
        .base = prb_vmemAlloc(bytes),
        .size = bytes,
        .used = 0,
        .lockedForStr = false,
        .tempCount = 0,
    };
    return arena;
}

prb_PUBLICDEF prb_Arena
prb_createArenaFromArena(prb_Arena* parent, intptr_t bytes) {
    prb_Arena arena = {
        .base = prb_arenaFreePtr(parent),
        .size = bytes,
        .used = 0,
        .lockedForStr = false,
        .tempCount = 0,
    };
    prb_arenaChangeUsed(parent, bytes);
    return arena;
}

prb_PUBLICDEF void*
prb_arenaAllocAndZero(prb_Arena* arena, int32_t size, int32_t align) {
    prb_assert(!arena->lockedForStr);
    prb_arenaAlignFreePtr(arena, align);
    void* result = prb_arenaFreePtr(arena);
    prb_arenaChangeUsed(arena, size);
    prb_memset(result, 0, (size_t)size);
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

prb_PUBLICDEF intptr_t
prb_arenaFreeSize(prb_Arena* arena) {
    intptr_t result = arena->size - arena->used;
    return result;
}

prb_PUBLICDEF void
prb_arenaChangeUsed(prb_Arena* arena, intptr_t byteDelta) {
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

#if prb_PLATFORM_WINDOWS

typedef struct prb_windows_WideStr {
    LPWSTR  ptr;
    int32_t len;
} prb_windows_WideStr;

static prb_windows_WideStr
prb_windows_getWideStr(prb_Arena* arena, prb_Str str) {
    prb_windows_WideStr result = {.ptr = 0, .len = 0};
    prb_arenaAlignFreePtr(arena, prb_alignof(uint16_t));
    result.ptr = (LPWSTR)prb_arenaFreePtr(arena);
    int multiByteResult = MultiByteToWideChar(CP_UTF8, 0, str.ptr, str.len, result.ptr, (int)prb_min(prb_arenaFreeSize(arena), INT32_MAX) / (int32_t)sizeof(uint16_t));
    prb_assert(multiByteResult > 0);
    result.len = multiByteResult;
    LPWSTR temp = result.ptr;
    prb_unused(temp);
    prb_arenaChangeUsed(arena, multiByteResult * (int32_t)sizeof(uint16_t));
    prb_arenaAllocAndZero(arena, 2, 1);  // NOTE(khvorov) Null terminator just in case
    return result;
}

static prb_Str
prb_windows_strFromWideStr(prb_Arena* arena, prb_windows_WideStr wstr) {
    prb_Str result = {.ptr = 0, .len = 0};
    char*   ptr = (char*)prb_arenaFreePtr(arena);
    int     bytesWritten = WideCharToMultiByte(CP_UTF8, 0, wstr.ptr, wstr.len, ptr, (int)prb_min(prb_arenaFreeSize(arena), INT32_MAX), 0, 0);
    prb_assert(bytesWritten > 0);
    result.ptr = ptr;
    result.len = bytesWritten;
    prb_arenaChangeUsed(arena, bytesWritten);
    prb_arenaAllocAndZero(arena, 1, 1);  // NOTE(khvorov) Null terminator
    return result;
}

static prb_windows_WideStr
prb_windows_getWidePath(prb_Arena* arena, prb_Str str) {
    prb_windows_WideStr wstr = prb_windows_getWideStr(arena, str);

    // NOTE(khvorov) 248 is from
    // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createdirectoryw
    if (wstr.len >= 248) {
        int32_t newlen = wstr.len + 4;
        LPWSTR  newptr = (LPWSTR)prb_arenaAllocArray(arena, uint16_t, newlen);
        newptr[0] = '\\';
        newptr[1] = '\\';
        newptr[2] = '?';
        newptr[3] = '\\';
        prb_memcpy(newptr + 4, wstr.ptr, wstr.len * sizeof(*wstr.ptr));
        for (int32_t ind = 0; ind < newlen; ind++) {
            if (newptr[ind] == '/') {
                newptr[ind] = '\\';
            }
        }
        wstr = (prb_windows_WideStr) {newptr, newlen};
        prb_arenaAllocAndZero(arena, 2, 1);  // NOTE(khvorov) Null terminator
    }

    return wstr;
}

typedef struct prb_windows_GetFileStatResult {
    bool                      success;
    WIN32_FILE_ATTRIBUTE_DATA stat;
} prb_windows_GetFileStatResult;

static prb_windows_GetFileStatResult
prb_windows_getFileStat(prb_Arena* arena, prb_Str path) {
    prb_TempMemory                temp = prb_beginTempMemory(arena);
    prb_windows_GetFileStatResult result;
    prb_memset(&result, 0, sizeof(result));
    prb_windows_WideStr pathWide = prb_windows_getWidePath(arena, path);
    if (GetFileAttributesExW(pathWide.ptr, GetFileExInfoStandard, &result.stat) != 0) {
        result.success = true;
    }
    prb_endTempMemory(temp);
    return result;
}

typedef struct prb_windows_OpenResult {
    bool   success;
    HANDLE handle;
} prb_windows_OpenResult;

static prb_windows_OpenResult
prb_windows_open(prb_Arena* arena, prb_Str path, DWORD access, DWORD share, DWORD create, SECURITY_ATTRIBUTES* securityAttr) {
    prb_windows_OpenResult result = {.success = false, .handle = 0};
    prb_TempMemory         temp = prb_beginTempMemory(arena);
    prb_windows_WideStr    pathWide = prb_windows_getWidePath(arena, path);

    HANDLE handle = CreateFileW(pathWide.ptr, access, share, securityAttr, create, FILE_ATTRIBUTE_NORMAL, 0);
    if (handle != INVALID_HANDLE_VALUE) {
        result.success = true;
        result.handle = handle;
    }

    prb_endTempMemory(temp);
    return result;
}

#elif prb_PLATFORM_LINUX

typedef struct prb_linux_GetFileStatResult {
    bool success;
    struct stat stat;
} prb_linux_GetFileStatResult;

static prb_linux_GetFileStatResult
prb_linux_getFileStat(prb_Arena* arena, prb_Str path) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_linux_GetFileStatResult result = {};
    const char* pathNull = prb_strGetNullTerminated(arena, path);
    struct stat statBuf = {};
    if (stat(pathNull, &statBuf) == 0) {
        result = (prb_linux_GetFileStatResult) {.success = true, .stat = statBuf};
    }
    prb_endTempMemory(temp);
    return result;
}

typedef struct prb_linux_OpenResult {
    bool success;
    int handle;
} prb_linux_OpenResult;

static prb_linux_OpenResult
prb_linux_open(prb_Arena* arena, prb_Str path, int oflags, mode_t mode) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    const char* pathNull = prb_strGetNullTerminated(arena, path);
    prb_linux_OpenResult result = {};
    result.handle = open(pathNull, oflags, mode);
    result.success = result.handle != -1;
    prb_endTempMemory(temp);
    return result;
}

typedef struct prb_linux_Dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
} prb_linux_Dirent64;

#endif

prb_PUBLICDEF bool
prb_pathExists(prb_Arena* arena, prb_Str path) {
    bool result = false;
#if prb_PLATFORM_WINDOWS
    prb_windows_GetFileStatResult stat = prb_windows_getFileStat(arena, path);
    result = stat.success;
#elif prb_PLATFORM_LINUX
    prb_linux_GetFileStatResult stat = prb_linux_getFileStat(arena, path);
    result = stat.success;
#else
#error unimplemented
#endif
    return result;
}

prb_PUBLICDEF bool
prb_pathIsAbsolute(prb_Str path) {
    bool result = false;
#if prb_PLATFORM_WINDOWS

    // NOTE(khvorov) Paths like \file.txt are "absolute" according to microsoft but not according to me
    // because they are still relative to the current volume.
    bool doubleSlash = path.len >= 2 && prb_charIsSep(path.ptr[0]) && prb_charIsSep(path.ptr[1]);
    bool disk = path.len >= 3 && path.ptr[1] == ':' && prb_charIsSep(path.ptr[2]);
    result = doubleSlash || disk;

#elif prb_PLATFORM_LINUX

    result = path.len > 0 && path.ptr[0] == '/';

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF prb_Str
prb_getAbsolutePath(prb_Arena* arena, prb_Str path) {
    prb_Str pathAbs = path;
    if (!prb_pathIsAbsolute(path)) {
        prb_Str cwd = prb_getWorkingDir(arena);
        prb_Str toJoin = cwd;
#if prb_PLATFORM_WINDOWS
        // NOTE(khvorov) These are the semi-absolute \test.txt type paths
        if (path.len > 0 && prb_charIsSep(path.ptr[0])) {
            prb_PathEntryIter iter = prb_createPathEntryIter(cwd);
            prb_assert(prb_pathEntryIterNext(&iter));
            toJoin = iter.curEntryPath;
        }
#endif
        pathAbs = prb_pathJoin(arena, toJoin, path);
    }

    prb_GrowingStr    gstr = prb_beginStr(arena);
    prb_PathEntryIter iter = prb_createPathEntryIter(pathAbs);
    while (prb_pathEntryIterNext(&iter) == prb_Success) {
        bool addThisEntry = true;
        if (prb_streq(iter.curEntryName, prb_STR("."))) {
            addThisEntry = false;
        } else {
            prb_PathEntryIter iterCopy = iter;
            if (prb_pathEntryIterNext(&iterCopy) == prb_Success) {
                if (prb_streq(iterCopy.curEntryName, prb_STR(".."))) {
                    addThisEntry = false;
                    // NOTE(khvorov) Skip the next one (..) as well
                    prb_pathEntryIterNext(&iter);
                }
            }
        }
        if (addThisEntry) {
            if (gstr.str.len == 0 || prb_charIsSep(gstr.str.ptr[gstr.str.len - 1])) {
                prb_addStrSegment(&gstr, "%.*s", prb_LIT(iter.curEntryName));
            } else {
                prb_addStrSegment(&gstr, "/%.*s", prb_LIT(iter.curEntryName));
            }
        }
    }

    prb_Str result = prb_endStr(&gstr);
    return result;
}

prb_PUBLICDEF bool
prb_isDir(prb_Arena* arena, prb_Str path) {
    bool result = false;
#if prb_PLATFORM_WINDOWS
    prb_windows_GetFileStatResult stat = prb_windows_getFileStat(arena, path);
    if (stat.success) {
        result = (stat.stat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;
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
prb_isFile(prb_Arena* arena, prb_Str path) {
    bool result = false;
#if prb_PLATFORM_WINDOWS
    prb_windows_GetFileStatResult stat = prb_windows_getFileStat(arena, path);
    if (stat.success) {
        result = (stat.stat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }
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
prb_dirIsEmpty(prb_Arena* arena, prb_Str path) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Str*       entries = prb_getAllDirEntries(arena, path, prb_Recursive_No);
    bool           result = prb_stbds_arrlen(entries) == 0;
    prb_stbds_arrfree(entries);
    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Status
prb_createDirIfNotExists(prb_Arena* arena, prb_Str path) {
    prb_TempMemory    temp = prb_beginTempMemory(arena);
    prb_Status        result = prb_Success;
    prb_Str           pathAbs = prb_getAbsolutePath(arena, path);
    prb_PathEntryIter iter = prb_createPathEntryIter(pathAbs);
    while (prb_pathEntryIterNext(&iter) && result == prb_Success) {
        if (!prb_isDir(arena, iter.curEntryPath)) {
#if prb_PLATFORM_WINDOWS
            prb_windows_WideStr pathWide = prb_windows_getWidePath(arena, iter.curEntryPath);
            result = CreateDirectoryW(pathWide.ptr, 0) != 0 ? prb_Success : prb_Failure;
#elif prb_PLATFORM_LINUX
            const char* pathNull = prb_strGetNullTerminated(arena, iter.curEntryPath);
            result = mkdir(pathNull, S_IRWXU | S_IRWXG | S_IRWXO) == 0 ? prb_Success : prb_Failure;
#else
#error unimplemented
#endif
        }
    }
    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Status
prb_removePathIfExists(prb_Arena* arena, prb_Str path) {
    prb_Status     result = prb_Success;
    prb_TempMemory temp = prb_beginTempMemory(arena);

    prb_Str*    toRemove = 0;
    const char* pathNull = prb_strGetNullTerminated(arena, path);
    prb_Str     pathNullStr = {pathNull, path.len};
    prb_stbds_arrput(toRemove, pathNullStr);
    if (prb_isDir(arena, path)) {
        // NOTE(khvorov) These are null-terminated
        prb_getAllDirEntriesCustomBuffer(arena, path, prb_Recursive_Yes, &toRemove);
    }

    // NOTE(khvorov) Remove all files
    for (int32_t entryIndex = 0; entryIndex < prb_stbds_arrlen(toRemove) && result == prb_Success; entryIndex++) {
        prb_Str entry = toRemove[entryIndex];
        if (prb_isFile(arena, entry)) {
#if prb_PLATFORM_WINDOWS
            prb_windows_WideStr entryWide = prb_windows_getWidePath(arena, entry);
            if (DeleteFileW(entryWide.ptr) == 0) {
                result = prb_Failure;
                if (SetFileAttributesW(entryWide.ptr, FILE_ATTRIBUTE_NORMAL)) {
                    if (DeleteFileW(entryWide.ptr)) {
                        result = prb_Success;
                    }
                }
            }
#elif prb_PLATFORM_LINUX
            result = unlink(entry.ptr) == 0 ? prb_Success : prb_Failure;
#else
#error unimplemented
#endif
        }
    }

    // NOTE(khvorov) Remove all directories in reverse order because
    // getAllDirEntries puts the most nested ones at the bottom
    for (int32_t entryIndex = (int32_t)prb_stbds_arrlen(toRemove) - 1; entryIndex >= 0 && result == prb_Success; entryIndex--) {
        prb_Str entry = toRemove[entryIndex];
        if (prb_isDir(arena, entry)) {
#if prb_PLATFORM_WINDOWS
            prb_windows_WideStr entryWide = prb_windows_getWidePath(arena, entry);
            if (RemoveDirectoryW(entryWide.ptr) == 0) {
                result = prb_Failure;
            }
#elif prb_PLATFORM_LINUX
            result = rmdir(entry.ptr) == 0 ? prb_Success : prb_Failure;
#else
#error unimplemented
#endif
        }
    }

    prb_stbds_arrfree(toRemove);
    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Status
prb_clearDir(prb_Arena* arena, prb_Str path) {
    prb_Status result = prb_removePathIfExists(arena, path);
    if (result == prb_Success) {
        result = prb_createDirIfNotExists(arena, path);
    }
    return result;
}

prb_PUBLICDEF prb_Str
prb_getWorkingDir(prb_Arena* arena) {
    prb_Str result = {.ptr = 0, .len = 0};

#if prb_PLATFORM_WINDOWS

    prb_arenaAlignFreePtr(arena, prb_alignof(uint16_t));
    LPWSTR ptrWide = (LPWSTR)prb_arenaFreePtr(arena);
    DWORD  lenWide = GetCurrentDirectoryW((DWORD)prb_min(prb_arenaFreeSize(arena), UINT32_MAX) / sizeof(uint16_t), ptrWide);
    prb_assert(lenWide > 0);
    prb_arenaChangeUsed(arena, (int32_t)lenWide * (int32_t)sizeof(uint16_t));
    result = prb_windows_strFromWideStr(arena, (prb_windows_WideStr) {ptrWide, (int32_t)lenWide});
    // NOTE(khvorov) Change backslashes to forward slashes
    for (int32_t index = 0; index < result.len; index++) {
        if (result.ptr[index] == '\\') {
            ((char*)result.ptr)[index] = '/';
        }
    }

#elif prb_PLATFORM_LINUX

    char* ptr = (char*)prb_arenaFreePtr(arena);
    prb_assert(getcwd(ptr, prb_arenaFreeSize(arena)));
    result = prb_STR(ptr);
    prb_arenaChangeUsed(arena, result.len);
    prb_arenaAllocAndZero(arena, 1, 1);  // NOTE(khvorov) Null terminator

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF prb_Status
prb_setWorkingDir(prb_Arena* arena, prb_Str dir) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Status     result = prb_Failure;

#if prb_PLATFORM_WINDOWS

    prb_windows_WideStr pathWide = prb_windows_getWidePath(arena, dir);
    // NOTE(khvorov) Windows might want to add a trailing slash, so leave 2 null terminators at the end just in case
    prb_arenaAllocAndZero(arena, 2, 1);
    result = SetCurrentDirectoryW(pathWide.ptr) != 0 ? prb_Success : prb_Failure;

#elif prb_PLATFORM_LINUX

    const char* dirNull = prb_strGetNullTerminated(arena, dir);
    result = chdir(dirNull) == 0 ? prb_Success : prb_Failure;

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Str
prb_pathJoin(prb_Arena* arena, prb_Str path1, prb_Str path2) {
    prb_assert(path1.ptr && path2.ptr && path1.len > 0 && path2.len > 0);
    char path1LastChar = path1.ptr[path1.len - 1];
    bool path1EndsOnSep = prb_charIsSep(path1LastChar);
    if (path1EndsOnSep) {
        path1.len -= 1;
    }
    char path2FirstChar = path2.ptr[0];
    bool path2StartsOnSep = prb_charIsSep(path2FirstChar);
    if (path2StartsOnSep) {
        path2 = prb_strSlice(path2, 1, path2.len);
    }
    prb_Str result = prb_fmt(arena, "%.*s/%.*s", prb_LIT(path1), prb_LIT(path2));
    return result;
}

prb_PUBLICDEF bool
prb_charIsSep(char ch) {
    bool result = false;

#if prb_PLATFORM_WINDOWS

    result = ch == '/' || ch == '\\';

#elif prb_PLATFORM_LINUX

    result = ch == '/';

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF prb_Str
prb_getParentDir(prb_Arena* arena, prb_Str path) {
    prb_assert(path.ptr && path.len > 0);
    prb_Str           pathAbs = prb_getAbsolutePath(arena, path);
    prb_PathEntryIter iter = prb_createPathEntryIter(pathAbs);
    while (prb_pathEntryIterNext(&iter)) {
        prb_PathEntryIter iterCopy = iter;
        if (prb_pathEntryIterNext(&iterCopy)) {
            if (!prb_pathEntryIterNext(&iterCopy)) {
                break;
            }
        }
    }
    prb_Str result = iter.curEntryPath;
    return result;
}

prb_PUBLICDEF prb_Str
prb_getLastEntryInPath(prb_Str path) {
    prb_assert(path.ptr && path.len > 0);
    prb_PathEntryIter iter = prb_createPathEntryIter(path);
    while (prb_pathEntryIterNext(&iter)) {}
    prb_Str result = iter.curEntryName;
    return result;
}

prb_PUBLICDEF prb_Str
prb_replaceExt(prb_Arena* arena, prb_Str path, prb_Str newExt) {
    bool    dotFound = false;
    int32_t dotIndex = path.len - 1;
    for (; dotIndex >= 0; dotIndex--) {
        char ch = path.ptr[dotIndex];
        if (prb_charIsSep(ch)) {
            break;
        } else if (ch == '.') {
            dotFound = true;
            break;
        }
    }
    prb_Str result = {.ptr = 0, .len = 0};
    if (dotFound) {
        result = prb_fmt(arena, "%.*s.%.*s", dotIndex, path.ptr, prb_LIT(newExt));
    } else {
        result = prb_fmt(arena, "%.*s.%.*s", prb_LIT(path), prb_LIT(newExt));
    }
    return result;
}

prb_PUBLICDEF prb_PathEntryIter
prb_createPathEntryIter(prb_Str path) {
    prb_PathEntryIter iter;
    prb_memset(&iter, 0, sizeof(iter));
    iter.ogstr = path;
    return iter;
}

prb_PUBLICDEF prb_Status
prb_pathEntryIterNext(prb_PathEntryIter* iter) {
    prb_Status result = prb_Failure;
    if (iter->curOffset < iter->ogstr.len) {
        result = prb_Success;
        int32_t oldOffset = iter->curOffset;

        bool    sepFound = false;
        int32_t firstFoundSepIndex = 0;
        while (iter->curOffset < iter->ogstr.len && !sepFound) {
            if (prb_charIsSep(iter->ogstr.ptr[iter->curOffset])) {
                firstFoundSepIndex = iter->curOffset;

#if prb_PLATFORM_WINDOWS
                // NOTE(khvorov) Windows's //network type path
                sepFound = !(iter->curOffset == 0 && iter->ogstr.len >= 2 && prb_charIsSep(iter->ogstr.ptr[1]));
                if (!sepFound) {
                    iter->curOffset = 2;
                }
#elif prb_PLATFORM_LINUX
                sepFound = true;
#else
#error unimplemented
#endif

                // NOTE(khvorov) Ignore multiple separators in a row
                while (iter->curOffset < iter->ogstr.len && prb_charIsSep(iter->ogstr.ptr[iter->curOffset])) {
                    iter->curOffset += 1;
                }
            } else {
                iter->curOffset += 1;
            }
        }

        if (!sepFound) {
            firstFoundSepIndex = iter->ogstr.len;
        }

        // NOTE(khvorov) Linux's root `/` path. Also valid on windows (means relative to the root of the current volume)
        if (oldOffset == 0 && firstFoundSepIndex == 0) {
            firstFoundSepIndex = 1;
        }

        prb_assert(firstFoundSepIndex > oldOffset);
        iter->curEntryName = prb_strSlice(iter->ogstr, oldOffset, firstFoundSepIndex);
        iter->curEntryPath = prb_strSlice(iter->ogstr, 0, firstFoundSepIndex);
    }
    return result;
}

prb_PUBLICDEF void
prb_getAllDirEntriesCustomBuffer(prb_Arena* arena, prb_Str dir, prb_Recursive mode, prb_Str** storage) {
    if (dir.ptr && dir.len > 0) {
#if prb_PLATFORM_WINDOWS

        prb_Str* dirs = 0;
        prb_stbds_arrput(dirs, dir);

        while (prb_stbds_arrlen(dirs) > 0) {
            prb_Str          thisDir = prb_stbds_arrpop(dirs);
            WIN32_FIND_DATAW findData;
            prb_memset(&findData, 0, sizeof(findData));
            prb_Str             pattern = prb_pathJoin(arena, thisDir, prb_STR("*"));
            prb_windows_WideStr pathWide = prb_windows_getWidePath(arena, pattern);
            HANDLE              handle = FindFirstFileExW(pathWide.ptr, FindExInfoStandard, &findData, FindExSearchNameMatch, 0, 0);
            if (handle != INVALID_HANDLE_VALUE) {
                for (;;) {
                    prb_windows_WideStr fileNameWide = {findData.cFileName, prb_arrayCount(findData.cFileName)};
                    bool                isDot = fileNameWide.ptr[0] == '.' && fileNameWide.ptr[1] == '\0';
                    bool                isDoubleDot = fileNameWide.ptr[0] == '.' && fileNameWide.ptr[1] == '.' && fileNameWide.ptr[2] == '\0';
                    if (!isDot && !isDoubleDot) {
                        prb_Str filename = prb_windows_strFromWideStr(arena, fileNameWide);
                        prb_Str filepath = prb_pathJoin(arena, thisDir, filename);
                        prb_stbds_arrput(*storage, filepath);
                        if (mode == prb_Recursive_Yes) {
                            bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;
                            if (isDir) {
                                prb_stbds_arrput(dirs, filepath);
                            }
                        }
                    }
                    if (FindNextFileW(handle, &findData) == 0) {
                        FindClose(handle);
                        break;
                    }
                }
            }
        }

        prb_stbds_arrfree(dirs);

#elif prb_PLATFORM_LINUX

        prb_Str* dirs = 0;
        prb_stbds_arrput(dirs, dir);

        while (prb_stbds_arrlen(dirs) > 0) {
            prb_Str thisDir = prb_stbds_arrpop(dirs);
            prb_linux_OpenResult openRes = prb_linux_open(arena, thisDir, O_RDONLY | O_DIRECTORY, 0);
            if (openRes.success) {
                prb_arenaAlignFreePtr(arena, prb_alignof(prb_linux_Dirent64));
                prb_linux_Dirent64* buf = (prb_linux_Dirent64*)(prb_arenaFreePtr(arena));
                unsigned int bufSize = (unsigned int)prb_min(prb_arenaFreeSize(arena), 1 * prb_GIGABYTE);
                bufSize += prb_getOffsetForAlignment((void*)(uintptr_t)bufSize, prb_alignof(prb_linux_Dirent64));
                long syscallReturn = syscall(SYS_getdents64, openRes.handle, buf, bufSize);
                if (syscallReturn > 0) {
                    prb_arenaChangeUsed(arena, syscallReturn);
                    for (long offset = 0; offset < syscallReturn;) {
                        prb_linux_Dirent64* ent = (prb_linux_Dirent64*)((uint8_t*)buf + offset);
                        bool isDot = ent->d_name[0] == '.' && ent->d_name[1] == '\0';
                        bool isDoubleDot = ent->d_name[0] == '.' && ent->d_name[1] == '.' && ent->d_name[2] == '\0';
                        if (!isDot && !isDoubleDot) {
                            prb_Str fullpath = prb_pathJoin(arena, thisDir, prb_STR(ent->d_name));
                            if (mode == prb_Recursive_Yes) {
                                bool isDir = ent->d_type == DT_DIR;
                                if (!isDir && ent->d_type == DT_UNKNOWN) {
                                    isDir = prb_isDir(arena, fullpath);
                                }
                                if (isDir) {
                                    prb_stbds_arrput(dirs, fullpath);
                                }
                            }
                            prb_stbds_arrput(*storage, fullpath);
                        }
                        offset += ent->d_reclen;
                    }
                }
            }
        }

        prb_stbds_arrfree(dirs);

#else
#error unimplemented
#endif
    }
}

prb_PUBLICDEF prb_Str*
prb_getAllDirEntries(prb_Arena* arena, prb_Str dir, prb_Recursive mode) {
    prb_Str* entries = 0;
    prb_getAllDirEntriesCustomBuffer(arena, dir, mode, &entries);
    return entries;
}

prb_PUBLICDEF prb_FileTimestamp
prb_getLastModified(prb_Arena* arena, prb_Str path) {
    prb_FileTimestamp result = {.valid = false, .timestamp = 0};

#if prb_PLATFORM_WINDOWS

    prb_windows_GetFileStatResult stat = prb_windows_getFileStat(arena, path);
    if (stat.success) {
        result.valid = true;
        result.timestamp = ((uint64_t)stat.stat.ftLastWriteTime.dwHighDateTime << 32) | stat.stat.ftLastWriteTime.dwLowDateTime;
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

prb_PUBLICDEF prb_Multitime
prb_createMultitime(void) {
    prb_Multitime result = {.validAddedTimestampsCount = 0, .invalidAddedTimestampsCount = 0, .timeLatest = 0, .timeEarliest = UINT64_MAX};
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
prb_readEntireFile(prb_Arena* arena, prb_Str path) {
    prb_ReadEntireFileResult result;
    prb_memset(&result, 0, sizeof(result));

#if prb_PLATFORM_WINDOWS

    prb_windows_OpenResult handle = prb_windows_open(arena, path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, 0);
    if (handle.success) {
        LARGE_INTEGER size;
        prb_memset(&size, 0, sizeof(size));
        if (GetFileSizeEx(handle.handle, &size)) {
            prb_assert(size.QuadPart <= INT32_MAX);
            int32_t  bytesToRead = (int32_t)size.QuadPart;
            uint8_t* buf = (uint8_t*)prb_arenaAllocAndZero(arena, bytesToRead, 1);
            DWORD    bytesRead = 0;
            if (ReadFile(handle.handle, buf, (DWORD)bytesToRead, &bytesRead, 0)) {
                prb_assert(bytesRead <= INT32_MAX);
                if ((int32_t)bytesRead == bytesToRead) {
                    result.success = true;
                    result.content.data = buf;
                    result.content.len = (int32_t)bytesRead;
                }
            }
        }
        CloseHandle(handle.handle);
    }

#elif prb_PLATFORM_LINUX

    prb_linux_OpenResult handle = prb_linux_open(arena, path, O_RDONLY, 0);
    if (handle.success) {
        struct stat statBuf = {};
        if (fstat(handle.handle, &statBuf) == 0) {
            uint8_t* buf = (uint8_t*)prb_arenaAllocAndZero(arena, statBuf.st_size + 1, 1);  // NOTE(sen) Null terminator just in case
            int32_t readResult = read(handle.handle, buf, statBuf.st_size);
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
prb_writeEntireFile(prb_Arena* arena, prb_Str path, const void* content, int32_t contentLen) {
    prb_assert(contentLen >= 0);
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Status     result = prb_Failure;
    prb_Str        parent = prb_getParentDir(arena, path);
    if (prb_createDirIfNotExists(arena, parent)) {
#if prb_PLATFORM_WINDOWS
        prb_windows_OpenResult handle = prb_windows_open(arena, path, GENERIC_WRITE, 0, CREATE_ALWAYS, 0);
        if (handle.success) {
            DWORD bytesWritten = 0;
            if (WriteFile(handle.handle, content, (DWORD)contentLen, &bytesWritten, 0)) {
                prb_assert(bytesWritten <= INT32_MAX);
                result = (int32_t)bytesWritten == contentLen ? prb_Success : prb_Failure;
            }
            CloseHandle(handle.handle);
        }
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
    }
    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_FileHash
prb_getFileHash(prb_Arena* arena, prb_Str filepath) {
    prb_FileHash             result = {.valid = false, .hash = 0};
    prb_TempMemory           temp = prb_beginTempMemory(arena);
    prb_ReadEntireFileResult readRes = prb_readEntireFile(arena, filepath);
    if (readRes.success) {
        result.valid = true;
        result.hash = prb_stbds_hash_bytes(readRes.content.data, (size_t)readRes.content.len, (size_t)1);
    }
    prb_endTempMemory(temp);
    return result;
}

//
// SECTION Strings (implementation)
//

prb_PUBLICDEF bool
prb_streq(prb_Str str1, prb_Str str2) {
    bool result = false;
    if (str1.len == str2.len) {
        result = prb_memeq(str1.ptr, str2.ptr, str1.len);
    }
    return result;
}

prb_PUBLICDEF prb_Str
prb_strSlice(prb_Str str, int32_t start, int32_t onePastEnd) {
    prb_assert(onePastEnd >= start);
    prb_Str result = {str.ptr + start, onePastEnd - start};
    return result;
}

prb_PUBLICDEF const char*
prb_strGetNullTerminated(prb_Arena* arena, prb_Str str) {
    const char* result = prb_fmt(arena, "%.*s", prb_LIT(str)).ptr;
    return result;
}

prb_PUBLICDEF prb_Str
prb_strFromBytes(prb_Bytes bytes) {
    prb_Str result = {(const char*)bytes.data, bytes.len};
    return result;
}

prb_PUBLICDEF prb_Str
prb_strTrimSide(prb_Str str, prb_StrDirection dir) {
    prb_Str result = str;

    bool    found = false;
    int32_t index = 0;
    int32_t byteStart = 0;
    int32_t onePastEnd = str.len;
    int32_t delta = 1;
    if (dir == prb_StrDirection_FromEnd) {
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
            case prb_StrDirection_FromStart: result = prb_strSlice(str, index, str.len); break;
            case prb_StrDirection_FromEnd: result.len = index + 1; break;
        }
    } else {
        result.len = 0;
    }

    return result;
}

prb_PUBLICDEF prb_Str
prb_strTrim(prb_Str str) {
    prb_Str result = prb_strTrimSide(str, prb_StrDirection_FromStart);
    result = prb_strTrimSide(result, prb_StrDirection_FromEnd);
    return result;
}

prb_PUBLICDEF prb_StrFindResult
prb_strFind(prb_Str str, prb_StrFindSpec spec) {
    prb_StrFindResult result;
    prb_memset(&result, 0, sizeof(result));

    prb_StrFindMode mode = spec.mode;
    if (spec.pattern.len == 1 && mode == prb_StrFindMode_Exact) {
        mode = prb_StrFindMode_AnyChar;
    }

    switch (mode) {
        case prb_StrFindMode_Exact: {
            // Raita string matching algorithm
            // https://en.wikipedia.org/wiki/Raita_algorithm

            if (str.len >= spec.pattern.len && spec.pattern.len > 0) {
                uint8_t* bstr = (uint8_t*)str.ptr;
                uint8_t* pat = (uint8_t*)spec.pattern.ptr;
                int32_t  charOffsets[256];

                for (int32_t i = 0; i < 256; ++i) {
                    charOffsets[i] = spec.pattern.len;
                }

                {
                    int32_t from = 0;
                    int32_t to = spec.pattern.len - 1;
                    int32_t delta = 1;
                    if (spec.direction == prb_StrDirection_FromEnd) {
                        from = spec.pattern.len - 1;
                        to = 0;
                        delta = -1;
                    }

                    int32_t count = 0;
                    for (int32_t i = from; i != to; i += delta) {
                        uint8_t patternChar = pat[i];
                        charOffsets[patternChar] = spec.pattern.len - count++ - 1;
                    }

                    if (spec.direction == prb_StrDirection_FromEnd) {
                        for (int32_t i = 0; i < 256; ++i) {
                            charOffsets[i] *= -1;
                        }
                    }
                }

                uint8_t patFirstCh = pat[0];
                uint8_t patMiddleCh = pat[spec.pattern.len / 2];
                uint8_t patLastCh = pat[spec.pattern.len - 1];

                int32_t off = 0;
                if (spec.direction == prb_StrDirection_FromEnd) {
                    off = str.len - spec.pattern.len;
                }

                for (;;) {
                    bool notEnoughStrLeft = false;
                    switch (spec.direction) {
                        case prb_StrDirection_FromStart: notEnoughStrLeft = off + spec.pattern.len > str.len; break;
                        case prb_StrDirection_FromEnd: notEnoughStrLeft = off < 0; break;
                    }

                    if (notEnoughStrLeft) {
                        break;
                    }

                    uint8_t strFirstChar = bstr[off];
                    uint8_t strLastCh = bstr[off + spec.pattern.len - 1];
                    if (patLastCh == strLastCh && patMiddleCh == bstr[off + spec.pattern.len / 2] && patFirstCh == strFirstChar
                        && prb_memeq(pat + 1, bstr + off + 1, spec.pattern.len - 2)) {
                        result.found = true;
                        result.beforeMatch = prb_strSlice(str, 0, off);
                        result.match = prb_strSlice(str, off, off + spec.pattern.len);
                        result.afterMatch = prb_strSlice(str, off + spec.pattern.len, str.len);
                        break;
                    }

                    uint8_t relChar = strLastCh;
                    if (spec.direction == prb_StrDirection_FromEnd) {
                        relChar = strFirstChar;
                    }
                    off += charOffsets[relChar];
                }
            }
        } break;

        case prb_StrFindMode_AnyChar: {
            if (str.len > 0) {
                for (prb_Utf8CharIter iter = prb_createUtf8CharIter(str, spec.direction);
                     prb_utf8CharIterNext(&iter) == prb_Success && !result.found;) {
                    if (iter.curIsValid) {
                        for (prb_Utf8CharIter patIter = prb_createUtf8CharIter(spec.pattern, prb_StrDirection_FromStart);
                             prb_utf8CharIterNext(&patIter) == prb_Success && !result.found;) {
                            if (patIter.curIsValid) {
                                if (iter.curUtf32Char == patIter.curUtf32Char) {
                                    result.found = true;
                                    result.beforeMatch = prb_strSlice(str, 0, iter.curByteOffset);
                                    result.match = prb_strSlice(str, iter.curByteOffset, iter.curByteOffset + iter.curUtf8Bytes);
                                    result.afterMatch = prb_strSlice(str, iter.curByteOffset + iter.curUtf8Bytes, str.len);
                                    break;
                                }
                            }
                        }
                    }
                }

                if (!result.found && spec.alwaysMatchEnd) {
                    int32_t matchPos = str.len;
                    if (spec.direction == prb_StrDirection_FromEnd) {
                        matchPos = 0;
                    }
                    result.found = true;
                    result.beforeMatch = prb_strSlice(str, 0, matchPos);
                    result.match = prb_strSlice(str, matchPos, matchPos);
                    result.afterMatch = prb_strSlice(str, matchPos, str.len);
                }
            }
        } break;

        case prb_StrFindMode_LineBreak: {
            if (str.len > 0) {
                int32_t start = 0;
                int32_t onePastEnd = str.len;
                int32_t delta = 1;
                if (spec.direction == prb_StrDirection_FromEnd) {
                    start = str.len - 1;
                    onePastEnd = -1;
                    delta = -1;
                }

                bool    found = false;
                int32_t index = start;
                for (; index != onePastEnd; index += delta) {
                    char ch = str.ptr[index];
                    if (ch == '\n' || ch == '\r') {
                        found = true;
                        break;
                    }
                }

                int32_t lineEndLen = 0;
                if (found) {
                    lineEndLen = 1;
                    bool isForwardDouble = spec.direction == prb_StrDirection_FromStart && index + 1 < str.len && str.ptr[index] == '\r' && str.ptr[index + 1] == '\n';
                    bool isBackwardDouble = spec.direction == prb_StrDirection_FromEnd && index - 1 >= 0 && str.ptr[index] == '\n' && str.ptr[index - 1] == '\r';
                    if (isForwardDouble || isBackwardDouble) {
                        lineEndLen = 2;
                    }
                }

                int32_t lineEndIndex = index;
                if (spec.direction == prb_StrDirection_FromEnd) {
                    lineEndIndex = index - lineEndLen + 1;
                }

                result.found = true;
                result.beforeMatch = prb_strSlice(str, 0, lineEndIndex);
                result.match = prb_strSlice(str, lineEndIndex, lineEndIndex + lineEndLen);
                result.afterMatch = prb_strSlice(str, lineEndIndex + lineEndLen, str.len);
            }
        }
    }

    return result;
}

prb_PUBLICDEF bool
prb_strStartsWith(prb_Str str, prb_Str pattern) {
    bool result = false;
    if (pattern.len <= str.len) {
        result = prb_memeq(str.ptr, pattern.ptr, pattern.len);
    }
    return result;
}

prb_PUBLICDEF bool
prb_strEndsWith(prb_Str str, prb_Str pattern) {
    bool result = false;
    if (str.len >= pattern.len) {
        result = prb_memeq(str.ptr + str.len - pattern.len, pattern.ptr, pattern.len);
    }
    return result;
}

prb_PUBLICDEF prb_Str
prb_stringsJoin(prb_Arena* arena, prb_Str* strings, int32_t stringsCount, prb_Str sep) {
    prb_GrowingStr gstr = prb_beginStr(arena);
    for (int32_t strIndex = 0; strIndex < stringsCount; strIndex++) {
        prb_Str str = strings[strIndex];
        prb_addStrSegment(&gstr, "%.*s", str.len, str.ptr);
        if (strIndex < stringsCount - 1) {
            prb_addStrSegment(&gstr, "%.*s", sep.len, sep.ptr);
        }
    }
    prb_Str result = prb_endStr(&gstr);
    return result;
}

prb_PUBLICDEF prb_GrowingStr
prb_beginStr(prb_Arena* arena) {
    prb_assert(!arena->lockedForStr);
    arena->lockedForStr = true;
    prb_Str        str = {(const char*)prb_arenaFreePtr(arena), 0};
    prb_GrowingStr result = {arena, str};
    return result;
}

prb_PUBLICDEF void
prb_addStrSegment(prb_GrowingStr* gstr, const char* fmt, ...) {
    prb_assert(gstr->arena->lockedForStr);
    va_list args;
    va_start(args, fmt);
    prb_Str seg = prb_vfmtCustomBuffer((uint8_t*)prb_arenaFreePtr(gstr->arena), (int32_t)prb_min(prb_arenaFreeSize(gstr->arena), INT32_MAX), fmt, args);
    prb_arenaChangeUsed(gstr->arena, seg.len);
    gstr->str.len += seg.len;
    va_end(args);
}

prb_PUBLICDEF prb_Str
prb_endStr(prb_GrowingStr* gstr) {
    prb_assert(gstr->arena->lockedForStr);
    gstr->arena->lockedForStr = false;
    prb_arenaAllocAndZero(gstr->arena, 1, 1);  // NOTE(khvorov) Null terminator
    prb_Str result = gstr->str;
    prb_memset(gstr, 0, sizeof(*gstr));
    return result;
}

prb_PUBLICDEF prb_Str
prb_vfmtCustomBuffer(void* buf, int32_t bufSize, const char* fmt, va_list args) {
    int32_t len = prb_stbsp_vsnprintf((char*)buf, bufSize, fmt, args);
    prb_Str result = {(const char*)buf, len};
    return result;
}

prb_PUBLICDEF prb_Str
prb_fmt(prb_Arena* arena, const char* fmt, ...) {
    prb_assert(!arena->lockedForStr);
    va_list args;
    va_start(args, fmt);
    prb_Str result = prb_vfmtCustomBuffer(prb_arenaFreePtr(arena), (int32_t)prb_min(prb_arenaFreeSize(arena), INT32_MAX), fmt, args);
    prb_arenaChangeUsed(arena, result.len);
    prb_arenaAllocAndZero(arena, 1, 1);  // NOTE(khvorov) Null terminator
    va_end(args);
    return result;
}

prb_PUBLICDEF prb_Status
prb_writeToStdout(prb_Str msg) {
    prb_Status result = prb_Failure;
#if prb_PLATFORM_WINDOWS

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(out, msg.ptr, (DWORD)msg.len, 0, 0);

#elif prb_PLATFORM_LINUX

    ssize_t writeResult = write(STDOUT_FILENO, msg.ptr, msg.len);
    result = writeResult == msg.len ? prb_Success : prb_Failure;

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF prb_Status
prb_writelnToStdout(prb_Arena* arena, prb_Str str) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Status     result = prb_writeToStdout(prb_fmt(arena, "%.*s\n", prb_LIT(str)));
    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Str
prb_colorEsc(prb_ColorID color) {
    prb_Str str = {.ptr = 0, .len = 0};
    switch (color) {
        case prb_ColorID_Reset: str = prb_STR("\x1b[0m"); break;
        case prb_ColorID_Black: str = prb_STR("\x1b[30m"); break;
        case prb_ColorID_Red: str = prb_STR("\x1b[31m"); break;
        case prb_ColorID_Green: str = prb_STR("\x1b[32m"); break;
        case prb_ColorID_Yellow: str = prb_STR("\x1b[33m"); break;
        case prb_ColorID_Blue: str = prb_STR("\x1b[34m"); break;
        case prb_ColorID_Magenta: str = prb_STR("\x1b[35m"); break;
        case prb_ColorID_Cyan: str = prb_STR("\x1b[36m"); break;
        case prb_ColorID_White: str = prb_STR("\x1b[37m"); break;
    }
    return str;
}

prb_PUBLICDEF prb_Utf8CharIter
prb_createUtf8CharIter(prb_Str str, prb_StrDirection direction) {
    prb_assert(str.ptr && str.len >= 0);
    int32_t curByteOffset = -1;
    if (direction == prb_StrDirection_FromEnd) {
        curByteOffset = str.len;
    }

    prb_Utf8CharIter iter = {
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
prb_utf8CharIterNext(prb_Utf8CharIter* iter) {
    prb_Status result = prb_Failure;

    if (iter->curIsValid) {
        switch (iter->direction) {
            case prb_StrDirection_FromStart: iter->curByteOffset += iter->curUtf8Bytes; break;
            case prb_StrDirection_FromEnd: iter->curByteOffset -= 1; break;
        }
    } else {
        switch (iter->direction) {
            case prb_StrDirection_FromStart: iter->curByteOffset += 1; break;
            case prb_StrDirection_FromEnd: iter->curByteOffset -= 1; break;
        }
    }
    iter->curByteOffset = prb_clamp(iter->curByteOffset, -1, iter->str.len);
    iter->curUtf8Bytes = 0;
    iter->curUtf32Char = 0;
    iter->curIsValid = false;

    bool more = iter->curByteOffset < iter->str.len;
    if (iter->direction == prb_StrDirection_FromEnd) {
        more = iter->curByteOffset >= 0;
    }

    if (more) {
        result = prb_Success;

        uint32_t ch = 0;
        int32_t  chBytes = 0;
        bool     isValid = false;

        uint8_t firstByte = (uint8_t)iter->str.ptr[iter->curByteOffset];
        bool    isAscii = firstByte < 0b10000000;
        if (isAscii) {
            isValid = true;
            ch = firstByte;
            chBytes = 1;
        } else {
            uint8_t firstByteMask[] = {
                0b00011111,
                0b00001111,
                0b00000111,
            };

            switch (iter->direction) {
                case prb_StrDirection_FromStart: {
                    isValid = firstByte >= 0b11000000 && firstByte < 0b11111000;
                    if (isValid) {
                        chBytes = 2;
                        if (firstByte >= 0b11100000) {
                            chBytes = 3;
                            if (firstByte >= 0b11110000) {
                                chBytes = 4;
                            }
                        }
                        ch = (uint32_t)(firstByte & firstByteMask[chBytes - 2]);
                        for (int32_t byteIndex = 1; byteIndex < chBytes; byteIndex++) {
                            uint8_t byte = (uint8_t)iter->str.ptr[iter->curByteOffset + byteIndex];
                            if (byte >= 0b10000000 && byte < 0b11000000) {
                                ch = (ch << 6) | (byte & 0b00111111);
                            } else {
                                isValid = false;
                                break;
                            }
                        }
                    }
                } break;

                case prb_StrDirection_FromEnd: {
                    if (firstByte < 0b11000000) {
                        ch = (uint32_t)(firstByte & 0b00111111);
                        int32_t maxExtraBytes = prb_min(3, iter->curByteOffset);
                        for (int32_t byteIndex = 0; byteIndex < maxExtraBytes; byteIndex++) {
                            uint8_t byte = (uint8_t)iter->str.ptr[iter->curByteOffset - 1 - byteIndex];
                            int32_t chBytesTaken = 6 * (byteIndex + 1);
                            if (byte >= 0b10000000 && byte < 0b11000000) {
                                ch = ((byte & 0b00111111) << chBytesTaken) | ch;
                            } else {
                                if (byte >= 0b11000000 && byte < 0b11111000) {
                                    int32_t leadingOnes = 2;
                                    if (byte >= 0b11100000) {
                                        leadingOnes = 3;
                                        if (byte >= 0b11110000) {
                                            leadingOnes = 4;
                                        }
                                    }
                                    if (leadingOnes == byteIndex + 2) {
                                        isValid = true;
                                        chBytes = leadingOnes;
                                        ch = ((byte & firstByteMask[leadingOnes - 2]) << chBytesTaken) | ch;
                                        iter->curByteOffset -= (byteIndex + 1);
                                        break;
                                    } else {
                                        break;
                                    }
                                } else {
                                    break;
                                }
                            }
                        }
                    }
                } break;
            }
        }

        if (isValid) {
            iter->curUtf32Char = ch;
            iter->curUtf8Bytes = chBytes;
            iter->curIsValid = true;
            iter->curCharCount += 1;
        }
    }

    return result;
}

prb_PUBLICDEF prb_StrScanner
prb_createStrScanner(prb_Str str) {
    prb_assert(str.ptr && str.len >= 0);
    prb_StrScanner iter = {
        .ogstr = str,
        .beforeMatch = prb_strSlice(str, 0, 0),
        .match = str,
        .afterMatch = str,
        .matchCount = 0,
        .betweenLastMatches = prb_strSlice(str, 0, 0),
    };
    iter.match.len = 0;
    return iter;
}

prb_PUBLICDEF prb_Status
prb_strScannerMove(prb_StrScanner* scanner, prb_StrFindSpec spec, prb_StrScannerSide side) {
    prb_Status result = prb_Failure;

    prb_Str search = scanner->afterMatch;
    if (side == prb_StrScannerSide_BeforeMatch) {
        search = scanner->beforeMatch;
    }

    prb_StrFindResult find = prb_strFind(search, spec);
    if (find.found) {
        scanner->betweenLastMatches = find.beforeMatch;
        if (side == prb_StrScannerSide_BeforeMatch) {
            scanner->betweenLastMatches = find.afterMatch;
        }

        scanner->match = find.match;
        scanner->matchCount += 1;

        scanner->beforeMatch = (prb_Str) {scanner->ogstr.ptr, (int32_t)(scanner->match.ptr - scanner->ogstr.ptr)};
        const char* afterStart = scanner->match.ptr + scanner->match.len;
        scanner->afterMatch = (prb_Str) {afterStart, (int32_t)((scanner->ogstr.ptr + scanner->ogstr.len) - afterStart)};

        result = prb_Success;
    }

    return result;
}

prb_PUBLICDEF prb_ParseUintResult
prb_parseUint(prb_Str digits, uint64_t base) {
    prb_assert(base == 16 || base == 10);
    prb_ParseUintResult result = {.success = false, .number = 0};
    if (digits.len > 0) {
        result.success = true;
        for (int32_t digitsIndex = 0; digitsIndex < digits.len && result.success; digitsIndex++) {
            uint64_t value = 0;
            char     ch = digits.ptr[digitsIndex];
            if (ch >= '0' && ch <= '9') {
                value = (uint64_t)(ch - '0');
            } else if (base == 16 && ch >= 'A' && ch <= 'F') {
                value = (uint64_t)(ch - 'A' + 10);
            } else if (base == 16 && ch >= 'a' && ch <= 'f') {
                value = (uint64_t)(ch - 'a' + 10);
            } else {
                result.success = false;
            }
            result.number = result.number * base + value;
        }
    }
    return result;
}

prb_PUBLICDEF prb_ParsedNumber
prb_parseNumber(prb_Str str) {
    prb_ParsedNumber number;
    prb_memset(&number, 0, sizeof(number));

    bool leadingMinus = false;
    if (str.len > 0) {
        leadingMinus = str.ptr[0] == '-';
        if (leadingMinus) {
            str = prb_strSlice(str, 1, str.len);
        }
    }

    prb_ParseUintResult intParse = {.success = false, .number = 0};
    bool                isReal = false;
    double              realValue = 0.0;
    if (str.len > 0) {
        // NOTE(khvorov) Hex integer
        if (prb_strStartsWith(str, prb_STR("0x"))) {
            prb_Str digits = prb_strSlice(str, 2, str.len);
            intParse = prb_parseUint(digits, 16);
        } else {
            prb_StrFindSpec dotFindSpec = {
                .mode = prb_StrFindMode_AnyChar,
                .direction = prb_StrDirection_FromStart,
                .pattern = prb_STR("."),
                .alwaysMatchEnd = false,
            };
            prb_StrFindResult dotFind = prb_strFind(str, dotFindSpec);
            if (dotFind.found) {
                // NOTE(khvorov) Real number
                prb_ParseUintResult leftOfDot = {.success = false, .number = 0};
                if (dotFind.beforeMatch.len == 0) {
                    leftOfDot.success = true;
                } else {
                    leftOfDot = prb_parseUint(dotFind.beforeMatch, 10);
                }

                if (leftOfDot.success) {
                    realValue = (double)leftOfDot.number;

                    prb_ParseUintResult rightOfDot = {.success = false, .number = 0};
                    if (dotFind.afterMatch.len == 0) {
                        rightOfDot.success = true;
                    } else {
                        rightOfDot = prb_parseUint(dotFind.afterMatch, 10);
                    }

                    if (rightOfDot.success) {
                        isReal = true;
                        int32_t  digitsLeft = dotFind.afterMatch.len - 1;
                        uint64_t divisor = 10;
                        while (digitsLeft > 0) {
                            divisor *= 10;
                            digitsLeft -= 1;
                        }
                        double fraction = (double)rightOfDot.number / (double)divisor;
                        realValue += fraction;
                    }
                }
            } else {
                // NOTE(khvorov) Decimal integer
                intParse = prb_parseUint(str, 10);
            }
        }
    }

    if (intParse.success) {
        if (leadingMinus) {
            // NOTE(khvorov) Won't be supporting parsing INT64_MIN for fear of undefined behaviour
            if (intParse.number <= (((uint64_t)INT64_MAX))) {
                number.kind = prb_ParsedNumberKind_I64;
                number.parsedI64 = -((int64_t)intParse.number);
            }
        } else {
            number.kind = prb_ParsedNumberKind_U64;
            number.parsedU64 = intParse.number;
        }
    } else if (isReal) {
        number.kind = prb_ParsedNumberKind_F64;
        number.parsedF64 = realValue;
        if (leadingMinus) {
            number.parsedF64 *= -1.0;
        }
    }

    return number;
}

prb_PUBLICDEF prb_Str
prb_binaryToCArray(prb_Arena* arena, prb_Str arrayName, void* data, int32_t dataLen) {
    prb_GrowingStr arrayGstr = prb_beginStr(arena);
    prb_addStrSegment(&arrayGstr, "unsigned char %.*s[] = {\n    ", prb_LIT(arrayName));
    for (int32_t byteIndex = 0; byteIndex < dataLen; byteIndex++) {
        uint8_t byte = ((uint8_t*)data)[byteIndex];
        prb_addStrSegment(&arrayGstr, "0x%x", byte);
        if (byteIndex != dataLen - 1) {
            prb_addStrSegment(&arrayGstr, ",");
            if ((byteIndex + 1) % 10 == 0) {
                prb_addStrSegment(&arrayGstr, "\n    ");
            } else {
                prb_addStrSegment(&arrayGstr, " ");
            }
        } else {
            prb_addStrSegment(&arrayGstr, "\n");
        }
    }
    prb_addStrSegment(&arrayGstr, "};");
    prb_Str arrayStr = prb_endStr(&arrayGstr);
    return arrayStr;
}

//
// SECTION Processes (implementation)
//

#if prb_PLATFORM_WINDOWS

static void
prb_windows_waitForProcess(prb_Process* handle) {
    WaitForSingleObject(handle->processInfo.hProcess, INFINITE);
    handle->status = prb_ProcessStatus_CompletedFailed;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(handle->processInfo.hProcess, &exitCode)) {
        if (exitCode == 0) {
            handle->status = prb_ProcessStatus_CompletedSuccess;
        }
    }
    CloseHandle(handle->processInfo.hProcess);
    CloseHandle(handle->processInfo.hThread);
}

typedef struct prb_windows_GetAffinityResult {
    bool      success;
    DWORD_PTR affinity;
    int32_t   setBits;
} prb_windows_GetAffinityResult;

static prb_windows_GetAffinityResult
prb_windows_getAffinity(void) {
    prb_windows_GetAffinityResult result = {.success = false, .affinity = 0, .setBits = 0};
    HANDLE                        thisProc = GetCurrentProcess();
    DWORD_PTR                     procAffinity = 0;
    DWORD_PTR                     sysAffinity = 0;
    if (GetProcessAffinityMask(thisProc, &procAffinity, &sysAffinity)) {
        result.success = true;
        result.affinity = procAffinity;
        for (size_t bitIndex = 0; bitIndex < sizeof(DWORD_PTR) * 8; bitIndex++) {
            DWORD_PTR mask = 1ULL << bitIndex;
            result.setBits += (result.affinity & mask) != 0;
        }
    }
    return result;
}

#elif prb_PLATFORM_LINUX

static prb_Bytes
prb_linux_readFromProc(prb_Arena* arena, prb_Str filename) {
    prb_linux_OpenResult handle = prb_linux_open(arena, prb_fmt(arena, "/proc/%.*s", prb_LIT(filename)), O_RDONLY, 0);
    prb_assert(handle.success);
    prb_GrowingStr gstr = prb_beginStr(arena);
    gstr.str.len = read(handle.handle, (void*)gstr.str.ptr, prb_arenaFreeSize(arena));
    prb_assert(gstr.str.len > 0);
    prb_arenaChangeUsed(arena, gstr.str.len);
    prb_Str str = prb_endStr(&gstr);
    prb_Bytes result = {(uint8_t*)str.ptr, str.len};
    return result;
}

static void
prb_linux_waitForProcess(prb_Process* handle) {
    int32_t status = 0;
    pid_t waitResult = waitpid(handle->pid, &status, 0);
    handle->status = prb_ProcessStatus_CompletedFailed;
    if (waitResult == handle->pid && status == 0) {
        handle->status = prb_ProcessStatus_CompletedSuccess;
    }
}

typedef struct prb_linux_GetAffinityResult {
    bool success;
    uint8_t* affinity;
    int32_t size;
    int32_t setBits;
} prb_linux_GetAffinityResult;

static prb_linux_GetAffinityResult
prb_linux_getAffinity(prb_Arena* arena) {
    // NOTE(khvorov) The syscall is picky about alignment and can't handle buffers that are too big.
    // I don't know how big is too big but a megabyte is apparently not too big.
    prb_linux_GetAffinityResult result = {};
    int32_t reqAlign = prb_alignof(unsigned long);
    prb_arenaAlignFreePtr(arena, reqAlign);
    result.affinity = (uint8_t*)prb_arenaFreePtr(arena);
    int32_t arenaSize = (int32_t)prb_min(prb_arenaFreeSize(arena), INT32_MAX);
    int32_t arenaSizeAligned = arenaSize & (~(reqAlign - 1));
    int32_t sizeReduced = prb_min(arenaSizeAligned, prb_MEGABYTE);
    result.size = syscall(SYS_sched_getaffinity, 0, sizeReduced, result.affinity);
    if (result.size > 0) {
        result.success = true;
        for (int32_t byteIndex = 0; byteIndex < result.size; byteIndex++) {
            uint8_t byte = ((uint8_t*)result.affinity)[byteIndex];
            for (int32_t bitIndex = 0; bitIndex < 8; bitIndex++) {
                uint8_t mask = 1 << bitIndex;
                result.setBits += (byte & mask) != 0;
            }
        }
    }
    return result;
}

#endif

prb_PUBLICDEF void
prb_terminate(int32_t code) {
#if prb_PLATFORM_WINDOWS
    ExitProcess((UINT)code);
#elif prb_PLATFORM_LINUX
    exit(code);
#else
#error unimplemented
#endif
}

prb_PUBLICDEF prb_Str
prb_getCmdline(prb_Arena* arena) {
    prb_Str result = {.ptr = 0, .len = 0};

#if prb_PLATFORM_WINDOWS

    LPWSTR  wstr = GetCommandLineW();
    int32_t len = 0;
    while (wstr[len]) {
        len += 1;
    }
    result = prb_windows_strFromWideStr(arena, (prb_windows_WideStr) {wstr, len});

#elif prb_PLATFORM_LINUX

    prb_Bytes procSelfContent = prb_linux_readFromProc(arena, prb_STR("self/cmdline"));
    for (int32_t byteIndex = 0; byteIndex < procSelfContent.len - 1; byteIndex++) {
        if (procSelfContent.data[byteIndex] == '\0') {
            procSelfContent.data[byteIndex] = ' ';
        }
    }

    result = (prb_Str) {(const char*)procSelfContent.data, procSelfContent.len - 1};

#elif
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF prb_Str*
prb_getCmdArgs(prb_Arena* arena) {
    prb_Str* result = 0;

#if prb_PLATFORM_WINDOWS

    prb_Str         str = prb_getCmdline(arena);
    prb_StrFindSpec spec = {
        .mode = prb_StrFindMode_AnyChar,
        .direction = prb_StrDirection_FromStart,
        .pattern = prb_STR(" "),
        .alwaysMatchEnd = true,
    };
    prb_StrScanner scanner = prb_createStrScanner(str);
    while (prb_strScannerMove(&scanner, spec, prb_StrScannerSide_AfterMatch)) {
        if (scanner.betweenLastMatches.len > 0) {
            prb_stbds_arrput(result, scanner.betweenLastMatches);
        }
    }

#elif prb_PLATFORM_LINUX

    prb_Bytes procSelfContent = prb_linux_readFromProc(arena, prb_STR("self/cmdline"));
    prb_Str procSelfContentLeft = {(const char*)procSelfContent.data, procSelfContent.len};
    for (int32_t byteIndex = 0; byteIndex < procSelfContent.len; byteIndex++) {
        if (procSelfContent.data[byteIndex] == '\0') {
            int32_t processed = procSelfContent.len - procSelfContentLeft.len;
            prb_Str arg = {procSelfContentLeft.ptr, byteIndex - processed};
            prb_stbds_arrput(result, arg);
            procSelfContentLeft = prb_strSlice(procSelfContentLeft, arg.len + 1, procSelfContentLeft.len);
        }
    }

#elif
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF const char**
prb_getArgArrayFromStr(prb_Arena* arena, prb_Str str) {
    const char** args = 0;

    {
        prb_StrScanner  scanner = prb_createStrScanner(str);
        prb_StrFindSpec space = {
            .mode = prb_StrFindMode_AnyChar,
            .direction = prb_StrDirection_FromStart,
            .pattern = prb_STR(" "),
            .alwaysMatchEnd = true,
        };
        while (prb_strScannerMove(&scanner, space, prb_StrScannerSide_AfterMatch)) {
            if (scanner.betweenLastMatches.len > 0) {
                const char* argNull = prb_fmt(arena, "%.*s", prb_LIT(scanner.betweenLastMatches)).ptr;
                prb_stbds_arrput(args, argNull);
            }
        }
    }

    // NOTE(khvorov) Arg array needs a null at the end
    prb_stbds_arrsetcap(args, (size_t)(prb_stbds_arrlen(args) + 1));
    args[prb_stbds_arrlen(args)] = 0;

    return args;
}

prb_PUBLICDEF prb_CoreCountResult
prb_getCoreCount(prb_Arena* arena) {
    prb_TempMemory      temp = prb_beginTempMemory(arena);
    prb_CoreCountResult result = {.success = false, .cores = 0};
#if prb_PLATFORM_WINDOWS
    SYSTEM_INFO sysinfo;
    prb_memset(&sysinfo, 0, sizeof(sysinfo));
    GetSystemInfo(&sysinfo);
    result.success = true;
    result.cores = (int32_t)sysinfo.dwNumberOfProcessors;
#elif prb_PLATFORM_LINUX
    prb_Str cpuinfo = prb_strFromBytes(prb_linux_readFromProc(arena, prb_STR("cpuinfo")));
    prb_StrScanner scanner = prb_createStrScanner(cpuinfo);
    prb_StrFindSpec spec = {};
    spec.pattern = prb_STR("siblings");
    if (prb_strScannerMove(&scanner, spec, prb_StrScannerSide_AfterMatch)) {
        spec.pattern = prb_STR(":");
        if (prb_strScannerMove(&scanner, spec, prb_StrScannerSide_AfterMatch)) {
            spec.mode = prb_StrFindMode_LineBreak;
            if (prb_strScannerMove(&scanner, spec, prb_StrScannerSide_AfterMatch)) {
                prb_Str coresStr = prb_strTrim(scanner.betweenLastMatches);
                prb_ParseUintResult parse = prb_parseUint(coresStr, 10);
                if (parse.success) {
                    result.success = true;
                    result.cores = parse.number;
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

prb_PUBLICDEF prb_CoreCountResult
prb_getAllowExecutionCoreCount(prb_Arena* arena) {
    prb_CoreCountResult result = {.success = false, .cores = 0};
    prb_TempMemory      temp = prb_beginTempMemory(arena);

#if prb_PLATFORM_WINDOWS
    prb_windows_GetAffinityResult affinityRes = prb_windows_getAffinity();
    if (affinityRes.success) {
        result.success = true;
        result.cores = affinityRes.setBits;
    }
#elif prb_PLATFORM_LINUX
    prb_linux_GetAffinityResult affinityRes = prb_linux_getAffinity(arena);
    if (affinityRes.success) {
        result.success = true;
        result.cores = affinityRes.setBits;
    }
#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Status
prb_allowExecutionOnCores(prb_Arena* arena, int32_t coreCount) {
    prb_Status     result = prb_Failure;
    prb_TempMemory temp = prb_beginTempMemory(arena);
    if (coreCount >= 1) {
#if prb_PLATFORM_WINDOWS
        prb_windows_GetAffinityResult affinityRes = prb_windows_getAffinity();
        if (affinityRes.success) {
            DWORD_PTR newAffinityMask = affinityRes.affinity;
            int32_t   coreDelta = coreCount - affinityRes.setBits;
            for (int32_t bitIndex = 0; bitIndex < (int32_t)sizeof(DWORD_PTR) * 8 && coreDelta != 0; bitIndex++) {
                DWORD_PTR mask = ((DWORD_PTR)1) << bitIndex;
                bool      coreIsScheduled = (newAffinityMask & mask) == mask;
                if (coreIsScheduled) {
                    if (coreDelta > 0) {
                        newAffinityMask = newAffinityMask | mask;
                        coreDelta -= 1;
                    } else {
                        newAffinityMask = newAffinityMask & (~mask);
                        coreDelta += 1;
                    }
                }
            }

            if (coreCount != affinityRes.setBits) {
                if (SetProcessAffinityMask(GetCurrentProcess(), newAffinityMask)) {
                    result = prb_Success;
                }
            } else {
                result = prb_Success;
            }
        }

#elif prb_PLATFORM_LINUX

        prb_linux_GetAffinityResult affinityRes = prb_linux_getAffinity(arena);
        if (affinityRes.success) {
            if (coreCount > affinityRes.setBits) {
                int32_t coresToSchedule = coreCount - affinityRes.setBits;
                for (int32_t byteIndex = 0; byteIndex < affinityRes.size && coresToSchedule > 0; byteIndex++) {
                    uint8_t byte = affinityRes.affinity[byteIndex];
                    for (int32_t bitIndex = 0; bitIndex < 8 && coresToSchedule > 0; bitIndex++) {
                        uint8_t mask = 1 << bitIndex;
                        bool coreIsScheduled = (byte & mask) != 0;
                        if (!coreIsScheduled) {
                            byte = byte | mask;
                            affinityRes.affinity[byteIndex] = byte;
                            coresToSchedule -= 1;
                        }
                    }
                }
            } else if (coreCount < affinityRes.setBits) {
                int32_t coresToUnschedule = affinityRes.setBits - coreCount;
                for (int32_t byteIndex = 0; byteIndex < affinityRes.size && coresToUnschedule > 0; byteIndex++) {
                    uint8_t byte = affinityRes.affinity[byteIndex];
                    for (int32_t bitIndex = 0; bitIndex < 8 && coresToUnschedule > 0; bitIndex++) {
                        uint8_t mask = 1 << bitIndex;
                        bool coreIsScheduled = (byte & mask) != 0;
                        if (coreIsScheduled) {
                            byte = byte & (~mask);
                            affinityRes.affinity[byteIndex] = byte;
                            coresToUnschedule -= 1;
                        }
                    }
                }
            }

            if (coreCount != affinityRes.setBits) {
                int setAffinityResult = syscall(SYS_sched_setaffinity, 0, affinityRes.size, affinityRes.affinity);
                if (setAffinityResult == 0) {
                    result = prb_Success;
                }
            } else {
                result = prb_Success;
            }
        }
#else
#error unimplemented
#endif
    }
    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Process
prb_createProcess(prb_Str cmd, prb_ProcessSpec spec) {
    prb_Process proc;
    prb_memset(&proc, 0, sizeof(proc));
    proc.cmd = cmd;
    proc.spec = spec;
    return proc;
}

prb_PUBLICDEF prb_Status
prb_launchProcesses(prb_Arena* arena, prb_Process* procs, int32_t procCount, prb_Background mode) {
    prb_Status     result = prb_Success;
    prb_TempMemory temp = prb_beginTempMemory(arena);

    for (int32_t procIndex = 0; procIndex < procCount; procIndex++) {
        prb_Process* proc = procs + procIndex;
        if (proc->status == prb_ProcessStatus_NotLaunched) {
            prb_ProcessSpec spec = proc->spec;

#if prb_PLATFORM_WINDOWS

            STARTUPINFOW startupInfo;
            prb_memset(&startupInfo, 0, sizeof(startupInfo));
            startupInfo.cb = sizeof(STARTUPINFOW);

            bool    redirectSuccessful = true;
            BOOL    inheritHandles = spec.redirectStdout || spec.redirectStderr;
            HANDLE  handlesToClose[2] = {0, 0};
            int32_t handlesToCloseCount = 0;

            if (inheritHandles) {
                startupInfo.dwFlags = STARTF_USESTDHANDLES;

                SECURITY_ATTRIBUTES securityAttr = {
                    .nLength = sizeof(securityAttr),
                    .lpSecurityDescriptor = NULL,
                    .bInheritHandle = TRUE,
                };

                prb_Str stdoutPath = spec.stdoutFilepath;
                if (stdoutPath.ptr == 0 || stdoutPath.len == 0) {
                    stdoutPath = prb_STR("NUL");
                }

                prb_Str stderrPath = spec.stderrFilepath;
                if (stderrPath.ptr == 0 || stderrPath.len == 0) {
                    stderrPath = prb_STR("NUL");
                }

                if (spec.redirectStdout && spec.redirectStderr && prb_streq(stdoutPath, stderrPath)) {
                    prb_windows_OpenResult openRes = prb_windows_open(arena, stdoutPath, GENERIC_WRITE, FILE_SHARE_WRITE, CREATE_ALWAYS, &securityAttr);
                    if (openRes.success) {
                        startupInfo.hStdOutput = openRes.handle;
                        startupInfo.hStdError = openRes.handle;
                        handlesToClose[handlesToCloseCount++] = openRes.handle;
                    } else {
                        redirectSuccessful = false;
                    }
                } else {
                    if (spec.redirectStdout) {
                        prb_windows_OpenResult openRes = prb_windows_open(arena, stdoutPath, GENERIC_WRITE, 0, CREATE_ALWAYS, &securityAttr);
                        if (openRes.success) {
                            startupInfo.hStdOutput = openRes.handle;
                            handlesToClose[handlesToCloseCount++] = openRes.handle;
                        } else {
                            redirectSuccessful = false;
                        }
                    }

                    if (spec.redirectStderr) {
                        prb_windows_OpenResult openRes = prb_windows_open(arena, stderrPath, GENERIC_WRITE, 0, CREATE_ALWAYS, &securityAttr);
                        if (openRes.success) {
                            startupInfo.hStdError = openRes.handle;
                            handlesToClose[handlesToCloseCount++] = openRes.handle;
                        } else {
                            redirectSuccessful = false;
                        }
                    }
                }
            }

            if (redirectSuccessful) {
                // NOTE(khvorov) Just replace spaces with null terminators and stick that to the start of the
                // existing environment and call it a day.
                LPWCH env = 0;
                if (spec.addEnv.ptr && spec.addEnv.len > 0) {
                    prb_windows_WideStr envCopy = prb_windows_getWideStr(arena, spec.addEnv);
                    for (int32_t envIndex = 0; envIndex < envCopy.len; envIndex++) {
                        if (envCopy.ptr[envIndex] == ' ') {
                            envCopy.ptr[envIndex] = '\0';
                        }
                    }
                    LPWCH   existingEnv = GetEnvironmentStringsW();
                    int32_t existingEnvLen = 0;
                    for (;;) {
                        if (existingEnv[existingEnvLen] == '\0' && existingEnv[existingEnvLen + 1] == '\0') {
                            break;
                        } else {
                            existingEnvLen += 1;
                        }
                    }
                    env = (LPWCH)prb_arenaAllocArray(arena, uint16_t, envCopy.len + 1 + existingEnvLen + 2);
                    prb_memcpy(env, envCopy.ptr, envCopy.len * sizeof(*envCopy.ptr));
                    prb_memcpy(env + envCopy.len + 1, existingEnv, existingEnvLen * sizeof(*existingEnv));
                    FreeEnvironmentStringsW(existingEnv);
                }

                prb_windows_WideStr wcmd = prb_windows_getWideStr(arena, proc->cmd);
                if (CreateProcessW(0, wcmd.ptr, 0, 0, inheritHandles, CREATE_UNICODE_ENVIRONMENT, env, 0, &startupInfo, &proc->processInfo)) {
                    proc->status = prb_ProcessStatus_Launched;
                    if (mode == prb_Background_No) {
                        prb_windows_waitForProcess(proc);
                    }
                }
            }

            for (int32_t handleIndex = 0; handleIndex < handlesToCloseCount; handleIndex++) {
                CloseHandle(handlesToClose[handleIndex]);
            }

#elif prb_PLATFORM_LINUX

            const char* stdoutPath = 0;
            if (spec.redirectStdout) {
                if (spec.stdoutFilepath.ptr && spec.stdoutFilepath.len > 0) {
                    stdoutPath = prb_strGetNullTerminated(arena, spec.stdoutFilepath);
                } else {
                    stdoutPath = "/dev/null";
                }
            }

            const char* stderrPath = 0;
            if (spec.redirectStderr) {
                if (spec.stderrFilepath.ptr && spec.stderrFilepath.len > 0) {
                    stderrPath = prb_strGetNullTerminated(arena, spec.stderrFilepath);
                } else {
                    stderrPath = "/dev/null";
                }
            }

            bool fileActionsSucceeded = true;
            bool fileActionsInited = false;
            posix_spawn_file_actions_t* fileActionsPtr = 0;
            posix_spawn_file_actions_t fileActions = {};
            if (spec.redirectStdout || spec.redirectStderr) {
                fileActionsPtr = &fileActions;
                int initResult = posix_spawn_file_actions_init(&fileActions);
                fileActionsSucceeded = initResult == 0;
                fileActionsInited = initResult == 0;
                if (fileActionsSucceeded) {
                    if (spec.redirectStdout) {
                        int stdoutRedirectResult = posix_spawn_file_actions_addopen(
                            &fileActions,
                            STDOUT_FILENO,
                            stdoutPath,
                            O_WRONLY | O_CREAT | O_TRUNC,
                            S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR
                        );
                        fileActionsSucceeded = stdoutRedirectResult == 0;
                    }

                    if (fileActionsSucceeded && spec.redirectStderr) {
                        if (spec.redirectStdout && prb_streq(prb_STR(stdoutPath), prb_STR(stderrPath))) {
                            int dupResult = posix_spawn_file_actions_adddup2(&fileActions, STDOUT_FILENO, STDERR_FILENO);
                            fileActionsSucceeded = dupResult == 0;
                        } else {
                            int stderrRedirectResult = posix_spawn_file_actions_addopen(
                                &fileActions,
                                STDERR_FILENO,
                                stderrPath,
                                O_WRONLY | O_CREAT | O_TRUNC,
                                S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR
                            );
                            fileActionsSucceeded = stderrRedirectResult == 0;
                        }
                    }
                }
            }

            if (fileActionsSucceeded) {
                bool envSucceeded = true;
                char** env = __environ;
                bool envAllocated = false;
                if (proc->spec.addEnv.ptr && proc->spec.addEnv.len > 0) {
                    envAllocated = true;
                    env = 0;

                    prb_StrFindSpec space = {};
                    space.pattern = prb_STR(" ");
                    space.alwaysMatchEnd = true;
                    prb_StrFindSpec equals = {};
                    equals.pattern = prb_STR("=");
                    prb_StrScanner scanner = prb_createStrScanner(proc->spec.addEnv);
                    prb_Str* newVarNames = 0;
                    while (envSucceeded && prb_strScannerMove(&scanner, space, prb_StrScannerSide_AfterMatch)) {
                        if (scanner.betweenLastMatches.len > 0) {
                            envSucceeded = false;
                            prb_StrScanner nameScanner = prb_createStrScanner(scanner.betweenLastMatches);
                            if (prb_strScannerMove(&nameScanner, equals, prb_StrScannerSide_AfterMatch)) {
                                if (nameScanner.betweenLastMatches.len > 0) {
                                    char* newEnvEntry = (char*)prb_strGetNullTerminated(arena, scanner.betweenLastMatches);
                                    prb_stbds_arrput(env, newEnvEntry);
                                    prb_stbds_arrput(newVarNames, nameScanner.betweenLastMatches);
                                    envSucceeded = true;
                                }
                            }
                        }
                    }

                    if (envSucceeded) {
                        for (int32_t envIndex = 0;; envIndex++) {
                            char* existingEntry = __environ[envIndex];
                            if (existingEntry) {
                                prb_StrScanner nameScanner = prb_createStrScanner(prb_STR(existingEntry));
                                prb_assert(prb_strScannerMove(&nameScanner, equals, prb_StrScannerSide_AfterMatch));
                                prb_assert(nameScanner.betweenLastMatches.len > 0);
                                prb_Str existingName = nameScanner.betweenLastMatches;
                                bool existingInNew = false;
                                for (int32_t newVarIndex = 0; newVarIndex < prb_stbds_arrlen(newVarNames) && !existingInNew; newVarIndex++) {
                                    prb_Str newVarName = newVarNames[newVarIndex];
                                    existingInNew = prb_streq(newVarName, existingName);
                                }
                                if (!existingInNew) {
                                    prb_stbds_arrput(env, existingEntry);
                                }
                            } else {
                                break;
                            }
                        }

                        // NOTE(khvorov) Null-terminate the new env array
                        prb_stbds_arrput(env, 0);
                    }

                    prb_stbds_arrfree(newVarNames);
                }

                if (envSucceeded) {
                    const char** args = prb_getArgArrayFromStr(arena, proc->cmd);
                    int spawnResult = posix_spawnp(&proc->pid, args[0], fileActionsPtr, 0, (char**)args, env);
                    if (spawnResult == 0) {
                        proc->status = prb_ProcessStatus_Launched;
                        if (mode == prb_Background_No) {
                            prb_linux_waitForProcess(proc);
                        }
                    }
                    prb_stbds_arrfree(args);
                }

                if (envAllocated) {
                    prb_stbds_arrfree(env);
                }
            }

            if (fileActionsInited) {
                posix_spawn_file_actions_destroy(fileActionsPtr);
            }

#else
#error unimplemented
#endif

            prb_ProcessStatus reqStatus = prb_ProcessStatus_CompletedSuccess;
            if (mode == prb_Background_Yes) {
                reqStatus = prb_ProcessStatus_Launched;
            }
            if (proc->status != reqStatus) {
                result = prb_Failure;
            }
        }
    }

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Status
prb_waitForProcesses(prb_Process* handles, int32_t handleCount) {
    prb_Status result = prb_Success;

    for (int32_t handleIndex = 0; handleIndex < handleCount; handleIndex++) {
        prb_Process* handle = handles + handleIndex;
        prb_assert(handle->status != prb_ProcessStatus_NotLaunched);
        if (handle->status == prb_ProcessStatus_Launched) {
#if prb_PLATFORM_WINDOWS
            prb_windows_waitForProcess(handle);
#elif prb_PLATFORM_LINUX
            prb_linux_waitForProcess(handle);
#else
#error unimplemented
#endif

            if (handle->status != prb_ProcessStatus_CompletedSuccess) {
                result = prb_Failure;
            }
        }
    }

    return result;
}

prb_PUBLICDEF prb_Status
prb_killProcesses(prb_Process* handles, int32_t handleCount) {
    prb_Status result = prb_Success;

    for (int32_t handleIndex = 0; handleIndex < handleCount; handleIndex++) {
        prb_Process* handle = handles + handleIndex;
        prb_assert(handle->status != prb_ProcessStatus_NotLaunched);
        if (handle->status == prb_ProcessStatus_Launched) {
#if prb_PLATFORM_WINDOWS
            if (TerminateProcess(handle->processInfo.hProcess, 9)) {
                handle->status = prb_ProcessStatus_CompletedFailed;
                CloseHandle(handle->processInfo.hProcess);
                CloseHandle(handle->processInfo.hThread);
            }
#elif prb_PLATFORM_LINUX
            if (kill(handle->pid, SIGKILL) == 0) {
                handle->status = prb_ProcessStatus_CompletedFailed;
            }
#else
#error unimplemented
#endif

            if (handle->status != prb_ProcessStatus_CompletedFailed) {
                result = prb_Failure;
            }
        }
    }

    return result;
}

prb_PUBLICDEF void
prb_sleep(float ms) {
#if prb_PLATFORM_WINDOWS

    prb_TimeStart timeStart = prb_timeStart();
    DWORD         msec = (DWORD)(ms);
    Sleep(msec);
    float msSpentSleeping = prb_getMsFrom(timeStart);

    float left = ms - msSpentSleeping;
    float msSpentSpinning = 0.0f;
    timeStart = prb_timeStart();
    while (msSpentSpinning < left) {
        msSpentSpinning = prb_getMsFrom(timeStart);
    }

#elif prb_PLATFORM_LINUX

    float secf = ms * 0.001f;
    long int sec = (long int)(secf);
    long int nsec = (long int)((secf - (float)sec) * 1000.0f * 1000.0f * 1000.0f);
    struct timespec ts = {.tv_sec = sec, .tv_nsec = nsec};
    nanosleep(&ts, 0);

#else
#error unimplemented
#endif
}

prb_PUBLICDEF bool
prb_debuggerPresent(prb_Arena* arena) {
    bool           result = false;
    prb_TempMemory temp = prb_beginTempMemory(arena);

#if prb_PLATFORM_WINDOWS

    result = IsDebuggerPresent();

#elif prb_PLATFORM_LINUX

    prb_Bytes content = prb_linux_readFromProc(arena, prb_STR("self/status"));
    prb_Str str = {(const char*)content.data, content.len};
    prb_StrScanner iter = prb_createStrScanner(str);
    prb_StrFindSpec lineBreakSpec = {};
    lineBreakSpec.mode = prb_StrFindMode_LineBreak;
    while (prb_strScannerMove(&iter, lineBreakSpec, prb_StrScannerSide_AfterMatch)) {
        prb_Str search = prb_STR("TracerPid:");
        if (prb_strStartsWith(iter.betweenLastMatches, search)) {
            prb_Str number = prb_strTrim(prb_strSlice(iter.betweenLastMatches, search.len, iter.betweenLastMatches.len));
            result = number.len > 1 || number.ptr[0] != '0';
            break;
        }
    }

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_Status
prb_setenv(prb_Arena* arena, prb_Str name, prb_Str value) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Status     result = prb_Failure;

#if prb_PLATFORM_WINDOWS

    prb_windows_WideStr wname = prb_windows_getWideStr(arena, name);
    prb_windows_WideStr wvalue = prb_windows_getWideStr(arena, value);
    if (SetEnvironmentVariableW(wname.ptr, wvalue.ptr)) {
        result = prb_Success;
    }

#elif prb_PLATFORM_LINUX
    const char* nameNull = prb_strGetNullTerminated(arena, name);
    const char* valueNull = prb_strGetNullTerminated(arena, value);
    result = setenv(nameNull, valueNull, 1) == 0 ? prb_Success : prb_Failure;
#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF prb_GetenvResult
prb_getenv(prb_Arena* arena, prb_Str name) {
    prb_GetenvResult result;
    prb_memset(&result, 0, sizeof(result));

#if prb_PLATFORM_WINDOWS

    prb_arenaAlignFreePtr(arena, prb_alignof(uint16_t));
    prb_windows_WideStr wname = prb_windows_getWideStr(arena, name);
    LPWSTR              wptr = (LPWSTR)prb_arenaFreePtr(arena);
    DWORD               getEnvResult = GetEnvironmentVariableW(wname.ptr, wptr, (DWORD)prb_min(prb_arenaFreeSize(arena), UINT32_MAX));
    if (getEnvResult > 0) {
        prb_arenaChangeUsed(arena, (int32_t)getEnvResult * (int32_t)sizeof(*wptr));
        prb_arenaAllocAndZero(arena, 2, 1);  // NOTE(khvorov) Null terminator
        result.str = prb_windows_strFromWideStr(arena, (prb_windows_WideStr) {wptr, (int32_t)getEnvResult});
        result.found = true;
    }

#elif prb_PLATFORM_LINUX
    prb_TempMemory temp = prb_beginTempMemory(arena);
    const char* nameNull = prb_strGetNullTerminated(arena, name);
    char* str = getenv(nameNull);
    if (str) {
        result.found = true;
        result.str = prb_STR(str);
    }
    prb_endTempMemory(temp);

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF prb_Status
prb_unsetenv(prb_Arena* arena, prb_Str name) {
    prb_TempMemory temp = prb_beginTempMemory(arena);
    prb_Status     result = prb_Failure;

#if prb_PLATFORM_WINDOWS

    prb_windows_WideStr wname = prb_windows_getWideStr(arena, name);
    if (SetEnvironmentVariableW(wname.ptr, NULL)) {
        result = prb_Success;
    }

#elif prb_PLATFORM_LINUX

    const char* nameNull = prb_strGetNullTerminated(arena, name);
    result = unsetenv(nameNull) == 0 ? prb_Success : prb_Failure;

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

//
// SECTION Timing (implementation)
//

prb_PUBLICDEF prb_TimeStart
prb_timeStart(void) {
    prb_TimeStart result;
    prb_memset(&result, 0, sizeof(result));
#if prb_PLATFORM_WINDOWS
    LARGE_INTEGER ticks;
    prb_memset(&ticks, 0, sizeof(ticks));
    if (QueryPerformanceCounter(&ticks)) {
        result.valid = true;
        result.ticks = ticks.QuadPart;
    }
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
#if prb_PLATFORM_WINDOWS
        LARGE_INTEGER ticksPerSecond;
        prb_memset(&ticksPerSecond, 0, sizeof(ticksPerSecond));
        if (QueryPerformanceFrequency(&ticksPerSecond)) {
            LONGLONG ticksDiff = now.ticks - timeStart.ticks;
            float    secs = (float)ticksDiff / (float)ticksPerSecond.QuadPart;
            result = secs * 1000.0f;
        }
#elif prb_PLATFORM_LINUX
        uint64_t nsec = now.nsec - timeStart.nsec;
        result = (float)nsec / 1000.0f / 1000.0f;
#else
#error unimplemented
#endif
    }
    return result;
}

//
// SECTION Multithreading (implementation)
//

#if prb_PLATFORM_WINDOWS

static DWORD WINAPI
prb_windows_threadProc(void* data) {
    prb_Job* job = (prb_Job*)data;
    job->proc(&job->arena, job->data);
    return 0;
}

#elif prb_PLATFORM_LINUX

static void*
prb_linux_threadProc(void* data) {
    prb_Job* job = (prb_Job*)data;
    job->proc(&job->arena, job->data);
    return 0;
}

#endif

prb_PUBLICDEF prb_Job
prb_createJob(prb_JobProc proc, void* data, prb_Arena* arena, int32_t arenaBytes) {
    prb_Job job;
    prb_memset(&job, 0, sizeof(job));
    job.proc = proc;
    job.data = data;
    job.arena = prb_createArenaFromArena(arena, arenaBytes);
    return job;
}

prb_PUBLICDEF prb_Status
prb_launchJobs(prb_Job* jobs, int32_t jobsCount, prb_Background mode) {
    prb_Status result = prb_Success;

    switch (mode) {
        case prb_Background_No: {
            for (int32_t jobIndex = 0; jobIndex < jobsCount && result == prb_Success; jobIndex++) {
                prb_Job* job = jobs + jobIndex;
                if (job->status == prb_JobStatus_NotLaunched) {
                    job->status = prb_JobStatus_Launched;
                    job->proc(&job->arena, job->data);
                    job->status = prb_JobStatus_Completed;
                }
            }
        } break;

        case prb_Background_Yes: {
            for (int32_t jobIndex = 0; jobIndex < jobsCount && result == prb_Success; jobIndex++) {
                prb_Job* job = jobs + jobIndex;
                if (job->status == prb_JobStatus_NotLaunched) {
                    job->status = prb_JobStatus_Launched;
#if prb_PLATFORM_WINDOWS
                    job->threadhandle = CreateThread(0, 0, prb_windows_threadProc, job, 0, &job->threadid);
                    if (job->threadhandle == NULL) {
                        result = prb_Failure;
                    }
#elif prb_PLATFORM_LINUX
                    if (pthread_create(&job->threadid, 0, prb_linux_threadProc, job) != 0) {
                        result = prb_Failure;
                    }
#else
#error unimplemented
#endif
                }
            }
        } break;
    }

    return result;
}

prb_PUBLICDEF prb_Status
prb_waitForJobs(prb_Job* jobs, int32_t jobsCount) {
    prb_Status result = prb_Success;
    for (int32_t jobIndex = 0; jobIndex < jobsCount; jobIndex++) {
        prb_Job* job = jobs + jobIndex;
        prb_assert(job->status != prb_JobStatus_NotLaunched);
        if (job->status == prb_JobStatus_Launched) {
#if prb_PLATFORM_WINDOWS

            if (WaitForSingleObject(job->threadhandle, INFINITE) == WAIT_OBJECT_0) {
                job->status = prb_JobStatus_Completed;
            } else {
                result = prb_Failure;
            }

#elif prb_PLATFORM_LINUX

            if (pthread_join(job->threadid, 0) == 0) {
                job->status = prb_JobStatus_Completed;
            } else {
                result = prb_Failure;
            }

#else
#error unimplemented
#endif
        }
    }

    return result;
}

//
// SECTION Random numbers (implementation)
//

prb_PUBLICDEF prb_Rng
prb_createRng(uint32_t seed) {
    prb_Rng rng = {.state = seed, .inc = seed | 1};
    // NOTE(khvorov) When seed is 0 the first 2 numbers are always 0 which is probably not what we want
    prb_randomU32(&rng);
    prb_randomU32(&rng);
    return rng;
}

// PCG-XSH-RR
// state_new = a * state_old + b
// output = rotate32((state ^ (state >> 18)) >> 27, state >> 59)
// as per `PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for Random Number Generation`
prb_PUBLICDEF uint32_t
prb_randomU32(prb_Rng* rng) {
    uint64_t state = rng->state;
    uint64_t xorWith = state >> 18u;
    uint64_t xored = state ^ xorWith;
    uint64_t shifted64 = xored >> 27u;
    uint32_t shifted32 = (uint32_t)shifted64;
    uint32_t rotateBy = state >> 59u;
    uint32_t shiftRightBy = rotateBy;
    uint32_t resultRight = shifted32 >> shiftRightBy;
    // NOTE(khvorov) This is `32 - rotateBy` but produces 0 when rotateBy is 0
    // Shifting a 32 bit value by 32 is apparently UB and the compiler is free to remove that code
    // I guess, so we are avoiding it by doing this weird bit hackery
    uint32_t shiftLeftBy = (-rotateBy) & 0b11111u;
    uint32_t resultLeft = shifted32 << shiftLeftBy;
    uint32_t result = resultRight | resultLeft;
    // NOTE(khvorov) This is just one of those magic LCG constants "in common use"
    // https://en.wikipedia.org/wiki/Linear_congruential_generator#Parameters_in_common_use
    rng->state = 6364136223846793005ULL * state + rng->inc;
    return result;
}

prb_PUBLICDEF uint32_t
prb_randomU32Bound(prb_Rng* rng, uint32_t max) {
    // NOTE(khvorov) This is equivalent to (UINT32_MAX + 1) % max;
    uint32_t threshold = -max % max;
    uint32_t unbound = prb_randomU32(rng);
    while (unbound < threshold) {
        unbound = prb_randomU32(rng);
    }
    uint32_t result = unbound % max;
    return result;
}

prb_PUBLICDEF float
prb_randomF3201(prb_Rng* rng) {
    uint32_t randomU32 = prb_randomU32(rng);
    float    randomF32 = (float)randomU32;
    float    onePastMaxRandomU32 = (float)(1ULL << 32ULL);
    float    result = randomF32 / onePastMaxRandomU32;
    return result;
}

//
// SECTION stb snprintf (implementation)
//

#define prb_stbsp__uint32 unsigned int
#define prb_stbsp__int32 signed int

#ifdef _MSC_VER
#define prb_stbsp__uint64 unsigned __int64
#define prb_stbsp__int64 signed __int64
#else
#define prb_stbsp__uint64 unsigned long long
#define prb_stbsp__int64 signed long long
#endif
#define prb_stbsp__uint16 unsigned short

#ifndef prb_stbsp__uintptr
#if defined(__ppc64__) || defined(__powerpc64__) || defined(__aarch64__) || defined(_M_X64) || defined(__x86_64__) \
    || defined(__x86_64) || defined(__s390x__)
#define prb_stbsp__uintptr prb_stbsp__uint64
#else
#define prb_stbsp__uintptr prb_stbsp__uint32
#endif
#endif

#ifndef prb_STB_SPRINTF_MSVC_MODE  // used for MSVC2013 and earlier (MSVC2015 matches GCC)
#if defined(_MSC_VER) && (_MSC_VER < 1900)
#define prb_STB_SPRINTF_MSVC_MODE
#endif
#endif

#ifdef prb_STB_SPRINTF_NOUNALIGNED  // define this before inclusion to force prb_stbsp_sprintf to always use aligned accesses
#define prb_STBSP__UNALIGNED(code)
#else
#define prb_STBSP__UNALIGNED(code) code
#endif

#ifndef prb_STB_SPRINTF_NOFLOAT
// internal float utility functions
static prb_stbsp__int32 prb_stbsp__real_to_str(
    char const**       start,
    prb_stbsp__uint32* len,
    char*              out,
    prb_stbsp__int32*  decimal_pos,
    double             value,
    prb_stbsp__uint32  frac_digits
);
static prb_stbsp__int32 prb_stbsp__real_to_parts(prb_stbsp__int64* bits, prb_stbsp__int32* expo, double value);
#define prb_STBSP__SPECIAL 0x7000
#endif

static char prb_stbsp__period = '.';
static char prb_stbsp__comma = ',';
static struct {
    short temp;  // force next field to be 2-byte aligned
    char  pair[201];
} prb_stbsp__digitpair = {
    0,
    "00010203040506070809101112131415161718192021222324"
    "25262728293031323334353637383940414243444546474849"
    "50515253545556575859606162636465666768697071727374"
    "75767778798081828384858687888990919293949596979899"};

prb_STBSP__PUBLICDEF void
prb_STB_SPRINTF_DECORATE(set_separators)(char pcomma, char pperiod) {
    prb_stbsp__period = pperiod;
    prb_stbsp__comma = pcomma;
}

#define prb_STBSP__LEFTJUST 1
#define prb_STBSP__LEADINGPLUS 2
#define prb_STBSP__LEADINGSPACE 4
#define prb_STBSP__LEADING_0X 8
#define prb_STBSP__LEADINGZERO 16
#define prb_STBSP__INTMAX 32
#define prb_STBSP__TRIPLET_COMMA 64
#define prb_STBSP__NEGATIVE 128
#define prb_STBSP__METRIC_SUFFIX 256
#define prb_STBSP__HALFWIDTH 512
#define prb_STBSP__METRIC_NOSPACE 1024
#define prb_STBSP__METRIC_1024 2048
#define prb_STBSP__METRIC_JEDEC 4096

static void
prb_stbsp__lead_sign(prb_stbsp__uint32 fl, char* sign) {
    sign[0] = 0;
    if (fl & prb_STBSP__NEGATIVE) {
        sign[0] = 1;
        sign[1] = '-';
    } else if (fl & prb_STBSP__LEADINGSPACE) {
        sign[0] = 1;
        sign[1] = ' ';
    } else if (fl & prb_STBSP__LEADINGPLUS) {
        sign[0] = 1;
        sign[1] = '+';
    }
}

static prb_STBSP__ASAN prb_stbsp__uint32
prb_stbsp__strlen_limited(char const* s, prb_stbsp__uint32 limit) {
    char const* sn = s;

    // get up to 4-byte alignment
    for (;;) {
        if (((prb_stbsp__uintptr)sn & 3) == 0)
            break;

        if (!limit || *sn == 0)
            return (prb_stbsp__uint32)(sn - s);

        ++sn;
        --limit;
    }

    // scan over 4 bytes at a time to find terminating 0
    // this will intentionally scan up to 3 bytes past the end of buffers,
    // but becase it works 4B aligned, it will never cross page boundaries
    // (hence the prb_STBSP__ASAN markup; the over-read here is intentional
    // and harmless)
    while (limit >= 4) {
        prb_stbsp__uint32 v = *(prb_stbsp__uint32*)sn;
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

    return (prb_stbsp__uint32)(sn - s);
}

prb_STBSP__PUBLICDEF int
prb_STB_SPRINTF_DECORATE(vsprintfcb)(prb_STBSP_SPRINTFCB* callback, void* user, char* buf, char const* fmt, va_list va) {
    static char hex[] = "0123456789abcdefxp";
    static char hexu[] = "0123456789ABCDEFXP";
    char*       bf;
    char const* f;
    int         tlen = 0;

    bf = buf;
    f = fmt;
    for (;;) {
        prb_stbsp__int32  fw, pr, tz;
        prb_stbsp__uint32 fl;

// macros for the callback buffer stuff
#define prb_stbsp__chk_cb_bufL(bytes) \
    { \
        int len = (int)(bf - buf); \
        if ((len + (bytes)) >= prb_STB_SPRINTF_MIN) { \
            tlen += len; \
            if (0 == (bf = buf = callback(buf, user, len))) \
                goto done; \
        } \
    }
#define prb_stbsp__chk_cb_buf(bytes) \
    { \
        if (callback) { \
            prb_stbsp__chk_cb_bufL(bytes); \
        } \
    }
#define prb_stbsp__flush_cb() \
    { prb_stbsp__chk_cb_bufL(prb_STB_SPRINTF_MIN - 1); }  // flush if there is even one byte in the buffer
#define prb_stbsp__cb_buf_clamp(cl, v) \
    cl = v; \
    if (callback) { \
        int lg = prb_STB_SPRINTF_MIN - (int)(bf - buf); \
        if (cl > lg) \
            cl = lg; \
    }

        // fast copy everything up to the next % (or end of string)
        for (;;) {
            while (((prb_stbsp__uintptr)f) & 3) {
            schk1:
                if (f[0] == '%')
                    goto scandd;
            schk2:
                if (f[0] == 0)
                    goto endfmt;
                prb_stbsp__chk_cb_buf(1);
                *bf++ = f[0];
                ++f;
            }
            for (;;) {
                // Check if the next 4 bytes contain %(0x25) or end of string.
                // Using the 'hasless' trick:
                // https://graphics.stanford.edu/~seander/bithacks.html#HasLessInWord
                prb_stbsp__uint32 v, c;
                v = *(prb_stbsp__uint32*)f;
                c = (~v) & 0x80808080;
                if (((v ^ 0x25252525) - 0x01010101) & c)
                    goto schk1;
                if ((v - 0x01010101) & c)
                    goto schk2;
                if (callback)
                    if ((prb_STB_SPRINTF_MIN - (int)(bf - buf)) < 4)
                        goto schk1;
#ifdef prb_STB_SPRINTF_NOUNALIGNED
                if (((prb_stbsp__uintptr)bf) & 3) {
                    bf[0] = f[0];
                    bf[1] = f[1];
                    bf[2] = f[2];
                    bf[3] = f[3];
                } else
#endif
                {
                    *(prb_stbsp__uint32*)bf = v;
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
                    fl |= prb_STBSP__LEFTJUST;
                    ++f;
                    continue;
                // if we have leading plus
                case '+':
                    fl |= prb_STBSP__LEADINGPLUS;
                    ++f;
                    continue;
                // if we have leading space
                case ' ':
                    fl |= prb_STBSP__LEADINGSPACE;
                    ++f;
                    continue;
                // if we have leading 0x
                case '#':
                    fl |= prb_STBSP__LEADING_0X;
                    ++f;
                    continue;
                // if we have thousand commas
                case '\'':
                    fl |= prb_STBSP__TRIPLET_COMMA;
                    ++f;
                    continue;
                // if we have kilo marker (none->kilo->kibi->jedec)
                case '$':
                    if (fl & prb_STBSP__METRIC_SUFFIX) {
                        if (fl & prb_STBSP__METRIC_1024) {
                            fl |= prb_STBSP__METRIC_JEDEC;
                        } else {
                            fl |= prb_STBSP__METRIC_1024;
                        }
                    } else {
                        fl |= prb_STBSP__METRIC_SUFFIX;
                    }
                    ++f;
                    continue;
                // if we don't want space between metric suffix and number
                case '_':
                    fl |= prb_STBSP__METRIC_NOSPACE;
                    ++f;
                    continue;
                // if we have leading zero
                case '0':
                    fl |= prb_STBSP__LEADINGZERO;
                    ++f;
                    goto flags_done;
                default: goto flags_done;
            }
        }
    flags_done:

        // get the field width
        if (f[0] == '*') {
            fw = va_arg(va, prb_stbsp__int32);
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
                pr = va_arg(va, prb_stbsp__int32);
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
                fl |= prb_STBSP__HALFWIDTH;
                ++f;
                if (f[0] == 'h')
                    ++f;  // QUARTERWIDTH
                break;
            // are we 64-bit (unix style)
            case 'l':
                fl |= ((sizeof(long) == 8) ? prb_STBSP__INTMAX : 0);
                ++f;
                if (f[0] == 'l') {
                    fl |= prb_STBSP__INTMAX;
                    ++f;
                }
                break;
            // are we 64-bit on intmax? (c99)
            case 'j':
                fl |= (sizeof(size_t) == 8) ? prb_STBSP__INTMAX : 0;
                ++f;
                break;
            // are we 64-bit on size_t or ptrdiff_t? (c99)
            case 'z':
                fl |= (sizeof(ptrdiff_t) == 8) ? prb_STBSP__INTMAX : 0;
                ++f;
                break;
            case 't':
                fl |= (sizeof(ptrdiff_t) == 8) ? prb_STBSP__INTMAX : 0;
                ++f;
                break;
            // are we 64-bit (msft style)
            case 'I':
                if ((f[1] == '6') && (f[2] == '4')) {
                    fl |= prb_STBSP__INTMAX;
                    f += 3;
                } else if ((f[1] == '3') && (f[2] == '2')) {
                    f += 3;
                } else {
                    fl |= ((sizeof(void*) == 8) ? prb_STBSP__INTMAX : 0);
                    ++f;
                }
                break;
            default: break;
        }

        // handle each replacement
        switch (f[0]) {
#define prb_STBSP__NUMSZ 512  // big enough for e308 (with commas) or e-307
            char              num[prb_STBSP__NUMSZ];
            char              lead[8];
            char              tail[8];
            char*             s;
            char const*       h;
            prb_stbsp__uint32 l, n, cs;
            prb_stbsp__uint64 n64;
#ifndef prb_STB_SPRINTF_NOFLOAT
            double fv;
#endif
            prb_stbsp__int32 dp;
            char const*      sn;

            case 's':
                // get the string
                s = va_arg(va, char*);
                if (s == 0)
                    s = (char*)"null";
                // get the length, limited to desired precision
                // always limit to ~0u chars since our counts are 32b
                l = prb_stbsp__strlen_limited(s, (pr >= 0) ? (prb_stbsp__uint32)pr : ~0u);
                lead[0] = 0;
                tail[0] = 0;
                pr = 0;
                dp = 0;
                cs = 0;
                // copy the string in
                goto scopy;

            case 'c':  // char
                // get the character
                s = num + prb_STBSP__NUMSZ - 1;
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

#ifdef prb_STB_SPRINTF_NOFLOAT
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
                prb_STBSP__NOTUSED(dp);
                goto scopy;
#else
            case 'A':  // hex float
            case 'a':  // hex float
                h = (f[0] == 'A') ? hexu : hex;
                fv = va_arg(va, double);
                if (pr == -1)
                    pr = 6;  // default is 6
                // read the double into a string
                if (prb_stbsp__real_to_parts((prb_stbsp__int64*)&n64, &dp, fv))
                    fl |= prb_STBSP__NEGATIVE;

                s = num + 64;

                prb_stbsp__lead_sign(fl, lead);

                if (dp == -1023)
                    dp = (n64) ? -1022 : 0;
                else
                    n64 |= (((prb_stbsp__uint64)1) << 52);
                n64 <<= (64 - 56);
                if (pr < 15)
                    n64 += ((((prb_stbsp__uint64)8) << 56) >> (pr * 4));
                    // add leading chars

#ifdef prb_STB_SPRINTF_MSVC_MODE
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
                    *s++ = prb_stbsp__period;
                sn = s;

                // print the bits
                n = (unsigned int)pr;
                if (n > 13)
                    n = 13;
                if (pr > (prb_stbsp__int32)n)
                    tz = (int)(pr - n);
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
                n = (dp >= 1000) ? (unsigned int)6 : ((dp >= 100) ? 5 : ((dp >= 10) ? 4 : 3));
                tail[0] = (char)n;
                for (;;) {
                    tail[n] = '0' + dp % 10;
                    if (n <= 3)
                        break;
                    --n;
                    dp /= 10;
                }

                dp = (int)(s - sn);
                l = (unsigned int)(s - (num + 64));
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
                if (prb_stbsp__real_to_str(&sn, &l, num, &dp, fv, (pr - 1) | 0x80000000))
                    fl |= prb_STBSP__NEGATIVE;

                // clamp the precision and delete extra zeros after clamp
                n = (unsigned int)pr;
                if (l > (prb_stbsp__uint32)pr)
                    l = (unsigned int)pr;
                while ((l > 1) && (pr) && (sn[l - 1] == '0')) {
                    --pr;
                    --l;
                }

                // should we use %e
                if ((dp <= -4) || (dp > (prb_stbsp__int32)n)) {
                    if (pr > (prb_stbsp__int32)l)
                        pr = (int)(l - 1);
                    else if (pr)
                        --pr;  // when using %e, there is one digit before the decimal
                    goto doexpfromg;
                }
                // this is the insane action to get the pr to match %g semantics for %f
                if (dp > 0) {
                    pr = (dp < (prb_stbsp__int32)l) ? (int)(l - dp) : 0;
                } else {
                    pr = -dp + ((pr > (prb_stbsp__int32)l) ? (prb_stbsp__int32)l : pr);
                }
                goto dofloatfromg;

            case 'E':  // float
            case 'e':  // float
                h = (f[0] == 'E') ? hexu : hex;
                fv = va_arg(va, double);
                if (pr == -1)
                    pr = 6;  // default is 6
                // read the double into a string
                if (prb_stbsp__real_to_str(&sn, &l, num, &dp, fv, pr | 0x80000000))
                    fl |= prb_STBSP__NEGATIVE;
            doexpfromg:
                tail[0] = 0;
                prb_stbsp__lead_sign(fl, lead);
                if (dp == prb_STBSP__SPECIAL) {
                    s = (char*)sn;
                    cs = 0;
                    pr = 0;
                    goto scopy;
                }
                s = num + 64;
                // handle leading chars
                *s++ = sn[0];

                if (pr)
                    *s++ = prb_stbsp__period;

                // handle after decimal
                if ((l - 1) > (prb_stbsp__uint32)pr)
                    l = (unsigned int)(pr + 1);
                for (n = 1; n < l; n++)
                    *s++ = sn[n];
                // trailing zeros
                tz = (int)(pr - (l - 1));
                pr = 0;
                // dump expo
                tail[1] = h[0xe];
                dp -= 1;
                if (dp < 0) {
                    tail[2] = '-';
                    dp = -dp;
                } else
                    tail[2] = '+';
#ifdef prb_STB_SPRINTF_MSVC_MODE
                n = 5;
#else
                n = (dp >= 100) ? (unsigned int)5 : (unsigned int)4;
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
                if (fl & prb_STBSP__METRIC_SUFFIX) {
                    double divisor;
                    divisor = 1000.0f;
                    if (fl & prb_STBSP__METRIC_1024)
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
                if (prb_stbsp__real_to_str(&sn, &l, num, &dp, fv, (unsigned int)pr))
                    fl |= prb_STBSP__NEGATIVE;
            dofloatfromg:
                tail[0] = 0;
                prb_stbsp__lead_sign(fl, lead);
                if (dp == prb_STBSP__SPECIAL) {
                    s = (char*)sn;
                    cs = 0;
                    pr = 0;
                    goto scopy;
                }
                s = num + 64;

                // handle the three decimal varieties
                if (dp <= 0) {
                    prb_stbsp__int32 i;
                    // handle 0.000*000xxxx
                    *s++ = '0';
                    if (pr)
                        *s++ = prb_stbsp__period;
                    n = (unsigned int)(-dp);
                    if ((prb_stbsp__int32)n > pr)
                        n = (unsigned int)pr;
                    i = (int)n;
                    while (i) {
                        if ((((prb_stbsp__uintptr)s) & 3) == 0)
                            break;
                        *s++ = '0';
                        --i;
                    }
                    while (i >= 4) {
                        *(prb_stbsp__uint32*)s = 0x30303030;
                        s += 4;
                        i -= 4;
                    }
                    while (i) {
                        *s++ = '0';
                        --i;
                    }
                    if ((prb_stbsp__int32)(l + n) > pr)
                        l = pr - n;
                    i = (int)l;
                    while (i) {
                        *s++ = *sn++;
                        --i;
                    }
                    tz = (int)(pr - (n + l));
                    cs = 1 + (3 << 24);  // how many tens did we write (for commas below)
                } else {
                    cs = (fl & prb_STBSP__TRIPLET_COMMA) ? ((600 - (prb_stbsp__uint32)dp) % 3) : 0;
                    if ((prb_stbsp__uint32)dp >= l) {
                        // handle xxxx000*000.0
                        n = 0;
                        for (;;) {
                            if ((fl & prb_STBSP__TRIPLET_COMMA) && (++cs == 4)) {
                                cs = 0;
                                *s++ = prb_stbsp__comma;
                            } else {
                                *s++ = sn[n];
                                ++n;
                                if (n >= l)
                                    break;
                            }
                        }
                        if (n < (prb_stbsp__uint32)dp) {
                            n = dp - n;
                            if ((fl & prb_STBSP__TRIPLET_COMMA) == 0) {
                                while (n) {
                                    if ((((prb_stbsp__uintptr)s) & 3) == 0)
                                        break;
                                    *s++ = '0';
                                    --n;
                                }
                                while (n >= 4) {
                                    *(prb_stbsp__uint32*)s = 0x30303030;
                                    s += 4;
                                    n -= 4;
                                }
                            }
                            while (n) {
                                if ((fl & prb_STBSP__TRIPLET_COMMA) && (++cs == 4)) {
                                    cs = 0;
                                    *s++ = prb_stbsp__comma;
                                } else {
                                    *s++ = '0';
                                    --n;
                                }
                            }
                        }
                        cs = (unsigned int)((int)(s - (num + 64)) + (3 << 24));  // cs is how many tens
                        if (pr) {
                            *s++ = prb_stbsp__period;
                            tz = pr;
                        }
                    } else {
                        // handle xxxxx.xxxx000*000
                        n = 0;
                        for (;;) {
                            if ((fl & prb_STBSP__TRIPLET_COMMA) && (++cs == 4)) {
                                cs = 0;
                                *s++ = prb_stbsp__comma;
                            } else {
                                *s++ = sn[n];
                                ++n;
                                if (n >= (prb_stbsp__uint32)dp)
                                    break;
                            }
                        }
                        cs = (unsigned int)((int)(s - (num + 64)) + (3 << 24));  // cs is how many tens
                        if (pr)
                            *s++ = prb_stbsp__period;
                        if ((l - dp) > (prb_stbsp__uint32)pr)
                            l = (unsigned int)(pr + dp);
                        while (n < l) {
                            *s++ = sn[n];
                            ++n;
                        }
                        tz = pr - ((int)l - dp);
                    }
                }
                pr = 0;

                // handle k,m,g,t
                if (fl & prb_STBSP__METRIC_SUFFIX) {
                    char idx;
                    idx = 1;
                    if (fl & prb_STBSP__METRIC_NOSPACE)
                        idx = 0;
                    tail[0] = idx;
                    tail[1] = ' ';
                    {
                        if (fl >> 24) {  // SI kilo is 'k', JEDEC and SI kibits are 'K'.
                            if (fl & prb_STBSP__METRIC_1024)
                                tail[idx + 1] = "_KMGT"[fl >> 24];
                            else
                                tail[idx + 1] = "_kMGT"[fl >> 24];
                            idx++;
                            // If printing kibits and not in jedec, add the 'i'.
                            if (fl & prb_STBSP__METRIC_1024 && !(fl & prb_STBSP__METRIC_JEDEC)) {
                                tail[idx + 1] = 'i';
                                idx++;
                            }
                            tail[0] = idx;
                        }
                    }
                };

            flt_lead:
                // get the length that we copied
                l = (prb_stbsp__uint32)(s - (num + 64));
                s = num + 64;
                goto scopy;
#endif

            case 'B':  // upper binary
            case 'b':  // lower binary
                h = (f[0] == 'B') ? hexu : hex;
                lead[0] = 0;
                if (fl & prb_STBSP__LEADING_0X) {
                    lead[0] = 2;
                    lead[1] = '0';
                    lead[2] = h[0xb];
                }
                l = (8 << 4) | (1 << 8);
                goto radixnum;

            case 'o':  // octal
                h = hexu;
                lead[0] = 0;
                if (fl & prb_STBSP__LEADING_0X) {
                    lead[0] = 1;
                    lead[1] = '0';
                }
                l = (3 << 4) | (3 << 8);
                goto radixnum;

            case 'p':  // pointer
                fl |= (sizeof(void*) == 8) ? prb_STBSP__INTMAX : 0;
                pr = sizeof(void*) * 2;
                fl &= ~prb_STBSP__LEADINGZERO;  // 'p' only prints the pointer with zeros
                prb_FALLTHROUGH;
                // fall through - to X

            case 'X':  // upper hex
            case 'x':  // lower hex
                h = (f[0] == 'X') ? hexu : hex;
                l = (4 << 4) | (4 << 8);
                lead[0] = 0;
                if (fl & prb_STBSP__LEADING_0X) {
                    lead[0] = 2;
                    lead[1] = '0';
                    lead[2] = h[16];
                }
            radixnum:
                // get the number
                if (fl & prb_STBSP__INTMAX)
                    n64 = va_arg(va, prb_stbsp__uint64);
                else
                    n64 = va_arg(va, prb_stbsp__uint32);

                s = num + prb_STBSP__NUMSZ;
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
                    if (!((n64) || ((prb_stbsp__int32)((num + prb_STBSP__NUMSZ) - s) < pr)))
                        break;
                    if (fl & prb_STBSP__TRIPLET_COMMA) {
                        ++l;
                        if ((l & 15) == ((l >> 4) & 15)) {
                            l &= ~15;
                            *--s = prb_stbsp__comma;
                        }
                    }
                };
                // get the tens and the comma pos
                cs = (prb_stbsp__uint32)((num + prb_STBSP__NUMSZ) - s) + ((((l >> 4) & 15)) << 24);
                // get the length that we copied
                l = (prb_stbsp__uint32)((num + prb_STBSP__NUMSZ) - s);
                // copy it
                goto scopy;

            case 'u':  // unsigned
            case 'i':
            case 'd':  // integer
                // get the integer and abs it
                if (fl & prb_STBSP__INTMAX) {
                    prb_stbsp__int64 i64 = va_arg(va, prb_stbsp__int64);
                    n64 = (prb_stbsp__uint64)i64;
                    if ((f[0] != 'u') && (i64 < 0)) {
                        n64 = (prb_stbsp__uint64)-i64;
                        fl |= prb_STBSP__NEGATIVE;
                    }
                } else {
                    prb_stbsp__int32 i = va_arg(va, prb_stbsp__int32);
                    n64 = (prb_stbsp__uint32)i;
                    if ((f[0] != 'u') && (i < 0)) {
                        n64 = (prb_stbsp__uint32)-i;
                        fl |= prb_STBSP__NEGATIVE;
                    }
                }

#ifndef prb_STB_SPRINTF_NOFLOAT
                if (fl & prb_STBSP__METRIC_SUFFIX) {
                    if (n64 < 1024)
                        pr = 0;
                    else if (pr == -1)
                        pr = 1;
                    fv = (double)(prb_stbsp__int64)n64;
                    goto doafloat;
                }
#endif

                // convert to string
                s = num + prb_STBSP__NUMSZ;
                l = 0;

                for (;;) {
                    // do in 32-bit chunks (avoid lots of 64-bit divides even with constant denominators)
                    char* o = s - 8;
                    if (n64 >= 100000000) {
                        n = (prb_stbsp__uint32)(n64 % 100000000);
                        n64 /= 100000000;
                    } else {
                        n = (prb_stbsp__uint32)n64;
                        n64 = 0;
                    }
                    if ((fl & prb_STBSP__TRIPLET_COMMA) == 0) {
                        do {
                            s -= 2;
                            *(prb_stbsp__uint16*)s = *(prb_stbsp__uint16*)&prb_stbsp__digitpair.pair[(n % 100) * 2];
                            n /= 100;
                        } while (n);
                    }
                    while (n) {
                        if ((fl & prb_STBSP__TRIPLET_COMMA) && (l++ == 3)) {
                            l = 0;
                            *--s = prb_stbsp__comma;
                            --o;
                        } else {
                            *--s = (char)(n % 10) + '0';
                            n /= 10;
                        }
                    }
                    if (n64 == 0) {
                        if ((s[0] == '0') && (s != (num + prb_STBSP__NUMSZ)))
                            ++s;
                        break;
                    }
                    while (s != o)
                        if ((fl & prb_STBSP__TRIPLET_COMMA) && (l++ == 3)) {
                            l = 0;
                            *--s = prb_stbsp__comma;
                            --o;
                        } else {
                            *--s = '0';
                        }
                }

                tail[0] = 0;
                prb_stbsp__lead_sign(fl, lead);

                // get the length that we copied
                l = (prb_stbsp__uint32)((num + prb_STBSP__NUMSZ) - s);
                if (l == 0) {
                    *--s = '0';
                    l = 1;
                }
                cs = l + (3 << 24);
                if (pr < 0)
                    pr = 0;

            scopy:
                // get fw=leading/trailing space, pr=leading zeros
                if (pr < (prb_stbsp__int32)l)
                    pr = (int)l;
                n = pr + lead[0] + tail[0] + (unsigned int)tz;
                if (fw < (prb_stbsp__int32)n)
                    fw = (int)n;
                fw -= n;
                pr -= l;

                // handle right justify and leading zeros
                if ((fl & prb_STBSP__LEFTJUST) == 0) {
                    if (fl & prb_STBSP__LEADINGZERO)  // if leading zeros, everything is in pr
                    {
                        pr = (fw > pr) ? fw : pr;
                        fw = 0;
                    } else {
                        fl &= ~prb_STBSP__TRIPLET_COMMA;  // if no leading zeros, then no commas
                    }
                }

                // copy the spaces and/or zeros
                if (fw + pr) {
                    prb_stbsp__int32  i;
                    prb_stbsp__uint32 c;

                    // copy leading spaces (or when doing %8.4d stuff)
                    if ((fl & prb_STBSP__LEFTJUST) == 0)
                        while (fw > 0) {
                            prb_stbsp__cb_buf_clamp(i, fw);
                            fw -= i;
                            while (i) {
                                if ((((prb_stbsp__uintptr)bf) & 3) == 0)
                                    break;
                                *bf++ = ' ';
                                --i;
                            }
                            while (i >= 4) {
                                *(prb_stbsp__uint32*)bf = 0x20202020;
                                bf += 4;
                                i -= 4;
                            }
                            while (i) {
                                *bf++ = ' ';
                                --i;
                            }
                            prb_stbsp__chk_cb_buf(1);
                        }

                    // copy leader
                    sn = lead + 1;
                    while (lead[0]) {
                        prb_stbsp__cb_buf_clamp(i, lead[0]);
                        lead[0] -= (char)i;
                        while (i) {
                            // NOLINTBEGIN(clang-analyzer-core.uninitialized.Assign)
                            *bf++ = *sn++;
                            // NOLINTEND(clang-analyzer-core.uninitialized.Assign)
                            --i;
                        }
                        prb_stbsp__chk_cb_buf(1);
                    }

                    // copy leading zeros
                    c = cs >> 24;
                    cs &= 0xffffff;
                    cs = (fl & prb_STBSP__TRIPLET_COMMA) ? ((prb_stbsp__uint32)(c - ((pr + cs) % (c + 1)))) : 0;
                    while (pr > 0) {
                        prb_stbsp__cb_buf_clamp(i, pr);
                        pr -= i;
                        if ((fl & prb_STBSP__TRIPLET_COMMA) == 0) {
                            while (i) {
                                if ((((prb_stbsp__uintptr)bf) & 3) == 0)
                                    break;
                                *bf++ = '0';
                                --i;
                            }
                            while (i >= 4) {
                                *(prb_stbsp__uint32*)bf = 0x30303030;
                                bf += 4;
                                i -= 4;
                            }
                        }
                        while (i) {
                            if ((fl & prb_STBSP__TRIPLET_COMMA) && (cs++ == c)) {
                                cs = 0;
                                *bf++ = prb_stbsp__comma;
                            } else
                                *bf++ = '0';
                            --i;
                        }
                        prb_stbsp__chk_cb_buf(1);
                    }
                }

                // copy leader if there is still one
                sn = lead + 1;
                while (lead[0]) {
                    prb_stbsp__int32 i;
                    prb_stbsp__cb_buf_clamp(i, lead[0]);
                    lead[0] -= (char)i;
                    while (i) {
                        *bf++ = *sn++;
                        --i;
                    }
                    prb_stbsp__chk_cb_buf(1);
                }

                // copy the string
                n = l;
                while (n) {
                    prb_stbsp__int32 i;
                    prb_stbsp__cb_buf_clamp(i, (int)n);
                    n -= i;
                    prb_STBSP__UNALIGNED(while (i >= 4) {
                        *(prb_stbsp__uint32 volatile*)bf = *(prb_stbsp__uint32 volatile*)s;
                        bf += 4;
                        s += 4;
                        i -= 4;
                    }) while (i) {
                        // NOLINTBEGIN(clang-analyzer-core.uninitialized.Assign)
                        *bf++ = *s++;
                        // NOLINTEND(clang-analyzer-core.uninitialized.Assign)
                        --i;
                    }
                    prb_stbsp__chk_cb_buf(1);
                }

                // copy trailing zeros
                while (tz) {
                    prb_stbsp__int32 i;
                    prb_stbsp__cb_buf_clamp(i, tz);
                    tz -= i;
                    while (i) {
                        if ((((prb_stbsp__uintptr)bf) & 3) == 0)
                            break;
                        *bf++ = '0';
                        --i;
                    }
                    while (i >= 4) {
                        *(prb_stbsp__uint32*)bf = 0x30303030;
                        bf += 4;
                        i -= 4;
                    }
                    while (i) {
                        *bf++ = '0';
                        --i;
                    }
                    prb_stbsp__chk_cb_buf(1);
                }

                // copy tail if there is one
                sn = tail + 1;
                while (tail[0]) {
                    prb_stbsp__int32 i;
                    prb_stbsp__cb_buf_clamp(i, tail[0]);
                    tail[0] -= (char)i;
                    while (i) {
                        *bf++ = *sn++;
                        --i;
                    }
                    prb_stbsp__chk_cb_buf(1);
                }

                // handle the left justify
                if (fl & prb_STBSP__LEFTJUST)
                    if (fw > 0) {
                        while (fw) {
                            prb_stbsp__int32 i;
                            prb_stbsp__cb_buf_clamp(i, fw);
                            fw -= i;
                            while (i) {
                                if ((((prb_stbsp__uintptr)bf) & 3) == 0)
                                    break;
                                *bf++ = ' ';
                                --i;
                            }
                            while (i >= 4) {
                                *(prb_stbsp__uint32*)bf = 0x20202020;
                                bf += 4;
                                i -= 4;
                            }
                            while (i--)
                                *bf++ = ' ';
                            prb_stbsp__chk_cb_buf(1);
                        }
                    }
                break;

            default:  // unknown, just copy code
                s = num + prb_STBSP__NUMSZ - 1;
                *s = f[0];
                l = 1;
                fl = 0;
                fw = 0;
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
        prb_stbsp__flush_cb();

done:
    return tlen + (int)(bf - buf);
}

// cleanup
#undef prb_STBSP__LEFTJUST
#undef prb_STBSP__LEADINGPLUS
#undef prb_STBSP__LEADINGSPACE
#undef prb_STBSP__LEADING_0X
#undef prb_STBSP__LEADINGZERO
#undef prb_STBSP__INTMAX
#undef prb_STBSP__TRIPLET_COMMA
#undef prb_STBSP__NEGATIVE
#undef prb_STBSP__METRIC_SUFFIX
#undef prb_STBSP__NUMSZ
#undef prb_stbsp__chk_cb_bufL
#undef prb_stbsp__chk_cb_buf
#undef prb_stbsp__flush_cb
#undef prb_stbsp__cb_buf_clamp

// ============================================================================
//   wrapper functions

prb_STBSP__PUBLICDEF int
prb_STB_SPRINTF_DECORATE(sprintf)(char* buf, char const* fmt, ...) {
    int     result;
    va_list va;
    va_start(va, fmt);
    result = prb_STB_SPRINTF_DECORATE(vsprintfcb)(0, 0, buf, fmt, va);
    va_end(va);
    return result;
}

typedef struct prb_stbsp__context {
    char* buf;
    int   count;
    int   length;
    char  tmp[prb_STB_SPRINTF_MIN];
} prb_stbsp__context;

static char*
prb_stbsp__clamp_callback(const char* buf, void* user, int len) {
    prb_stbsp__context* c = (prb_stbsp__context*)user;
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
    return (c->count >= prb_STB_SPRINTF_MIN) ? c->buf : c->tmp;  // go direct into buffer if you can
}

static char*
prb_stbsp__count_clamp_callback(const char* buf, void* user, int len) {
    prb_stbsp__context* c = (prb_stbsp__context*)user;
    (void)sizeof(buf);

    c->length += len;
    return c->tmp;  // go direct into buffer if you can
}

prb_STBSP__PUBLICDEF int
prb_STB_SPRINTF_DECORATE(vsnprintf)(char* buf, int count, char const* fmt, va_list va) {
    prb_assert(count >= 0);
    prb_stbsp__context c;

    if ((count == 0) && !buf) {
        c.length = 0;

        prb_STB_SPRINTF_DECORATE(vsprintfcb)(prb_stbsp__count_clamp_callback, &c, c.tmp, fmt, va);
    } else {
        int l;

        c.buf = buf;
        c.count = count;
        c.length = 0;

        prb_STB_SPRINTF_DECORATE(vsprintfcb)(prb_stbsp__clamp_callback, &c, prb_stbsp__clamp_callback(0, &c, 0), fmt, va);

        // zero-terminate
        l = (int)(c.buf - buf);
        if (l >= count)  // should never be greater, only equal (or less) than count
            l = count - 1;
        buf[l] = 0;
    }

    return c.length;
}

prb_STBSP__PUBLICDEF int
prb_STB_SPRINTF_DECORATE(snprintf)(char* buf, int count, char const* fmt, ...) {
    int     result;
    va_list va;
    va_start(va, fmt);

    result = prb_STB_SPRINTF_DECORATE(vsnprintf)(buf, count, fmt, va);
    va_end(va);

    return result;
}

prb_STBSP__PUBLICDEF int
prb_STB_SPRINTF_DECORATE(vsprintf)(char* buf, char const* fmt, va_list va) {
    return prb_STB_SPRINTF_DECORATE(vsprintfcb)(0, 0, buf, fmt, va);
}

// =======================================================================
//   low level float utility functions

#ifndef prb_STB_SPRINTF_NOFLOAT

// copies d to bits w/ strict aliasing (this compiles to nothing on /Ox)
#define prb_STBSP__COPYFP(dest, src) \
    { \
        int cn; \
        for (cn = 0; cn < 8; cn++) \
            ((char*)&dest)[cn] = ((char*)&src)[cn]; \
    }

// get float info
static prb_stbsp__int32
prb_stbsp__real_to_parts(prb_stbsp__int64* bits, prb_stbsp__int32* expo, double value) {
    double           d;
    prb_stbsp__int64 b = 0;

    // load value and round at the frac_digits
    d = value;

    prb_STBSP__COPYFP(b, d);

    *bits = b & (signed long long)((((prb_stbsp__uint64)1) << 52) - 1);
    *expo = (prb_stbsp__int32)(((b >> 52) & 2047) - 1023);

    return (prb_stbsp__int32)((prb_stbsp__uint64)b >> 63);
}

// clang-format off
static double const prb_stbsp__bot[23] = {1e+000, 1e+001, 1e+002, 1e+003, 1e+004, 1e+005, 1e+006, 1e+007,
                                      1e+008, 1e+009, 1e+010, 1e+011, 1e+012, 1e+013, 1e+014, 1e+015,
                                      1e+016, 1e+017, 1e+018, 1e+019, 1e+020, 1e+021, 1e+022};
static double const prb_stbsp__negbot[22] = {1e-001, 1e-002, 1e-003, 1e-004, 1e-005, 1e-006, 1e-007, 1e-008,
                                         1e-009, 1e-010, 1e-011, 1e-012, 1e-013, 1e-014, 1e-015, 1e-016,
                                         1e-017, 1e-018, 1e-019, 1e-020, 1e-021, 1e-022};
static double const prb_stbsp__negboterr[22] = {
    -5.551115123125783e-018,  -2.0816681711721684e-019, -2.0816681711721686e-020, -4.7921736023859299e-021,
    -8.1803053914031305e-022, 4.5251888174113741e-023,  4.5251888174113739e-024,  -2.0922560830128471e-025,
    -6.2281591457779853e-026, -3.6432197315497743e-027, 6.0503030718060191e-028,  2.0113352370744385e-029,
    -3.0373745563400371e-030, 1.1806906454401013e-032,  -7.7705399876661076e-032, 2.0902213275965398e-033,
    -7.1542424054621921e-034, -7.1542424054621926e-035, 2.4754073164739869e-036,  5.4846728545790429e-037,
    9.2462547772103625e-038,  -4.8596774326570872e-039};
static double const prb_stbsp__top[13] =
    {1e+023, 1e+046, 1e+069, 1e+092, 1e+115, 1e+138, 1e+161, 1e+184, 1e+207, 1e+230, 1e+253, 1e+276, 1e+299};
static double const prb_stbsp__negtop[13] =
    {1e-023, 1e-046, 1e-069, 1e-092, 1e-115, 1e-138, 1e-161, 1e-184, 1e-207, 1e-230, 1e-253, 1e-276, 1e-299};
static double const prb_stbsp__toperr[13] = {
    8388608, 6.8601809640529717e+028, -7.253143638152921e+052,
    -4.3377296974619174e+075, -1.5559416129466825e+098, -3.2841562489204913e+121, -3.7745893248228135e+144,
    -1.7356668416969134e+167, -3.8893577551088374e+190, -9.9566444326005119e+213, 6.3641293062232429e+236,
    -5.2069140800249813e+259, -5.2504760255204387e+282};
static double const prb_stbsp__negtoperr[13] = {
   3.9565301985100693e-040,  -2.299904345391321e-063,  3.6506201437945798e-086,  1.1875228833981544e-109,
   -5.0644902316928607e-132, -6.7156837247865426e-155, -2.812077463003139e-178,  -5.7778912386589953e-201,
   7.4997100559334532e-224,  -4.6439668915134491e-247, -6.3691100762962136e-270, -9.436808465446358e-293,
   8.0970921678014997e-317};
// clang-format on

#if defined(_MSC_VER) && (_MSC_VER <= 1200)
static prb_stbsp__uint64 const prb_stbsp__powten[20] = {
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
#define prb_stbsp__tento19th ((prb_stbsp__uint64)1000000000000000000)
#else
static prb_stbsp__uint64 const prb_stbsp__powten[20] = {
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
#define prb_stbsp__tento19th (1000000000000000000ULL)
#endif

#define prb_stbsp__ddmulthi(oh, ol, xh, yh) \
    { \
        double           ahi = 0, alo, bhi = 0, blo; \
        prb_stbsp__int64 bt; \
        oh = xh * yh; \
        prb_STBSP__COPYFP(bt, xh); \
        bt &= ((~(prb_stbsp__uint64)0) << 27); \
        prb_STBSP__COPYFP(ahi, bt); \
        alo = xh - ahi; \
        prb_STBSP__COPYFP(bt, yh); \
        bt &= ((~(prb_stbsp__uint64)0) << 27); \
        prb_STBSP__COPYFP(bhi, bt); \
        blo = yh - bhi; \
        ol = ((ahi * bhi - oh) + ahi * blo + alo * bhi) + alo * blo; \
    }

#define prb_stbsp__ddtoS64(ob, xh, xl) \
    { \
        double ahi = 0, alo, vh, t; \
        ob = (prb_stbsp__int64)xh; \
        vh = (double)ob; \
        ahi = (xh - vh); \
        t = (ahi - xh); \
        alo = (xh - (ahi - t)) - (vh + t); \
        ob += (prb_stbsp__int64)(ahi + alo + xl); \
    }

#define prb_stbsp__ddrenorm(oh, ol) \
    { \
        double s; \
        s = oh + ol; \
        ol = ol - (s - oh); \
        oh = s; \
    }

#define prb_stbsp__ddmultlo(oh, ol, xh, xl, yh, yl) ol = ol + (xh * yl + xl * yh);

#define prb_stbsp__ddmultlos(oh, ol, xh, yl) ol = ol + (xh * yl);

static void
prb_stbsp__raise_to_power10(double* ohi, double* olo, double d, prb_stbsp__int32 power)  // power can be -323 to +350
{
    double ph, pl;
    if ((power >= 0) && (power <= 22)) {
        prb_stbsp__ddmulthi(ph, pl, d, prb_stbsp__bot[power]);
    } else {
        prb_stbsp__int32 e, et, eb;
        double           p2h, p2l;

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
                prb_stbsp__ddmulthi(ph, pl, d, prb_stbsp__negbot[eb]);
                prb_stbsp__ddmultlos(ph, pl, d, prb_stbsp__negboterr[eb]);
            }
            if (et) {
                prb_stbsp__ddrenorm(ph, pl);
                --et;
                prb_stbsp__ddmulthi(p2h, p2l, ph, prb_stbsp__negtop[et]);
                prb_stbsp__ddmultlo(p2h, p2l, ph, pl, prb_stbsp__negtop[et], prb_stbsp__negtoperr[et]);
                ph = p2h;
                pl = p2l;
            }
        } else {
            if (eb) {
                e = eb;
                if (eb > 22)
                    eb = 22;
                e -= eb;
                prb_stbsp__ddmulthi(ph, pl, d, prb_stbsp__bot[eb]);
                if (e) {
                    prb_stbsp__ddrenorm(ph, pl);
                    prb_stbsp__ddmulthi(p2h, p2l, ph, prb_stbsp__bot[e]);
                    prb_stbsp__ddmultlos(p2h, p2l, prb_stbsp__bot[e], pl);
                    ph = p2h;
                    pl = p2l;
                }
            }
            if (et) {
                prb_stbsp__ddrenorm(ph, pl);
                --et;
                prb_stbsp__ddmulthi(p2h, p2l, ph, prb_stbsp__top[et]);
                prb_stbsp__ddmultlo(p2h, p2l, ph, pl, prb_stbsp__top[et], prb_stbsp__toperr[et]);
                ph = p2h;
                pl = p2l;
            }
        }
    }
    prb_stbsp__ddrenorm(ph, pl);
    *ohi = ph;
    *olo = pl;
}

// given a float value, returns the significant bits in bits, and the position of the
//   decimal point in decimal_pos.  +/-INF and NAN are specified by special values
//   returned in the decimal_pos parameter.
// frac_digits is absolute normally, but if you want from first significant digits (got %g and %e), or in 0x80000000
static prb_stbsp__int32
prb_stbsp__real_to_str(
    char const**       start,
    prb_stbsp__uint32* len,
    char*              out,
    prb_stbsp__int32*  decimal_pos,
    double             value,
    prb_stbsp__uint32  frac_digits
) {
    double           d;
    prb_stbsp__int64 bits = 0;
    prb_stbsp__int32 expo, e, ng, tens;

    d = value;
    prb_STBSP__COPYFP(bits, d);
    expo = (prb_stbsp__int32)((bits >> 52) & 2047);
    ng = (prb_stbsp__int32)((prb_stbsp__uint64)bits >> 63);
    if (ng)
        d = -d;

    if (expo == 2047)  // is nan or inf?
    {
        *start = (bits & ((((prb_stbsp__uint64)1) << 52) - 1)) ? "NaN" : "Inf";
        *decimal_pos = prb_STBSP__SPECIAL;
        *len = 3;
        return ng;
    }

    if (expo == 0)  // is zero or denormal
    {
        if (((prb_stbsp__uint64)bits << 1) == 0)  // do zero
        {
            *decimal_pos = 1;
            *start = out;
            out[0] = '0';
            *len = 1;
            return ng;
        }
        // find the right expo for denormals
        {
            prb_stbsp__int64 v = ((prb_stbsp__uint64)1) << 51;
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
        prb_stbsp__raise_to_power10(&ph, &pl, d, 18 - tens);

        // get full as much precision from double-double as possible
        prb_stbsp__ddtoS64(bits, ph, pl);

        // check if we undershot
        if (((prb_stbsp__uint64)bits) >= prb_stbsp__tento19th)
            ++tens;
    }

    // now do the rounding in integer land
    frac_digits = (frac_digits & 0x80000000) ? ((frac_digits & 0x7ffffff) + 1) : (tens + frac_digits);
    if ((frac_digits < 24)) {
        prb_stbsp__uint32 dg = 1;
        if ((prb_stbsp__uint64)bits >= prb_stbsp__powten[9])
            dg = 10;
        while ((prb_stbsp__uint64)bits >= prb_stbsp__powten[dg]) {
            ++dg;
            if (dg == 20)
                goto noround;
        }
        if (frac_digits < dg) {
            prb_stbsp__uint64 r;
            // add 0.5 at the right position and round
            e = (signed int)(dg - frac_digits);
            if ((prb_stbsp__uint32)e >= 24)
                goto noround;
            r = prb_stbsp__powten[e];
            bits = bits + (signed long long)(r / 2);
            if ((prb_stbsp__uint64)bits >= prb_stbsp__powten[dg])
                ++tens;
            bits /= r;
        }
    noround:;
    }

    // kill long trailing runs of zeros
    if (bits) {
        prb_stbsp__uint32 n;
        for (;;) {
            if (bits <= 0xffffffff)
                break;
            if (bits % 1000)
                goto donez;
            bits /= 1000;
        }
        n = (prb_stbsp__uint32)bits;
        while ((n % 1000) == 0)
            n /= 1000;
        bits = n;
    donez:;
    }

    // convert to string
    out += 64;
    e = 0;
    for (;;) {
        prb_stbsp__uint32 n;
        char*             o = out - 8;
        // do the conversion in chunks of U32s (avoid most 64-bit divides, worth it, constant denomiators be damned)
        if (bits >= 100000000) {
            n = (prb_stbsp__uint32)(bits % 100000000);
            bits /= 100000000;
        } else {
            n = (prb_stbsp__uint32)bits;
            bits = 0;
        }
        while (n) {
            out -= 2;
            *(prb_stbsp__uint16*)out = *(prb_stbsp__uint16*)&prb_stbsp__digitpair.pair[(n % 100) * 2];
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
    *len = (unsigned int)e;
    return ng;
}

#undef prb_stbsp__ddmulthi
#undef prb_stbsp__ddrenorm
#undef prb_stbsp__ddmultlo
#undef prb_stbsp__ddmultlos
#undef prb_STBSP__SPECIAL
#undef prb_STBSP__COPYFP

#endif  // prb_STB_SPRINTF_NOFLOAT

// clean up
#undef prb_stbsp__uint16
#undef prb_stbsp__uint32
#undef prb_stbsp__int32
#undef prb_stbsp__uint64
#undef prb_stbsp__int64
#undef prb_STBSP__UNALIGNED

//
// SECTION stb ds (implementation)
//

#define prb_STBDS_ASSERT(x) prb_assert(x)

#ifdef prb_STBDS_STATISTICS
#define prb_STBDS_STATS(x) x
size_t prb_stbds_array_grow;
size_t prb_stbds_hash_grow;
size_t prb_stbds_hash_shrink;
size_t prb_stbds_hash_rebuild;
size_t prb_stbds_hash_probes;
size_t prb_stbds_hash_alloc;
size_t prb_stbds_rehash_probes;
size_t prb_stbds_rehash_items;
#else
#define prb_STBDS_STATS(x)
#endif

//
// prb_stbds_arr implementation
//

//int *prev_allocs[65536];
//int num_prev;

prb_STBDS__PUBLICDEF void*
prb_stbds_arrgrowf(void* a, size_t elemsize, size_t addlen, size_t min_cap) {
    prb_stbds_array_header temp = {.length = 0, .capacity = 0, .hash_table = 0, .temp = 0};  // force debugging
    void*                  b;
    size_t                 min_len = prb_stbds_arrlen(a) + addlen;
    (void)sizeof(temp);

    // compute the minimum capacity needed
    if (min_len > min_cap)
        min_cap = min_len;

    if (min_cap <= prb_stbds_arrcap(a))
        return a;

    // increase needed capacity to guarantee O(1) amortized
    if (min_cap < 2 * prb_stbds_arrcap(a))
        min_cap = 2 * prb_stbds_arrcap(a);
    else if (min_cap < 4)
        min_cap = 4;

    //if (num_prev < 65536) if (a) prev_allocs[num_prev++] = (int *) ((char *) a+1);
    //if (num_prev == 2201)
    //  num_prev = num_prev;
    b = prb_STBDS_REALLOC(NULL, (a) ? prb_stbds_header(a) : 0, elemsize * min_cap + sizeof(prb_stbds_array_header));
    //if (num_prev < 65536) prev_allocs[num_prev++] = (int *) (char *) b;
    b = (char*)b + sizeof(prb_stbds_array_header);
    if (a == NULL) {
        prb_stbds_header(b)->length = 0;
        prb_stbds_header(b)->hash_table = 0;
        prb_stbds_header(b)->temp = 0;
    } else {
        prb_STBDS_STATS(++prb_stbds_array_grow);
    }
    prb_stbds_header(b)->capacity = min_cap;

    return b;
}

prb_STBDS__PUBLICDEF void
prb_stbds_arrfreef(void* a) {
    prb_STBDS_FREE(NULL, prb_stbds_header(a));
}

//
// stbds_hm hash table implementation
//

#ifdef prb_STBDS_INTERNAL_SMALL_BUCKET
#define prb_STBDS_BUCKET_LENGTH 4
#else
#define prb_STBDS_BUCKET_LENGTH 8
#endif

#define prb_STBDS_BUCKET_SHIFT (prb_STBDS_BUCKET_LENGTH == 8 ? 3 : 2)
#define prb_STBDS_BUCKET_MASK (prb_STBDS_BUCKET_LENGTH - 1)
#define prb_STBDS_CACHE_LINE_SIZE 64

#define prb_STBDS_ALIGN_FWD(n, a) (((n) + (a)-1) & ~((a)-1))

typedef struct {
    size_t    hash[prb_STBDS_BUCKET_LENGTH];
    ptrdiff_t index[prb_STBDS_BUCKET_LENGTH];
} prb_stbds_hash_bucket;  // in 32-bit, this is one 64-byte cache line; in 64-bit, each array is one 64-byte cache line

typedef struct {
    char*                  temp_key;  // this MUST be the first field of the hash table
    size_t                 slot_count;
    size_t                 used_count;
    size_t                 used_count_threshold;
    size_t                 used_count_shrink_threshold;
    size_t                 tombstone_count;
    size_t                 tombstone_count_threshold;
    size_t                 seed;
    size_t                 slot_count_log2;
    prb_stbds_string_arena string;
    prb_stbds_hash_bucket* storage;  // not a separate allocation, just 64-byte aligned storage after this struct
} prb_stbds_hash_index;

#define prb_STBDS_INDEX_EMPTY -1
#define prb_STBDS_INDEX_DELETED -2
#define prb_STBDS_INDEX_IN_USE(x) ((x) >= 0)

#define prb_STBDS_HASH_EMPTY 0
#define prb_STBDS_HASH_DELETED 1

static size_t prb_stbds_hash_seed = 0x31415926;

prb_STBDS__PUBLICDEF void
prb_stbds_rand_seed(size_t seed) {
    prb_stbds_hash_seed = seed;
}

#define prb_stbds_load_32_or_64(var, temp, v32, v64_hi, v64_lo) \
    temp = v64_lo ^ v32, temp <<= 16, temp <<= 16, temp >>= 16, temp >>= 16, /* discard if 32-bit */ \
        var = v64_hi, var <<= 16, var <<= 16, /* discard if 32-bit */ \
        var ^= temp ^ v32

#define prb_STBDS_SIZE_T_BITS ((sizeof(size_t)) * 8)

static size_t
prb_stbds_probe_position(size_t hash, size_t slot_count, size_t slot_log2) {
    size_t pos;
    prb_STBDS_NOTUSED(slot_log2);
    pos = hash & (slot_count - 1);
#ifdef prb_STBDS_INTERNAL_BUCKET_START
    pos &= ~prb_STBDS_BUCKET_MASK;
#endif
    return pos;
}

static size_t
prb_stbds_log2(size_t slot_count) {
    size_t n = 0;
    while (slot_count > 1) {
        slot_count >>= 1;
        ++n;
    }
    return n;
}

static prb_stbds_hash_index*
prb_stbds_make_hash_index(size_t slot_count, prb_stbds_hash_index* ot) {
    prb_stbds_hash_index* t;
    t = (prb_stbds_hash_index*)prb_STBDS_REALLOC(
        NULL,
        0,
        (slot_count >> prb_STBDS_BUCKET_SHIFT) * sizeof(prb_stbds_hash_bucket) + sizeof(prb_stbds_hash_index)
            + prb_STBDS_CACHE_LINE_SIZE - 1
    );
    t->storage = (prb_stbds_hash_bucket*)prb_STBDS_ALIGN_FWD((size_t)(t + 1), prb_STBDS_CACHE_LINE_SIZE);
    t->slot_count = slot_count;
    t->slot_count_log2 = prb_stbds_log2(slot_count);
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

    if (slot_count <= prb_STBDS_BUCKET_LENGTH)
        t->used_count_shrink_threshold = 0;
    // to avoid infinite loop, we need to guarantee that at least one slot is empty and will terminate probes
    prb_STBDS_ASSERT(t->used_count_threshold + t->tombstone_count_threshold < t->slot_count);
    prb_STBDS_STATS(++prb_stbds_hash_alloc);
    if (ot) {
        t->string = ot->string;
        // reuse old seed so we can reuse old hashes so below "copy out old data" doesn't do any hashing
        t->seed = ot->seed;
    } else {
        size_t a, b, temp;
        prb_memset(&t->string, 0, sizeof(t->string));
        t->seed = prb_stbds_hash_seed;
        // LCG
        // in 32-bit, a =          2147001325   b =  715136305
        // in 64-bit, a = 2862933555777941757   b = 3037000493
        prb_stbds_load_32_or_64(a, temp, 2147001325, 0x27bb2ee6, 0x87b0b0fd);
        prb_stbds_load_32_or_64(b, temp, 715136305, 0, 0xb504f32d);
        prb_stbds_hash_seed = prb_stbds_hash_seed * a + b;
    }

    {
        size_t i, j;
        for (i = 0; i < slot_count >> prb_STBDS_BUCKET_SHIFT; ++i) {
            prb_stbds_hash_bucket* b = &t->storage[i];
            for (j = 0; j < prb_STBDS_BUCKET_LENGTH; ++j)
                b->hash[j] = prb_STBDS_HASH_EMPTY;
            for (j = 0; j < prb_STBDS_BUCKET_LENGTH; ++j)
                b->index[j] = prb_STBDS_INDEX_EMPTY;
        }
    }

    // copy out the old data, if any
    if (ot) {
        size_t i, j;
        t->used_count = ot->used_count;
        for (i = 0; i < ot->slot_count >> prb_STBDS_BUCKET_SHIFT; ++i) {
            prb_stbds_hash_bucket* ob = &ot->storage[i];
            for (j = 0; j < prb_STBDS_BUCKET_LENGTH; ++j) {
                if (prb_STBDS_INDEX_IN_USE(ob->index[j])) {
                    size_t hash = ob->hash[j];
                    size_t pos = prb_stbds_probe_position(hash, t->slot_count, t->slot_count_log2);
                    size_t step = prb_STBDS_BUCKET_LENGTH;
                    prb_STBDS_STATS(++prb_stbds_rehash_items);
                    for (;;) {
                        size_t                 limit, z;
                        prb_stbds_hash_bucket* bucket;
                        bucket = &t->storage[pos >> prb_STBDS_BUCKET_SHIFT];
                        prb_STBDS_STATS(++prb_stbds_rehash_probes);

                        for (z = pos & prb_STBDS_BUCKET_MASK; z < prb_STBDS_BUCKET_LENGTH; ++z) {
                            if (bucket->hash[z] == 0) {
                                bucket->hash[z] = hash;
                                bucket->index[z] = ob->index[j];
                                goto done;
                            }
                        }

                        limit = pos & prb_STBDS_BUCKET_MASK;
                        for (z = 0; z < limit; ++z) {
                            if (bucket->hash[z] == 0) {
                                bucket->hash[z] = hash;
                                bucket->index[z] = ob->index[j];
                                goto done;
                            }
                        }

                        pos += step;  // quadratic probing
                        step += prb_STBDS_BUCKET_LENGTH;
                        pos &= (t->slot_count - 1);
                    }
                }
            done:;
            }
        }
    }

    return t;
}

#define prb_STBDS_ROTATE_LEFT(val, n) (((val) << (n)) | ((val) >> (prb_STBDS_SIZE_T_BITS - (n))))
#define prb_STBDS_ROTATE_RIGHT(val, n) (((val) >> (n)) | ((val) << (prb_STBDS_SIZE_T_BITS - (n))))

prb_STBDS__PUBLICDEF size_t
prb_stbds_hash_string(char* str, size_t seed) {
    size_t hash = seed;
    while (*str)
        hash = prb_STBDS_ROTATE_LEFT(hash, 9) + (unsigned char)*str++;

    // Thomas Wang 64-to-32 bit mix function, hopefully also works in 32 bits
    hash ^= seed;
    hash = (~hash) + (hash << 18);
    hash ^= hash ^ prb_STBDS_ROTATE_RIGHT(hash, 31);
    hash = hash * 21;
    hash ^= hash ^ prb_STBDS_ROTATE_RIGHT(hash, 11);
    hash += (hash << 6);
    hash ^= prb_STBDS_ROTATE_RIGHT(hash, 22);
    return hash + seed;
}

#ifdef prb_STBDS_SIPHASH_2_4
#define prb_STBDS_SIPHASH_C_ROUNDS 2
#define prb_STBDS_SIPHASH_D_ROUNDS 4
typedef int prb_STBDS_SIPHASH_2_4_can_only_be_used_in_64_bit_builds[sizeof(size_t) == 8 ? 1 : -1];
#endif

#ifndef prb_STBDS_SIPHASH_C_ROUNDS
#define prb_STBDS_SIPHASH_C_ROUNDS 1
#endif
#ifndef prb_STBDS_SIPHASH_D_ROUNDS
#define prb_STBDS_SIPHASH_D_ROUNDS 1
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127)  // conditional expression is constant, for do..while(0) and sizeof()==
#endif

static size_t
prb_stbds_siphash_bytes(void* p, size_t len, size_t seed) {
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

#ifdef prb_STBDS_TEST_SIPHASH_2_4
    // hardcoded with key material in the siphash test vectors
    v0 ^= 0x0706050403020100ull ^ seed;
    v1 ^= 0x0f0e0d0c0b0a0908ull ^ ~seed;
    v2 ^= 0x0706050403020100ull ^ seed;
    v3 ^= 0x0f0e0d0c0b0a0908ull ^ ~seed;
#endif

#define prb_STBDS_SIPROUND() \
    do { \
        v0 += v1; \
        v1 = prb_STBDS_ROTATE_LEFT(v1, 13); \
        v1 ^= v0; \
        v0 = prb_STBDS_ROTATE_LEFT(v0, prb_STBDS_SIZE_T_BITS / 2); \
        v2 += v3; \
        v3 = prb_STBDS_ROTATE_LEFT(v3, 16); \
        v3 ^= v2; \
        v2 += v1; \
        v1 = prb_STBDS_ROTATE_LEFT(v1, 17); \
        v1 ^= v2; \
        v2 = prb_STBDS_ROTATE_LEFT(v2, prb_STBDS_SIZE_T_BITS / 2); \
        v0 += v3; \
        v3 = prb_STBDS_ROTATE_LEFT(v3, 21); \
        v3 ^= v0; \
    } while (0)

    for (i = 0; i + sizeof(size_t) <= len; i += sizeof(size_t), d += sizeof(size_t)) {
        data = d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24);
        data |= (size_t)(d[4] | (d[5] << 8) | (d[6] << 16) | ((uint32_t)d[7] << 24)) << 16 << 16;  // discarded if size_t == 4

        v3 ^= data;
        for (j = 0; j < prb_STBDS_SIPHASH_C_ROUNDS; ++j)
            prb_STBDS_SIPROUND();
        v0 ^= data;
    }
    data = len << (prb_STBDS_SIZE_T_BITS - 8);
    switch (len - i) {
        case 7: data |= ((size_t)d[6] << 24) << 24; prb_FALLTHROUGH;  // fall through
        case 6: data |= ((size_t)d[5] << 20) << 20; prb_FALLTHROUGH;  // fall through
        case 5: data |= ((size_t)d[4] << 16) << 16; prb_FALLTHROUGH;  // fall through
        case 4: data |= (d[3] << 24); prb_FALLTHROUGH;  // fall through
        case 3: data |= (d[2] << 16); prb_FALLTHROUGH;  // fall through
        case 2: data |= (d[1] << 8); prb_FALLTHROUGH;  // fall through
        case 1: data |= d[0]; prb_FALLTHROUGH;  // fall through
        case 0: break;
    }
    v3 ^= data;
    for (j = 0; j < prb_STBDS_SIPHASH_C_ROUNDS; ++j)
        prb_STBDS_SIPROUND();
    v0 ^= data;
    v2 ^= 0xff;
    for (j = 0; j < prb_STBDS_SIPHASH_D_ROUNDS; ++j)
        prb_STBDS_SIPROUND();

#ifdef prb_STBDS_SIPHASH_2_4
    return v0 ^ v1 ^ v2 ^ v3;
#else
    return v1 ^ v2
        ^ v3;  // slightly stronger since v0^v3 in above cancels out final round operation? I tweeted at the authors of SipHash about this but they didn't reply
#endif
}

prb_STBDS__PUBLICDEF size_t
prb_stbds_hash_bytes(void* p, size_t len, size_t seed) {
#ifdef prb_STBDS_SIPHASH_2_4
    return prb_stbds_siphash_bytes(p, len, seed);
#else
    unsigned char* d = (unsigned char*)p;

    if (len == 4) {
        unsigned int hash = (unsigned int)d[0] | (unsigned int)(d[1] << 8) | (unsigned int)(d[2] << 16) | (unsigned int)(d[3] << 24);
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
        size_t hash = (size_t)d[0] | (size_t)(d[1] << 8) | (size_t)(d[2] << 16) | (size_t)(d[3] << 24);
        hash |= (size_t)(d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24)) << 16 << 16;  // avoid warning if size_t == 4
        hash ^= seed;
        hash = (~hash) + (hash << 21);
        hash ^= prb_STBDS_ROTATE_RIGHT(hash, 24);
        hash *= 265;
        hash ^= prb_STBDS_ROTATE_RIGHT(hash, 14);
        hash ^= seed;
        hash *= 21;
        hash ^= prb_STBDS_ROTATE_RIGHT(hash, 28);
        hash += (hash << 31);
        hash = (~hash) + (hash << 18);
        return hash;
    } else {
        return prb_stbds_siphash_bytes(p, len, seed);
    }
#endif
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

static int
prb_stbds_is_key_equal(void* a, size_t elemsize, void* key, size_t keysize, size_t keyoffset, int mode, size_t i) {
    if (mode >= prb_STBDS_HM_STRING)
        return 0 == prb_strcmp((char*)key, *(char**)((char*)a + elemsize * i + keyoffset));
    else
        return prb_memeq(key, (char*)a + elemsize * i + keyoffset, (int32_t)keysize);
}

#define prb_STBDS_HASH_TO_ARR(x, elemsize) ((char*)(x) - (elemsize))
#define prb_STBDS_ARR_TO_HASH(x, elemsize) ((char*)(x) + (elemsize))

#define prb_stbds_hash_table(a) ((prb_stbds_hash_index*)prb_stbds_header(a)->hash_table)

prb_STBDS__PUBLICDEF void
prb_stbds_hmfree_func(void* a, size_t elemsize) {
    prb_STBDS_NOTUSED(elemsize);
    if (a == NULL)
        return;
    if (prb_stbds_hash_table(a) != NULL) {
        if (prb_stbds_hash_table(a)->string.mode == prb_STBDS_SH_STRDUP) {
            size_t i;
            // skip 0th element, which is default
            for (i = 1; i < prb_stbds_header(a)->length; ++i)
                prb_STBDS_FREE(NULL, *(char**)((char*)a + elemsize * i));
        }
        prb_stbds_strreset(&prb_stbds_hash_table(a)->string);
    }
    prb_STBDS_FREE(NULL, prb_stbds_header(a)->hash_table);
    prb_STBDS_FREE(NULL, prb_stbds_header(a));
}

static ptrdiff_t
prb_stbds_hm_find_slot(void* a, size_t elemsize, void* key, size_t keysize, size_t keyoffset, int mode) {
    void*                  raw_a = prb_STBDS_HASH_TO_ARR(a, elemsize);
    prb_stbds_hash_index*  table = prb_stbds_hash_table(raw_a);
    size_t                 hash = mode >= prb_STBDS_HM_STRING ? prb_stbds_hash_string((char*)key, table->seed)
                                                              : prb_stbds_hash_bytes(key, keysize, table->seed);
    size_t                 step = prb_STBDS_BUCKET_LENGTH;
    size_t                 limit, i;
    size_t                 pos;
    prb_stbds_hash_bucket* bucket;

    if (hash < 2)
        hash += 2;  // stored hash values are forbidden from being 0, so we can detect empty slots

    pos = prb_stbds_probe_position(hash, table->slot_count, table->slot_count_log2);

    for (;;) {
        prb_STBDS_STATS(++prb_stbds_hash_probes);
        bucket = &table->storage[pos >> prb_STBDS_BUCKET_SHIFT];

        // start searching from pos to end of bucket, this should help performance on small hash tables that fit in cache
        for (i = pos & prb_STBDS_BUCKET_MASK; i < prb_STBDS_BUCKET_LENGTH; ++i) {
            if (bucket->hash[i] == hash) {
                if (prb_stbds_is_key_equal(a, elemsize, key, keysize, keyoffset, mode, (size_t)bucket->index[i])) {
                    return (ptrdiff_t)((pos & ~prb_STBDS_BUCKET_MASK) + i);
                }
            } else if (bucket->hash[i] == prb_STBDS_HASH_EMPTY) {
                return -1;
            }
        }

        // search from beginning of bucket to pos
        limit = pos & prb_STBDS_BUCKET_MASK;
        for (i = 0; i < limit; ++i) {
            if (bucket->hash[i] == hash) {
                if (prb_stbds_is_key_equal(a, elemsize, key, keysize, keyoffset, mode, (size_t)bucket->index[i])) {
                    return (ptrdiff_t)((pos & ~prb_STBDS_BUCKET_MASK) + i);
                }
            } else if (bucket->hash[i] == prb_STBDS_HASH_EMPTY) {
                return -1;
            }
        }

        // quadratic probing
        pos += step;
        step += prb_STBDS_BUCKET_LENGTH;
        pos &= (table->slot_count - 1);
    }
    /* NOTREACHED */
}

prb_STBDS__PUBLICDEF void*
prb_stbds_hmget_key_ts(void* a, size_t elemsize, void* key, size_t keysize, ptrdiff_t* temp, int mode) {
    size_t keyoffset = 0;
    if (a == NULL) {
        // make it non-empty so we can return a temp
        a = prb_stbds_arrgrowf(0, elemsize, 0, 1);
        prb_stbds_header(a)->length += 1;
        prb_memset(a, 0, elemsize);
        *temp = prb_STBDS_INDEX_EMPTY;
        // adjust a to point after the default element
        return prb_STBDS_ARR_TO_HASH(a, elemsize);
    } else {
        prb_stbds_hash_index* table;
        void*                 raw_a = prb_STBDS_HASH_TO_ARR(a, elemsize);
        // adjust a to point to the default element
        table = (prb_stbds_hash_index*)prb_stbds_header(raw_a)->hash_table;
        if (table == 0) {
            *temp = -1;
        } else {
            ptrdiff_t slot = prb_stbds_hm_find_slot(a, elemsize, key, keysize, keyoffset, mode);
            if (slot < 0) {
                *temp = prb_STBDS_INDEX_EMPTY;
            } else {
                prb_stbds_hash_bucket* b = &table->storage[slot >> prb_STBDS_BUCKET_SHIFT];
                *temp = b->index[slot & prb_STBDS_BUCKET_MASK];
            }
        }
        return a;
    }
}

prb_STBDS__PUBLICDEF void*
prb_stbds_hmget_key(void* a, size_t elemsize, void* key, size_t keysize, int mode) {
    ptrdiff_t temp;
    void*     p = prb_stbds_hmget_key_ts(a, elemsize, key, keysize, &temp, mode);
    prb_stbds_temp(prb_STBDS_HASH_TO_ARR(p, elemsize)) = temp;
    return p;
}

prb_STBDS__PUBLICDEF void*
prb_stbds_hmput_default(void* a, size_t elemsize) {
    // three cases:
    //   a is NULL <- allocate
    //   a has a hash table but no entries, because of shmode <- grow
    //   a has entries <- do nothing
    if (a == NULL || prb_stbds_header(prb_STBDS_HASH_TO_ARR(a, elemsize))->length == 0) {
        a = prb_stbds_arrgrowf(a ? prb_STBDS_HASH_TO_ARR(a, elemsize) : NULL, elemsize, 0, 1);
        prb_stbds_header(a)->length += 1;
        prb_memset(a, 0, elemsize);
        a = prb_STBDS_ARR_TO_HASH(a, elemsize);
    }
    return a;
}

static char* prb_stbds_strdup(char* str);

prb_STBDS__PUBLICDEF void*
prb_stbds_hmput_key(void* a, size_t elemsize, void* key, size_t keysize, int mode) {
    size_t                keyoffset = 0;
    void*                 raw_a;
    prb_stbds_hash_index* table;

    if (a == NULL) {
        a = prb_stbds_arrgrowf(0, elemsize, 0, 1);
        prb_memset(a, 0, elemsize);
        prb_stbds_header(a)->length += 1;
        // adjust a to point AFTER the default element
        a = prb_STBDS_ARR_TO_HASH(a, elemsize);
    }

    // adjust a to point to the default element
    raw_a = a;
    a = prb_STBDS_HASH_TO_ARR(a, elemsize);

    table = (prb_stbds_hash_index*)prb_stbds_header(a)->hash_table;

    if (table == NULL || table->used_count >= table->used_count_threshold) {
        prb_stbds_hash_index* nt;
        size_t                slot_count;

        slot_count = (table == NULL) ? prb_STBDS_BUCKET_LENGTH : table->slot_count * 2;
        nt = prb_stbds_make_hash_index(slot_count, table);
        if (table)
            prb_STBDS_FREE(NULL, table);
        else
            nt->string.mode = mode >= prb_STBDS_HM_STRING ? (unsigned char)prb_STBDS_SH_DEFAULT : (unsigned char)0;
        prb_stbds_header(a)->hash_table = table = nt;
        prb_STBDS_STATS(++prb_stbds_hash_grow);
    }

    // we iterate hash table explicitly because we want to track if we saw a tombstone
    {
        size_t                 hash = mode >= prb_STBDS_HM_STRING ? prb_stbds_hash_string((char*)key, table->seed)
                                                                  : prb_stbds_hash_bytes(key, keysize, table->seed);
        size_t                 step = prb_STBDS_BUCKET_LENGTH;
        size_t                 pos;
        ptrdiff_t              tombstone = -1;
        prb_stbds_hash_bucket* bucket;

        // stored hash values are forbidden from being 0, so we can detect empty slots to early out quickly
        if (hash < 2)
            hash += 2;

        pos = prb_stbds_probe_position(hash, table->slot_count, table->slot_count_log2);

        for (;;) {
            size_t limit, i;
            prb_STBDS_STATS(++prb_stbds_hash_probes);
            bucket = &table->storage[pos >> prb_STBDS_BUCKET_SHIFT];

            // start searching from pos to end of bucket
            for (i = pos & prb_STBDS_BUCKET_MASK; i < prb_STBDS_BUCKET_LENGTH; ++i) {
                if (bucket->hash[i] == hash) {
                    if (prb_stbds_is_key_equal(raw_a, elemsize, key, keysize, keyoffset, mode, (size_t)bucket->index[i])) {
                        prb_stbds_temp(a) = bucket->index[i];
                        if (mode >= prb_STBDS_HM_STRING)
                            prb_stbds_temp_key(a) = *(char**)((char*)raw_a + elemsize * bucket->index[i] + keyoffset);
                        return prb_STBDS_ARR_TO_HASH(a, elemsize);
                    }
                } else if (bucket->hash[i] == 0) {
                    pos = (pos & ~prb_STBDS_BUCKET_MASK) + i;
                    goto found_empty_slot;
                } else if (tombstone < 0) {
                    if (bucket->index[i] == prb_STBDS_INDEX_DELETED)
                        tombstone = (ptrdiff_t)((pos & ~prb_STBDS_BUCKET_MASK) + i);
                }
            }

            // search from beginning of bucket to pos
            limit = pos & prb_STBDS_BUCKET_MASK;
            for (i = 0; i < limit; ++i) {
                if (bucket->hash[i] == hash) {
                    if (prb_stbds_is_key_equal(raw_a, elemsize, key, keysize, keyoffset, mode, (size_t)bucket->index[i])) {
                        prb_stbds_temp(a) = bucket->index[i];
                        return prb_STBDS_ARR_TO_HASH(a, elemsize);
                    }
                } else if (bucket->hash[i] == 0) {
                    pos = (pos & ~prb_STBDS_BUCKET_MASK) + i;
                    goto found_empty_slot;
                } else if (tombstone < 0) {
                    if (bucket->index[i] == prb_STBDS_INDEX_DELETED)
                        tombstone = (ptrdiff_t)((pos & ~prb_STBDS_BUCKET_MASK) + i);
                }
            }

            // quadratic probing
            pos += step;
            step += prb_STBDS_BUCKET_LENGTH;
            pos &= (table->slot_count - 1);
        }
    found_empty_slot:
        if (tombstone >= 0) {
            pos = (size_t)tombstone;
            --table->tombstone_count;
        }
        ++table->used_count;

        {
            ptrdiff_t i = (ptrdiff_t)prb_stbds_arrlen(a);
            // we want to do prb_stbds_arraddn(1), but we can't use the macros since we don't have something of the right type
            if ((size_t)i + 1 > prb_stbds_arrcap(a))
                *(void**)&a = prb_stbds_arrgrowf(a, elemsize, 1, 0);
            raw_a = prb_STBDS_ARR_TO_HASH(a, elemsize);
            prb_unused(raw_a);

            prb_STBDS_ASSERT((size_t)i + 1 <= prb_stbds_arrcap(a));
            prb_stbds_header(a)->length = (size_t)(i + 1);
            bucket = &table->storage[pos >> prb_STBDS_BUCKET_SHIFT];
            bucket->hash[pos & prb_STBDS_BUCKET_MASK] = hash;
            bucket->index[pos & prb_STBDS_BUCKET_MASK] = i - 1;
            prb_stbds_temp(a) = i - 1;

            switch (table->string.mode) {
                case prb_STBDS_SH_STRDUP:
                    prb_stbds_temp_key(a) = *(char**)((char*)a + elemsize * i) = prb_stbds_strdup((char*)key);
                    break;
                case prb_STBDS_SH_ARENA:
                    prb_stbds_temp_key(a) = *(char**)((char*)a + elemsize * i) = prb_stbds_stralloc(&table->string, (char*)key);
                    break;
                case prb_STBDS_SH_DEFAULT: prb_stbds_temp_key(a) = *(char**)((char*)a + elemsize * i) = (char*)key; break;
                default: prb_memcpy((char*)a + elemsize * i, key, keysize); break;
            }
        }
        return prb_STBDS_ARR_TO_HASH(a, elemsize);
    }
}

prb_STBDS__PUBLICDEF void*
prb_stbds_shmode_func(size_t elemsize, int mode) {
    void*                 a = prb_stbds_arrgrowf(0, elemsize, 0, 1);
    prb_stbds_hash_index* h;
    prb_memset(a, 0, elemsize);
    prb_stbds_header(a)->length = 1;
    prb_stbds_header(a)->hash_table = h = (prb_stbds_hash_index*)prb_stbds_make_hash_index(prb_STBDS_BUCKET_LENGTH, NULL);
    h->string.mode = (unsigned char)mode;
    return prb_STBDS_ARR_TO_HASH(a, elemsize);
}

prb_STBDS__PUBLICDEF void*
prb_stbds_hmdel_key(void* a, size_t elemsize, void* key, size_t keysize, size_t keyoffset, int mode) {
    if (a == NULL) {
        return 0;
    } else {
        prb_stbds_hash_index* table;
        void*                 raw_a = prb_STBDS_HASH_TO_ARR(a, elemsize);
        table = (prb_stbds_hash_index*)prb_stbds_header(raw_a)->hash_table;
        prb_stbds_temp(raw_a) = 0;
        if (table == 0) {
            return a;
        } else {
            ptrdiff_t slot;
            slot = prb_stbds_hm_find_slot(a, elemsize, key, keysize, keyoffset, mode);
            if (slot < 0)
                return a;
            else {
                prb_stbds_hash_bucket* b = &table->storage[slot >> prb_STBDS_BUCKET_SHIFT];
                int                    i = slot & prb_STBDS_BUCKET_MASK;
                ptrdiff_t              old_index = b->index[i];
                ptrdiff_t              final_index =
                    (ptrdiff_t)prb_stbds_arrlen(raw_a) - 1 - 1;  // minus one for the raw_a vs a, and minus one for 'last'
                prb_STBDS_ASSERT(slot < (ptrdiff_t)table->slot_count);
                --table->used_count;
                ++table->tombstone_count;
                prb_stbds_temp(raw_a) = 1;
                // prb_STBDS_ASSERT(table->used_count >= 0);
                //prb_STBDS_ASSERT(table->tombstone_count < table->slot_count/4);
                b->hash[i] = prb_STBDS_HASH_DELETED;
                b->index[i] = prb_STBDS_INDEX_DELETED;

                if (mode == prb_STBDS_HM_STRING && table->string.mode == prb_STBDS_SH_STRDUP) {
                    prb_STBDS_FREE(NULL, *(char**)((char*)a + elemsize * old_index));
                }

                // if indices are the same, memcpy is a no-op, but back-pointer-fixup will fail, so skip
                if (old_index != final_index) {
                    // swap delete
                    prb_memmove((char*)a + elemsize * old_index, (char*)a + elemsize * final_index, elemsize);

                    // now find the slot for the last element
                    if (mode == prb_STBDS_HM_STRING)
                        slot = prb_stbds_hm_find_slot(
                            a,
                            elemsize,
                            *(char**)((char*)a + elemsize * old_index + keyoffset),
                            keysize,
                            keyoffset,
                            mode
                        );
                    else
                        slot = prb_stbds_hm_find_slot(
                            a,
                            elemsize,
                            (char*)a + elemsize * old_index + keyoffset,
                            keysize,
                            keyoffset,
                            mode
                        );
                    prb_STBDS_ASSERT(slot >= 0);
                    b = &table->storage[slot >> prb_STBDS_BUCKET_SHIFT];
                    i = slot & prb_STBDS_BUCKET_MASK;
                    prb_STBDS_ASSERT(b->index[i] == final_index);
                    b->index[i] = old_index;
                }
                prb_stbds_header(raw_a)->length -= 1;

                if (table->used_count < table->used_count_shrink_threshold && table->slot_count > prb_STBDS_BUCKET_LENGTH) {
                    prb_stbds_header(raw_a)->hash_table = prb_stbds_make_hash_index(table->slot_count >> 1, table);
                    prb_STBDS_FREE(NULL, table);
                    prb_STBDS_STATS(++prb_stbds_hash_shrink);
                } else if (table->tombstone_count > table->tombstone_count_threshold) {
                    prb_stbds_header(raw_a)->hash_table = prb_stbds_make_hash_index(table->slot_count, table);
                    prb_STBDS_FREE(NULL, table);
                    prb_STBDS_STATS(++prb_stbds_hash_rebuild);
                }

                return a;
            }
        }
    }
    /* NOTREACHED */
}

static char*
prb_stbds_strdup(char* str) {
    // to keep replaceable allocator simple, we don't want to use strdup.
    // rolling our own also avoids problem of strdup vs _strdup
    size_t len = (size_t)prb_strlen(str) + 1;
    char*  p = (char*)prb_STBDS_REALLOC(NULL, 0, len);
    prb_memmove(p, str, len);
    return p;
}

#ifndef prb_STBDS_STRING_ARENA_BLOCKSIZE_MIN
#define prb_STBDS_STRING_ARENA_BLOCKSIZE_MIN 512u
#endif
#ifndef prb_STBDS_STRING_ARENA_BLOCKSIZE_MAX
#define prb_STBDS_STRING_ARENA_BLOCKSIZE_MAX (1u << 20)
#endif

prb_STBDS__PUBLICDEF char*
prb_stbds_stralloc(prb_stbds_string_arena* a, char* str) {
    char*  p;
    size_t len = (size_t)prb_strlen(str) + 1;
    if (len > a->remaining) {
        // compute the next blocksize
        size_t blocksize = a->block;

        // size is 512, 512, 1024, 1024, 2048, 2048, 4096, 4096, etc., so that
        // there are log(SIZE) allocations to free when we destroy the table
        blocksize = (size_t)(prb_STBDS_STRING_ARENA_BLOCKSIZE_MIN) << (blocksize >> 1);

        // if size is under 1M, advance to next blocktype
        if (blocksize < (size_t)(prb_STBDS_STRING_ARENA_BLOCKSIZE_MAX))
            ++a->block;

        if (len > blocksize) {
            // if string is larger than blocksize, then just allocate the full size.
            // note that we still advance string_block so block size will continue
            // increasing, so e.g. if somebody only calls this with 1000-long strings,
            // eventually the arena will start doubling and handling those as well
            prb_stbds_string_block* sb = (prb_stbds_string_block*)prb_STBDS_REALLOC(NULL, 0, sizeof(*sb) - 8 + len);
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
            prb_stbds_string_block* sb = (prb_stbds_string_block*)prb_STBDS_REALLOC(NULL, 0, sizeof(*sb) - 8 + blocksize);
            sb->next = a->storage;
            a->storage = sb;
            a->remaining = blocksize;
        }
    }

    prb_STBDS_ASSERT(len <= a->remaining);
    p = a->storage->storage + a->remaining - len;
    a->remaining -= len;
    prb_memmove(p, str, len);
    return p;
}

prb_STBDS__PUBLICDEF void
prb_stbds_strreset(prb_stbds_string_arena* a) {
    prb_stbds_string_block *x, *y;
    x = a->storage;
    while (x) {
        y = x->next;
        prb_STBDS_FREE(NULL, x);
        x = y;
    }
    prb_memset(a, 0, sizeof(*a));
}

#endif  // prb_NO_IMPLEMENTATION

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// NOLINTEND(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)

// This software is available under 2 licenses -- choose whichever you prefer.
// ------------------------------------------------------------------------------
// ALTERNATIVE A - MIT License
// Copyright (c) 2023 Arseniy Khvorov
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ------------------------------------------------------------------------------
// ALTERNATIVE B - Public Domain (www.unlicense.org)
// This is free and unencumbered software released into the public domain.
// Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
// software, either in source code form or as a compiled binary, for any purpose,
// commercial or non-commercial, and by any means.
// In jurisdictions that recognize copyright laws, the author or authors of this
// software dedicate any and all copyright interest in the software to the public
// domain. We make this dedication for the benefit of the public at large and to
// the detriment of our heirs and successors. We intend this dedication to be an
// overt act of relinquishment in perpetuity of all present and future rights to
// this software under copyright law.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
