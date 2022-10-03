# Why not zig build?

* zig build requires you to depend on the entire zig toolchain.

* zig build locks you in to clang for C compiling.

* zig build hides the generated build executables somewhere so you can't easily run them in a debugger.

* zig build has a weird, poorly documented and hard to use "abstraction layer" over the compiler commands it generates. So it's not enough to read the compiler documentation, you also have to read the source code of the zig build system to figure out how to convince it to generate the compiler commands you want to run.

* zig build works by creating a job graph and then executing the jobs on it. I found this to be an unnecessary level of complexity. Just run (or skip) the steps as you go. You know what the steps in your program compilation are. You know what the dependencies between them are. You know when some of the steps can be skipped. You don't need a generic graph-based job system to compile your program when you already have a programming language. If you want to run multiple processes in parallel then create multiple processes and have them run in parallel.
