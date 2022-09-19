#include <stdbool.h>

#include <ft2build.h>
#include <freetype/freetype.h>

#include <SDL.h>

// NOTE(khvorov) SDL provides platform detection
#define PLATFORM_WINDOWS __WIN32__
#define PLATFORM_LINUX __LINUX__

#define function static

typedef uint32_t u32;
typedef int32_t  i32;

#include "fontdata.c"

typedef struct Font {
    u32* atlas;
    i32  atlasWidthPx;
    i32  atlasHeightPx;
} Font;

function void
processEvent(SDL_Window* window, SDL_Event* event, bool* running) {
    switch (event->type) {
        case SDL_QUIT: {
            *running = false;
        } break;

        case SDL_WINDOWEVENT: {
            if (event->window.event == SDL_WINDOWEVENT_CLOSE && event->window.windowID == SDL_GetWindowID(window)) {
                *running = false;
            }
        } break;
    }
}

function void
pollEvents(SDL_Window* window, bool* running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        processEvent(window, &event, running);
    }
}

int
main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) == 0) {
        // TODO(khvorov) Actually do something here
        FT_Library ftLib;
        FT_Error ftInitResult = FT_Init_FreeType(&ftLib);
        if (ftInitResult == FT_Err_Ok) {
            FT_Face ftFace;
            FT_Error ftFaceResult = FT_New_Memory_Face(ftLib, fontdata, sizeof(fontdata), 0, &ftFace);
            if (ftFaceResult == FT_Err_Ok) {
                SDL_Window* sdlWindow = SDL_CreateWindow("test", 0, 0, 1000, 1000, 0);
                if (sdlWindow) {
                    SDL_Renderer* sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_PRESENTVSYNC);
                    if (sdlRenderer) {
                        bool running = true;
                        while (running) {
                            SDL_Event event;
                            SDL_WaitEvent(&event);
                            processEvent(sdlWindow, &event, &running);
                            pollEvents(sdlWindow, &running);
                            SDL_RenderPresent(sdlRenderer);
                        }
                    }
                }
            }
        }
    }

    return 0;
}
