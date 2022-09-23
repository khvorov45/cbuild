#include <stdbool.h>

#include <ft2build.h>
#include <freetype/freetype.h>

#include <SDL.h>

// NOTE(khvorov) SDL provides platform detection
#define PLATFORM_WINDOWS __WIN32__
#define PLATFORM_LINUX __LINUX__

#define function static

// clang-format off
#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))
#define clamp(x, a, b) (((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x)))
#define isPowerOf2(x) (((x) > 0) && (((x) & ((x)-1)) == 0))
#define assert(condition) do { if (!(condition)) { SDL_TriggerBreakpoint(); } } while (0)
// clang-format on

typedef uint8_t  u8;
typedef uint32_t u32;
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
        int  initResult = SDL_Init(SDL_INIT_VIDEO);
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

typedef SDL_Rect  Rect;
typedef SDL_Color Color;

function void
drawRect(Renderer* renderer, Rect rect, Color color) {
    assert(rect.w >= 0 && rect.h >= 0);
    if (rect.w > 0 && rect.h > 0) {
        SDL_SetRenderDrawColor(renderer->sdlRenderer, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer->sdlRenderer, &rect);
    }
}

//
// SECTION Game
//

function Rect
rectCenterDim(i32 centerX, i32 centerY, i32 dimX, i32 dimY) {
    assert(dimX >= 0 && dimY >= 0);
    Rect result = {.x = centerX - dimX / 2, .y = centerY - dimY / 2, .w = dimX, .h = dimY};
    return result;
}

typedef struct GameState {
    f32  plankPosX01;
    bool showEntireFontTexture;
} GameState;

function void
gameUpdateAndRender(GameState* gameState, Renderer* renderer, Input* input) {
    if (wasPressed(input, InputKeyID_MouseLeft)) {
        gameState->showEntireFontTexture = !gameState->showEntireFontTexture;
    }

    i32 plankHeightPx = 20;
    i32 plankWidthPx = 50;
    {
        f32 plankHalfWidth = (f32)plankWidthPx / 2.0f;
        f32 plankMin = plankHalfWidth / (f32)renderer->width;
        f32 plankMax = 1.0f - plankMin;
        gameState->plankPosX01 = clamp((f32)input->cursorX / (f32)renderer->width, plankMin, plankMax);
    }

    assert(renderBegin(renderer) == CompletionStatus_Sucess);

    Rect plankRect = rectCenterDim(
        (i32)(gameState->plankPosX01 * (f32)renderer->width),
        renderer->height - plankHeightPx / 2,
        plankWidthPx,
        plankHeightPx
    );
    Color plankColor = {.r = 100, .g = 0, .b = 0, .a = 255};
    drawRect(renderer, plankRect, plankColor);

    if (gameState->showEntireFontTexture) {
        drawEntireFontTexture(renderer);
    }

    renderEnd(renderer);
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
    // TODO(khvorov) Actually do something here
    Allocator            sdlGeneralPurposeAllocator = {.allocAndZero = sdlCallocWrapper};
    CreateRendererResult createRendererResult = createRenderer(sdlGeneralPurposeAllocator);
    if (createRendererResult.success) {
        Renderer    renderer = createRendererResult.renderer;
        SDL_Window* sdlWindow = renderer.sdlWindow;
        Input       input = {0};
        GameState   gameState = {0};
        bool        running = true;
        while (running) {
            inputBeginFrame(&input);

            SDL_Event event;
            assert(SDL_WaitEvent(&event) == 1);
            processEvent(sdlWindow, &event, &running, &input);
            while (SDL_PollEvent(&event)) {
                processEvent(sdlWindow, &event, &running, &input);
            }

            gameUpdateAndRender(&gameState, &renderer, &input);
        }
    }
    return 0;
}
