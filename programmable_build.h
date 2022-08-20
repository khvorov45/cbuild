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

#define prb_arrayLength(arr) ((sizeof((arr)[0])) / sizeof(arr))
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

typedef struct prb_StepData {
    prb_StepDataKind kind;
    union {
        struct {
            prb_String url;
            prb_String dest;
        } gitClone;

        struct {
            prb_String dir;
            prb_String* sources;
            int32_t sourcesCount;
            prb_String* flags;
            int32_t flagsCount;
            prb_String* extraWatch;
            int32_t extraWatchCount;
        } compile;

        void* custom;
    };
} prb_StepData;

typedef enum prb_CompletionStatus {
    prb_CompletionStatus_Success,
    prb_CompletionStatus_Failure,
} prb_CompletionStatus;

typedef prb_CompletionStatus (*prb_StepProc)(prb_StepData data);

typedef struct prb_Step {
    prb_StepProc proc;
    prb_StepData data;
} prb_Step;

typedef enum prb_StepStatus {
    prb_StepStatus_NotStarted,
    prb_StepStatus_NotStartedBecauseDepsFailed,
    prb_StepStatus_InProgress,
    prb_StepStatus_CompletedSuccessfully,
    prb_StepStatus_CompletedUnsuccessfully,
} prb_StepStatus;

// SECTION Core
void prb_init(prb_String rootPath);
prb_StepHandle prb_addStep(prb_StepProc proc, prb_StepData data);
void prb_setDependency(prb_StepHandle dependent, prb_StepHandle dependency);
void prb_run(void);

// SECTION Helpers
bool prb_directoryExists(prb_String path);
bool prb_directoryIsEmpty(prb_String path);
bool prb_charIsSep(char ch);
prb_StringBuilder prb_createStringBuilder(int32_t len);
void prb_stringBuilderWrite(prb_StringBuilder* builder, prb_String source);
prb_String prb_stringCopy(prb_String source, int32_t len);
prb_String prb_getParentDir(prb_String path);
prb_String prb_stringJoin(prb_String str1, prb_String str2);
prb_String prb_pathJoin(prb_String path1, prb_String path2);
prb_String prb_createIncludeFlag(prb_String path);
prb_CompletionStatus prb_execCmd(prb_String cmd);
void prb_logMessage(prb_String msg);
void prb_logMessageLn(prb_String msg);
int32_t prb_atomicIncrement(int32_t volatile* addend);
bool prb_atomicCompareExchange(int32_t volatile* dest, int32_t exchange, int32_t compare);
void prb_sleepMs(int32_t ms);

// SECTION Sample step procedures
prb_CompletionStatus prb_gitClone(prb_StepData data);
prb_CompletionStatus prb_compileStaticLibrary(prb_StepData data);
prb_CompletionStatus prb_compileExecutable(prb_StepData data);

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

void
prb_init(prb_String rootPath) {
    prb_assert(prb_directoryExists(rootPath));
    prb_globalBuilder.rootPath = rootPath;
}

prb_StepHandle
prb_addStep(prb_StepProc proc, prb_StepData data) {
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
prb_stringCopy(prb_String source, int32_t len) {
    prb_assert(len <= source.len);
    prb_StringBuilder builder = prb_createStringBuilder(len);
    prb_stringBuilderWrite(&builder, (prb_String) {source.ptr, len});
    return builder.string;
}

prb_String
prb_getParentDir(prb_String path) {
    prb_assert(path.ptr && path.len > 0);
    int32_t lastPathSepIndex = -1;
    for (int32_t index = path.len - 1; index >= 0; index--) {
        char ch = path.ptr[index];
        if (ch == '/' || ch == '\\') {
            lastPathSepIndex = index;
            break;
        }
    }
    prb_assert(lastPathSepIndex >= 0);
    prb_String result = prb_stringCopy(path, lastPathSepIndex + 1);
    return result;
}

prb_String
prb_stringJoin(prb_String str1, prb_String str2) {
    prb_StringBuilder builder = prb_createStringBuilder(str1.len + str2.len);
    prb_stringBuilderWrite(&builder, str1);
    prb_stringBuilderWrite(&builder, str2);
    return builder.string;
}

prb_String
prb_pathJoin(prb_String path1, prb_String path2) {
    prb_assert(path1.ptr && path1.len > 0 && path2.ptr && path2.len > 0);
    char path1LastChar = path1.ptr[path1.len - 1];
    bool path1EndsOnSep = prb_charIsSep(path1LastChar);
    int32_t totalLen = path1EndsOnSep ? path1.len + path2.len : path1.len + 1 + path2.len;
    prb_StringBuilder builder = prb_createStringBuilder(totalLen);
    prb_stringBuilderWrite(&builder, path1);
    if (!path1EndsOnSep) {
        prb_stringBuilderWrite(
            &builder,
            prb_STR("/")
        );  // NOTE(khvorov) Windows seems to handle mixing \ and / just fine
    }
    prb_stringBuilderWrite(&builder, path2);
    return builder.string;
}

prb_String
prb_createIncludeFlag(prb_String path) {
    prb_String result = prb_stringJoin(prb_STR("-I"), prb_pathJoin(prb_globalBuilder.rootPath, path));
    return result;
}

//
// SECTION Example step procedures
//

prb_CompletionStatus
prb_gitClone(prb_StepData data) {
    prb_assert(data.kind == prb_StepDataKind_GitClone);
    prb_String cmdStart = prb_STR("git clone ");
    prb_String realDest = prb_pathJoin(prb_globalBuilder.rootPath, data.gitClone.dest);

    prb_CompletionStatus status = prb_CompletionStatus_Success;
    if (!prb_directoryExists(realDest) || prb_directoryIsEmpty(realDest)) {
        prb_StringBuilder cmdBuilder = prb_createStringBuilder(cmdStart.len + data.gitClone.url.len + 1 + realDest.len);
        prb_stringBuilderWrite(&cmdBuilder, cmdStart);
        prb_stringBuilderWrite(&cmdBuilder, data.gitClone.url);
        prb_stringBuilderWrite(&cmdBuilder, prb_STR(" "));
        prb_stringBuilderWrite(&cmdBuilder, realDest);
        prb_logMessageLn(cmdBuilder.string);
        status = prb_execCmd(cmdBuilder.string);
    }

    return status;
}

prb_CompletionStatus
prb_compileStaticLibrary(prb_StepData data) {
    prb_assert(!"unimplemented");
    return prb_CompletionStatus_Success;
}

prb_CompletionStatus
prb_compileExecutable(prb_StepData data) {
    prb_assert(!"unimplemented");
    return prb_CompletionStatus_Success;
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
        pathNoTrailingSlash = prb_stringCopy(path, path.len - 1);
    }
    bool result = false;
    WIN32_FIND_DATAA findData;
    HANDLE findHandle = FindFirstFileA(pathNoTrailingSlash.ptr, &findData);
    if (findHandle != INVALID_HANDLE_VALUE) {
        result = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return result;
}

bool
prb_directoryIsEmpty(prb_String path) {
    prb_assert(prb_directoryExists(path));
    prb_String search = prb_pathJoin(path, prb_STR("*"));
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
    STARTUPINFOA startupInfo = {.cb = sizeof(STARTUPINFOA)};

    PROCESS_INFORMATION processInfo;
    if (CreateProcessA(0, cmd.ptr, 0, 0, 0, 0, 0, 0, &startupInfo, &processInfo)) {
        WaitForSingleObject(processInfo.hProcess, INFINITE);
    }

    prb_CompletionStatus cmdStatus = prb_CompletionStatus_Success;
    DWORD exitCode;
    if (GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        if (exitCode != 0) {
            cmdStatus = prb_CompletionStatus_Failure;
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

    #endif  // prb_PLATFORM == prb_PLATFORM_WINDOWS

#endif  // PROGRAMMABLE_BUILD_IMPLEMENTATION
