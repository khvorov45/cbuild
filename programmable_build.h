// TODO(khvorov) Make sure this compiles under C++
// TODO(khvorov) Make sure this works as two translation units
// TODO(khvorov) Make sure utf8 paths work on windows

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#if defined(WIN32) || defined(_WIN32)
    #define prb_PLATFORM_WINDOWS 1
#elif (defined(linux) || defined(__linux) || defined(__linux__))
    #define prb_PLATFORM_LINUX 1
#else
    #error unrecognized platform
#endif

#define prb_BYTE 1
#define prb_KILOBYTE 1024 * prb_BYTE
#define prb_MEGABYTE 1024 * prb_KILOBYTE
#define prb_GIGABYTE 1024 * prb_MEGABYTE

// clang-format off
#define prb_max(a, b) (((a) > (b)) ? (a) : (b))
#define prb_min(a, b) (((a) < (b)) ? (a) : (b))
#define prb_clamp(x, a, b) (((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x)))
#define prb_arrayLength(arr) (sizeof(arr) / sizeof(arr[0]))
#define prb_allocArray(type, len) (type*)prb_allocAndZero((len) * sizeof(type), _Alignof(type))
#define prb_allocStruct(type) (type*)prb_allocAndZero(sizeof(type), _Alignof(type))
#define prb_globalArenaAlignTo(type) (type*)prb_globalArenaAlignTo_(_Alignof(type))
#define prb_STR(str) (prb_String) { .ptr = (str), .len = (int32_t)prb_strlen(str) }
#define prb_isPowerOf2(x) (((x) > 0) && (((x) & ((x) - 1)) == 0))

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

#define prb_assert(condition) do { if (!(condition)) { prb_debugbreak(); } } while (0)
// clang-format on

typedef struct prb_Arena {
    void*   base;
    int32_t size;
    int32_t used;
} prb_Arena;

// Assume: null-terminated, permanently allocated, immutable, utf8
typedef struct prb_String {
    char*   ptr;
    int32_t len;
} prb_String;

typedef struct prb_StringArray {
    prb_String* ptr;
    int32_t     len;
} prb_StringArray;

typedef struct prb_StringBuilder {
    char*   ptr;
    int32_t capacity;
    int32_t written;
} prb_StringBuilder;

typedef struct prb_StringArrayBuilder {
    prb_String* ptr;
    int32_t     capacity;
    int32_t     written;
} prb_StringArrayBuilder;

typedef enum prb_CompletionStatus {
    prb_CompletionStatus_Success,
    prb_CompletionStatus_Failure,
} prb_CompletionStatus;

typedef struct prb_TimeStart {
    bool     valid;
    uint64_t time;
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
    prb_ColorID_Count,
} prb_ColorID;

#if prb_PLATFORM_WINDOWS

typedef struct prb_ProcessHandle {
    #error unimplemented
} prb_ProcessHandle;

#elif prb_PLATFORM_LINUX
    #include <unistd.h>

typedef struct prb_ProcessHandle {
    bool  valid;
    pid_t pid;
} prb_ProcessHandle;

#endif  // prb_PLATFORM

// SECTION Core
void prb_init(void);

// SECTION CRT
size_t prb_strlen(const char* string);
void*  prb_memcpy(void* restrict dest, const void* restrict src, size_t n);

// SECTION Memory
bool    prb_memeq(void* ptr1, void* ptr2, int32_t bytes);
void*   prb_vmemAllocate(int32_t size);
void    prb_alignPtr(void** ptr, int32_t align, int32_t* size);
void*   prb_allocAndZero(int32_t size, int32_t align);
void*   prb_globalArenaCurrentFreePtr(void);
int32_t prb_globalArenaCurrentFreeSize(void);
void*   prb_globalArenaAlignTo_(int32_t align);

// SECTION Filesystem
bool            prb_isDirectory(prb_String path);
bool            prb_isFile(prb_String path);
bool            prb_directoryIsEmpty(prb_String path);
void            prb_createDirIfNotExists(prb_String path);
void            prb_removeFileOrDirectoryIfExists(prb_String path);
void            prb_removeFileIfExists(prb_String path);
void            prb_removeDirectoryIfExists(prb_String path);
void            prb_clearDirectory(prb_String path);
prb_String      prb_pathJoin(prb_String path1, prb_String path2);
prb_String      prb_getCurrentWorkingDir(void);
int32_t         prb_getLastPathSepIndex(prb_String path);
prb_String      prb_getParentDir(prb_String path);
prb_String      prb_getLastEntryInPath(prb_String path);
prb_String      prb_replaceExt(prb_String path, prb_String newExt);
prb_StringArray prb_getAllMatches(prb_String pattern);
uint64_t        prb_getLatestLastModifiedFromPattern(prb_String pattern);
uint64_t        prb_getEarliestLastModifiedFromPattern(prb_String pattern);
uint64_t        prb_getLatestLastModifiedFromPatterns(prb_String* patterns, int32_t patternsCount);
uint64_t        prb_getEarliestLastModifiedFromPatterns(prb_String* patterns, int32_t patternsCount);
void            prb_textfileReplace(prb_String path, prb_String pattern, prb_String replacement);
prb_String      prb_readEntireFileAsString(prb_String path);
void            prb_writeEntireFileAsString(prb_String path, prb_String content);

// SECTION Strings
bool                   prb_streq(prb_String str1, prb_String str2);
bool                   prb_charIsSep(char ch);
int32_t                prb_strFindIndexFromLeft(prb_String str, char ch);
int32_t                prb_strFindIndexFromRight(prb_String str, char ch);
int32_t                prb_strFindIndexFromLeftString(prb_String str, prb_String pattern);
prb_String             prb_strReplace(prb_String str, prb_String pattern, prb_String replacement);
prb_StringBuilder      prb_createStringBuilder(int32_t len);
void                   prb_stringBuilderWrite(prb_StringBuilder* builder, char* source, int32_t len);
prb_String             prb_stringBuilderGetString(prb_StringBuilder* builder);
prb_StringArrayBuilder prb_createStringArrayBuilder(int32_t len);
void                   prb_stringArrayBuilderCopy(prb_StringArrayBuilder* builder, prb_StringArray arr);
prb_StringArray        prb_stringArrayFromCstrings(char** cstrings, int32_t cstringsCount);
prb_String             prb_stringCopy(prb_String source, int32_t from, int32_t to);
prb_String             prb_stringFromCstring(char* cstring);
char**                 prb_getArgArrayFromString(prb_String string);
prb_StringArray        prb_stringArrayJoin(prb_StringArray arr1, prb_StringArray arr2);
prb_String             prb_fmtCustomBuffer(void* buf, int32_t bufSize, char* fmt, ...);
prb_String             prb_vfmtCustomBuffer(void* buf, int32_t bufSize, char* fmt, va_list args);
prb_String             prb_fmt(char* fmt, ...);
prb_String             prb_vfmt(char* fmt, va_list args);
prb_String             prb_fmtAndPrint(char* fmt, ...);
prb_String             prb_fmtAndPrintColor(prb_ColorID color, char* fmt, ...);
prb_String             prb_vfmtAndPrint(char* fmt, va_list args);
prb_String             prb_vfmtAndPrintColor(prb_ColorID color, char* fmt, va_list args);
prb_String             prb_fmtAndPrintln(char* fmt, ...);
prb_String             prb_fmtAndPrintlnColor(prb_ColorID color, char* fmt, ...);
prb_String             prb_vfmtAndPrintln(char* fmt, va_list args);
prb_String             prb_vfmtAndPrintlnColor(prb_ColorID color, char* fmt, va_list args);
void                   prb_print(prb_String str);
void                   prb_printColor(prb_ColorID color, prb_String str);
void                   prb_println(prb_String str);
void                   prb_printlnColor(prb_ColorID color, prb_String str);
void                   prb_setPrintColor(prb_ColorID color);
void                   prb_resetPrintColor(void);

// SECTION Processes
prb_CompletionStatus prb_execCmdAndWait(prb_String cmd);
prb_CompletionStatus prb_execCmdAndWaitRedirectStdout(prb_String cmd, prb_String stdoutPath);
prb_ProcessHandle    prb_execCmdAndDontWait(prb_String cmd);
prb_CompletionStatus prb_waitForProcesses(prb_ProcessHandle* handles, int32_t handleCount);

// SECTION Timing
prb_TimeStart prb_timeStart(void);
float         prb_getMsFrom(prb_TimeStart timeStart);

// SECTION stb snprintf
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

// SECTION Global state
struct {
    prb_Arena     arena;
    prb_TimeStart initTimeStart;
    prb_String    terminalColorCodes[prb_ColorID_Count];
} prb_globalState;

//
// SECTION Implementation
//

bool
prb_memeq(void* ptr1, void* ptr2, int32_t bytes) {
    prb_assert(bytes >= 0);
    uint8_t *l = ptr1, *r = ptr2;
    int32_t  left = bytes;
    for (; left > 0 && *l == *r; left--, l++, r++) {}
    bool result = left == 0;
    return result;
}

void
prb_init(void) {
    prb_globalState.arena.size = 1 * prb_GIGABYTE;
    prb_globalState.arena.base = prb_vmemAllocate(prb_globalState.arena.size);
    prb_globalState.initTimeStart = prb_timeStart();

    prb_globalState.terminalColorCodes[prb_ColorID_Reset] = prb_STR("\x1b[0m");
    prb_globalState.terminalColorCodes[prb_ColorID_Black] = prb_STR("\x1b[30m");
    prb_globalState.terminalColorCodes[prb_ColorID_Red] = prb_STR("\x1b[31m");
    prb_globalState.terminalColorCodes[prb_ColorID_Green] = prb_STR("\x1b[32m");
    prb_globalState.terminalColorCodes[prb_ColorID_Yellow] = prb_STR("\x1b[33m");
    prb_globalState.terminalColorCodes[prb_ColorID_Blue] = prb_STR("\x1b[34m");
    prb_globalState.terminalColorCodes[prb_ColorID_Magenta] = prb_STR("\x1b[35m");
    prb_globalState.terminalColorCodes[prb_ColorID_Cyan] = prb_STR("\x1b[36m");
    prb_globalState.terminalColorCodes[prb_ColorID_White] = prb_STR("\x1b[37m");
}

size_t
prb_strlen(const char* string) {
    const char* ptr = string;
    for (; *ptr; ptr++) {}
    size_t len = ptr - string;
    return len;
}

void*
prb_memcpy(void* restrict dest, const void* restrict src, size_t n) {
    unsigned char*       d = dest;
    const unsigned char* s = src;
    for (; n; n--) {
        *d++ = *s++;
    }
    return dest;
}

void
prb_alignPtr(void** ptr, int32_t align, int32_t* size) {
    prb_assert(prb_isPowerOf2(align));
    void*   ptrOg = *ptr;
    int32_t offBy = ((size_t)ptrOg) & (align - 1);
    int32_t moveBy = 0;
    if (offBy > 0) {
        moveBy = align - offBy;
    }
    void* ptrAligned = (uint8_t*)ptrOg + moveBy;
    *ptr = ptrAligned;
    *size = *size + moveBy;
}

void*
prb_allocAndZero(int32_t size, int32_t align) {
    void* result = prb_globalArenaAlignTo_(align);
    prb_assert(prb_globalArenaCurrentFreeSize() >= size);
    prb_globalState.arena.used += size;
    return result;
}

void*
prb_globalArenaCurrentFreePtr(void) {
    void* result = (uint8_t*)prb_globalState.arena.base + prb_globalState.arena.used;
    return result;
}

int32_t
prb_globalArenaCurrentFreeSize(void) {
    int32_t result = prb_globalState.arena.size - prb_globalState.arena.used;
    return result;
}

void*
prb_globalArenaAlignTo_(int32_t align) {
    prb_alignPtr(&prb_globalState.arena.base, align, &prb_globalState.arena.used);
    prb_assert(prb_globalState.arena.used <= prb_globalState.arena.size);
    void* result = prb_globalArenaCurrentFreePtr();
    return result;
}

bool
prb_streq(prb_String str1, prb_String str2) {
    bool result = false;
    if (str1.len == str2.len) {
        result = prb_memeq(str1.ptr, str2.ptr, str1.len);
    }
    return result;
}

bool
prb_charIsSep(char ch) {
    bool result = ch == '/' || ch == '\\';
    return result;
}

int32_t
prb_strFindIndexFromLeft(prb_String str, char ch) {
    int32_t result = -1;
    for (int32_t strIndex = 0; strIndex < str.len; strIndex++) {
        char testCh = str.ptr[strIndex];
        if (testCh == ch) {
            result = strIndex;
            break;
        }
    }
    return result;
}

int32_t
prb_strFindIndexFromRight(prb_String str, char ch) {
    int32_t result = -1;
    for (int32_t strIndex = str.len - 1; strIndex >= 0; strIndex--) {
        char testCh = str.ptr[strIndex];
        if (testCh == ch) {
            result = strIndex;
            break;
        }
    }
    return result;
}

int32_t
prb_strFindIndexFromLeftString(prb_String str, prb_String pattern) {
    int32_t result = -1;
    if (pattern.len <= str.len) {
        if (pattern.len == 0) {
            result = 0;
        } else if (pattern.len == 1) {
            result = prb_strFindIndexFromLeft(str, pattern.ptr[0]);
        } else {
            // Raita string matching algorithm
            // https://en.wikipedia.org/wiki/Raita_algorithm
            ptrdiff_t bmBc[256];

            for (int32_t i = 0; i < 256; ++i) {
                bmBc[i] = pattern.len;
            }
            for (int32_t i = 0; i < pattern.len - 1; ++i) {
                char patternChar = pattern.ptr[i];
                bmBc[(int32_t)patternChar] = pattern.len - i - 1;
            }

            char patFirstCh = pattern.ptr[0];
            char patMiddleCh = pattern.ptr[pattern.len / 2];
            char patLastCh = pattern.ptr[pattern.len - 1];

            int32_t j = 0;
            while (j <= str.len - pattern.len) {
                char strLastCh = str.ptr[j + pattern.len - 1];
                if (patLastCh == strLastCh && patMiddleCh == str.ptr[j + pattern.len / 2] && patFirstCh == str.ptr[j]
                    && prb_memeq(pattern.ptr + 1, str.ptr + j + 1, pattern.len - 2)) {
                    result = j;
                    break;
                }
                j += bmBc[(int32_t)strLastCh];
            }
        }
    }
    return result;
}

prb_String
prb_strReplace(prb_String str, prb_String pattern, prb_String replacement) {
    int32_t    patternIndex = prb_strFindIndexFromLeftString(str, pattern);
    prb_String result = str;
    if (patternIndex != -1) {
        prb_StringBuilder builder = prb_createStringBuilder(str.len - pattern.len + replacement.len);
        prb_stringBuilderWrite(&builder, str.ptr, patternIndex);
        prb_stringBuilderWrite(&builder, replacement.ptr, replacement.len);
        prb_stringBuilderWrite(&builder, str.ptr + patternIndex + pattern.len, str.len - patternIndex - pattern.len);
        result = prb_stringBuilderGetString(&builder);
    }
    return result;
}

prb_StringBuilder
prb_createStringBuilder(int32_t len) {
    prb_assert(len >= 0);
    // NOTE(khvorov) +1 is for the null terminator
    prb_StringBuilder result = {.ptr = prb_allocAndZero(len + 1, 1), .capacity = len};
    return result;
}

void
prb_stringBuilderWrite(prb_StringBuilder* builder, char* source, int32_t len) {
    prb_assert(len <= builder->capacity - builder->written);
    prb_memcpy(builder->ptr + builder->written, source, len);
    builder->written += len;
}

prb_String
prb_stringBuilderGetString(prb_StringBuilder* builder) {
    prb_assert(builder->written == builder->capacity);
    prb_String result = {.ptr = builder->ptr, .len = builder->written};
    return result;
}

prb_String
prb_stringCopy(prb_String source, int32_t from, int32_t to) {
    prb_assert(to >= from && to >= 0 && from >= 0 && to < source.len && from < source.len);
    int32_t           len = to - from + 1;
    prb_StringBuilder builder = prb_createStringBuilder(len);
    prb_stringBuilderWrite(&builder, source.ptr + from, len);
    prb_String result = prb_stringBuilderGetString(&builder);
    return result;
}

prb_String
prb_stringFromCstring(char* cstring) {
    prb_String temp = prb_STR(cstring);
    prb_String result = prb_stringCopy(temp, 0, temp.len - 1);
    return result;
}

int32_t
prb_getLastPathSepIndex(prb_String path) {
    prb_assert(path.ptr && path.len > 0);
    int32_t lastPathSepIndex = -1;
    for (int32_t index = path.len - 1; index >= 0; index--) {
        char ch = path.ptr[index];
        if (ch == '/' || ch == '\\') {
            lastPathSepIndex = index;
            break;
        }
    }
    return lastPathSepIndex;
}

prb_String
prb_getParentDir(prb_String path) {
    int32_t    lastPathSepIndex = prb_getLastPathSepIndex(path);
    prb_String result = lastPathSepIndex >= 0 ? prb_stringCopy(path, 0, lastPathSepIndex) : prb_getCurrentWorkingDir();
    return result;
}

prb_String
prb_getLastEntryInPath(prb_String path) {
    int32_t    lastPathSepIndex = prb_getLastPathSepIndex(path);
    prb_String result = lastPathSepIndex >= 0 ? prb_stringCopy(path, lastPathSepIndex + 1, path.len - 1) : path;
    return result;
}

char**
prb_getArgArrayFromString(prb_String string) {
    int32_t spacesCount = 0;
    for (int32_t strIndex = 0; strIndex < string.len; strIndex++) {
        char ch = string.ptr[strIndex];
        if (ch == ' ') {
            spacesCount++;
        }
    }

    int32_t* spacesIndices = prb_allocArray(int32_t, spacesCount);
    {
        int32_t index = 0;
        for (int32_t strIndex = 0; strIndex < string.len; strIndex++) {
            char ch = string.ptr[strIndex];
            if (ch == ' ') {
                spacesIndices[index++] = strIndex;
            }
        }
    }

    int32_t argCount = 0;
    // NOTE(khvorov) Arg array needs a null at the end
    char** args = prb_allocArray(char*, spacesCount + 1 + 1);
    for (int32_t spaceIndex = 0; spaceIndex <= spacesCount; spaceIndex++) {
        int32_t spaceBefore = spaceIndex == 0 ? -1 : spacesIndices[spaceIndex - 1];
        int32_t spaceAfter = spaceIndex == spacesCount ? string.len : spacesIndices[spaceIndex];
        char*   argStart = string.ptr + spaceBefore + 1;
        int32_t argLen = spaceAfter - spaceBefore - 1;
        if (argLen > 0) {
            prb_String argString = prb_stringCopy((prb_String) {argStart, argLen}, 0, argLen - 1);
            args[argCount++] = argString.ptr;
        }
    }

    return args;
}

prb_String
prb_replaceExt(prb_String path, prb_String newExt) {
    int32_t    dotIndex = prb_strFindIndexFromRight(path, '.');
    prb_String result = prb_fmt("%.*s.%s", dotIndex, path.ptr, newExt.ptr);
    return result;
}

void
prb_removeFileOrDirectoryIfExists(prb_String path) {
    if (prb_isDirectory(path)) {
        prb_removeDirectoryIfExists(path);
    } else {
        prb_removeFileIfExists(path);
    }
}

void
prb_clearDirectory(prb_String path) {
    prb_removeFileOrDirectoryIfExists(path);
    prb_createDirIfNotExists(path);
}

prb_String
prb_stringsJoin(prb_String* strings, int32_t stringsCount, prb_String sep) {
    prb_assert(sep.len >= 0 && stringsCount >= 0);

    int32_t totalLen = prb_max(stringsCount - 1, 0) * sep.len;
    for (int32_t strIndex = 0; strIndex < stringsCount; strIndex++) {
        prb_String str = strings[strIndex];
        totalLen += str.len;
    }

    prb_StringBuilder builder = prb_createStringBuilder(totalLen);
    for (int32_t strIndex = 0; strIndex < stringsCount; strIndex++) {
        prb_String str = strings[strIndex];
        prb_stringBuilderWrite(&builder, str.ptr, str.len);
        if (strIndex < stringsCount - 1) {
            prb_stringBuilderWrite(&builder, sep.ptr, sep.len);
        }
    }
    prb_String result = prb_stringBuilderGetString(&builder);

    return result;
}

prb_String
prb_pathJoin(prb_String path1, prb_String path2) {
    prb_assert(path1.ptr && path1.len > 0 && path2.ptr && path2.len > 0);
    char              path1LastChar = path1.ptr[path1.len - 1];
    bool              path1EndsOnSep = prb_charIsSep(path1LastChar);
    int32_t           totalLen = path1EndsOnSep ? path1.len + path2.len : path1.len + 1 + path2.len;
    prb_StringBuilder builder = prb_createStringBuilder(totalLen);
    prb_stringBuilderWrite(&builder, path1.ptr, path1.len);
    if (!path1EndsOnSep) {
        // NOTE(khvorov) Windows seems to handle mixing \ and / just fine
        prb_stringBuilderWrite(&builder, "/", 1);
    }
    prb_stringBuilderWrite(&builder, path2.ptr, path2.len);
    prb_String result = prb_stringBuilderGetString(&builder);
    return result;
}

prb_StringArrayBuilder
prb_createStringArrayBuilder(int32_t len) {
    prb_StringArrayBuilder builder = {.ptr = prb_allocArray(prb_String, len), .capacity = len};
    return builder;
}

void
prb_stringArrayBuilderCopy(prb_StringArrayBuilder* builder, prb_StringArray arr) {
    prb_assert(builder->capacity >= arr.len + builder->written);
    prb_memcpy(builder->ptr + builder->written, arr.ptr, arr.len * sizeof(prb_String));
    builder->written += arr.len;
}

prb_StringArray
prb_stringArrayFromCstrings(char** cstrings, int32_t cstringsCount) {
    prb_String* resultBuf = prb_allocArray(prb_String, cstringsCount);
    for (int32_t cstringIndex = 0; cstringIndex < cstringsCount; cstringIndex++) {
        resultBuf[cstringIndex] = prb_STR(cstrings[cstringIndex]);
    }
    prb_StringArray result = {resultBuf, cstringsCount};
    return result;
}

prb_StringArray
prb_stringArrayBuilderGetArray(prb_StringArrayBuilder* builder) {
    prb_assert(builder->capacity == builder->written);
    prb_StringArray result = {builder->ptr, builder->written};
    return result;
}

prb_StringArray
prb_stringArrayJoin(prb_StringArray arr1, prb_StringArray arr2) {
    prb_StringArrayBuilder builder = prb_createStringArrayBuilder(arr1.len + arr2.len);
    prb_stringArrayBuilderCopy(&builder, arr1);
    prb_stringArrayBuilderCopy(&builder, arr2);
    prb_StringArray result = prb_stringArrayBuilderGetArray(&builder);
    return result;
}

uint64_t
prb_getLatestLastModifiedFromPatterns(prb_String* patterns, int32_t patternsCount) {
    uint64_t result = 0;
    for (int32_t patternIndex = 0; patternIndex < patternsCount; patternIndex++) {
        result = prb_max(result, prb_getLatestLastModifiedFromPattern(patterns[patternIndex]));
    }
    return result;
}

uint64_t
prb_getEarliestLastModifiedFromPatterns(prb_String* patterns, int32_t patternsCount) {
    uint64_t result = UINT64_MAX;
    for (int32_t patternIndex = 0; patternIndex < patternsCount; patternIndex++) {
        result = prb_min(result, prb_getEarliestLastModifiedFromPattern(patterns[patternIndex]));
    }
    return result;
}

void
prb_textfileReplace(prb_String path, prb_String pattern, prb_String replacement) {
    prb_String content = prb_readEntireFileAsString(path);
    prb_String newContent = prb_strReplace(content, pattern, replacement);
    prb_writeEntireFileAsString(path, newContent);
}

prb_String
prb_fmtCustomBuffer(void* buf, int32_t bufSize, char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    prb_String result = prb_vfmtCustomBuffer(buf, bufSize, fmt, args);
    va_end(args);
    return result;
}

prb_String
prb_vfmtCustomBuffer(void* buf, int32_t bufSize, char* fmt, va_list args) {
    int32_t    strSize = stbsp_vsnprintf(buf, bufSize, fmt, args);
    prb_String result = {buf, strSize};
    return result;
}

prb_String
prb_fmt(char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    prb_String result = prb_vfmt(fmt, args);
    va_end(args);
    return result;
}

prb_String
prb_vfmt(char* fmt, va_list args) {
    prb_String result = prb_vfmtCustomBuffer(
        prb_globalState.arena.base + prb_globalState.arena.used,
        prb_globalState.arena.size - prb_globalState.arena.used,
        fmt,
        args
    );
    prb_globalState.arena.used += result.len + 1;  // NOTE(khvorov) Null terminator
    return result;
}

prb_String
prb_fmtAndPrint(char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    prb_String result = prb_vfmtAndPrint(fmt, args);
    va_end(args);
    return result;
}

prb_String
prb_fmtAndPrintColor(prb_ColorID color, char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    prb_String result = prb_vfmtAndPrintColor(color, fmt, args);
    va_end(args);
    return result;
}

prb_String
prb_vfmtAndPrint(char* fmt, va_list args) {
    prb_String result = prb_vfmt(fmt, args);
    prb_print(result);
    return result;
}

prb_String
prb_vfmtAndPrintColor(prb_ColorID color, char* fmt, va_list args) {
    prb_setPrintColor(color);
    prb_String result = prb_vfmtAndPrint(fmt, args);
    prb_resetPrintColor();
    return result;
}

prb_String
prb_fmtAndPrintln(char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    prb_String result = prb_vfmtAndPrintln(fmt, args);
    va_end(args);
    return result;
}

prb_String
prb_fmtAndPrintlnColor(prb_ColorID color, char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    prb_String result = prb_vfmtAndPrintlnColor(color, fmt, args);
    va_end(args);
    return result;
}

prb_String
prb_vfmtAndPrintln(char* fmt, va_list args) {
    prb_String result = prb_vfmtAndPrint(fmt, args);
    prb_fmtAndPrint("\n");
    return result;
}

prb_String
prb_vfmtAndPrintlnColor(prb_ColorID color, char* fmt, va_list args) {
    prb_setPrintColor(color);
    prb_String result = prb_vfmtAndPrintln(fmt, args);
    prb_resetPrintColor();
    return result;
}

void
prb_printColor(prb_ColorID color, prb_String str) {
    prb_setPrintColor(color);
    prb_print(str);
    prb_resetPrintColor();
}

void
prb_println(prb_String msg) {
    prb_print(msg);
    prb_print(prb_STR("\n"));
}

void
prb_printlnColor(prb_ColorID color, prb_String str) {
    prb_setPrintColor(color);
    prb_println(str);
    prb_resetPrintColor();
}

void
prb_setPrintColor(prb_ColorID color) {
    prb_print(prb_globalState.terminalColorCodes[color]);
}

void
prb_resetPrintColor() {
    prb_print(prb_globalState.terminalColorCodes[prb_ColorID_Reset]);
}

//
// SECTION Implementation (platform-specific)
//

#if prb_PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <shellapi.h>

    #pragma comment(lib, "Shell32.lib")

void*
prb_vmemAllocate(int32_t size) {
    void* ptr = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return ptr;
}

prb_CompletionStatus
prb_execCmd(prb_String cmd) {
    prb_CompletionStatus cmdStatus = prb_CompletionStatus_Failure;

    STARTUPINFOA        startupInfo = {.cb = sizeof(STARTUPINFOA)};
    PROCESS_INFORMATION processInfo;
    if (CreateProcessA(0, cmd.ptr, 0, 0, 0, 0, 0, 0, &startupInfo, &processInfo)) {
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        DWORD exitCode;
        if (GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
            if (exitCode == 0) {
                cmdStatus = prb_CompletionStatus_Success;
            }
        }
    }

    return cmdStatus;
}

void
prb_print(prb_String msg) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(out, msg.ptr, msg.len, 0, 0);
}

bool
prb_isDirectory(prb_String path) {
    prb_assert(path.ptr && path.len > 0);
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
    return result;
}

bool
prb_directoryIsEmpty(prb_String path) {
    prb_assert(prb_isDirectory(path));
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
    return result;
}

void
prb_createDirIfNotExists(prb_String path) {
    CreateDirectory(path.ptr, 0);
}

void
prb_removeFileIfExists(prb_String path) {
    prb_assert(!"unimplemented");
}

void
prb_removeFileOrDirectoryIfExists(prb_String path) {
    prb_StringBuilder doubleNullBuilder = prb_createStringBuilder(path.len + 2);
    prb_stringBuilderWrite(&doubleNullBuilder, path);
    SHFileOperationA(&(SHFILEOPSTRUCTA) {
        .wFunc = FO_DELETE,
        .pFrom = doubleNullBuilder.string.ptr,
        .fFlags = FOF_NO_UI,
    });
}

prb_String
prb_getCurrentWorkingDir(void) {
    prb_StringBuilder builder = prb_createStringBuilder(MAX_PATH);
    GetCurrentDirectoryA(builder.string.len, builder.string.ptr);
    return builder.string;
}

uint64_t
prb_getLatestLastModifiedFromPattern(prb_String pattern) {
    uint64_t result = 0;

    WIN32_FIND_DATAA findData;
    HANDLE           firstHandle = FindFirstFileA(pattern.ptr, &findData);
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

    return result;
}

uint64_t
prb_getEarliestLastModifiedFromPattern(prb_String pattern) {
    uint64_t result = UINT64_MAX;

    WIN32_FIND_DATAA findData;
    HANDLE           firstHandle = FindFirstFileA(pattern.ptr, &findData);
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

    return result;
}

#elif prb_PLATFORM_LINUX
    #include <stdatomic.h>
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

void*
prb_vmemAllocate(int32_t size) {
    void* ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    prb_assert(ptr != MAP_FAILED);
    return ptr;
}

bool
prb_isDirectory(prb_String path) {
    prb_assert(path.ptr && path.len > 0);
    struct stat statBuf = {0};
    bool        result = false;
    if (stat(path.ptr, &statBuf) == 0) {
        result = S_ISDIR(statBuf.st_mode);
    }
    return result;
}

bool
prb_isFile(prb_String path) {
    prb_assert(path.ptr && path.len > 0);
    struct stat statBuf = {0};
    bool        result = stat(path.ptr, &statBuf) == 0;
    return result;
}

void
prb_createDirIfNotExists(prb_String path) {
    if (mkdir(path.ptr, S_IRWXU | S_IRWXG | S_IRWXO) == -1) {
        prb_assert(prb_isDirectory(path));
    }
}

bool
prb_directoryIsEmpty(prb_String path) {
    bool result = true;
    DIR* pathHandle = opendir(path.ptr);
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
    return result;
}

void
prb_removeDirectoryIfExists(prb_String path) {
    DIR* pathHandle = opendir(path.ptr);
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
        int32_t rmdirResult = rmdir(path.ptr);
        prb_assert(rmdirResult == 0);
    }
}

void
prb_removeFileIfExists(prb_String path) {
    prb_assert(!prb_isDirectory(path));
    unlink(path.ptr);
}

prb_CompletionStatus
prb_execCmdAndWait(prb_String cmd) {
    char**               args = prb_getArgArrayFromString(cmd);
    prb_CompletionStatus cmdStatus = prb_CompletionStatus_Failure;
    pid_t                pid;
    int32_t              spawnResult = posix_spawnp(&pid, args[0], 0, 0, args, __environ);
    if (spawnResult == 0) {
        int32_t status;
        pid_t   waitResult = waitpid(pid, &status, 0);
        if (waitResult == pid && status == 0) {
            cmdStatus = prb_CompletionStatus_Success;
        }
    }
    return cmdStatus;
}

prb_CompletionStatus
prb_execCmdAndWaitRedirectStdout(prb_String cmd, prb_String stdoutPath) {
    char**                     args = prb_getArgArrayFromString(cmd);
    prb_CompletionStatus       cmdStatus = prb_CompletionStatus_Failure;
    posix_spawn_file_actions_t fileActions = {0};
    if (posix_spawn_file_actions_init(&fileActions) == 0) {
        if (posix_spawn_file_actions_addopen(
                &fileActions,
                STDOUT_FILENO,
                stdoutPath.ptr,
                O_WRONLY | O_CREAT | O_TRUNC,
                S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR
            )
            == 0) {
            if (posix_spawn_file_actions_adddup2(&fileActions, STDOUT_FILENO, STDERR_FILENO) == 0) {
                pid_t   pid;
                int32_t spawnResult = posix_spawnp(&pid, args[0], &fileActions, 0, args, __environ);
                if (spawnResult == 0) {
                    int32_t status;
                    pid_t   waitResult = waitpid(pid, &status, 0);
                    if (waitResult == pid && status == 0) {
                        cmdStatus = prb_CompletionStatus_Success;
                    }
                }
            }
        }
    }
    return cmdStatus;
}

prb_ProcessHandle
prb_execCmdAndDontWait(prb_String cmd) {
    char**            args = prb_getArgArrayFromString(cmd);
    prb_ProcessHandle result = {0};
    int32_t           spawnResult = posix_spawnp(&result.pid, args[0], 0, 0, args, __environ);
    if (spawnResult == 0) {
        result.valid = true;
    }
    return result;
}

prb_CompletionStatus
prb_waitForProcesses(prb_ProcessHandle* handles, int32_t handleCount) {
    prb_CompletionStatus result = prb_CompletionStatus_Success;
    for (int32_t handleIndex = 0; handleIndex < handleCount; handleIndex++) {
        prb_ProcessHandle handle = handles[handleIndex];
        if (handle.valid) {
            int32_t status;
            pid_t   waitResult = waitpid(handle.pid, &status, 0);
            if (waitResult != handle.pid || status != 0) {
                result = prb_CompletionStatus_Failure;
                break;
            }
        } else {
            result = prb_CompletionStatus_Failure;
            break;
        }
    }
    return result;
}

void
prb_print(prb_String msg) {
    ssize_t writeResult = write(STDOUT_FILENO, msg.ptr, msg.len);
    prb_assert(writeResult == msg.len);
}

int32_t
prb_atomicIncrement(int32_t volatile* addend) {
    int32_t result = atomic_fetch_add(addend, 1);
    return result;
}

bool
prb_atomicCompareExchange(int32_t volatile* dest, int32_t exchange, int32_t compare) {
    int32_t compareCopy = compare;
    bool    result = atomic_compare_exchange_strong((_Atomic int32_t*)dest, &compareCopy, exchange);
    return result;
}

void
prb_sleepMs(int32_t ms) {
    usleep(ms * 1000);
}

prb_String
prb_getCurrentWorkingDir(void) {
    int32_t maxLen = PATH_MAX + 1;
    char*   ptr = prb_allocAndZero(maxLen, 1);
    prb_assert(getcwd(ptr, maxLen));
    int32_t    len = prb_strlen(ptr);
    prb_String result = {ptr, len};
    return result;
}

uint64_t
prb_getLatestLastModifiedFromPattern(prb_String pattern) {
    uint64_t result = 0;
    glob_t   globResult = {0};
    if (glob(pattern.ptr, GLOB_NOSORT, 0, &globResult) == 0) {
        prb_assert(globResult.gl_pathc <= INT32_MAX);
        for (int32_t resultIndex = 0; resultIndex < (int32_t)globResult.gl_pathc; resultIndex++) {
            char*       path = globResult.gl_pathv[resultIndex];
            struct stat statBuf = {0};
            if (stat(path, &statBuf) == 0) {
                uint64_t thisLastMod = statBuf.st_mtim.tv_sec;
                result = prb_max(result, thisLastMod);
            }
        }
    }
    globfree(&globResult);
    return result;
}

uint64_t
prb_getEarliestLastModifiedFromPattern(prb_String pattern) {
    uint64_t result = UINT64_MAX;
    glob_t   globResult = {0};
    if (glob(pattern.ptr, GLOB_NOSORT, 0, &globResult) == 0) {
        prb_assert(globResult.gl_pathc <= INT32_MAX);
        for (int32_t resultIndex = 0; resultIndex < (int32_t)globResult.gl_pathc; resultIndex++) {
            char*       path = globResult.gl_pathv[resultIndex];
            struct stat statBuf = {0};
            if (stat(path, &statBuf) == 0) {
                uint64_t thisLastMod = statBuf.st_mtim.tv_sec;
                result = prb_min(result, thisLastMod);
            }
        }
    } else {
        result = 0;
    }
    globfree(&globResult);
    return result;
}

prb_StringArray
prb_getAllMatches(prb_String pattern) {
    prb_StringArray result = {0};
    glob_t          globResult = {0};
    if (glob(pattern.ptr, GLOB_NOSORT, 0, &globResult) == 0) {
        prb_assert(globResult.gl_pathc <= INT32_MAX && globResult.gl_pathc > 0);
        result.len = (int32_t)globResult.gl_pathc;
        result.ptr = prb_allocArray(prb_String, result.len);
        for (int32_t resultIndex = 0; resultIndex < (int32_t)globResult.gl_pathc; resultIndex++) {
            char* path = globResult.gl_pathv[resultIndex];
            result.ptr[resultIndex] = prb_stringFromCstring(path);
        }
    }
    globfree(&globResult);
    return result;
}

prb_TimeStart
prb_timeStart(void) {
    prb_TimeStart   result = {0};
    struct timespec tp;
    if (clock_gettime(CLOCK_MONOTONIC, &tp) == 0) {
        prb_assert(tp.tv_nsec >= 0 && tp.tv_sec >= 0);
        result.time = (uint64_t)tp.tv_nsec + (uint64_t)tp.tv_sec * 1000 * 1000 * 1000;
        result.valid = true;
    }
    return result;
}

float
prb_getMsFrom(prb_TimeStart timeStart) {
    prb_TimeStart now = prb_timeStart();
    float         result = 0.0f;
    if (now.valid && timeStart.valid) {
        uint64_t nsec = now.time - timeStart.time;
        result = (float)nsec / 1000.0f / 1000.0f;
    }
    return result;
}

prb_String
prb_readEntireFileAsString(prb_String path) {
    int32_t handle = open(path.ptr, O_RDONLY, 0);
    prb_assert(handle != -1);
    struct stat statBuf = {0};
    prb_assert(fstat(handle, &statBuf) == 0);
    char*   buf = prb_allocAndZero(statBuf.st_size, 1);
    int32_t readResult = read(handle, buf, statBuf.st_size);
    prb_assert(readResult == statBuf.st_size);
    prb_String result = {buf, statBuf.st_size};
    close(handle);
    return result;
}

void
prb_writeEntireFileAsString(prb_String path, prb_String content) {
    int32_t handle = open(path.ptr, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR);
    prb_assert(handle != -1);
    int32_t writeResult = write(handle, content.ptr, content.len);
    prb_assert(writeResult == content.len);
    close(handle);
}

#endif  // prb_PLATFORM

//
// SECTION Implementation (stb snprintf)
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
                default:
                    goto flags_done;
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
            default:
                break;
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

        STB_SPRINTF_DECORATE(vsprintfcb)(stbsp__count_clamp_callback, &c, c.tmp, fmt, va);
    } else {
        int l;

        c.buf = buf;
        c.count = count;
        c.length = 0;

        STB_SPRINTF_DECORATE(vsprintfcb)(stbsp__clamp_callback, &c, stbsp__clamp_callback(0, &c, 0), fmt, va);

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
    8388608,
    6.8601809640529717e+028,
    -7.253143638152921e+052,
    -4.3377296974619174e+075,
    -1.5559416129466825e+098,
    -3.2841562489204913e+121,
    -3.7745893248228135e+144,
    -1.7356668416969134e+167,
    -3.8893577551088374e+190,
    -9.9566444326005119e+213,
    6.3641293062232429e+236,
    -5.2069140800249813e+259,
    -5.2504760255204387e+282};
static double const stbsp__negtoperr[13] = {
    3.9565301985100693e-040,
    -2.299904345391321e-063,
    3.6506201437945798e-086,
    1.1875228833981544e-109,
    -5.0644902316928607e-132,
    -6.7156837247865426e-155,
    -2.812077463003139e-178,
    -5.7778912386589953e-201,
    7.4997100559334532e-224,
    -4.6439668915134491e-247,
    -6.3691100762962136e-270,
    -9.436808465446358e-293,
    8.0970921678014997e-317};

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
