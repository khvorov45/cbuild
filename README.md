# cbuild

A set of utilities for writing "build scripts" as small C (or C++) programs.

See `example/build.c` for a non-trivial example build program.

A simple build program might look like this

File structure

```
project-directory
|
|-cbuild.h
|-build.c
|-program.c
```

Contents of `build.c`:

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

# Why not zig build?

* zig build requires you to depend on the entire zig toolchain.

* zig build locks you in to clang for C compiling.

* zig build hides the generated build executables somewhere so you can't easily run them in a debugger.

* zig build has a weird, poorly documented and hard to use "abstraction layer" over the compiler commands it generates. So it's not enough to read the compiler documentation, you also have to read the source code of the zig build system to figure out how to convince it to generate the compiler commands you want to run.

* zig build works by creating a job graph and then executing the jobs on it. I find this is unnecessary when all you want is to compile some code. The kinds of "jobs" you want to run in parallel (such as compiling different translation units) don't have many data dependencies between them and so they are easy enough to parallelise without the need for a complicated general purpose job graph abstraction.
