#include <stdbool.h>
#include <stdatomic.h>

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftbitmap.h>

#include <SDL.h>

#include <pango/pangoft2.h>

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

function void*
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

function ByteSlice
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

function Arena
createArena(i32 size, Allocator allocator) {
    u8*   base = allocArray(allocator, u8, size);
    Arena arena = {.base = base, .size = size, .used = 0};
    return arena;
}

function Arena
createArenaFromVmem(i32 size) {
    u8*   base = vmemAlloc(size);
    Arena arena = {.base = base, .size = size, .used = 0};
    return arena;
}

function void*
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

function void*
arenaRealloc(void* data, void* ptr, i32 size, i32 align) {
    Arena* arena = (Arena*)data;
    void*  result = arenaAllocAndZero(arena, size, align);
    if (result && ptr) {
        SDL_memcpy(result, ptr, size);
    }
    return result;
}

function Allocator
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

function void*
sdlMallocWrapper(usize size) {
    void* result = dlmalloc(size);
    if (result) {
        usize* sizeActual = (usize*)result - 1;
        atomic_fetch_add(&globalSDLMemoryUsed, *sizeActual);
    }
    return result;
}

function void*
sdlCallocWrapper(usize n, usize size) {
    void* result = dlcalloc(n, size);
    if (result) {
        usize* sizeActual = (usize*)result - 1;
        atomic_fetch_add(&globalSDLMemoryUsed, *sizeActual);
    }
    return result;
}

function void*
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

function void
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

// NOTE(khvorov) Freetype is single-threaded
global_variable usize globalFTMemoryUsed;

FT_CALLBACK_DEF(void*)
ftAllocCustom(FT_Memory memory, long size) {
    UNUSED(memory);
    void* result = dlmalloc(size);
    if (result) {
        globalFTMemoryUsed += *((usize*)result - 1);
    }
    return result;
}

FT_CALLBACK_DEF(void*)
ftReallocCustom(FT_Memory memory, long curSize, long newSize, void* block) {
    UNUSED(memory);
    UNUSED(curSize);
    if (block) {
        globalFTMemoryUsed -= *((usize*)block - 1);
    }
    void* result = dlrealloc(block, newSize);
    if (result) {
        globalFTMemoryUsed += *((usize*)result - 1);
    }
    return result;
}

FT_CALLBACK_DEF(void)
ftFreeCustom(FT_Memory memory, void* block) {
    UNUSED(memory);
    globalFTMemoryUsed -= *((usize*)block - 1);
    dlfree(block);
}

FT_BASE_DEF(FT_Memory)
FT_New_Memory(void) {
    FT_Memory memory = (FT_Memory)dlmalloc(sizeof(*memory));
    globalFTMemoryUsed += *((usize*)memory - 1);
    if (memory) {
        // NOTE(khvorov) This user pointer is very helpful and all but this
        // function doesn't take it, so freetype data has to be a global anyway
        memory->user = NULL;
        memory->alloc = ftAllocCustom;
        memory->realloc = ftReallocCustom;
        memory->free = ftFreeCustom;
    }
    return memory;
}

FT_BASE_DEF(void)
FT_Done_Memory(FT_Memory memory) {
    globalFTMemoryUsed -= *((usize*)memory - 1);
    dlfree(memory);
}

global_variable Arena globalTempArena;

//
// SECTION Strings
//

typedef struct String {
    char* ptr;
    i32   len;
} String;

function String
stringFromCstring(char* cstring) {
    usize len = SDL_strlen(cstring);
    assert(len <= SDL_MAX_SINT32);
    String str = {.ptr = cstring, .len = (i32)len};
    return str;
}

function String
fmtTempString(char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char* ptr = (char*)(globalTempArena.base + globalTempArena.used);
    i32   len = SDL_vsnprintf(ptr, globalTempArena.size - globalTempArena.used, fmt, ap);
    globalTempArena.used += len;
    va_end(ap);
    String result = {ptr, len};
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

#include "fontdata.c"

typedef struct Glyph {
    i32 width, height, pitch;
    i32 offsetX, offsetY;
    i32 advanceX;
} Glyph;

typedef struct Font {
    FT_Face ftFace;
    i32     lineHeight;
    i32     fontHeight;
    Glyph   loadedGlyph;
    u32*    loadedGlyphPx;
    i32     loadedGlyphPxWidth, loadedGlyphPxHeight;
} Font;

function CompletionStatus
loadGlyph(Font* font, u32 ch) {
    CompletionStatus result = CompletionStatus_Failure;
    u32              ftGlyphIndex = FT_Get_Char_Index(font->ftFace, ch);
    FT_Error         loadGlyphResult = FT_Load_Glyph(font->ftFace, ftGlyphIndex, FT_LOAD_DEFAULT);
    if (loadGlyphResult == FT_Err_Ok) {
        if (font->ftFace->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
            FT_Error renderGlyphResult = FT_Render_Glyph(font->ftFace->glyph, FT_RENDER_MODE_NORMAL);
            if (renderGlyphResult == FT_Err_Ok) {
                FT_GlyphSlot ftGlyph = font->ftFace->glyph;
                FT_Bitmap    bm = ftGlyph->bitmap;

                font->loadedGlyph = (Glyph) {
                    .advanceX = ftGlyph->advance.x >> 6,
                    .width = bm.width,
                    .height = bm.rows,
                    .pitch = bm.pitch,
                    .offsetX = ftGlyph->bitmap_left,
                    .offsetY = font->fontHeight - ftGlyph->bitmap_top,
                };

                for (i32 bmRow = 0; bmRow < (i32)bm.rows; bmRow++) {
                    for (i32 bmCol = 0; bmCol < (i32)bm.width; bmCol++) {
                        i32 bmIndex = bmRow * bm.pitch + bmCol;
                        i32 alpha = bm.buffer[bmIndex];
                        i32 fullColorRGBA = 0xFFFFFF00 | alpha;
                        i32 bufIndex = bmRow * font->loadedGlyphPxWidth + bmCol;
                        font->loadedGlyphPx[bufIndex] = fullColorRGBA;
                    }
                }

                result = CompletionStatus_Sucess;
            };
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
                i32  lineHeight = FT_MulFix(ftFace->height, ftFace->size->metrics.y_scale) >> 6;
                i32  pxBufferWidth = 100;
                i32  pxBufferHeight = 100;
                u32* pxBuffer = allocArray(allocator, u32, pxBufferWidth * pxBufferWidth);
                Font font = {
                    .ftFace = ftFace,
                    .lineHeight = lineHeight,
                    .fontHeight = fontHeight,
                    .loadedGlyphPx = pxBuffer,
                    .loadedGlyphPxWidth = pxBufferWidth,
                    .loadedGlyphPxHeight = pxBufferHeight,
                };
                result = (LoadFontResult) {.success = true, .font = font};
            }
        }
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

function Clock
getCurrentClock() {
    Clock result = {.countsPerSecond = SDL_GetPerformanceFrequency(), .counter = SDL_GetPerformanceCounter()};
    return result;
}

function f32
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
                    // TODO(khvorov) Fix this texture
                    SDL_Texture* sdlTexture =
                        SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, 100, 100);
                    if (sdlTexture) {
                        int setTexBlendModeResult = SDL_SetTextureBlendMode(sdlTexture, SDL_BLENDMODE_BLEND);
                        if (setTexBlendModeResult == 0) {
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
    SDL_Rect destRect = {.y = 150, .w = renderer->font.loadedGlyphPxWidth, .h = renderer->font.loadedGlyphPxHeight};
    int      renderCopyResult = SDL_RenderCopy(renderer->sdlRenderer, renderer->sdlFontTexture, 0, &destRect);
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

typedef struct OutlineRects {
    Rect2i rects[4];
} OutlineRects;

function OutlineRects
getOutlineRects(Rect2i rect, i32 thickness) {
    assert(rect.h >= 0 && rect.w >= 0);
    Rect2i top = rect;
    top.h = thickness;

    Rect2i bottom = top;
    bottom.y += rect.h - thickness;

    Rect2i right = rect;
    right.w = thickness;

    Rect2i left = right;
    left.x += rect.w - thickness;

    OutlineRects result = {{top, bottom, left, right}};
    return result;
}

function void
drawRectOutline(Renderer* renderer, Rect2i rect, Color color, i32 thickness) {
    assert(rect.w >= 0 && rect.h >= 0);
    OutlineRects outline = getOutlineRects(rect, thickness);
    for (usize index = 0; index < arrayLen(outline.rects); index++) {
        Rect2i outlineRect = outline.rects[index];
        drawRect(renderer, outlineRect, color);
    }
}

function CompletionStatus
drawGlyph(Renderer* renderer, u32 ch, i32 topleftX, i32 topleftY) {
    CompletionStatus result = CompletionStatus_Failure;
    CompletionStatus glyphResult = loadGlyph(&renderer->font, ch);
    if (glyphResult == CompletionStatus_Sucess) {
        Glyph glyph = renderer->font.loadedGlyph;
        int   updateTexResult = SDL_UpdateTexture(
            renderer->sdlFontTexture,
            0,
            renderer->font.loadedGlyphPx,
            renderer->font.loadedGlyphPxWidth * sizeof(u32)
        );
        if (updateTexResult == 0) {
            SDL_Rect texRect = {.w = glyph.width, .h = glyph.height};
            SDL_Rect destRect =
                {.x = topleftX + glyph.offsetX, .y = topleftY + glyph.offsetY, .w = glyph.width, .h = glyph.height};
            int copyResult = SDL_RenderCopy(renderer->sdlRenderer, renderer->sdlFontTexture, &texRect, &destRect);
            if (copyResult == 0) {
                result = CompletionStatus_Sucess;
            }
        }
    }
    return result;
}

typedef struct UtfIter {
    String str;
    i32    read;
    u32    lastCh;
} UtfIter;

function CompletionStatus
utfIterNext(UtfIter* iter) {
    u32 result = CompletionStatus_Failure;
    if (iter->read < iter->str.len) {
        char* charBytes = iter->str.ptr + iter->read;
        u8    firstByte = charBytes[0];
        i32   leading1sCount = firstByte ? __builtin_clz(~(firstByte << 24)) : 0;
        i32   mask = (1 << (8 - leading1sCount)) - 1;
        u32   value = firstByte & mask;
        i32   extraBytesLeft = leading1sCount == 0 ? 0 : leading1sCount - 1;
        iter->read += 1 + extraBytesLeft;
        for (u8 byte = charBytes[leading1sCount - extraBytesLeft]; extraBytesLeft > 0;
             byte = charBytes[leading1sCount - (--extraBytesLeft)]) {
            value <<= 6;
            value += (byte & 0x3F);
        }
        iter->lastCh = value;
        result = CompletionStatus_Sucess;
    }
    return result;
}

function UtfIter
createUtfIter(String str) {
    UtfIter iter = {.str = str};
    return iter;
}

function void
drawTextline(Renderer* renderer, String text, Rect2i rect) {
    i32 curTopleftX = rect.x;  // + rect.w / 2 - textlineWidth / 2;
    i32 topleftY = rect.y + (rect.h - renderer->font.lineHeight) / 2;
    for (UtfIter iter = createUtfIter(text); utfIterNext(&iter) == CompletionStatus_Sucess;) {
        u32              ch = iter.lastCh;
        CompletionStatus glyphResult = drawGlyph(renderer, ch, curTopleftX, topleftY);
        if (glyphResult == CompletionStatus_Sucess) {
            curTopleftX += renderer->font.loadedGlyph.advanceX;
        }
    }
}

function i32
drawMemRect(
    Renderer* renderer,
    i32       topleftX,
    i32       topleftXText,
    i32       topleftYTextMultiplier,
    i32       memUsed,
    i32       totalMemoryUsed,
    i32       height,
    Color     color,
    String    name
) {
    Rect2i memRect = {
        .x = topleftX,
        .y = 0,
        .w = (i32)((f32)memUsed / (f32)totalMemoryUsed * (f32)renderer->width + 0.5f),
        .h = height,
    };
    drawRect(renderer, memRect, color);
    drawRectOutline(renderer, memRect, (Color) {.a = 255}, 1);

    // NOTE(khvorov) Draw memory usage
    {
        char* sizes[] = {"B", "KB", "MB", "GB", "TB"};
        f32   sizeSmallEnough = (f32)memUsed;
        usize divisions = 0;
        while (sizeSmallEnough > 1024.0f && divisions < arrayLen(sizes) - 1) {
            sizeSmallEnough /= 1024.0f;
            divisions++;
        }
        String memsizeString = fmtTempString("%s: %.1f%s", name.ptr, sizeSmallEnough, sizes[divisions]);

        Rect2i textRect =
            {.x = topleftXText, .y = memRect.y + memRect.h * topleftYTextMultiplier, .w = memRect.w, .h = memRect.h};
        drawTextline(renderer, memsizeString, textRect);
    }

    i32 toprightX = memRect.x + memRect.w;
    return toprightX;
}

function i32
drawArenaUsage(Renderer* renderer, i32 size, i32 used, i32 topleftX, i32 totalMemoryUsed, i32 height, String name) {
    i32 toprightX = drawMemRect(
        renderer,
        topleftX,
        topleftX,
        1,
        used,
        totalMemoryUsed,
        height,
        (Color) {.r = 100, .a = 255},
        fmtTempString("%s used", name)
    );
    toprightX = drawMemRect(
        renderer,
        toprightX,
        topleftX,
        2,
        size - used,
        totalMemoryUsed,
        height,
        (Color) {.g = 100, .a = 255},
        fmtTempString("%s free", name)
    );
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
    i32 tmp;
} EditorState;

function EditorState
createEditorState(void) {
    EditorState result = {0};
    return result;
}

function void
editorUpdateAndRender(EditorState* editorState, Renderer* renderer, Input* input) {
    UNUSED(editorState);
    UNUSED(input);
    UNUSED(renderer);
    // TODO(khvorov) Implement
    Rect2i editorRect = {.x = 0, .y = 50, .w = renderer->width, .h = renderer->height};
    drawTextline(renderer, stringFromCstring("Mārtiņš Možeiko"), editorRect);
    // drawEntireFontTexture(renderer);
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

    Arena     virtualArena = createArenaFromVmem(3 * MEGABYTE);
    Allocator virtualArenaAllocator = createArenaAllocator(&virtualArena);
    globalTempArena = createArena(1 * MEGABYTE, virtualArenaAllocator);
    SDL_SetMemoryFunctions(sdlMallocWrapper, sdlCallocWrapper, sdlReallocWrapper, sdlFreeWrapper);

    PangoFontMap*         pangoFontMap = pango_ft2_font_map_new();
    PangoContext*         pangoCtx = pango_font_map_create_context(pangoFontMap);
    PangoLayout*          pangoLayout = pango_layout_new(pangoCtx);
    PangoFontDescription* pangoFontDescription = pango_font_description_from_string("Sans 12");
    pango_layout_set_font_description(pangoLayout, pangoFontDescription);
    pango_layout_set_text(pangoLayout, "من 6,000 عام", -1);

    i32       tmpBmWidth = 300;
    i32       tmpBmHeight = 300;
    FT_Bitmap ftBitmap = {
        .rows = tmpBmHeight,
        .width = tmpBmWidth,
        .pitch = tmpBmHeight,
        .buffer = allocArray(virtualArenaAllocator, u8, tmpBmWidth * tmpBmHeight),
        // .num_grays = 255,
        // .pixel_mode = FT_PIXEL_MODE_GRAY,
        // .palette_mode = 0,
        // .palette = 0,
    };
    // FT_Bitmap_Init(&ftBitmap);
    pango_ft2_render_layout(&ftBitmap, pangoLayout, 0, 0);

    u32* tmpBmPx = allocArray(virtualArenaAllocator, u32, tmpBmWidth * tmpBmHeight);
    {
        for (i32 bmRow = 0; bmRow < (i32)ftBitmap.rows; bmRow++) {
            for (i32 bmCol = 0; bmCol < (i32)ftBitmap.width; bmCol++) {
                i32 bmIndex = bmRow * ftBitmap.pitch + bmCol;
                i32 alpha = ftBitmap.buffer[bmIndex];
                i32 fullColorRGBA = 0xFFFFFF00 | alpha;
                i32 bufIndex = bmRow * tmpBmWidth + bmCol;
                tmpBmPx[bufIndex] = fullColorRGBA;
                // tmpBmPx[bufIndex] = 0xffffffff;
            }
        }
    }

    CreateRendererResult createRendererResult = createRenderer(virtualArenaAllocator);
    if (createRendererResult.success) {
        Renderer    renderer = createRendererResult.renderer;
        SDL_Window* sdlWindow = renderer.sdlWindow;
        Input       input = {0};
        EditorState editorState = createEditorState();

        SDL_Texture* tmpSdlTex = SDL_CreateTexture(
            renderer.sdlRenderer,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_STATIC,
            tmpBmWidth,
            tmpBmHeight
        );
        SDL_SetTextureBlendMode(tmpSdlTex, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(tmpSdlTex, 0, tmpBmPx, tmpBmWidth * sizeof(u32));

        bool running = true;
        while (running) {
            globalTempArena.used = 0;
            inputBeginFrame(&input);

            // NOTE(khvorov) Process one event at a time since input doesn't store the order they come in in
            SDL_Event event;
            assert(SDL_WaitEvent(&event) == 1);
            processEvent(sdlWindow, &event, &running, &input);

            assert(renderBegin(&renderer) == CompletionStatus_Sucess);

            // TODO(khvorov) Visualize timings
            // TODO(khvorov) Hardware rendering?
            editorUpdateAndRender(&editorState, &renderer, &input);

            // NOTE(khvorov) Visualize memory usage
            {
                assert(globalSDLMemoryUsed <= INT32_MAX);
                i32 totalMemoryUsed = globalSDLMemoryUsed + globalFTMemoryUsed + virtualArena.size;
                i32 memRectHeight = 20;
                i32 toprightX = drawMemRect(
                    &renderer,
                    0,
                    0,
                    1,
                    globalSDLMemoryUsed,
                    totalMemoryUsed,
                    memRectHeight,
                    (Color) {.r = 100, .g = 100, .a = 255},
                    stringFromCstring("SDL")
                );

                toprightX = drawMemRect(
                    &renderer,
                    toprightX,
                    0,
                    2,
                    globalFTMemoryUsed,
                    totalMemoryUsed,
                    memRectHeight,
                    (Color) {.r = 100, .b = 100, .a = 255},
                    stringFromCstring("FT")
                );

                toprightX = drawArenaUsage(
                    &renderer,
                    globalTempArena.size,
                    globalTempArena.used,
                    toprightX,
                    totalMemoryUsed,
                    memRectHeight,
                    stringFromCstring("TMP")
                );

                toprightX = drawArenaUsage(
                    &renderer,
                    virtualArena.size - globalTempArena.size,
                    virtualArena.used - globalTempArena.size,
                    toprightX,
                    totalMemoryUsed,
                    memRectHeight,
                    stringFromCstring("REST")
                );

                // NOTE(khvorov) Temp texture
                {
                    SDL_Rect texRect = {.w = tmpBmWidth, .h = tmpBmHeight};
                    SDL_Rect destRect = {.x = 500, .y = 500, .w = tmpBmWidth, .h = tmpBmHeight};
                    SDL_RenderCopy(renderer.sdlRenderer, tmpSdlTex, &texRect, &destRect);
                }
            }

            renderEnd(&renderer);
        }
    }
    return 0;
}
