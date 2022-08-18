#include <stdint.h>
#include <stdbool.h>

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

#ifndef prb_assert
    #include <assert.h>
    #define prb_assert assert
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
        .ptr = (str), .len = prb_strlen(str) \
    }

typedef struct prb_String {
    char* ptr;
    int32_t len;
} prb_String;

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

typedef void (*prb_StepProc)(prb_StepData data);

typedef struct prb_Step {
    prb_StepProc proc;
    prb_StepData data;
} prb_Step;

void prb_init(prb_String rootPath);
prb_StepHandle prb_addStep(prb_StepProc proc, prb_StepData data);
void prb_setDependency(prb_StepHandle dependent, prb_StepHandle dependency);
void prb_run(void);

prb_String prb_allocEmptyString(int32_t len);
bool prb_isDirectory(prb_String path);
prb_String prb_stringCopy(prb_String source, int32_t len);
prb_String prb_getParentDir(prb_String path);
prb_String prb_pathJoin(prb_String path1, prb_String path2);
prb_String prb_createIncludeFlag(prb_String path);

void prb_gitClone(prb_StepData data);
void prb_compileStaticLibrary(prb_StepData data);
void prb_compileExecutable(prb_StepData data);

#ifdef PROGRAMMABLE_BUILD_IMPLEMENTATION

struct {
    prb_String rootPath;
    prb_Step steps[prb_MAX_STEPS];
    int32_t stepCount;
    prb_StepHandle dependencies[prb_MAX_STEPS][prb_MAX_DEPENDENCIES_PER_STEP];
    int32_t dependenciesCounts[prb_MAX_STEPS];
} prb_globalBuilder;

void
prb_init(prb_String rootPath) {
    prb_assert(prb_isDirectory(rootPath));
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
prb_run(void) {
    for (int32_t stepIndex = 0; stepIndex < prb_globalBuilder.stepCount; stepIndex++) {
        // prb_Step* step = prb_globalBuilder.steps + stepIndex;
        // step->proc(step->data);
    }
}

prb_String
prb_allocEmptyString(int32_t len) {
    prb_assert(len > 0);
    prb_String result = {prb_allocAndZero(len + 1), len};
    return result;
}

prb_String
prb_stringCopy(prb_String source, int32_t len) {
    prb_String result = prb_allocEmptyString(len);
    prb_memcpy(result.ptr, source.ptr, len);
    return result;
}

bool
prb_isDirectory(prb_String path) {}

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
prb_pathJoin(prb_String path1, prb_String path2) {
    prb_assert(path1.ptr && path1.len > 0 && path2.ptr && path2.len > 0);
    prb_String result = prb_allocEmptyString(path1.len + 1 + path2.len);
    prb_memcpy(result.ptr, path1.ptr, path1.len);
    result.ptr[path1.len] = '/';
    prb_memcpy(result.ptr + path1.len + 1, path2.ptr, path2.len);
    return result;
}

prb_String
prb_createIncludeFlag(prb_String path) {}

void
prb_gitClone(prb_StepData data) {}

void
prb_compileStaticLibrary(prb_StepData data) {}

void
prb_compileExecutable(prb_StepData data) {}

#endif  // PROGRAMMABLE_BUILD_IMPLEMENTATION
