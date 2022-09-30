#include <stdbool.h>

#include <ft2build.h>
#include <freetype/freetype.h>

#include <SDL.h>

// NOTE(khvorov) SDL provides platform detection
#define PLATFORM_WINDOWS __WIN32__
#define PLATFORM_LINUX __LINUX__

#define INFINITY __builtin_inff()
#define function static

// clang-format off
#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))
#define clamp(x, a, b) (((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x)))
#define isPowerOf2(x) (((x) > 0) && (((x) & ((x)-1)) == 0))
#define arrayLen(arr) (sizeof(arr) / sizeof(arr[0]))
#define assert(condition) do { if (!(condition)) { SDL_TriggerBreakpoint(); } } while (0)
// clang-format on

typedef uint8_t  u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef size_t   usize;
typedef int32_t  i32;
typedef float    f32;

typedef enum CompletionStatus {
    CompletionStatus_Failure,
    CompletionStatus_Sucess,
} CompletionStatus;

//
// SECTION Memory
//

typedef void* (*AllocatorAllocAndZero)(i32 size, i32 align);
typedef void* (*AllocatorRealloc)(void* ptr, i32 size, i32 align);

typedef struct Allocator {
    AllocatorAllocAndZero allocAndZero;
} Allocator;

#define allocArray(allocator, type, count) (type*)(allocator).allocAndZero(sizeof(type) * (count), _Alignof(type))

function void*
sdlCallocWrapper(i32 size, i32 align) {
    assert(size >= 0 && align >= 0 && isPowerOf2(align) && align <= 8);
    void* result = SDL_calloc(size, 1);
    return result;
}

//
// SECTION Input
//

typedef struct InputKey {
    i32  halfTransitionCount;
    bool endedDown;
} InputKey;

typedef enum InputKeyID {
    InputKeyID_MouseLeft,
    InputKeyID_Count,
} InputKeyID;

typedef struct Input {
    InputKey keys[InputKeyID_Count];
    i32      cursorX;
    i32      cursorY;
} Input;

function Input
createInput() {
    Input input = {0};
    return input;
}

function void
inputBeginFrame(Input* input) {
    for (i32 keyIndex = 0; keyIndex < InputKeyID_Count; keyIndex += 1) {
        InputKey* key = input->keys + keyIndex;
        key->halfTransitionCount = 0;
    }
}

function InputKey*
getKey(Input* input, InputKeyID keyID) {
    assert(keyID >= 0 && keyID < InputKeyID_Count);
    InputKey* key = input->keys + keyID;
    return key;
}

function void
recordKey(Input* input, InputKeyID keyID, bool down) {
    InputKey* key = getKey(input, keyID);
    key->halfTransitionCount += 1;
    key->endedDown = down;
}

function bool
wasPressed(Input* input, InputKeyID keyID) {
    InputKey* key = getKey(input, keyID);
    bool      result = key->halfTransitionCount > 1 || (key->halfTransitionCount == 1 && key->endedDown);
    return result;
}

function bool
wasUnpressed(Input* input, InputKeyID keyID) {
    InputKey* key = getKey(input, keyID);
    bool      result = key->halfTransitionCount > 1 || (key->halfTransitionCount == 1 && !key->endedDown);
    return result;
}

//
// SECTION Font
//

typedef struct RectPacker {
    i32 width, height;
    i32 currentX, currentY;
    i32 tallestOnLine;
} RectPacker;

function RectPacker
rectPackBegin(i32 width) {
    RectPacker packer = {.width = width};
    return packer;
}

function void
rectPackAdd(RectPacker* packer, i32 width, i32 height, i32* topleftX, i32* topleftY) {
    i32 widthLeft = packer->width - packer->currentX;

    if (width > widthLeft) {
        assert(width <= packer->width);
        packer->currentX = 0;
        packer->currentY += packer->tallestOnLine;
        packer->tallestOnLine = 0;
    }

    *topleftX = packer->currentX;
    *topleftY = packer->currentY;

    packer->currentX += width;

    i32 prevTallest = packer->tallestOnLine;
    packer->tallestOnLine = max(packer->tallestOnLine, height);
    packer->height += max(packer->tallestOnLine - prevTallest, 0);
}

#include "fontdata.c"

typedef struct Glyph {
    i32 atlasTopleftX, atlasY;
    i32 width, height;
    i32 offsetX, offsetY;
    i32 advanceX;
} Glyph;

typedef struct Font {
    Glyph* glyphs;
    u32    firstChar;
    i32    charCount;
    i32    lineHeight;
    u32*   buffer;
    i32    width, height, pitch;
} Font;

function CompletionStatus
loadAndRenderFTBitmap(u32 firstchar, i32 chIndex, FT_Face ftFace) {
    assert(chIndex >= 0);
    CompletionStatus result = CompletionStatus_Failure;
    u32              ch = firstchar + chIndex;
    u32              ftGlyphIndex = FT_Get_Char_Index(ftFace, ch);
    FT_Error         loadGlyphResult = FT_Load_Glyph(ftFace, ftGlyphIndex, FT_LOAD_DEFAULT);
    if (loadGlyphResult == FT_Err_Ok) {
        if (ftFace->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
            FT_Error renderGlyphResult = FT_Render_Glyph(ftFace->glyph, FT_RENDER_MODE_NORMAL);
            if (renderGlyphResult == FT_Err_Ok) {
                result = CompletionStatus_Sucess;
            }
        }
    }
    return result;
}

typedef struct LoadFontResult {
    bool success;
    Font font;
} LoadFontResult;

function LoadFontResult
loadFont(Allocator allocator) {
    LoadFontResult result = {0};
    FT_Library     ftLib;
    FT_Error       ftInitResult = FT_Init_FreeType(&ftLib);
    if (ftInitResult == FT_Err_Ok) {
        FT_Face  ftFace;
        FT_Error ftFaceResult = FT_New_Memory_Face(ftLib, fontdata, sizeof(fontdata), 0, &ftFace);
        if (ftFaceResult == FT_Err_Ok) {
            i32      fontHeight = 14;
            FT_Error ftSetPxSizeResult = FT_Set_Pixel_Sizes(ftFace, 0, fontHeight);
            if (ftSetPxSizeResult == FT_Err_Ok) {
                i32        atlasWidth = 500;
                i32        atlasPitch = atlasWidth * sizeof(u32);
                RectPacker charPacker = rectPackBegin(atlasWidth);

                u32    firstChar = ' ';
                i32    charCount = '~' - firstChar + 1;
                Glyph* glyphs = allocArray(allocator, Glyph, charCount);
                for (i32 chIndex = 0; chIndex < charCount; chIndex++) {
                    assert(loadAndRenderFTBitmap(firstChar, chIndex, ftFace) == CompletionStatus_Sucess);
                    FT_GlyphSlot ftGlyph = ftFace->glyph;
                    FT_Bitmap    bm = ftGlyph->bitmap;
                    Glyph*       glyphInfo = glyphs + chIndex;
                    *glyphInfo = (Glyph) {
                        .advanceX = ftGlyph->advance.x >> 6,
                        .width = bm.width,
                        .height = bm.rows,
                        .offsetX = ftGlyph->bitmap_left,
                        .offsetY = fontHeight - ftGlyph->bitmap_top,
                    };
                    rectPackAdd(&charPacker, bm.width, bm.rows, &glyphInfo->atlasTopleftX, &glyphInfo->atlasY);
                }

                i32  atlasHeight = charPacker.height;
                i32  fontAtlasPx = charPacker.width * atlasHeight;
                u32* atlas = allocArray(allocator, u32, fontAtlasPx);
                for (i32 chIndex = 0; chIndex < charCount; chIndex++) {
                    assert(loadAndRenderFTBitmap(firstChar, chIndex, ftFace) == CompletionStatus_Sucess);
                    FT_GlyphSlot ftGlyph = ftFace->glyph;
                    FT_Bitmap    bm = ftGlyph->bitmap;
                    Glyph*       glyphInfo = glyphs + chIndex;

                    for (i32 bmRow = 0; bmRow < (i32)bm.rows; bmRow++) {
                        i32 atlasY = bmRow + glyphInfo->atlasY;
                        for (i32 bmCol = 0; bmCol < (i32)bm.width; bmCol++) {
                            i32 atlasX = bmCol + glyphInfo->atlasTopleftX;
                            i32 bmIndex = bmRow * (bm.pitch / sizeof(u8)) + bmCol;
                            i32 atlasIndex = atlasY * (atlasPitch / sizeof(u32)) + atlasX;

                            i32 alpha = bm.buffer[bmIndex];
                            i32 fullColorRGBA = 0xFFFFFF00 | alpha;
                            atlas[atlasIndex] = fullColorRGBA;
                        }
                    }
                }

                i32 lineHeight = FT_MulFix(ftFace->height, ftFace->size->metrics.y_scale) >> 6;

                Font font = {
                    .glyphs = glyphs,
                    .firstChar = firstChar,
                    .charCount = charCount,
                    .buffer = atlas,
                    .width = atlasWidth,
                    .height = atlasHeight,
                    .pitch = atlasPitch,
                    .lineHeight = lineHeight,
                };

                result = (LoadFontResult) {.success = true, .font = font};
            }

            FT_Done_Face(ftFace);
        }

        FT_Done_FreeType(ftLib);
    }

    return result;
}

//
// SECTION Timing
//

typedef struct Clock {
    u64 countsPerSecond;
    u64 counter;
} Clock;

Clock
getCurrentClock() {
    Clock result = {.countsPerSecond = SDL_GetPerformanceFrequency(), .counter = SDL_GetPerformanceCounter()};
    return result;
}

f32
getMsFrom(Clock clock) {
    u64 counterDiff = SDL_GetPerformanceCounter() - clock.counter;
    f32 result = (f32)counterDiff / (f32)clock.countsPerSecond * 1000.f;
    return result;
}

//
// SECTION Render
//

typedef struct Renderer {
    SDL_Window*   sdlWindow;
    SDL_Renderer* sdlRenderer;
    SDL_Texture*  sdlFontTexture;
    Font          font;
    i32           width, height;
} Renderer;

typedef struct CreateRendererResult {
    bool     success;
    Renderer renderer;
} CreateRendererResult;

function CreateRendererResult
createRenderer(Allocator allocator) {
    CreateRendererResult result = {0};
    LoadFontResult       loadFontResult = loadFont(allocator);
    if (loadFontResult.success) {
        Font font = loadFontResult.font;
        int  initResult = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
        if (initResult == 0) {
            i32         windowWidth = 1000;
            i32         windowHeight = 1000;
            SDL_Window* sdlWindow = SDL_CreateWindow("test", 0, 0, windowWidth, windowHeight, 0);
            if (sdlWindow) {
                SDL_Renderer* sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_PRESENTVSYNC);
                if (sdlRenderer) {
                    SDL_Texture* sdlTexture = SDL_CreateTexture(
                        sdlRenderer,
                        SDL_PIXELFORMAT_RGBA8888,
                        SDL_TEXTUREACCESS_STATIC,
                        font.width,
                        font.height
                    );
                    if (sdlTexture) {
                        int setTexBlendModeResult = SDL_SetTextureBlendMode(sdlTexture, SDL_BLENDMODE_BLEND);
                        if (setTexBlendModeResult == 0) {
                            int updateTexResult = SDL_UpdateTexture(sdlTexture, 0, font.buffer, font.pitch);
                            if (updateTexResult == 0) {
                                Renderer renderer = {
                                    .sdlWindow = sdlWindow,
                                    .sdlRenderer = sdlRenderer,
                                    .font = font,
                                    .sdlFontTexture = sdlTexture,
                                    .width = windowWidth,
                                    .height = windowHeight,
                                };

                                result = (CreateRendererResult) {.success = true, .renderer = renderer};
                            }
                        }
                    }
                }
            }
        } else {
            // NOTE(khvorov) SDL log functions work even if it's not initialized
            const char* sdlError = SDL_GetError();
            SDL_LogError(0, "Failed to init SDL: %s\n", sdlError);
        }
    }
    return result;
}

function CompletionStatus
renderBegin(Renderer* renderer) {
    CompletionStatus result = CompletionStatus_Failure;
    int              setDrawColorResult = SDL_SetRenderDrawColor(renderer->sdlRenderer, 0, 0, 0, 0);
    if (setDrawColorResult == 0) {
        int renderClearResult = SDL_RenderClear(renderer->sdlRenderer);
        if (renderClearResult == 0) {
            result = CompletionStatus_Sucess;
        }
    }
    return result;
}

function void
renderEnd(Renderer* renderer) {
    SDL_RenderPresent(renderer->sdlRenderer);
}

function CompletionStatus
drawEntireFontTexture(Renderer* renderer) {
    CompletionStatus result = CompletionStatus_Failure;
    SDL_Rect         destRect = {.y = 50, .w = renderer->font.width, .h = renderer->font.height};
    int              renderCopyResult = SDL_RenderCopy(renderer->sdlRenderer, renderer->sdlFontTexture, 0, &destRect);
    if (renderCopyResult == 0) {
        result = CompletionStatus_Sucess;
    }
    return result;
}

typedef SDL_Rect  Rect2i;
typedef SDL_Color Color;

function void
drawRect(Renderer* renderer, Rect2i rect, Color color) {
    assert(rect.w >= 0 && rect.h >= 0);
    if (rect.w > 0 && rect.h > 0) {
        SDL_SetRenderDrawColor(renderer->sdlRenderer, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer->sdlRenderer, &rect);
    }
}

//
// SECTION Game
//

function Rect2i
rect2iCenterDim(i32 centerX, i32 centerY, i32 dimX, i32 dimY) {
    assert(dimX >= 0 && dimY >= 0);
    Rect2i result = {.x = centerX - dimX / 2, .y = centerY - dimY / 2, .w = dimX, .h = dimY};
    return result;
}

typedef struct Rect2f {
    f32 x, y, w, h;
} Rect2f;

function Rect2f
rect2fCenterDim(f32 centerX, f32 centerY, f32 dimX, f32 dimY) {
    assert(dimX >= 0.0f && dimY >= 0.0f);
    Rect2f result = {.x = centerX - dimX / 2.0f, .y = centerY - dimY / 2.0f, .w = dimX, .h = dimY};
    return result;
}

function Rect2i
pxRectFromGameRect(Renderer* renderer, Rect2f rect) {
    Rect2i result = {
        .x = (i32)(rect.x * (f32)renderer->width + 0.5f),
        .y = renderer->height - (i32)((rect.y + rect.h) * (f32)renderer->height + 0.5f),
        .w = (i32)(rect.w * (f32)renderer->width + 0.5f),
        .h = (i32)(rect.h * (f32)renderer->height + 0.5f),
    };
    return result;
}

// Position units are proportions of the screen
// Time is in ms (including for velocity)
typedef struct GameState {
    Rect2f plankRect;
    Rect2f ballRect;

    f32 ballVelX;
    f32 ballVelY;
    f32 plankVelX;
    f32 plankVelY;

    bool showEntireFontTexture;
} GameState;

function GameState
createGameState(f32 widthOverHeight) {
    Rect2f    plankRect = (Rect2f) {.x = 0.5f, .y = 0.0f, .w = 0.05f, .h = 0.1f};
    Rect2f    ballRect = (Rect2f) {.x = plankRect.x, .y = plankRect.y, .w = 0.05f, .h = 0.05f};
    GameState result = {
        .plankRect = plankRect,
        .ballRect = ballRect,
        .ballVelX = 0.001f,
        .ballVelY = 0.0f,
        .plankVelX = 0.0f,
        .plankVelY = 0.0f,
    };
    return result;
}

typedef enum Direction {
    Direction_Left,
    Direction_Right,
    Direction_Top,
    Direction_Bottom,
} Direction;

typedef struct Collision {
    f32  deltaTime;
    bool horizontal;
    bool vertical;
} Collision;

function Collision
calcRectOuterRectCollision(Rect2f rect1, f32 vel1x, f32 vel1y, Rect2f rect2, f32 vel2x, f32 vel2y) {
    f32 rvelx = vel1x - vel2x;
    f32 rvely = vel1y - vel2y;

    f32 xCollisionDeltaTime = INFINITY;
    if (rvelx > 0.0f) {
        f32 coord1 = rect1.x + rect1.w;
        f32 coord2 = rect2.x;
        if (coord1 < coord2) {
            f32 testCollisionDeltaTime = (coord2 - coord1) / rvelx;
            xCollisionDeltaTime = min(testCollisionDeltaTime, xCollisionDeltaTime);
        }
    } else if (rvelx < 0.0f) {
        f32 coord1 = rect1.x;
        f32 coord2 = rect2.x + rect2.w;
        if (coord1 > coord2) {
            f32 testCollisionDeltaTime = (coord2 - coord1) / rvelx;
            xCollisionDeltaTime = min(testCollisionDeltaTime, xCollisionDeltaTime);
        }
    }

    f32 yCollisionDeltaTime = INFINITY;
    if (rvely > 0.0f) {
        f32 coord1 = rect1.y + rect1.h;
        f32 coord2 = rect2.y;
        if (coord1 < coord2) {
            f32 testCollisionDeltaTime = (coord2 - coord1) / rvely;
            yCollisionDeltaTime = min(testCollisionDeltaTime, yCollisionDeltaTime);
        }
    } else if (rvelx < 0.0f) {
        f32 coord1 = rect1.y;
        f32 coord2 = rect2.y + rect2.h;
        if (coord1 > coord2) {
            f32 testCollisionDeltaTime = (coord2 - coord1) / rvely;
            yCollisionDeltaTime = min(testCollisionDeltaTime, yCollisionDeltaTime);
        }
    }

    f32 collisionDeltaTime = min(xCollisionDeltaTime, yCollisionDeltaTime);
    assert(collisionDeltaTime > 0.0f);
    bool      horizontal = xCollisionDeltaTime <= yCollisionDeltaTime;
    bool      vertical = xCollisionDeltaTime >= yCollisionDeltaTime;
    Collision result = {.deltaTime = collisionDeltaTime, .horizontal = horizontal, .vertical = vertical};
    return result;
}

function Collision
calcRectInnerRectCollision(Rect2f rect1, f32 vel1x, f32 vel1y, Rect2f rect2, f32 vel2x, f32 vel2y) {
    f32 rvelx = vel1x - vel2x;
    f32 rvely = vel1y - vel2y;

    f32 xCollisionDeltaTime = INFINITY;
    if (rvelx > 0.0f) {
        f32 coord1 = rect1.x + rect1.w;
        f32 coord2 = rect2.x + rect2.w;
        if (coord1 < coord2) {
            f32 testCollisionDeltaTime = (coord2 - coord1) / rvelx;
            xCollisionDeltaTime = min(testCollisionDeltaTime, xCollisionDeltaTime);
        }
    } else if (rvelx < 0.0f) {
        f32 coord1 = rect1.x;
        f32 coord2 = rect2.x;
        if (coord1 > coord2) {
            f32 testCollisionDeltaTime = (coord2 - coord1) / rvelx;
            xCollisionDeltaTime = min(testCollisionDeltaTime, xCollisionDeltaTime);
        }
    }

    f32 yCollisionDeltaTime = INFINITY;
    if (rvely > 0.0f) {
        f32 coord1 = rect1.y + rect1.h;
        f32 coord2 = rect2.y + rect2.h;
        if (coord1 < coord2) {
            f32 testCollisionDeltaTime = (coord2 - coord1) / rvely;
            yCollisionDeltaTime = min(testCollisionDeltaTime, yCollisionDeltaTime);
        }
    } else if (rvelx < 0.0f) {
        f32 coord1 = rect1.y + rect1.h;
        f32 coord2 = rect2.y + rect2.h;
        if (coord1 > coord2) {
            f32 testCollisionDeltaTime = (coord2 - coord1) / rvely;
            yCollisionDeltaTime = min(testCollisionDeltaTime, yCollisionDeltaTime);
        }
    }

    f32 collisionDeltaTime = min(xCollisionDeltaTime, yCollisionDeltaTime);
    assert(collisionDeltaTime > 0.0f);
    bool      horizontal = xCollisionDeltaTime <= yCollisionDeltaTime;
    bool      vertical = xCollisionDeltaTime >= yCollisionDeltaTime;
    Collision result = {.deltaTime = collisionDeltaTime, .horizontal = horizontal, .vertical = vertical};
    return result;
}

function void
gameUpdateAndRender(GameState* gameState, Renderer* renderer, Input* input, f32 deltaTimeMs) {
    // NOTE(khvorov) Update plank
    {
        // TODO(khvorov) Plank movement
    }

    // NOTE(khvorov) Update ball
    {
        if (gameState->ballVelX == 0 && gameState->ballVelY == 0) {
            if (wasPressed(input, InputKeyID_MouseLeft)) {
                gameState->ballVelX = 0.001f;
                gameState->ballVelY = 0.001f;
            } else {
                gameState->ballRect.x = gameState->plankRect.x;
                gameState->ballRect.y = gameState->plankRect.h + gameState->ballRect.h * 0.5;
            }
        }

        // TODO(khvorov) Address tunnelling
        f32    deltaTimeUnaccounted = deltaTimeMs;
        Rect2f curRect = gameState->ballRect;
        f32    curVelX = gameState->ballVelX;
        f32    curVelY = gameState->ballVelY;
        while (deltaTimeUnaccounted > 0.0f) {
            Collision plankCollision = calcRectOuterRectCollision(
                curRect,
                curVelX,
                curVelY,
                gameState->plankRect,
                gameState->plankVelX,
                gameState->plankVelY
            );

            Rect2f    screenRect = {0.0f, 0.0f, 1.0f, 1.0f};
            Collision screenCollision = calcRectInnerRectCollision(curRect, curVelX, curVelY, screenRect, 0.0f, 0.0f);

            Collision collision = plankCollision;
            if (screenCollision.deltaTime < collision.deltaTime) {
                collision = screenCollision;
            }

            f32  accountedDeltaTime = min(collision.deltaTime, deltaTimeUnaccounted);
            bool collided = accountedDeltaTime == collision.deltaTime;

            curRect.x = curRect.x + accountedDeltaTime * curVelX;
            curRect.y = curRect.y + accountedDeltaTime * curVelY;

            if (collided) {
                if (collision.horizontal) {
                    curVelX *= -1;
                }
                if (collision.vertical) {
                    curVelY *= -1;
                }
            }

            deltaTimeUnaccounted -= accountedDeltaTime;
        }

        gameState->ballRect = curRect;
        gameState->ballVelX = curVelX;
        gameState->ballVelY = curVelY;
    }

    // NOTE(khvorov) Render
    // TODO(khvorov) Subpixel rendering

    Rect2i plankRect = pxRectFromGameRect(renderer, gameState->plankRect);
    Color  plankColor = {.r = 100, .g = 0, .b = 0, .a = 255};
    drawRect(renderer, plankRect, plankColor);

    Rect2i ballRect = pxRectFromGameRect(renderer, gameState->ballRect);
    Color ballColor = {.r = 0, .g = 100, .b = 0, .a = 255};
    drawRect(renderer, ballRect, ballColor);

    if (gameState->showEntireFontTexture) {
        drawEntireFontTexture(renderer);
    }

    // NOTE(khvorov) Draw grid
    f32   gridStep = 0.1f;
    Color gridColor = {.r = 0, .g = 0, .b = 100, .a = 255};
    i32   gridLineThickness = 2;
    for (f32 ycoord = 0.0f; ycoord <= 1.0f; ycoord += gridStep) {
        i32    yi = renderer->height - (i32)(ycoord * (f32)renderer->height);
        Rect2i rect = {.x = 0, .y = yi, .w = renderer->width, .h = gridLineThickness};
        drawRect(renderer, rect, gridColor);
    }
    for (f32 xcoord = 0.0f; xcoord <= 1.0f; xcoord += gridStep) {
        i32    xi = (i32)(xcoord * (f32)renderer->width);
        Rect2i rect = {.x = xi, .y = 0, .w = gridLineThickness, .h = renderer->height};
        drawRect(renderer, rect, gridColor);
    }
}

//
// SECTION Main loop and events
//

function void
processEvent(SDL_Window* window, SDL_Event* event, bool* running, Input* input) {
    switch (event->type) {
        case SDL_QUIT: {
            *running = false;
        } break;

        case SDL_WINDOWEVENT: {
            if (event->window.event == SDL_WINDOWEVENT_CLOSE && event->window.windowID == SDL_GetWindowID(window)) {
                *running = false;
            }
        } break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            bool       down = event->type == SDL_MOUSEBUTTONUP ? 0 : 1;
            InputKeyID keyID = InputKeyID_Count;
            switch (event->button.button) {
                case SDL_BUTTON_LEFT: {
                    keyID = InputKeyID_MouseLeft;
                } break;
            }
            if (keyID != InputKeyID_Count) {
                recordKey(input, keyID, down);
            }
        } break;

        case SDL_MOUSEMOTION: {
            input->cursorX = event->motion.x;
            input->cursorY = event->motion.y;
        } break;
    }
}

int
main(int argc, char* argv[]) {
    Allocator            sdlGeneralPurposeAllocator = {.allocAndZero = sdlCallocWrapper};
    CreateRendererResult createRendererResult = createRenderer(sdlGeneralPurposeAllocator);
    if (createRendererResult.success) {
        Renderer    renderer = createRendererResult.renderer;
        SDL_Window* sdlWindow = renderer.sdlWindow;
        Input       input = {0};
        GameState   gameState = createGameState((f32)renderer.width / (f32)renderer.height);
        f32         targetMsPerFrame = 1000.0f / 60.0f;
        Clock       lastRenderEnd = getCurrentClock();
        bool        running = true;
        while (running) {
            inputBeginFrame(&input);

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                processEvent(sdlWindow, &event, &running, &input);
            }

            assert(renderBegin(&renderer) == CompletionStatus_Sucess);

            // TODO(khvorov) Single step
            // TODO(khvorov) Visualize timings
            // TODO(khvorov) Grid of glyphs
            // TODO(khvorov) Hardware rendering?
            gameUpdateAndRender(&gameState, &renderer, &input, targetMsPerFrame);

            // NOTE(khvorov) Wait
            {
                f32 msFromPreviousRenderEnd = getMsFrom(lastRenderEnd);
                f32 msRemaining = targetMsPerFrame - msFromPreviousRenderEnd;
                if (msRemaining >= 2.0f) {
                    u32 toWait = (u32)(msRemaining - 1);
                    SDL_Delay(toWait);
                    msRemaining -= (f32)toWait;
                }
                while (msRemaining > 0.0f) {
                    msRemaining = targetMsPerFrame - getMsFrom(lastRenderEnd);
                }
            }

            lastRenderEnd = getCurrentClock();
            renderEnd(&renderer);
        }
    }
    return 0;
}
