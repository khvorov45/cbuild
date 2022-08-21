#include <stdint.h>
#include <stdbool.h>

#define prb_PLATFORM_WINDOWS 1
#define prb_PLATFORM_LINUX 2

#ifndef prb_PLATFORM
    #if defined(WIN32) || defined(_WIN32)
        #define prb_PLATFORM prb_PLATFORM_WINDOWS
    #elif (defined(linux) || defined(__linux) || defined(__linux__))
        #define prb_PLATFORM prb_PLATFORM_LINUX
    #else
        #error unrecognized platform, see prb_PLATFORM
    #endif
#endif

#ifndef prb_MAX_STEPS
    #define prb_MAX_STEPS 32
#endif

#ifndef prb_MAX_DEPENDENCIES_PER_STEP
    #define prb_MAX_DEPENDENCIES_PER_STEP 4
#endif

#ifndef prb_strlen
    #include <string.h>
    #define prb_strlen strlen
#endif

#ifndef prb_debugbreak
// Taken from SDL
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
    #elif ( \
        (!defined(__NACL__)) \
        && ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))) \
    )
        #define prb_debugbreak() __asm__ __volatile__("int $3\n\t")
    #elif ( \
        defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__)) \
    ) /* this might work on other ARM targets, but this is a known quantity... */
        #define prb_debugbreak() __asm__ __volatile__("brk #22\n\t")
    #elif defined(__APPLE__) && defined(__arm__)
        #define prb_debugbreak() __asm__ __volatile__("bkpt #22\n\t")
    #elif defined(__386__) && defined(__WATCOMC__)
        #define prb_debugbreak() \
            { \
                _asm { int 0x03 } \
            }
    #elif defined(prb_HAVE_SIGNAL_H) && !defined(__WATCOMC__)
        #include <signal.h>
        #define prb_debugbreak() raise(SIGTRAP)
    #else
        /* How do we trigger breakpoints on this platform? */
        #define prb_debugbreak()
    #endif

#endif

#ifndef prb_assert
    #define prb_assert(condition) \
        do { \
            if (!(condition)) { \
                prb_debugbreak(); \
            } \
        } while (0)
#endif

#ifndef prb_memcpy
    #include <stdlib.h>
    #define prb_memcpy memcpy
#endif

#ifndef prb_allocAndZero
    #include <stdlib.h>
    #define prb_allocAndZero(bytes) calloc(bytes, 1)
#endif

#define prb_max(a, b) (((a) > (b)) ? (a) : (b))
#define prb_min(a, b) (((a) < (b)) ? (a) : (b))
#define prb_arrayLength(arr) (sizeof(arr) / sizeof(arr[0]))
#define prb_STR(str) \
    (prb_String) { \
        .ptr = (str), .len = (int32_t)prb_strlen(str) \
    }

typedef struct prb_String {
    char* ptr;
    int32_t len;
} prb_String;

typedef struct prb_StringBuilder {
    prb_String string;
    int32_t written;
} prb_StringBuilder;

typedef struct prb_StepHandle {
    int32_t index;
} prb_StepHandle;

typedef enum prb_StepDataKind {
    prb_StepDataKind_GitClone,
    prb_StepDataKind_Compile,
    prb_StepDataKind_Custom,
} prb_StepDataKind;

typedef enum prb_CompletionStatus {
    prb_CompletionStatus_Success,
    prb_CompletionStatus_Failure,
} prb_CompletionStatus;

typedef prb_CompletionStatus (*prb_StepProc)(void* data);

typedef struct prb_Step {
    prb_StepProc proc;
    void* data;
} prb_Step;

typedef enum prb_StepStatus {
    prb_StepStatus_NotStarted,
    prb_StepStatus_NotStartedBecauseDepsFailed,
    prb_StepStatus_InProgress,
    prb_StepStatus_CompletedSuccessfully,
    prb_StepStatus_CompletedUnsuccessfully,
} prb_StepStatus;

// SECTION Core
prb_StepHandle prb_addStep(prb_StepProc proc, void* data);
void prb_setDependency(prb_StepHandle dependent, prb_StepHandle dependency);
void prb_run(void);

// SECTION Helpers
bool prb_directoryExists(prb_String path);
bool prb_directoryIsEmpty(prb_String path);
void prb_createDirIfNotExists(prb_String path);
bool prb_charIsSep(char ch);
prb_StringBuilder prb_createStringBuilder(int32_t len);
void prb_stringBuilderWrite(prb_StringBuilder* builder, prb_String source);
prb_String prb_stringCopy(prb_String source, int32_t from, int32_t to);
prb_String prb_getCurrentWorkingDir(void);
prb_String prb_getParentDir(prb_String path);
prb_String prb_getLastEntryInPath(prb_String path);
uint64_t prb_getLastModifiedFromPattern(prb_String pattern);
uint64_t prb_getLastModifiedFromPatterns(prb_String* patterns, int32_t patternsCount);
prb_String prb_stringsJoin(prb_String* strings, int32_t stringsCount, prb_String sep);
prb_String prb_stringJoin2(prb_String str1, prb_String str2);
prb_String prb_stringJoin3(prb_String str1, prb_String str2, prb_String str3);
prb_String prb_stringJoin4(prb_String str1, prb_String str2, prb_String str3, prb_String str4);
prb_String prb_pathJoin2(prb_String path1, prb_String path2);
prb_String prb_pathJoin3(prb_String path1, prb_String path2, prb_String path3);
prb_CompletionStatus prb_execCmd(prb_String cmd);
void prb_logMessage(prb_String msg);
void prb_logMessageLn(prb_String msg);
int32_t prb_atomicIncrement(int32_t volatile* addend);
bool prb_atomicCompareExchange(int32_t volatile* dest, int32_t exchange, int32_t compare);
void prb_sleepMs(int32_t ms);

#ifdef PROGRAMMABLE_BUILD_IMPLEMENTATION

struct {
    prb_String rootPath;
    prb_Step steps[prb_MAX_STEPS];
    prb_StepStatus stepStatus[prb_MAX_STEPS];
    int32_t stepCount;
    int32_t stepsCompleted;
    prb_StepHandle dependencies[prb_MAX_STEPS][prb_MAX_DEPENDENCIES_PER_STEP];
    int32_t dependenciesCounts[prb_MAX_STEPS];
} prb_globalBuilder;

//
// SECTION Core
//

prb_StepHandle
prb_addStep(prb_StepProc proc, void* data) {
    prb_StepHandle handle = {prb_globalBuilder.stepCount++};
    prb_globalBuilder.steps[handle.index] = (prb_Step) {proc, data};
    return handle;
}

void
prb_setDependency(prb_StepHandle dependent, prb_StepHandle dependency) {
    int32_t depIndex = prb_globalBuilder.dependenciesCounts[dependent.index]++;
    prb_assert(depIndex < prb_MAX_DEPENDENCIES_PER_STEP);
    prb_globalBuilder.dependencies[dependent.index][depIndex] = dependency;
}

void
prb_completeAllSteps(void) {
    while (prb_globalBuilder.stepsCompleted != prb_globalBuilder.stepCount) {
        int32_t stepsCompletedBeforeLoop = prb_globalBuilder.stepsCompleted;

        for (int32_t stepIndex = 0; stepIndex < prb_globalBuilder.stepCount; stepIndex++) {
            if (prb_globalBuilder.stepStatus[stepIndex] == prb_StepStatus_NotStarted) {
                bool anyDepsFailed = false;
                bool allDepsDone = true;
                prb_StepHandle* dependencies = prb_globalBuilder.dependencies[stepIndex];
                int32_t depCount = prb_globalBuilder.dependenciesCounts[stepIndex];
                for (int32_t depIndexIndex = 0; depIndexIndex < depCount; depIndexIndex++) {
                    prb_StepHandle depHandle = dependencies[depIndexIndex];
                    prb_StepStatus depStatus = prb_globalBuilder.stepStatus[depHandle.index];
                    if (depStatus != prb_StepStatus_CompletedSuccessfully) {
                        allDepsDone = false;
                    }
                    if (depStatus == prb_StepStatus_CompletedUnsuccessfully
                        || depStatus == prb_StepStatus_NotStartedBecauseDepsFailed) {
                        anyDepsFailed = true;
                    }
                }

                if (anyDepsFailed
                    && prb_atomicCompareExchange(
                        (int32_t volatile*)(prb_globalBuilder.stepStatus + stepIndex),
                        prb_StepStatus_NotStartedBecauseDepsFailed,
                        prb_StepStatus_NotStarted
                    )) {
                    prb_atomicIncrement(&prb_globalBuilder.stepsCompleted);
                } else if (allDepsDone && prb_atomicCompareExchange((int32_t volatile*)(prb_globalBuilder.stepStatus + stepIndex), prb_StepStatus_InProgress, prb_StepStatus_NotStarted)) {
                    prb_Step* step = prb_globalBuilder.steps + stepIndex;
                    prb_CompletionStatus procStatus = step->proc(step->data);

                    // NOTE(khvorov) Only one thread can be here per step, so these status writes are not atomic
                    switch (procStatus) {
                        case prb_CompletionStatus_Success: {
                            prb_globalBuilder.stepStatus[stepIndex] = prb_StepStatus_CompletedSuccessfully;
                        } break;
                        case prb_CompletionStatus_Failure: {
                            prb_globalBuilder.stepStatus[stepIndex] = prb_StepStatus_CompletedUnsuccessfully;
                        } break;
                    }

                    prb_atomicIncrement(&prb_globalBuilder.stepsCompleted);
                }
            }
        }

        while (stepsCompletedBeforeLoop == prb_globalBuilder.stepsCompleted
               && prb_globalBuilder.stepsCompleted != prb_globalBuilder.stepCount) {
            prb_sleepMs(100);
        }
    }
}

void
prb_run(void) {
    prb_completeAllSteps();
}

//
// SECTION Helpers
//

bool
prb_charIsSep(char ch) {
    bool result = ch == '/' || ch == '\\';
    return result;
}

prb_StringBuilder
prb_createStringBuilder(int32_t len) {
    prb_assert(len > 0);
    prb_StringBuilder result = {.string = {prb_allocAndZero(len + 1), len}};
    return result;
}

void
prb_stringBuilderWrite(prb_StringBuilder* builder, prb_String source) {
    prb_assert(source.len <= builder->string.len - builder->written);
    prb_memcpy(builder->string.ptr + builder->written, source.ptr, source.len);
    builder->written += source.len;
}

prb_String
prb_stringCopy(prb_String source, int32_t from, int32_t to) {
    prb_assert(to >= from && to >= 0 && from >= 0 && to < source.len && from < source.len);
    int32_t len = to - from + 1;
    prb_StringBuilder builder = prb_createStringBuilder(len);
    prb_stringBuilderWrite(&builder, (prb_String) {source.ptr + from, len});
    return builder.string;
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
    int32_t lastPathSepIndex = prb_getLastPathSepIndex(path);
    prb_String result = lastPathSepIndex >= 0 ? prb_stringCopy(path, 0, lastPathSepIndex) : prb_getCurrentWorkingDir();
    return result;
}

prb_String
prb_getLastEntryInPath(prb_String path) {
    int32_t lastPathSepIndex = prb_getLastPathSepIndex(path);
    prb_String result = lastPathSepIndex >= 0 ? prb_stringCopy(path, lastPathSepIndex + 1, path.len - 1) : path;
    return result;
}

prb_String
prb_stringsJoin(prb_String* strings, int32_t stringsCount, prb_String sep) {
    prb_assert(sep.ptr && sep.len > 0 && stringsCount >= 0);

    int32_t totalLen = stringsCount * sep.len;
    for (int32_t strIndex = 0; strIndex < stringsCount; strIndex++) {
        prb_String str = strings[strIndex];
        totalLen += str.len;
    }

    prb_StringBuilder builder = prb_createStringBuilder(totalLen);
    for (int32_t strIndex = 0; strIndex < stringsCount; strIndex++) {
        prb_String str = strings[strIndex];
        prb_stringBuilderWrite(&builder, str);
        prb_stringBuilderWrite(&builder, sep);
    }

    return builder.string;
}

prb_String
prb_stringJoin2(prb_String str1, prb_String str2) {
    prb_assert(str1.ptr && str1.len > 0 && str2.ptr && str2.len > 0);
    prb_StringBuilder builder = prb_createStringBuilder(str1.len + str2.len);
    prb_stringBuilderWrite(&builder, str1);
    prb_stringBuilderWrite(&builder, str2);
    return builder.string;
}

prb_String
prb_stringJoin3(prb_String str1, prb_String str2, prb_String str3) {
    prb_assert(str1.ptr && str1.len > 0 && str2.ptr && str2.len > 0 && str3.ptr && str3.len > 0);
    prb_StringBuilder builder = prb_createStringBuilder(str1.len + str2.len + str3.len);
    prb_stringBuilderWrite(&builder, str1);
    prb_stringBuilderWrite(&builder, str2);
    prb_stringBuilderWrite(&builder, str3);
    return builder.string;
}

prb_String
prb_stringJoin4(prb_String str1, prb_String str2, prb_String str3, prb_String str4) {
    prb_assert(
        str1.ptr && str1.len > 0 && str2.ptr && str2.len > 0 && str3.ptr && str3.len > 0 && str4.ptr && str4.len > 0
    );
    prb_StringBuilder builder = prb_createStringBuilder(str1.len + str2.len + str3.len + str4.len);
    prb_stringBuilderWrite(&builder, str1);
    prb_stringBuilderWrite(&builder, str2);
    prb_stringBuilderWrite(&builder, str3);
    prb_stringBuilderWrite(&builder, str4);
    return builder.string;
}

prb_String
prb_pathJoin2(prb_String path1, prb_String path2) {
    prb_assert(path1.ptr && path1.len > 0 && path2.ptr && path2.len > 0);
    char path1LastChar = path1.ptr[path1.len - 1];
    bool path1EndsOnSep = prb_charIsSep(path1LastChar);
    int32_t totalLen = path1EndsOnSep ? path1.len + path2.len : path1.len + 1 + path2.len;
    prb_StringBuilder builder = prb_createStringBuilder(totalLen);
    prb_stringBuilderWrite(&builder, path1);
    if (!path1EndsOnSep) {
        // NOTE(khvorov) Windows seems to handle mixing \ and / just fine
        prb_stringBuilderWrite(&builder, prb_STR("/"));
    }
    prb_stringBuilderWrite(&builder, path2);
    return builder.string;
}

prb_String
prb_pathJoin3(prb_String path1, prb_String path2, prb_String path3) {
    prb_String result = prb_pathJoin2(prb_pathJoin2(path1, path2), path3);
    return result;
}

//
// SECTION Platform-specific stuff
//

    #if prb_PLATFORM == prb_PLATFORM_WINDOWS
        #define WIN32_LEAN_AND_MEAN
        #include <windows.h>

bool
prb_directoryExists(prb_String path) {
    prb_assert(path.ptr && path.len > 0);
    prb_String pathNoTrailingSlash = path;
    char lastChar = path.ptr[path.len - 1];
    if (lastChar == '/' || lastChar == '\\') {
        pathNoTrailingSlash = prb_stringCopy(path, 0, path.len - 2);
    }
    bool result = false;
    WIN32_FIND_DATAA findData;
    HANDLE findHandle = FindFirstFileA(pathNoTrailingSlash.ptr, &findData);
    if (findHandle != INVALID_HANDLE_VALUE) {
        result = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return result;
}

void
prb_createDirIfNotExists(prb_String path) {
    CreateDirectory(path.ptr, 0);
}

bool
prb_directoryIsEmpty(prb_String path) {
    prb_assert(prb_directoryExists(path));
    prb_String search = prb_pathJoin2(path, prb_STR("*"));
    WIN32_FIND_DATAA findData;
    HANDLE firstHandle = FindFirstFileA(search.ptr, &findData);
    bool result = true;
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

prb_CompletionStatus
prb_execCmd(prb_String cmd) {
    prb_CompletionStatus cmdStatus = prb_CompletionStatus_Failure;

    STARTUPINFOA startupInfo = {.cb = sizeof(STARTUPINFOA)};
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
prb_logMessage(prb_String msg) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(out, msg.ptr, msg.len, 0, 0);
}

void
prb_logMessageLn(prb_String msg) {
    prb_logMessage(msg);
    prb_logMessage(prb_STR("\n"));
}

int32_t
prb_atomicIncrement(int32_t volatile* addend) {
    int32_t result = InterlockedAdd((long volatile*)addend, 1);
    return result;
}

bool
prb_atomicCompareExchange(int32_t volatile* dest, int32_t exchange, int32_t compare) {
    int32_t initValue = InterlockedCompareExchange((long volatile*)dest, exchange, compare);
    bool result = initValue == compare;
    return result;
}

void
prb_sleepMs(int32_t ms) {
    Sleep(ms);
}

prb_String
prb_getCurrentWorkingDir(void) {
    prb_StringBuilder builder = prb_createStringBuilder(MAX_PATH);
    GetCurrentDirectoryA(builder.string.len, builder.string.ptr);
    return builder.string;
}

uint64_t
prb_getLastModifiedFromPattern(prb_String pattern) {
    uint64_t result = 0;

    WIN32_FIND_DATAA findData;
    HANDLE firstHandle = FindFirstFileA(pattern.ptr, &findData);
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
prb_getLastModifiedFromPatterns(prb_String* patterns, int32_t patternsCount) {
    uint64_t result = 0;
    for (int32_t patternIndex = 0; patternIndex < patternsCount; patternIndex++) {
        result = prb_max(result, prb_getLastModifiedFromPattern(patterns[patternIndex]));
    }
    return result;
}

    #endif  // prb_PLATFORM == prb_PLATFORM_WINDOWS

#endif  // PROGRAMMABLE_BUILD_IMPLEMENTATION
