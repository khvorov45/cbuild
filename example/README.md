This example program has a "build script" that will download and compile sdl, freetype, harfbuzz, fribidi, icu and statically link them into an executable.

On linux you'll need headers for libX11, libXext and fontconfig (can't just download these locally because they may be different per distribution). Every linux distribution should have these libraries installed already, just not the headers.

```sh
sudo apt install libx11-dev libxext-dev libfontconfig-dev
```

To run the example provide compiler (gcc/clang on linux and msvc/clang on windows) and build mode (debug/release) like this

```sh
./example/build.sh clang debug
```

Note that only the basic compiler commands are used, I'm not actually invoking any of the build systems those libraries are meant to be compiled with (although you can do that as well).

The code in `example.c` is recompiled every time. The code in libraries is compiled when something in them changes on a per-translation unit basis. This is done by preprocessing all the source files, hashing the resulting preprocessed output and comparing that hash to the hash that was computed during previous compilation. This is enabled for all 5 libraries which is probably overkill (you would only need that if you were actively modifying code in all 5 libraries at the same time) but I wanted to do it this way to see how far this kind of custom incremental compilation system could be pushed. When no library code is changed it takes ~3-4 seconds on a modern high-end desktop PC and ~10-15 seconds on a mid-range laptop to recompile the example (this includes preprocessing all 5 libraries and hashing all the files to see if anything changed).

Note that it's probably better to include the source of libraries you are compiling yourself into your project. I didn't want to include all of the dependencies here because it would bloat the repository too much. So I'm downloading them in the example.
