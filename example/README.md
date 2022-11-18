This example program has a "build script" that will download and compile sdl, freetype, harfbuzz, fribidi, icu and statically link them into an executable. Note that only the basic compiler commands are used, I'm not actually invoking any of the build systems those libraries are meant to be compiled with.

On linux you'll need headers for libX11, libXext and fontconfig (can't just download these locally because they may be different per distribution). Every linux distribution should have these libraries installed already, just not the headers.

```sh
sudo apt install libx11-dev libxext-dev libfontconfig-dev
```

Note that it's probably better to include the source of libraries you are compiling yourself into your project. I didn't want to include all of the dependencies here because it would bloat the repository too much. So I'm downloading them in the example.
