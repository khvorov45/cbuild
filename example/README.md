This example program has a "build script" that will download and compile sdl, freetype, harfbuzz, fribidi, icu and statically link them into an executable.

Note that it's probably better to include the source of libraries you are compiling yourself into your project. I didn't want to include all of the dependencies here because it would bloat the repository too much. So I'm downloading them in the example.
