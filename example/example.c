#include <stdio.h>
#include <stdbool.h>

#include <ft2build.h>
#include <freetype/freetype.h>

#include <SDL.h>

void
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

void
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
        printf("sdl loaded successfully\n");

        FT_Library ftLib;
        if (FT_Init_FreeType(&ftLib) == FT_Err_Ok) {
            printf("freetype loaded successfully\n");

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

    return 0;
}
