#include <stdio.h>

#include <ft2build.h>
#include <freetype/freetype.h>

#include <SDL.h>

int
main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) == 0) {
        // TODO(khvorov) Actually do something here
        printf("sdl loaded successfully\n");

        FT_Library ftLib;
        if (FT_Init_FreeType(&ftLib) == FT_Err_Ok) {
            printf("freetype loaded successfully\n");
        }
    }
    return 0;
}