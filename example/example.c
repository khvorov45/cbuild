#include <stdbool.h>
#include <stdatomic.h>

#include <ft2build.h>
#include <freetype/freetype.h>

#include <SDL.h>

// NOTE(khvorov) SDL provides platform detection
#define PLATFORM_WINDOWS __WIN32__
#define PLATFORM_LINUX __LINUX__

#define INFINITY __builtin_inff()
#define function static
#define global_variable static

#define BYTE 1
#define KILOBYTE 1024 * BYTE
#define MEGABYTE 1024 * KILOBYTE
#define GIGABYTE 1024 * MEGABYTE

// clang-format off
#define UNUSED(x) ((x)=(x))
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
typedef int32_t  b32;
typedef float    f32;

typedef enum CompletionStatus {
    CompletionStatus_Failure,
    CompletionStatus_Sucess,
} CompletionStatus;

//
// SECTION Memory
//

#if PLATFORM_WINDOWS

    #error unimlemented

#elif PLATFORM_LINUX
    #include <sys/mman.h>

void*
vmemAlloc(i32 size) {
    void* ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(ptr != MAP_FAILED);
    return ptr;
}

#endif

typedef struct ByteSlice {
    u8* ptr;
    i32 len;
} ByteSlice;

typedef void* (*AllocatorAllocAndZero)(void* data, i32 size, i32 align);

typedef struct Allocator {
    void*                 data;
    AllocatorAllocAndZero allocAndZero;
} Allocator;

#define allocStruct(allocator, type) (type*)(allocator).allocAndZero((allocator).data, sizeof(type), _Alignof(type))
#define allocArray(allocator, type, count) \
    (type*)(allocator).allocAndZero((allocator).data, sizeof(type) * (count), _Alignof(type))

ByteSlice
alignPtr(void* ptr, i32 size, i32 align) {
    assert(isPowerOf2(align));
    i32       offBy = ((usize)ptr) & (align - 1);
    i32       moveBy = offBy > 0 ? align - offBy : 0;
    ByteSlice result = {(u8*)ptr + moveBy, size + moveBy};
    return result;
}

typedef struct Arena {
    u8* base;
    i32 size;
    i32 used;
} Arena;

Arena
createArena(i32 size, Allocator allocator) {
    u8*   base = allocArray(allocator, u8, size);
    Arena arena = {.base = base, .size = size, .used = 0};
    return arena;
}

Arena
createArenaFromVmem(i32 size) {
    u8*   base = vmemAlloc(size);
    Arena arena = {.base = base, .size = size, .used = 0};
    return arena;
}

void*
arenaAllocAndZero(void* data, i32 size, i32 align) {
    Arena* arena = (Arena*)data;
    assert(arena->used <= arena->size);
    ByteSlice aligned = alignPtr(arena->base + arena->used, size, align);
    i32       freeSize = arena->size - arena->used;
    void*     result = 0;
    if (aligned.len <= freeSize) {
        arena->used += aligned.len;
        result = aligned.ptr;
        for (i32 index = 0; index < aligned.len; index++) {
            aligned.ptr[index] = 0;
        }
    }
    return result;
}

void*
arenaRealloc(void* data, void* ptr, i32 size, i32 align) {
    Arena* arena = (Arena*)data;
    void*  result = arenaAllocAndZero(arena, size, align);
    if (result && ptr) {
        SDL_memcpy(result, ptr, size);
    }
    return result;
}

Allocator
createArenaAllocator(Arena* arena) {
    Allocator allocator = {.data = arena, .allocAndZero = arenaAllocAndZero};
    return allocator;
}

global_variable volatile usize globalSDLMemoryUsed;

// NOTE(khvorov) SDL has these, it just doesn't expose them
void* dlmalloc(size_t);
void* dlcalloc(size_t, size_t);
void* dlrealloc(void*, size_t);
void  dlfree(void*);

void*
sdlMallocWrapper(usize size) {
    void* result = dlmalloc(size);
    if (result) {
        usize* sizeActual = (usize*)result - 1;
        atomic_fetch_add(&globalSDLMemoryUsed, *sizeActual);
    }
    return result;
}

void*
sdlCallocWrapper(usize n, usize size) {
    void* result = dlcalloc(n, size);
    if (result) {
        usize* sizeActual = (usize*)result - 1;
        atomic_fetch_add(&globalSDLMemoryUsed, *sizeActual);
    }
    return result;
}

void*
sdlReallocWrapper(void* ptr, usize size) {
    usize sizeBefore = 0;
    if (ptr) {
        sizeBefore = *((usize*)ptr - 1);
    }
    void* result = dlrealloc(ptr, size);
    if (result) {
        usize sizeAfter = *((usize*)result - 1);
        if (sizeAfter > sizeBefore) {
            atomic_fetch_add(&globalSDLMemoryUsed, sizeAfter - sizeBefore);
        } else {
            atomic_fetch_sub(&globalSDLMemoryUsed, sizeBefore - sizeAfter);
        }
    }
    return result;
}

void
sdlFreeWrapper(void* ptr) {
    if (ptr) {
        usize* size = (usize*)ptr - 1;
        atomic_fetch_sub(&globalSDLMemoryUsed, *size);
    }
    dlfree(ptr);
}

#include FT_CONFIG_CONFIG_H
#include <freetype/internal/ftdebug.h>
#include <freetype/internal/ftstream.h>
#include <freetype/ftsystem.h>
#include <freetype/fterrors.h>
#include <freetype/fttypes.h>

global_variable Arena globalFTArena;

FT_CALLBACK_DEF(void*)
ftAllocCustom(FT_Memory memory, long size) {
    UNUSED(memory);
    assert(size <= INT32_MAX);
    void* result = arenaAllocAndZero(&globalFTArena, size, 8);
    return result;
}

FT_CALLBACK_DEF(void*)
ftReallocCustom(FT_Memory memory, long curSize, long newSize, void* block) {
    UNUSED(memory);
    UNUSED(curSize);
    assert(newSize <= INT32_MAX);
    void* result = arenaRealloc(&globalFTArena, block, newSize, 8);
    return result;
}
FT_CALLBACK_DEF(void)
ftFreeCustom(FT_Memory memory, void* block) {
    // NOTE(khvorov) NOOP
    UNUSED(memory);
    UNUSED(block);
}

FT_BASE_DEF(FT_Memory)
FT_New_Memory(void) {
    FT_Memory memory = (FT_Memory)arenaAllocAndZero(&globalFTArena, sizeof(*memory), _Alignof(*memory));
    if (memory) {
        // NOTE(khvorov) This user pointer is very helpful and all but this
        // function doesn't take it, so freetype arena has to be a global anyway
        memory->user = NULL;
        memory->alloc = ftAllocCustom;
        memory->realloc = ftReallocCustom;
        memory->free = ftFreeCustom;
    }
    return memory;
}

FT_BASE_DEF(void)
FT_Done_Memory(FT_Memory memory) {
    UNUSED(memory);
    // NOTE(khvorov) NOOP
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
            SDL_Window* sdlWindow = SDL_CreateWindow("test", 0, 0, windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);
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

i32
drawMemRect(Renderer* renderer, i32 topleftX, i32 memUsed, i32 totalMemoryUsed, i32 height, Color color) {
    Rect2i memRect = {
        .x = topleftX,
        .y = 0,
        .w = (i32)((f32)memUsed / (f32)totalMemoryUsed * (f32)renderer->width + 0.5f),
        .h = height,
    };
    drawRect(renderer, memRect, color);
    i32 toprightX = memRect.x + memRect.w;
    return toprightX;
}

i32
drawArenaUsage(Renderer* renderer, i32 size, i32 used, i32 topleftX, i32 totalMemoryUsed, i32 height) {
    i32 toprightX = drawMemRect(renderer, topleftX, used, totalMemoryUsed, height, (Color) {.r = 100, .a = 255});
    toprightX = drawMemRect(renderer, toprightX, size - used, totalMemoryUsed, height, (Color) {.g = 100, .a = 255});
    return toprightX;
}

//
// SECTION Editor
//

function Rect2i
rect2iCenterDim(i32 centerX, i32 centerY, i32 dimX, i32 dimY) {
    assert(dimX >= 0 && dimY >= 0);
    Rect2i result = {.x = centerX - dimX / 2, .y = centerY - dimY / 2, .w = dimX, .h = dimY};
    return result;
}

// Position units are proportions of the screen
// Time is in ms (including for velocity)
typedef struct EditorState {
    bool showEntireFontTexture;
} EditorState;

function EditorState
createEditorState(void) {
    EditorState result = {
        .showEntireFontTexture = true,
    };
    return result;
}

function void
editorUpdateAndRender(EditorState* editorState, Renderer* renderer, Input* input) {
    UNUSED(input);
    // TODO(khvorov) Implement
    if (editorState->showEntireFontTexture) {
        drawEntireFontTexture(renderer);
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
    UNUSED(argc);
    UNUSED(argv);
    Arena     virtualArena = createArenaFromVmem(2 * MEGABYTE);
    Allocator virtualArenaAllocator = createArenaAllocator(&virtualArena);
    globalFTArena = createArena(1 * MEGABYTE, virtualArenaAllocator);
    SDL_SetMemoryFunctions(sdlMallocWrapper, sdlCallocWrapper, sdlReallocWrapper, sdlFreeWrapper);
    CreateRendererResult createRendererResult = createRenderer(virtualArenaAllocator);
    if (createRendererResult.success) {
        Renderer    renderer = createRendererResult.renderer;
        SDL_Window* sdlWindow = renderer.sdlWindow;
        Input       input = {0};
        EditorState editorState = createEditorState();
        bool        running = true;
        while (running) {
            inputBeginFrame(&input);

            SDL_Event event;
            assert(SDL_WaitEvent(&event) == 1);
            processEvent(sdlWindow, &event, &running, &input);
            while (SDL_PollEvent(&event)) {
                processEvent(sdlWindow, &event, &running, &input);
            }

            assert(renderBegin(&renderer) == CompletionStatus_Sucess);

            // TODO(khvorov) Visualize timings
            // TODO(khvorov) Hardware rendering?
            editorUpdateAndRender(&editorState, &renderer, &input);

            // NOTE(khvorov) Visualize memory usage
            {
                assert(globalSDLMemoryUsed <= INT32_MAX);
                i32 totalMemoryUsed = globalSDLMemoryUsed + virtualArena.size;
                i32 memRectHeight = 20;
                i32 toprightX = drawMemRect(
                    &renderer,
                    0,
                    globalSDLMemoryUsed,
                    totalMemoryUsed,
                    memRectHeight,
                    (Color) {.r = 100, .g = 100, .a = 255}
                );
                toprightX = drawArenaUsage(
                    &renderer,
                    globalFTArena.size,
                    globalFTArena.used,
                    toprightX,
                    totalMemoryUsed,
                    memRectHeight
                );
                toprightX = drawArenaUsage(
                    &renderer,
                    virtualArena.size - globalFTArena.size,
                    virtualArena.used - globalFTArena.size,
                    toprightX,
                    totalMemoryUsed,
                    memRectHeight
                );
            }

            renderEnd(&renderer);
        }
    }
    return 0;
}
