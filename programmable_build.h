/* A set of utilities for writing "build scripts" as small C (or C++) programs.

Repository: https://github.com/khvorov45/programmable_build
See example/build.c for an example build script.

Define prb_IMPLEMENTATION before including the header in exactly one translation unit.
Define prb_NOT_STATIC if using in multiple translation units.
Don't forget to call prb_init() before doing anything.

Note that all memory operations (such as string formatting) 
are running off a linear allocator that is not thread-safe,
(so don't call prb_fmt* from multiple threads, for example).

All string formatting functions are wrappers around stb printf
https://github.com/nothings/stb/blob/master/stb_sprintf.h
The strings are allocated on the linear allocator everything else is using.
The original stb sprintf API is still exposed.

The library includes all of stb ds
https://github.com/nothings/stb/blob/master/stb_ds.h
There are no wrappers for it, use the original API.
All memory allocation calls in stb ds are hooked up to the linear allocator 
everything else is using (memory freeing doesn't do anything). 

If a prb_* function ever returns an array (pointer to multiple elements) then it's
an stb ds array, so get its length with arrlen()

All prb_* iterators are meant to be used in loops like this
for (prb_Iter iter = prb_createIter(); prb_iterNext(&iter) == prb_Success;) {
    // pull stuff you need off iter
}
*/

// TODO(khvorov) Make sure utf8 paths work on windows
// TODO(khvorov) File search by regex + recursive
// TODO(khvorov) Directory iterator to compress all readdir usages

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

#define prb_max(a, b) (((a) > (b)) ? (a) : (b))
#define prb_min(a, b) (((a) < (b)) ? (a) : (b))
#define prb_clamp(x, a, b) (((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x)))
#define prb_arrayLength(arr) (sizeof(arr) / sizeof(arr[0]))
#define prb_allocArray(type, len) (type*)prb_allocAndZero((len) * sizeof(type), alignof(type))
#define prb_beginArray(type) (type*)prb_beginArray_(alignof(type))
#define prb_allocStruct(type) (type*)prb_allocAndZero(sizeof(type), alignof(type))
#define prb_isPowerOf2(x) (((x) > 0) && (((x) & ((x)-1)) == 0))

#define prb_countLeading1sU32(x) __builtin_clz(~(x))
#define prb_countLeading1sU8(x) prb_countLeading1sU32((x) << 24)

// clang-format off

#define prb_STR(x) (prb_String) {x, prb_strlen(x)}
#define prb_LIT(x) (x).len, (x).str

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
// NOTE(khvorov) This will only work if we can actually allocate memory for the string.
// Lacking memory is an extremely unlikely problem for this library, so this is probably fine.
#define prb_assertAction() do {\
    prb_fmtAndPrintlnColor(prb_ColorID_Red, "%s:%d %s assertion failure", __FILE__, __LINE__, __FUNCTION__);\
    prb_debugbreak();\
    prb_terminate(1);\
} while (0)
#endif

#ifndef prb_assert
#define prb_assert(condition) do { if (!(condition)) { prb_assertAction(); } } while (0)
#endif
// clang-format on

#ifndef prb_IMPLEMENTATION
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
    bool    locked;
    int32_t tempCount;
} prb_Arena;

typedef struct prb_TempMemory {
    int32_t usedAtBegin;
    int32_t tempCountAtBegin;
} prb_TempMemory;

// Assume: utf-8, immutable
typedef struct prb_String {
    const char* str;
    int32_t     len;
} prb_String;

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

typedef struct prb_ProcessHandle {
    bool       valid;
    bool       completed;
    prb_Status completionStatus;

#if prb_PLATFORM_WINDOWS
#error unimplemented
#elif prb_PLATFORM_LINUX
    pid_t pid;
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
    prb_StringFindMode  mode;
    prb_StringDirection direction;
} prb_StringFindSpec;

typedef struct prb_StringFindResult {
    bool    found;
    int32_t matchByteIndex;
    int32_t matchLen;
} prb_StringFindResult;

typedef struct prb_LineIterator {
    prb_String ogstr;
    int32_t    curByteOffset;
    prb_String curLine;
    int32_t    curLineEndLen;
} prb_LineIterator;

typedef struct prb_Utf8CharIterator {
    prb_String          str;
    prb_StringDirection direction;
    int32_t             curByteOffset;
    uint32_t            curUtf32Char;
    int32_t             curUtf8Bytes;
    bool                curIsValid;
} prb_Utf8CharIterator;

#ifndef prb_IMPLEMENTATION
extern prb_Arena prb_globalArena;
#endif

// SECTION Core
prb_PUBLICDEC void prb_init(int32_t virtualMemoryBytesToUse);
prb_PUBLICDEC void prb_terminate(int32_t code);

// SECTION Memory
prb_PUBLICDEC bool           prb_memeq(const void* ptr1, const void* ptr2, int32_t bytes);
prb_PUBLICDEC void*          prb_vmemAllocate(int32_t size);
prb_PUBLICDEC int32_t        prb_getOffsetForAlignment(void* ptr, int32_t align);
prb_PUBLICDEC void           prb_globalArenaAlignFreePtr(int32_t align);
prb_PUBLICDEC void*          prb_allocAndZero(int32_t size, int32_t align);
prb_PUBLICDEC void*          prb_realloc(void* ptr, int32_t size);
prb_PUBLICDEC void*          prb_globalArenaCurrentFreePtr(void);
prb_PUBLICDEC int32_t        prb_globalArenaCurrentFreeSize(void);
prb_PUBLICDEC void           prb_globalArenaChangeUsed(int32_t byteDelta);
prb_PUBLICDEC void           prb_globalArenaLock(void);
prb_PUBLICDEC void           prb_globalArenaUnlock(void);
prb_PUBLICDEC void*          prb_beginArray_(int32_t align);
prb_PUBLICDEC void           prb_endArray(int32_t bytes);
prb_PUBLICDEC prb_TempMemory prb_beginTempMemory(void);
prb_PUBLICDEC void           prb_endTempMemory(prb_TempMemory temp);

// SECTION Filesystem
prb_PUBLICDEC bool                 prb_isDirectory(prb_String path);
prb_PUBLICDEC bool                 prb_isFile(prb_String path);
prb_PUBLICDEC bool                 prb_directoryIsEmpty(prb_String path);
prb_PUBLICDEC void                 prb_createDirIfNotExists(prb_String path);
prb_PUBLICDEC void                 prb_removeFileOrDirectoryIfExists(prb_String path);
prb_PUBLICDEC void                 prb_removeFileIfExists(prb_String path);
prb_PUBLICDEC void                 prb_removeDirectoryIfExists(prb_String path);
prb_PUBLICDEC void                 prb_clearDirectory(prb_String path);
prb_PUBLICDEC prb_String           prb_getCurrentWorkingDir(void);
prb_PUBLICDEC prb_String           prb_pathJoin(prb_String path1, prb_String path2);
prb_PUBLICDEC prb_StringFindResult prb_findSepBeforeLastEntry(prb_String path);
prb_PUBLICDEC prb_String           prb_getParentDir(prb_String path);
prb_PUBLICDEC prb_String           prb_getLastEntryInPath(prb_String path);
prb_PUBLICDEC prb_String           prb_replaceExt(prb_String path, prb_String newExt);
prb_PUBLICDEC prb_String*          prb_listAllEntriesInDir(prb_String path);
prb_PUBLICDEC prb_String*          prb_findAllMatchingPaths(prb_String pattern);
prb_PUBLICDEC uint64_t             prb_getLatestLastModifiedFromPattern(prb_String pattern);
prb_PUBLICDEC uint64_t             prb_getEarliestLastModifiedFromPattern(prb_String pattern);
prb_PUBLICDEC uint64_t             prb_getLatestLastModifiedFromPatterns(prb_String* patterns, int32_t patternsCount);
prb_PUBLICDEC uint64_t             prb_getEarliestLastModifiedFromPatterns(prb_String* patterns, int32_t patternsCount);
prb_PUBLICDEC prb_Bytes            prb_readEntireFile(prb_String path);
prb_PUBLICDEC void                 prb_writeEntireFile(prb_String path, prb_Bytes content);
prb_PUBLICDEC void                 prb_binaryToCArray(prb_String inPath, prb_String outPath, prb_String arrayName);

// SECTION Strings
prb_PUBLICDEC bool                 prb_streq(prb_String str1, prb_String str2);
prb_PUBLICDEC prb_String           prb_strSliceForward(prb_String str, int32_t bytes);
prb_PUBLICDEC const char*          prb_strGetNullTerminated(prb_String str);
prb_PUBLICDEC prb_StringFindResult prb_strFind(prb_StringFindSpec spec);
prb_PUBLICDEC bool                 prb_strStartsWith(prb_String str, prb_String pattern, prb_StringFindMode mode);
prb_PUBLICDEC bool                 prb_strEndsWith(prb_String str, prb_String pattern, prb_StringFindMode mode);
prb_PUBLICDEC prb_String           prb_strReplace(prb_StringFindSpec spec, prb_String replacement);
prb_PUBLICDEC prb_String           prb_stringCopy(prb_String source, int32_t fromInclusive, int32_t toInclusive);
prb_PUBLICDEC prb_String           prb_stringsJoin(prb_String* strings, int32_t stringsCount, prb_String sep);
prb_PUBLICDEC prb_String*          prb_stringArrayJoin(prb_String* arr1, int32_t arr1len, prb_String* arr2, int32_t arr2len);
prb_PUBLICDEC prb_String           prb_beginString(void);
prb_PUBLICDEC void                 prb_addStringSegment(prb_String* str, const char* fmt, ...) prb_ATTRIBUTE_FORMAT(2, 3);
prb_PUBLICDEC void                 prb_endString(prb_String str);
prb_PUBLICDEC prb_String           prb_fmtCustomBuffer(void* buf, int32_t bufSize, const char* fmt, ...) prb_ATTRIBUTE_FORMAT(3, 4);
prb_PUBLICDEC prb_String           prb_vfmtCustomBuffer(void* buf, int32_t bufSize, const char* fmt, va_list args);
prb_PUBLICDEC prb_String           prb_fmt(const char* fmt, ...) prb_ATTRIBUTE_FORMAT(1, 2);
prb_PUBLICDEC prb_String           prb_vfmt(const char* fmt, va_list args);
prb_PUBLICDEC prb_String           prb_fmtAndPrint(const char* fmt, ...) prb_ATTRIBUTE_FORMAT(1, 2);
prb_PUBLICDEC prb_String           prb_fmtAndPrintColor(prb_ColorID color, const char* fmt, ...) prb_ATTRIBUTE_FORMAT(2, 3);
prb_PUBLICDEC prb_String           prb_vfmtAndPrint(const char* fmt, va_list args);
prb_PUBLICDEC prb_String           prb_vfmtAndPrintColor(prb_ColorID color, const char* fmt, va_list args);
prb_PUBLICDEC prb_String           prb_fmtAndPrintln(const char* fmt, ...) prb_ATTRIBUTE_FORMAT(1, 2);
prb_PUBLICDEC prb_String           prb_fmtAndPrintlnColor(prb_ColorID color, const char* fmt, ...) prb_ATTRIBUTE_FORMAT(2, 3);
prb_PUBLICDEC prb_String           prb_vfmtAndPrintln(const char* fmt, va_list args);
prb_PUBLICDEC prb_String           prb_vfmtAndPrintlnColor(prb_ColorID color, const char* fmt, va_list args);
prb_PUBLICDEC void                 prb_writeToStdout(prb_String str);
prb_PUBLICDEC void                 prb_setPrintColor(prb_ColorID color);
prb_PUBLICDEC void                 prb_resetPrintColor(void);
prb_PUBLICDEC prb_Utf8CharIterator prb_createUtf8CharIter(prb_String str, prb_StringDirection direction);
prb_PUBLICDEC prb_Status           prb_utf8CharIterNext(prb_Utf8CharIterator* iter);
prb_PUBLICDEC prb_LineIterator     prb_createLineIter(prb_String str);
prb_PUBLICDEC prb_Status           prb_lineIterNext(prb_LineIterator* iter);

// SECTION Processes
prb_PUBLICDEC const char**      prb_getArgArrayFromString(prb_String string);
prb_PUBLICDEC prb_ProcessHandle prb_execCmd(prb_String cmd, prb_ProcessFlags flags, prb_String redirectFilepath);
prb_PUBLICDEC prb_Status        prb_waitForProcesses(prb_ProcessHandle* handles, int32_t handleCount);

// SECTION Timing
prb_PUBLICDEC prb_TimeStart prb_timeStart(void);
prb_PUBLICDEC float         prb_getMsFrom(prb_TimeStart timeStart);

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
#define STBDS_FREE(c, p)
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

#ifdef prb_IMPLEMENTATION

#ifdef prb_NOT_STATIC
prb_Arena prb_globalArena;
#else
static prb_Arena prb_globalArena;
#endif

//
// SECTION Core (implementation)
//

prb_PUBLICDEF void
prb_init(int32_t virtualMemoryBytesToUse) {
    prb_globalArena = (prb_Arena) {
        .base = prb_vmemAllocate(virtualMemoryBytesToUse),
        .size = virtualMemoryBytesToUse,
        .used = 0,
        .locked = false,
        .tempCount = 0,
    };
}

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

prb_PUBLICDEF void*
prb_vmemAllocate(int32_t size) {
#if prb_PLATFORM_WINDOWS

    void* ptr = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    prb_assert(ptr);
    return ptr;

#elif prb_PLATFORM_LINUX

    void* ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    prb_assert(ptr != MAP_FAILED);
    return ptr;

#else
#error unimplemented
#endif
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

prb_PUBLICDEF void
prb_globalArenaAlignFreePtr(int32_t align) {
    int32_t offset = prb_getOffsetForAlignment(prb_globalArenaCurrentFreePtr(), align);
    prb_globalArenaChangeUsed(offset);
}

prb_PUBLICDEF void*
prb_allocAndZero(int32_t size, int32_t align) {
    prb_assert(!prb_globalArena.locked);
    prb_globalArenaAlignFreePtr(align);
    void* result = prb_globalArenaCurrentFreePtr();
    prb_globalArenaChangeUsed(size);
    prb_memset(result, 0, size);
    return result;
}

prb_PUBLICDEF void*
prb_realloc(void* ptr, int32_t size) {
    void* result = prb_allocAndZero(size, 8);
    if (ptr) {
        prb_memcpy(result, ptr, size);
    }
    return result;
}

prb_PUBLICDEF void*
prb_globalArenaCurrentFreePtr(void) {
    void* result = (uint8_t*)prb_globalArena.base + prb_globalArena.used;
    return result;
}

prb_PUBLICDEF int32_t
prb_globalArenaCurrentFreeSize(void) {
    int32_t result = prb_globalArena.size - prb_globalArena.used;
    return result;
}

prb_PUBLICDEF void
prb_globalArenaChangeUsed(int32_t byteDelta) {
    prb_assert(prb_globalArenaCurrentFreeSize() >= byteDelta);
    prb_globalArena.used += byteDelta;
    prb_assert(prb_globalArena.used >= 0);
}

prb_PUBLICDEF void
prb_globalArenaLock(void) {
    prb_assert(!prb_globalArena.locked);
    prb_globalArena.locked = true;
}

prb_PUBLICDEF void
prb_globalArenaUnlock(void) {
    prb_assert(prb_globalArena.locked);
    prb_globalArena.locked = false;
}

prb_PUBLICDEF void*
prb_beginArray_(int32_t align) {
    prb_globalArenaAlignFreePtr(align);
    prb_globalArenaLock();
    void* result = prb_globalArenaCurrentFreePtr();
    return result;
}

prb_PUBLICDEF void
prb_endArray(int32_t bytes) {
    prb_assert(bytes >= 0);
    prb_globalArenaChangeUsed(bytes);
    prb_globalArenaUnlock();
}

prb_PUBLICDEF prb_TempMemory
prb_beginTempMemory(void) {
    prb_TempMemory temp = {.usedAtBegin = prb_globalArena.used, .tempCountAtBegin = prb_globalArena.tempCount};
    prb_globalArena.tempCount += 1;
    return temp;
}

prb_PUBLICDEF void
prb_endTempMemory(prb_TempMemory temp) {
    prb_assert(prb_globalArena.tempCount == temp.tempCountAtBegin + 1);
    prb_globalArena.used = temp.usedAtBegin;
    prb_globalArena.tempCount -= 1;
}

//
// SECTION Filesystem (implementation)
//

prb_PUBLICDEF bool
prb_isDirectory(prb_String path) {
    prb_assert(path.str && path.len > 0);
    bool           result = false;
    prb_TempMemory temp = prb_beginTempMemory();
    const char*    pathNull = prb_strGetNullTerminated(path);

#if prb_PLATFORM_WINDOWS

    prb_String pathNoTrailingSlash = path;
    char       lastChar = path.ptr[path.len - 1];
    if (lastChar == '/' || lastChar == '\\') {
        pathNoTrailingSlash = prb_stringCopy(path, 0, path.len - 2);
    }
    bool             result = false;
    WIN32_FIND_DATAA findData;
    HANDLE           findHandle = FindFirstFileA(pathNoTrailingSlash.ptr, &findData);
    if (findHandle != INVALID_HANDLE_VALUE) {
        result = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

#elif prb_PLATFORM_LINUX

    // TODO(khvorov) Test trailing slash

    struct stat statBuf = {};
    if (stat(pathNull, &statBuf) == 0) {
        result = S_ISDIR(statBuf.st_mode);
    }

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF bool
prb_isFile(prb_String path) {
    prb_assert(path.str && path.len > 0);
    bool           result = false;
    prb_TempMemory temp = prb_beginTempMemory();
    const char*    pathNull = prb_strGetNullTerminated(path);

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    struct stat statBuf = {};
    if (stat(pathNull, &statBuf) == 0) {
        result = S_ISREG(statBuf.st_mode);
    }

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF bool
prb_directoryIsEmpty(prb_String path) {
    prb_assert(prb_isDirectory(path));
    bool           result = true;
    prb_TempMemory temp = prb_beginTempMemory();
    const char*    pathNull = prb_strGetNullTerminated(path);

#if prb_PLATFORM_WINDOWS

    prb_String       search = prb_pathJoin2(path, prb_STR("*"));
    WIN32_FIND_DATAA findData;
    HANDLE           firstHandle = FindFirstFileA(search.ptr, &findData);
    bool             result = true;
    if (findData.cFileName[0] == '.' && findData.cFileName[1] == '\0') {
        while (FindNextFileA(firstHandle, &findData)) {
            if (findData.cFileName[0] == '.' && findData.cFileName[1] == '.' && findData.cFileName[2] == '\0') {
                continue;
            }
            result = false;
            break;
        }
    }

#elif prb_PLATFORM_LINUX

    DIR* pathHandle = opendir(pathNull);
    prb_assert(pathHandle);
    for (struct dirent* entry = readdir(pathHandle); entry; entry = readdir(pathHandle)) {
        bool isDot = entry->d_name[0] == '.' && entry->d_name[1] == '\0';
        bool isDoubleDot = entry->d_name[0] == '.' && entry->d_name[1] == '.' && entry->d_name[2] == '\0';
        if (!isDot && !isDoubleDot) {
            result = false;
            break;
        }
    }
    closedir(pathHandle);

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF void
prb_createDirIfNotExists(prb_String path) {
    prb_TempMemory temp = prb_beginTempMemory();
    const char*    pathNull = prb_strGetNullTerminated(path);

#if prb_PLATFORM_WINDOWS

    // TODO(khvorov) Check error
    CreateDirectory(path.ptr, 0);

#elif prb_PLATFORM_LINUX

    if (mkdir(pathNull, S_IRWXU | S_IRWXG | S_IRWXO) == -1) {
        prb_assert(prb_isDirectory(path));
    }

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
}

prb_PUBLICDEF void
prb_removeFileOrDirectoryIfExists(prb_String path) {
    if (prb_isDirectory(path)) {
        prb_removeDirectoryIfExists(path);
    } else {
        prb_removeFileIfExists(path);
    }
}

prb_PUBLICDEF void
prb_removeFileIfExists(prb_String path) {
    prb_assert(!prb_isDirectory(path));
    prb_TempMemory temp = prb_beginTempMemory();
    const char*    pathNull = prb_strGetNullTerminated(path);

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    unlink(pathNull);

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
}

prb_PUBLICDEF void
prb_removeDirectoryIfExists(prb_String path) {
    prb_TempMemory temp = prb_beginTempMemory();
    const char*    pathNull = prb_strGetNullTerminated(path);

#if prb_PLATFORM_WINDOWS

    prb_StringBuilder doubleNullBuilder = prb_createStringBuilder(path.len + 2);
    prb_stringBuilderWrite(&doubleNullBuilder, path);
    SHFileOperationA(&(SHFILEOPSTRUCTA) {
        .wFunc = FO_DELETE,
        .pFrom = doubleNullBuilder.string.ptr,
        .fFlags = FOF_NO_UI,
    });

#elif prb_PLATFORM_LINUX

    DIR* pathHandle = opendir(pathNull);
    if (pathHandle) {
        for (struct dirent* entry = readdir(pathHandle); entry; entry = readdir(pathHandle)) {
            bool isDot = entry->d_name[0] == '.' && entry->d_name[1] == '\0';
            bool isDoubleDot = entry->d_name[0] == '.' && entry->d_name[1] == '.' && entry->d_name[2] == '\0';
            if (!isDot && !isDoubleDot) {
                prb_String fullpath = prb_pathJoin(path, prb_STR(entry->d_name));
                prb_removeFileOrDirectoryIfExists(fullpath);
            }
        }
        prb_assert(prb_directoryIsEmpty(path));
        int32_t rmdirResult = rmdir(pathNull);
        prb_assert(rmdirResult == 0);
    }
    closedir(pathHandle);

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
}

prb_PUBLICDEF void
prb_clearDirectory(prb_String path) {
    prb_removeFileOrDirectoryIfExists(path);
    prb_createDirIfNotExists(path);
}

prb_PUBLICDEF prb_String
prb_getCurrentWorkingDir(void) {
#if prb_PLATFORM_WINDOWS

    // TODO(khvorov) Make sure long paths work
    // TODO(khvorov) Check error
    int32_t maxLen = MAX_PATH + 1;
    char*   ptr = (char*)prb_allocAndZero(maxLen, 1);
    GetCurrentDirectoryA(maxLen, ptr);
    return ptr;

#elif prb_PLATFORM_LINUX

    prb_globalArenaLock();
    char* ptr = (char*)prb_globalArenaCurrentFreePtr();
    prb_assert(getcwd(ptr, prb_globalArenaCurrentFreeSize()));
    prb_String result = prb_STR(ptr);
    prb_globalArenaChangeUsed(result.len + 1);  // NOTE(khvorov) Null terminator
    prb_globalArenaUnlock();
    return result;

#else
#error unimplemented
#endif
}

prb_PUBLICDEF prb_String
prb_pathJoin(prb_String path1, prb_String path2) {
    prb_assert(path1.str && path2.str && path1.len > 0 && path2.len > 0);
    char path1LastChar = path1.str[path1.len - 1];
    bool path1EndsOnSep = path1LastChar == '/' || path1LastChar == '\\';
    if (path1EndsOnSep) {
        path1.len -= 1;
    }
    char path2FirstChar = path2.str[0];
    bool path2StartsOnSep = path2FirstChar == '/' || path2FirstChar == '\\';
    if (path2StartsOnSep) {
        path2 = prb_strSliceForward(path2, 1);
    }
    prb_String result = prb_fmt("%.*s/%.*s", path1.len, path1.str, path2.len, path2.str);
    return result;
}

prb_PUBLICDEF prb_StringFindResult
prb_findSepBeforeLastEntry(prb_String path) {
    prb_StringFindSpec spec = {
        .str = path,
        .pattern = prb_STR("/\\"),
        .mode = prb_StringFindMode_AnyChar,
        .direction = prb_StringDirection_FromEnd,
    };
    prb_StringFindResult result = prb_strFind(spec);
    if (result.matchByteIndex == spec.str.len - 1 && spec.str.len > 1) {
        spec.str.len -= 1;
        result = prb_strFind(spec);
    }
    return result;
}

prb_PUBLICDEF prb_String
prb_getParentDir(prb_String path) {
    prb_StringFindResult findResult = prb_findSepBeforeLastEntry(path);
    prb_String           result = findResult.found ? prb_stringCopy(path, 0, findResult.matchByteIndex) : prb_getCurrentWorkingDir();
    return result;
}

prb_PUBLICDEF prb_String
prb_getLastEntryInPath(prb_String path) {
    prb_StringFindResult findResult = prb_findSepBeforeLastEntry(path);
    prb_String           result = findResult.found ? prb_stringCopy(path, findResult.matchByteIndex + 1, path.len - 1) : path;
    return result;
}

prb_PUBLICDEF prb_String
prb_replaceExt(prb_String path, prb_String newExt) {
    prb_StringFindSpec spec = {
        .str = path,
        .pattern = prb_STR("."),
        .mode = prb_StringFindMode_AnyChar,
        .direction = prb_StringDirection_FromEnd,
    };
    prb_StringFindResult dotFind = prb_strFind(spec);
    prb_String           result = {};
    if (dotFind.found) {
        result = prb_fmt("%.*s.%.*s", dotFind.matchByteIndex, path.str, newExt.len, newExt.str);
    } else {
        result = prb_fmt("%.*s.%.*s", path.len, path.str, newExt.len, newExt.str);
    }
    return result;
}

prb_PUBLICDEF prb_String*
prb_listAllEntriesInDir(prb_String path) {
    prb_assert(prb_isDirectory(path));
    prb_String*    result = 0;
    prb_TempMemory temp = prb_beginTempMemory();
    const char*    pathNull = prb_strGetNullTerminated(path);

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    DIR* pathHandle = opendir(pathNull);
    prb_assert(pathHandle);
    prb_endTempMemory(temp);

    for (struct dirent* entry = readdir(pathHandle); entry; entry = readdir(pathHandle)) {
        bool isDot = entry->d_name[0] == '.' && entry->d_name[1] == '\0';
        bool isDoubleDot = entry->d_name[0] == '.' && entry->d_name[1] == '.' && entry->d_name[2] == '\0';
        if (!isDot && !isDoubleDot) {
            prb_String fullpath = prb_pathJoin(path, prb_STR(entry->d_name));
            arrput(result, fullpath);
        }
    }
    closedir(pathHandle);

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF prb_String*
prb_findAllMatchingPaths(prb_String pattern) {
    prb_String*    result = 0;
    prb_TempMemory temp = prb_beginTempMemory();
    const char*    patNull = prb_strGetNullTerminated(pattern);

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    glob_t globResult = {};
    int globReturn = glob(patNull, GLOB_NOSORT, 0, &globResult);
    prb_endTempMemory(temp);

    if (globReturn == 0) {
        arrsetcap(result, globResult.gl_pathc);
        for (size_t resultIndex = 0; resultIndex < globResult.gl_pathc; resultIndex++) {
            char* path = globResult.gl_pathv[resultIndex];
            int32_t pathlen = prb_strlen(path);
            arrput(result, prb_stringCopy(prb_STR(path), 0, pathlen - 1));
        }
    }
    globfree(&globResult);

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF uint64_t
prb_getLatestLastModifiedFromPattern(prb_String pattern) {
    uint64_t       result = 0;
    prb_TempMemory temp = prb_beginTempMemory();
    const char*    patNull = prb_strGetNullTerminated(pattern);

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

    glob_t globResult = {};
    if (glob(patNull, GLOB_NOSORT, 0, &globResult) == 0) {
        prb_assert(globResult.gl_pathc <= INT32_MAX);
        for (int32_t resultIndex = 0; resultIndex < (int32_t)globResult.gl_pathc; resultIndex++) {
            char* path = globResult.gl_pathv[resultIndex];
            struct stat statBuf = {};
            if (stat(path, &statBuf) == 0) {
                uint64_t thisLastMod = statBuf.st_mtim.tv_sec;
                result = prb_max(result, thisLastMod);
            }
        }
    }
    globfree(&globResult);

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF uint64_t
prb_getEarliestLastModifiedFromPattern(prb_String pattern) {
    uint64_t       result = UINT64_MAX;
    prb_TempMemory temp = prb_beginTempMemory();
    const char*    patNull = prb_strGetNullTerminated(pattern);

#if prb_PLATFORM_WINDOWS

    WIN32_FIND_DATAA findData;
    HANDLE           firstHandle = FindFirstFileA(pattern, &findData);
    if (firstHandle != INVALID_HANDLE_VALUE) {
        uint64_t thisLastMod =
            ((uint64_t)findData.ftLastWriteTime.dwHighDateTime << 32) | findData.ftLastWriteTime.dwLowDateTime;
        result = prb_min(result, thisLastMod);
        while (FindNextFileA(firstHandle, &findData)) {
            thisLastMod =
                ((uint64_t)findData.ftLastWriteTime.dwHighDateTime << 32) | findData.ftLastWriteTime.dwLowDateTime;
            result = prb_min(result, thisLastMod);
        }
        FindClose(firstHandle);
    } else {
        result = 0;
    }

#elif prb_PLATFORM_LINUX

    glob_t globResult = {};
    if (glob(patNull, GLOB_NOSORT, 0, &globResult) == 0) {
        prb_assert(globResult.gl_pathc <= INT32_MAX);
        for (int32_t resultIndex = 0; resultIndex < (int32_t)globResult.gl_pathc; resultIndex++) {
            char* path = globResult.gl_pathv[resultIndex];
            struct stat statBuf = {};
            if (stat(path, &statBuf) == 0) {
                uint64_t thisLastMod = statBuf.st_mtim.tv_sec;
                result = prb_min(result, thisLastMod);
            }
        }
    } else {
        result = 0;
    }
    globfree(&globResult);

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
    return result;
}

prb_PUBLICDEF uint64_t
prb_getLatestLastModifiedFromPatterns(prb_String* patterns, int32_t patternsCount) {
    uint64_t result = 0;
    for (int32_t patternIndex = 0; patternIndex < patternsCount; patternIndex++) {
        result = prb_max(result, prb_getLatestLastModifiedFromPattern(patterns[patternIndex]));
    }
    return result;
}

prb_PUBLICDEF uint64_t
prb_getEarliestLastModifiedFromPatterns(prb_String* patterns, int32_t patternsCount) {
    uint64_t result = UINT64_MAX;
    for (int32_t patternIndex = 0; patternIndex < patternsCount; patternIndex++) {
        result = prb_min(result, prb_getEarliestLastModifiedFromPattern(patterns[patternIndex]));
    }
    return result;
}

prb_PUBLICDEF prb_Bytes
prb_readEntireFile(prb_String path) {
    prb_TempMemory temp = prb_beginTempMemory();
    const char*    pathNull = prb_strGetNullTerminated(path);

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    int32_t handle = open(pathNull, O_RDONLY, 0);
    prb_assert(handle != -1);
    prb_endTempMemory(temp);    

    struct stat statBuf = {};
    prb_assert(fstat(handle, &statBuf) == 0);
    uint8_t* buf = (uint8_t*)prb_allocAndZero(statBuf.st_size + 1, 1);  // NOTE(sen) Null terminator just in case
    int32_t readResult = read(handle, buf, statBuf.st_size);
    prb_assert(readResult == statBuf.st_size);
    close(handle);
    prb_Bytes result = {buf, readResult};

#else
#error unimplemented
#endif

    return result;
}

prb_PUBLICDEF void
prb_writeEntireFile(prb_String path, prb_Bytes content) {
    prb_TempMemory temp = prb_beginTempMemory();
    const char*    pathNull = prb_strGetNullTerminated(path);

#if prb_PLATFORM_WINDOWS

#error unimplemented

#elif prb_PLATFORM_LINUX

    int32_t handle = open(pathNull, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR);
    prb_assert(handle != -1);
    int32_t writeResult = write(handle, content.data, content.len);
    prb_assert(writeResult == content.len);
    close(handle);

#else
#error unimplemented
#endif

    prb_endTempMemory(temp);
}

prb_PUBLICDEF void
prb_binaryToCArray(prb_String inPath, prb_String outPath, prb_String arrayName) {
    prb_Bytes inContent = prb_readEntireFile(inPath);
    prb_assert(inContent.len > 0);

    prb_TempMemory temp = prb_beginTempMemory();
    prb_String     arrayStr = prb_beginString();
    prb_addStringSegment(&arrayStr, "unsigned char %.*s[] = {", arrayName.len, arrayName.str);

    for (int32_t byteIndex = 0; byteIndex < inContent.len; byteIndex++) {
        uint8_t byte = inContent.data[byteIndex];
        prb_addStringSegment(&arrayStr, "0x%x", byte);
        if (byteIndex != inContent.len - 1) {
            prb_addStringSegment(&arrayStr, ", ");
        }
    }
    prb_addStringSegment(&arrayStr, "};");
    prb_endString(arrayStr);

    prb_writeEntireFile(outPath, (prb_Bytes) {(uint8_t*)arrayStr.str, arrayStr.len});
    prb_endTempMemory(temp);
}

//
// SECTION Strings (implementation)
//

prb_PUBLICDEF bool
prb_streq(prb_String str1, prb_String str2) {
    bool result = false;
    if (str1.len == str2.len) {
        result = prb_memeq(str1.str, str2.str, str1.len);
    }
    return result;
}

prb_PUBLICDEF prb_String
prb_strSliceForward(prb_String str, int32_t bytes) {
    prb_assert(bytes <= str.len);
    prb_String result = {str.str + bytes, str.len - bytes};
    return result;
}

prb_PUBLICDEF const char*
prb_strGetNullTerminated(prb_String str) {
    prb_assert(str.str && str.len > 0);
    const char* result = str.str;
    if (str.str[str.len] != '\0') {
        result = prb_fmt("%.*s", str.len, str.str).str;
    }
    return result;
}

prb_PUBLICDEF prb_StringFindResult
prb_strFind(prb_StringFindSpec spec) {
    prb_assert(spec.str.str && spec.pattern.str && spec.str.len >= 0 && spec.pattern.len >= 0);
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
                    uint8_t* str = (uint8_t*)spec.str.str;
                    uint8_t* pat = (uint8_t*)spec.pattern.str;
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
                        spec.str.len = prb_strlen(spec.str.str);
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
                prb_TempMemory temp = prb_beginTempMemory();

                regex_t     regexCompiled = {};
                const char* pat = prb_strGetNullTerminated(spec.pattern);
                int         compResult = regcomp(&regexCompiled, pat, REG_EXTENDED);
                prb_assert(compResult == 0);
                regmatch_t  pos = {};
                const char* str = prb_strGetNullTerminated(spec.str);
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

prb_PUBLICDEF bool
prb_strStartsWith(prb_String str, prb_String pattern, prb_StringFindMode mode) {
    str.len = prb_min(str.len, pattern.len);
    prb_StringFindResult find = prb_strFind((prb_StringFindSpec) {
        .str = str,
        .pattern = pattern,
        .mode = mode,
        .direction = prb_StringDirection_FromStart,
    });
    bool                 result = find.found && find.matchByteIndex == 0;
    return result;
}

prb_PUBLICDEF bool
prb_strEndsWith(prb_String str, prb_String pattern, prb_StringFindMode mode) {
    if (str.len > pattern.len) {
        str = prb_strSliceForward(str, str.len - pattern.len);
    }
    bool result = prb_strStartsWith(str, pattern, mode);
    return result;
}

prb_PUBLICDEF prb_String
prb_strReplace(prb_StringFindSpec spec, prb_String replacement) {
    prb_String           result = spec.str;
    prb_StringFindResult findResult = prb_strFind(spec);
    if (findResult.found) {
        prb_String strAfterMatch = prb_strSliceForward(spec.str, findResult.matchByteIndex + findResult.matchLen);
        result = prb_fmt(
            "%.*s%.*s%.*s",
            findResult.matchByteIndex,
            spec.str.str,
            replacement.len,
            replacement.str,
            strAfterMatch.len,
            strAfterMatch.str
        );
    }
    return result;
}

prb_PUBLICDEF prb_String
prb_stringCopy(prb_String source, int32_t fromInclusive, int32_t toInclusive) {
    prb_assert(fromInclusive >= 0 && toInclusive >= 0 && toInclusive >= fromInclusive);
    const char* srcFrom = source.str + fromInclusive;
    prb_String  result = prb_fmt("%.*s", toInclusive - fromInclusive + 1, srcFrom);
    return result;
}

prb_PUBLICDEF prb_String
prb_stringsJoin(prb_String* strings, int32_t stringsCount, prb_String sep) {
    prb_String result = prb_beginString();
    for (int32_t strIndex = 0; strIndex < stringsCount; strIndex++) {
        prb_String str = strings[strIndex];
        prb_addStringSegment(&result, "%.*s", str.len, str.str);
        if (strIndex < stringsCount - 1) {
            prb_addStringSegment(&result, "%.*s", sep.len, sep.str);
        }
    }
    prb_endString(result);
    return result;
}

prb_PUBLICDEF prb_String*
prb_stringArrayJoin(prb_String* arr1, int32_t arr1len, prb_String* arr2, int32_t arr2len) {
    int32_t     buflen = arr1len + arr2len;
    prb_String* buf = 0;
    arrsetcap(buf, buflen);
    for (int32_t index = 0; index < arr1len; index++) {
        arrput(buf, arr1[index]);
    }
    for (int32_t index = 0; index < arr2len; index++) {
        arrput(buf, arr2[index]);
    }
    return buf;
}

prb_PUBLICDEF prb_String
prb_beginString(void) {
    prb_globalArenaLock();
    prb_String result = {(const char*)prb_globalArenaCurrentFreePtr(), 0};
    return result;
}

prb_PUBLICDEF void
prb_addStringSegment(prb_String* str, const char* fmt, ...) {
    prb_assert(prb_globalArena.locked);
    prb_assert(prb_globalArenaCurrentFreeSize() > str->len);
    va_list args;
    va_start(args, fmt);
    prb_String seg = prb_vfmtCustomBuffer(
        (uint8_t*)prb_globalArenaCurrentFreePtr() + str->len,
        prb_globalArenaCurrentFreeSize() - str->len,
        fmt,
        args
    );
    str->len += seg.len;
    va_end(args);
}

prb_PUBLICDEF void
prb_endString(prb_String str) {
    prb_globalArenaUnlock();
    prb_globalArenaChangeUsed(str.len + 1);  // NOTE(khvorov) Null terminator
}

prb_PUBLICDEF prb_String
prb_fmtCustomBuffer(void* buf, int32_t bufSize, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    prb_String result = prb_vfmtCustomBuffer(buf, bufSize, fmt, args);
    va_end(args);
    return result;
}

prb_PUBLICDEF prb_String
prb_vfmtCustomBuffer(void* buf, int32_t bufSize, const char* fmt, va_list args) {
    int32_t    len = stbsp_vsnprintf((char*)buf, bufSize, fmt, args);
    prb_String result = {(const char*)buf, len};
    return result;
}

prb_PUBLICDEF prb_String
prb_fmt(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    prb_String result = prb_vfmt(fmt, args);
    va_end(args);
    return result;
}

prb_PUBLICDEF prb_String
prb_vfmt(const char* fmt, va_list args) {
    prb_beginArray_(1);
    prb_String result =
        prb_vfmtCustomBuffer(prb_globalArenaCurrentFreePtr(), prb_globalArenaCurrentFreeSize(), fmt, args);
    prb_endArray(result.len + 1);  // NOTE(khvorov) Null terminator
    return result;
}

prb_PUBLICDEF prb_String
prb_fmtAndPrint(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    prb_String result = prb_vfmtAndPrint(fmt, args);
    va_end(args);
    return result;
}

prb_PUBLICDEF prb_String
prb_fmtAndPrintColor(prb_ColorID color, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    prb_String result = prb_vfmtAndPrintColor(color, fmt, args);
    va_end(args);
    return result;
}

prb_PUBLICDEF prb_String
prb_vfmtAndPrint(const char* fmt, va_list args) {
    prb_String result = prb_vfmt(fmt, args);
    prb_writeToStdout(result);
    return result;
}

prb_PUBLICDEF prb_String
prb_vfmtAndPrintColor(prb_ColorID color, const char* fmt, va_list args) {
    prb_setPrintColor(color);
    prb_String result = prb_vfmtAndPrint(fmt, args);
    prb_resetPrintColor();
    return result;
}

prb_PUBLICDEF prb_String
prb_fmtAndPrintln(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    prb_String result = prb_vfmtAndPrintln(fmt, args);
    va_end(args);
    return result;
}

prb_PUBLICDEF prb_String
prb_fmtAndPrintlnColor(prb_ColorID color, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    prb_String result = prb_vfmtAndPrintlnColor(color, fmt, args);
    va_end(args);
    return result;
}

prb_PUBLICDEF prb_String
prb_vfmtAndPrintln(const char* fmt, va_list args) {
    prb_String result = prb_vfmtAndPrint(fmt, args);
    prb_fmtAndPrint("\n");
    return result;
}

prb_PUBLICDEF prb_String
prb_vfmtAndPrintlnColor(prb_ColorID color, const char* fmt, va_list args) {
    prb_setPrintColor(color);
    prb_String result = prb_vfmtAndPrintln(fmt, args);
    prb_resetPrintColor();
    return result;
}

prb_PUBLICDEF void
prb_writeToStdout(prb_String msg) {
#if prb_PLATFORM_WINDOWS

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(out, msg.ptr, msg.len, 0, 0);

#elif prb_PLATFORM_LINUX

    ssize_t writeResult = write(STDOUT_FILENO, msg.str, msg.len);
    prb_assert(writeResult == msg.len);

#else
#error unimplemented
#endif
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
    prb_assert(str.str && str.len >= 0);
    int32_t curByteOffset = 0;
    if (direction == prb_StringDirection_FromEnd) {
        curByteOffset = str.len - 1;
    }

    prb_Utf8CharIterator iter = {
        .str = str,
        .direction = direction,
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
        result = prb_Success;
        iter->curIsValid = true;

        uint8_t firstByte = iter->str.str[iter->curByteOffset];
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
            uint8_t firstByteMask[] = {
                0b01111111,
                0b00011111,
                0b00001111,
                0b00000111,
            };

            uint32_t ch = 0;
            switch (iter->direction) {
                case prb_StringDirection_FromStart: {
                    int32_t charBytes = leading1s == 0 ? 1 : leading1s;
                    iter->curUtf8Bytes = charBytes;
                    ch = firstByte & firstByteMask[charBytes - 1];
                    for (int32_t byteIndex = 1; byteIndex < charBytes; byteIndex++) {
                        uint8_t byte = iter->str.str[iter->curByteOffset + byteIndex];
                        if (prb_countLeading1sU8(byte) == 1) {
                            ch = (ch << 6) | (byte & 0b00111111);
                        } else {
                            iter->curByteOffset += byteIndex;
                            iter->curUtf8Bytes = 0;
                            iter->curIsValid = false;
                            break;
                        }
                    }
                } break;

                case prb_StringDirection_FromEnd: {
                    if (leading1s != 0) {
                        ch = firstByte & 0b00111111;
                        int32_t maxExtraBytes = prb_min(3, iter->curByteOffset);
                        for (int32_t byteIndex = 0; byteIndex < maxExtraBytes; byteIndex++) {
                            uint8_t byte = iter->str.str[--iter->curByteOffset];
                            int32_t byteLeading1s = prb_countLeading1sU8(byte);
                            int32_t chBytesTaken = 6 * (byteIndex + 1);
                            if (byteLeading1s == 1) {
                                ch = ((byte & 0b00111111) << chBytesTaken) | ch;
                            } else if (byteLeading1s == byteIndex + 2) {
                                iter->curUtf8Bytes = byteLeading1s;
                                ch = ((byte & firstByteMask[byteLeading1s - 1]) << chBytesTaken) | ch;
                                break;
                            } else {
                                iter->curIsValid = false;
                                break;
                            }
                        }
                    } else {
                        ch = firstByte;
                    }
                } break;
            }

            if (iter->curIsValid) {
                iter->curUtf32Char = ch;
            }
        } else {
            iter->curByteOffset += iter->direction == prb_StringDirection_FromStart ? 1 : -1;
            iter->curIsValid = false;
        }
    }

    return result;
}

prb_PUBLICDEF prb_LineIterator
prb_createLineIter(prb_String str) {
    prb_assert(str.str && str.len >= 0);
    prb_LineIterator iter = {
        .ogstr = str,
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
        prb_StringFindSpec spec = {
            .str = iter->curLine,
            .pattern = prb_STR("\r\n"),
            .mode = prb_StringFindMode_AnyChar,
            .direction = prb_StringDirection_FromStart,
        };
        prb_StringFindResult lineEndResult = prb_strFind(spec);
        if (lineEndResult.found) {
            iter->curLine.len = lineEndResult.matchByteIndex;
            iter->curLineEndLen = 1;
            if (iter->curLine.str[iter->curLine.len] == '\r'
                && iter->curByteOffset + iter->curLine.len + 1 < iter->ogstr.len
                && iter->curLine.str[iter->curLine.len + 1] == '\n') {
                iter->curLineEndLen += 1;
            }
        }
        result = prb_Success;
    }

    return result;
}

//
// SECTION Processes (implementation)
//

prb_PUBLICDEF const char**
prb_getArgArrayFromString(prb_String string) {
    int32_t spacesCount = 0;
    for (int32_t strIndex = 0; strIndex < string.len; strIndex++) {
        char ch = string.str[strIndex];
        if (ch == ' ') {
            spacesCount++;
        }
    }

    int32_t* spacesIndices = prb_allocArray(int32_t, spacesCount);
    {
        int32_t index = 0;
        for (int32_t strIndex = 0; strIndex < string.len; strIndex++) {
            char ch = string.str[strIndex];
            if (ch == ' ') {
                spacesIndices[index++] = strIndex;
            }
        }
    }

    int32_t argCount = 0;
    // NOTE(khvorov) Arg array needs a null at the end
    const char** args = prb_allocArray(const char*, spacesCount + 1 + 1);
    for (int32_t spaceIndex = 0; spaceIndex <= spacesCount; spaceIndex++) {
        int32_t    spaceBefore = spaceIndex == 0 ? -1 : spacesIndices[spaceIndex - 1];
        int32_t    spaceAfter = spaceIndex == spacesCount ? string.len : spacesIndices[spaceIndex];
        prb_String arg = {string.str + spaceBefore + 1, spaceAfter - spaceBefore - 1};
        if (arg.len > 0) {
            const char* argNull = prb_strGetNullTerminated(arg);
            args[argCount++] = argNull;
        }
    }

    return args;
}

prb_PUBLICDEF prb_ProcessHandle
prb_execCmd(prb_String cmd, prb_ProcessFlags flags, prb_String redirectFilepath) {
    prb_ProcessHandle result = {};
    prb_TempMemory    temp = prb_beginTempMemory();
    const char*       redirectFilepathNull = 0;

    if ((flags & prb_ProcessFlag_RedirectStdout) || (flags & prb_ProcessFlag_RedirectStderr)) {
        prb_assert(redirectFilepath.str && redirectFilepath.len > 0);
        redirectFilepathNull = prb_strGetNullTerminated(redirectFilepath);
    } else {
        prb_assert(redirectFilepath.str == 0);
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

    bool fileActionsSucceeded = true;
    posix_spawn_file_actions_t* fileActionsPtr = 0;
    posix_spawn_file_actions_t fileActions = {};
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
        const char** args = prb_getArgArrayFromString(cmd);
        int spawnResult = posix_spawnp(&result.pid, args[0], fileActionsPtr, 0, (char**)args, __environ);
        if (spawnResult == 0) {
            result.valid = true;
            if (!(flags & prb_ProcessFlag_DontWait)) {
                int status = 0;
                pid_t waitResult = waitpid(result.pid, &status, 0);
                if (waitResult == result.pid && status == 0) {
                    result.completed = true;
                    result.completionStatus = prb_Success;
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
        if (handle->valid) {
            if (!handle->completed) {
                int32_t status = 0;
                pid_t waitResult = waitpid(handle->pid, &status, 0);
                handle->completed = true;
                if (waitResult == handle->pid && status == 0) {
                    handle->completionStatus = prb_Success;
                } else {
                    handle->completionStatus = prb_Failure;
                    result = prb_Failure;
                }
            }
        } else {
            result = prb_Failure;
        }
    }

#else
#error unimplemented
#endif

    return result;
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
    STBDS_NOTUSED(a);
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

#endif  // prb_IMPLEMENTATION

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
