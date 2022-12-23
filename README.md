# Why not zig build?

* zig build requires you to depend on the entire zig toolchain.

* zig build locks you in to clang for C compiling.

* zig build hides the generated build executables somewhere so you can't easily run them in a debugger.

* zig build has a weird, poorly documented and hard to use "abstraction layer" over the compiler commands it generates. So it's not enough to read the compiler documentation, you also have to read the source code of the zig build system to figure out how to convince it to generate the compiler commands you want to run.

* zig build works by creating a job graph and then executing the jobs on it. I find this is unnecessary when all you want is to compile some code. The kinds of "jobs" you want to run in parallel (such as compiling different translation units) don't have many data dependencies between them and so they are easy enough to parallelise without the need for a complicated general purpose job graph abstraction.
