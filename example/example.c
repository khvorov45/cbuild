#include <stdbool.h>

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftbitmap.h>

#include <hb.h>
#include <hb-ft.h>
#include <hb-icu.h>

#include <unicode/uscript.h>

#include <fribidi.h>

#include <SDL.h>

// NOTE(khvorov) SDL provides platform detection
#define PLATFORM_WINDOWS __WIN32__
#define PLATFORM_LINUX __LINUX__

#if PLATFORM_WINDOWS
    #error unimlemented
#elif PLATFORM_LINUX
    #include <sys/mman.h>
    #include <fontconfig/fontconfig.h>
#endif

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
#define arenaAllocArray(arena, type, count) (type*)(arenaAllocAndZero(arena, sizeof(type) * (count), _Alignof(type)))
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

function void*
vmemAlloc(i32 size) {
#if PLATFORM_WINDOWS
    #error unimplemented
#elif PLATFORM_LINUX
    void* ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(ptr != MAP_FAILED);
    return ptr;
#endif
}

bool
memeq(void* ptr1, void* ptr2, i32 bytes) {
    assert(bytes >= 0);
    u8 *l = ptr1, *r = ptr2;
    i32 left = bytes;
    for (; left > 0 && *l == *r; left--, l++, r++) {}
    bool result = left == 0;
    return result;
}

typedef struct ByteSlice {
    u8* ptr;
    i32 len;
} ByteSlice;

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
    i32 tempCount;
} Arena;

function Arena
createArena(u8* base, i32 size) {
    assert(base && size >= 0);
    Arena arena = {.base = base, .size = size};
    return arena;
}

function Arena
createArenaFromVmem(i32 size) {
    assert(size >= 0);
    u8*   base = vmemAlloc(size);
    Arena arena = createArena(base, size);
    return arena;
}

function void*
arenaAllocAndZero(Arena* arena, i32 size, i32 align) {
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

typedef struct TempMemory {
    Arena* arena;
    i32    usedWhenBegan;
} TempMemory;

function TempMemory
arenaBeginTemp(Arena* arena) {
    assert(arena->tempCount < INT32_MAX && arena->tempCount >= 0);
    arena->tempCount += 1;
    TempMemory tempMemory = {.arena = arena, .usedWhenBegan = arena->used};
    return tempMemory;
}

function void
endTempMemory(TempMemory tempMemory) {
    Arena* arena = tempMemory.arena;
    assert(arena->tempCount > 0);
    arena->tempCount -= 1;
    arena->used = tempMemory.usedWhenBegan;
}

// NOTE(khvorov) SDL has these, it just doesn't expose them
void* dlmalloc(size_t);
void* dlcalloc(size_t, size_t);
void* dlrealloc(void*, size_t);
void  dlfree(void*);

typedef struct GeneralPurposeAllocatorData {
    usize used;
} GeneralPurposeAllocatorData;

function usize
getSizeFromMallocPtr(void* ptr) {
    usize result = 0;
    if (ptr) {
        result = *((usize*)ptr - 1);
    }
    return result;
}

function void
gpaAddSizeFromMallocPtr(GeneralPurposeAllocatorData* gpa, void* ptr) {
    usize size = getSizeFromMallocPtr(ptr);
    assert(size <= UINTPTR_MAX - gpa->used);
    gpa->used += size;
}

function void
gpaSubSizeFromMallocPtr(GeneralPurposeAllocatorData* gpa, void* ptr) {
    usize size = getSizeFromMallocPtr(ptr);
    assert(gpa->used >= size);
    gpa->used -= size;
}

function void*
gpaAlloc(GeneralPurposeAllocatorData* gpa, usize size) {
    void* result = dlmalloc(size);
    gpaAddSizeFromMallocPtr(gpa, result);
    return result;
}

function void*
gpaAllocAndZero(GeneralPurposeAllocatorData* gpa, usize size) {
    void* result = dlcalloc(size, 1);
    gpaAddSizeFromMallocPtr(gpa, result);
    return result;
}

function void*
gpaRealloc(GeneralPurposeAllocatorData* gpa, void* ptr, usize size) {
    gpaSubSizeFromMallocPtr(gpa, ptr);
    void* result = dlrealloc(ptr, size);
    gpaAddSizeFromMallocPtr(gpa, result);
    return result;
}

function void
gpaFree(GeneralPurposeAllocatorData* gpa, void* ptr) {
    gpaSubSizeFromMallocPtr(gpa, ptr);
    dlfree(ptr);
}

global_variable GeneralPurposeAllocatorData globalGPADataSDL;

function void*
sdlCustomMalloc(usize size) {
    void* result = gpaAlloc(&globalGPADataSDL, size);
    return result;
}

function void*
sdlCustomCalloc(usize n, usize size) {
    void* result = gpaAllocAndZero(&globalGPADataSDL, n * size);
    return result;
}

function void*
sdlCustomRealloc(void* ptr, usize size) {
    void* result = gpaRealloc(&globalGPADataSDL, ptr, size);
    return result;
}

function void
sdlCustomFree(void* ptr) {
    gpaFree(&globalGPADataSDL, ptr);
}

global_variable GeneralPurposeAllocatorData globalGPADataHB;

void*
hb_malloc_impl(usize size) {
    void* result = gpaAlloc(&globalGPADataHB, size);
    return result;
}

void*
hb_calloc_impl(usize n, usize size) {
    void* result = gpaAllocAndZero(&globalGPADataHB, n * size);
    return result;
}

void*
hb_realloc_impl(void* ptr, usize size) {
    void* result = gpaRealloc(&globalGPADataHB, ptr, size);
    return result;
}

void
hb_free_impl(void* ptr) {
    gpaFree(&globalGPADataHB, ptr);
}

#include FT_CONFIG_CONFIG_H
#include <freetype/internal/ftdebug.h>
#include <freetype/internal/ftstream.h>
#include <freetype/ftsystem.h>
#include <freetype/fterrors.h>
#include <freetype/fttypes.h>

global_variable GeneralPurposeAllocatorData globalGPADataFT;

FT_CALLBACK_DEF(void*)
ftCustomAlloc(FT_Memory memory, long size) {
    UNUSED(memory);
    void* result = gpaAlloc(&globalGPADataFT, size);
    return result;
}

FT_CALLBACK_DEF(void*)
ftCustomRealloc(FT_Memory memory, long curSize, long newSize, void* block) {
    UNUSED(memory);
    UNUSED(curSize);
    void* result = gpaRealloc(&globalGPADataFT, block, newSize);
    return result;
}

FT_CALLBACK_DEF(void)
ftCustomFree(FT_Memory memory, void* block) {
    UNUSED(memory);
    gpaFree(&globalGPADataFT, block);
}

FT_BASE_DEF(FT_Memory)
FT_New_Memory(void) {
    FT_Memory memory = (FT_Memory)gpaAlloc(&globalGPADataFT, sizeof(*memory));
    if (memory) {
        // NOTE(khvorov) This user pointer is very helpful and all but this
        // function doesn't take it, so freetype data has to be a global anyway
        memory->user = NULL;
        memory->alloc = ftCustomAlloc;
        memory->realloc = ftCustomRealloc;
        memory->free = ftCustomFree;
    }
    return memory;
}

FT_BASE_DEF(void)
FT_Done_Memory(FT_Memory memory) {
    gpaFree(&globalGPADataFT, memory);
}

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
stringFmt(Arena* arena, char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char* ptr = (char*)(arena->base + arena->used);
    i32   len = SDL_vsnprintf(ptr, arena->size - arena->used, fmt, ap);
    arena->used += len + 1;  // NOTE(khvorov) Null terminator
    va_end(ap);
    String result = {ptr, len};
    return result;
}

bool
streq(String str1, String str2) {
    bool result = false;
    if (str1.len == str2.len) {
        result = memeq(str1.ptr, str2.ptr, str1.len);
    }
    return result;
}

//
// SECTION Font
//

typedef struct Glyph {
    i32 width, height, pitch;
    i32 offsetX, offsetY;
    i32 advanceX;
} Glyph;

typedef struct Font {
    FT_Face    ftFace;
    hb_font_t* hbFont;
    String     path;
    ByteSlice  fileContents;
    i32        lineHeight;
    i32        fontHeight;
} Font;

typedef struct FontManager {
    FcConfig*    fcConfig;
    FcPattern*   fcPattern;
    FT_Library   ftLib;
    Font*        fonts;
    hb_buffer_t* hbBuf;
#if PLATFORM_WINDOWS
    #error unimplemented;
#elif PLATFORM_LINUX
    FcCharSet** fcCharSets;
#endif
} FontManager;

typedef struct LoadFontResult {
    bool success;
    Font font;
} LoadFontResult;

function LoadFontResult
loadFont(FT_Library ftLib, String path, ByteSlice fileContents) {
    LoadFontResult result = {0};
    FT_Face        ftFace = 0;
    FT_Error       ftFaceResult = FT_New_Memory_Face(ftLib, fileContents.ptr, fileContents.len, 0, &ftFace);
    if (ftFaceResult == FT_Err_Ok) {
        i32      fontHeight = 18;
        FT_Error ftSetPxSizeResult = FT_Set_Pixel_Sizes(ftFace, 0, fontHeight);
        if (ftSetPxSizeResult == FT_Err_Ok) {
            i32        lineHeight = FT_MulFix(ftFace->height, ftFace->size->metrics.y_scale) >> 6;
            hb_font_t* hbFont = hb_ft_font_create_referenced(ftFace);

            Font font = {
                .ftFace = ftFace,
                .hbFont = hbFont,
                .path = path,
                .fileContents = fileContents,
                .lineHeight = lineHeight,
                .fontHeight = fontHeight,
            };
            result = (LoadFontResult) {.success = true, .font = font};
        }
    }
    return result;
}

function void
unloadFont(Font* font) {
    if (font->ftFace) {
        hb_font_destroy(font->hbFont);
        FT_Error doneResult = FT_Done_Face(font->ftFace);
        assert(doneResult == FT_Err_Ok);
        SDL_free(font->fileContents.ptr);
    }
    *font = (Font) {0};
}

typedef struct GetFontResult {
    bool  success;
    Font* font;
} GetFontResult;

// This has some limitations:
// - may read the same file twice if it supports multiple scripts
// - treats USCRIPT_COMMON (numbers, punctuation, etc) as a distinct script
function GetFontResult
getFontForScriptAndUtf32Chars(FontManager* fontManager, UScriptCode script, FriBidiChar* chars, i32 chCount) {
    GetFontResult result = {0};

#if PLATFORM_WINDOWS

    #error unimplemented

#elif PLATFORM_LINUX
    if (script >= 0 && script < USCRIPT_CODE_LIMIT) {
        Font*      font = fontManager->fonts + script;
        FcCharSet* fcCharSet = fontManager->fcCharSets[script];

        for (i32 charIndex = 0; charIndex < chCount; charIndex++) {
            FriBidiChar ch = chars[charIndex];
            if (!FcCharSetHasChar(fcCharSet, ch)) {
                FcCharSetAddChar(fcCharSet, ch);
            }
        }

        FcPatternDel(fontManager->fcPattern, FC_CHARSET);
        FcPatternAddCharSet(fontManager->fcPattern, FC_CHARSET, fcCharSet);

        FcResult   matchResult = FcResultNoMatch;
        FcPattern* matchFont = FcFontMatch(fontManager->fcConfig, fontManager->fcPattern, &matchResult);

        char* matchFontFilename = 0;
        FcPatternGetString(matchFont, FC_FILE, 0, (FcChar8**)&matchFontFilename);

        String fontpath = stringFromCstring(matchFontFilename);
        if (font->ftFace == 0 || !streq(font->path, fontpath)) {
            usize fileSize = 0;
            void* fileContents = SDL_LoadFile(matchFontFilename, &fileSize);
            assert(fileSize <= INT32_MAX);
            ByteSlice      fontFileContents = {fileContents, fileSize};
            LoadFontResult loadFontResult = loadFont(fontManager->ftLib, fontpath, fontFileContents);
            if (loadFontResult.success) {
                unloadFont(font);
                *font = loadFontResult.font;
            }
        }

        if (font->ftFace) {
            result = (GetFontResult) {.success = true, .font = font};
        }
    }
#endif

    return result;
}

typedef struct CreateFontManagerResult {
    bool        success;
    FontManager fontManager;
} CreateFontManagerResult;

function CreateFontManagerResult
createFontManager(Arena* arena) {
    CreateFontManagerResult result = {0};
    FT_Library              ftLib = 0;
    FT_Error                ftInitResult = FT_Init_FreeType(&ftLib);
    if (ftInitResult == FT_Err_Ok) {
        i32         fontCount = USCRIPT_CODE_LIMIT;
        FontManager fontManager = {
            .fcConfig = FcInitLoadConfigAndFonts(),
            .fcPattern = FcPatternCreate(),
            .ftLib = ftLib,
            .fonts = arenaAllocArray(arena, Font, fontCount),
            .fcCharSets = arenaAllocArray(arena, FcCharSet*, fontCount),
            .hbBuf = hb_buffer_create(),
        };
        for (i32 fontIndex = 0; fontIndex < fontCount; fontIndex++) {
            fontManager.fcCharSets[fontIndex] = FcCharSetCreate();
        }

        FcPatternAddInteger(fontManager.fcPattern, FC_WEIGHT, FC_WEIGHT_MEDIUM);
        FcPatternAddInteger(fontManager.fcPattern, FC_SLANT, FC_SLANT_ROMAN);

        FcConfigSubstitute(fontManager.fcConfig, fontManager.fcPattern, FcMatchPattern);
        FcDefaultSubstitute(fontManager.fcPattern);

        FriBidiChar ascii[] = {
            ' ', '!', '"', '#',  '$', '%', '&', '\'', '(', ')', '*', '+', ',', '-', '.', '/', '0', '1', '2',
            '3', '4', '5', '6',  '7', '8', '9', ':',  ';', '<', '=', '>', '?', '@', 'A', 'B', 'C', 'D', 'E',
            'F', 'G', 'H', 'I',  'J', 'K', 'L', 'M',  'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
            'Y', 'Z', '[', '\\', ']', '^', '_', '`',  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k',
            'l', 'm', 'n', 'o',  'p', 'q', 'r', 's',  't', 'u', 'v', 'w', 'x', 'y', 'z', '{', '|', '}', '~',
        };
        GetFontResult getDefaultFontResult =
            getFontForScriptAndUtf32Chars(&fontManager, USCRIPT_LATIN, ascii, arrayLen(ascii));

        if (getDefaultFontResult.success) {
            result = (CreateFontManagerResult) {.success = true, .fontManager = fontManager};
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
    i32           fontTexWidth, fontTexHeight;
    FontManager   fontManager;
    i32           width, height;
    Arena*        arena;
} Renderer;

typedef struct CreateRendererResult {
    bool     success;
    Renderer renderer;
} CreateRendererResult;

function CreateRendererResult
createRenderer(Arena* arena) {
    CreateRendererResult    result = {0};
    CreateFontManagerResult createFontManagerResult = createFontManager(arena);
    if (createFontManagerResult.success) {
        FontManager fontManager = createFontManagerResult.fontManager;
        i32         windowWidth = 1000;
        i32         windowHeight = 1000;
        SDL_Window* sdlWindow = SDL_CreateWindow("example", 0, 0, windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);
        if (sdlWindow) {
            SDL_Renderer* sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_PRESENTVSYNC);
            if (sdlRenderer) {
                i32          texW = 100;
                i32          texH = 100;
                SDL_Texture* sdlTexture =
                    SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, texW, texH);

                if (sdlTexture) {
                    int setTexBlendModeResult = SDL_SetTextureBlendMode(sdlTexture, SDL_BLENDMODE_BLEND);
                    if (setTexBlendModeResult == 0) {
                        Renderer renderer = {
                            .sdlWindow = sdlWindow,
                            .sdlRenderer = sdlRenderer,
                            .fontManager = fontManager,
                            .fontTexWidth = texW,
                            .fontTexHeight = texH,
                            .sdlFontTexture = sdlTexture,
                            .width = windowWidth,
                            .height = windowHeight,
                            .arena = arena,
                        };
                        result = (CreateRendererResult) {.success = true, .renderer = renderer};
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
    SDL_Rect         destRect = {.y = 150, .w = renderer->fontTexWidth, .h = renderer->fontTexHeight};
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

// Main limitation here is that it doesn't tolerate errors and will just trigger an assert if anything goes wrong
function void
drawTextline(Renderer* renderer, String text, i32 leftX, i32 topY, Color color) {
    if (text.len > 0) {
        TempMemory tempMem = arenaBeginTemp(renderer->arena);

        // NOTE(khvorov) Convert to UTF32 from UTF8
        FriBidiChar* ogStringUtf32 = arenaAllocArray(renderer->arena, FriBidiChar, text.len + 1);
        i32          utf32Length = 0;
        {
            u32 codepoint = 0;
            i32 textOffset = 0;
            while (textOffset < text.len) {
                U8_NEXT(text.ptr, textOffset, text.len, codepoint);
                ogStringUtf32[utf32Length++] = codepoint;
            }
        }

        // NOTE(khvorov) Break string up into segments where the script doesn't change
        i32 curTopY = topY;
        i32 curLeftX = leftX;
        for (i32 curOffset = 0; curOffset < utf32Length;) {
            UErrorCode  icuError = U_ZERO_ERROR;
            UScriptCode curIcuScript = uscript_getScript(ogStringUtf32[curOffset], &icuError);
            i32         nextOffset = curOffset + 1;
            while (nextOffset < utf32Length) {
                UScriptCode nextIcuScript = uscript_getScript(ogStringUtf32[nextOffset], &icuError);
                if (nextIcuScript != curIcuScript && nextIcuScript != USCRIPT_COMMON
                    && curIcuScript != USCRIPT_COMMON) {
                    break;
                }
                nextOffset += 1;
            }

            assert(icuError == U_ZERO_ERROR);

            FriBidiChar* segmentStart = ogStringUtf32 + curOffset;
            i32          segmentLength = nextOffset - curOffset;

            // NOTE(khvorov) Reverse parts of the string segment for presentation if necessary
            hb_script_t    hbScript = hb_icu_script_to_script(curIcuScript);
            hb_direction_t hbDir = hb_script_get_horizontal_direction(hbScript);

            FriBidiCharType* bidiTypes = arenaAllocArray(renderer->arena, FriBidiCharType, segmentLength);
            fribidi_get_bidi_types(segmentStart, segmentLength, bidiTypes);

            FriBidiBracketType* fribidiBracketTypes =
                arenaAllocArray(renderer->arena, FriBidiBracketType, segmentLength);
            fribidi_get_bracket_types(segmentStart, segmentLength, bidiTypes, fribidiBracketTypes);

            FriBidiParType fribidiBaseDirection = hbDir == HB_DIRECTION_RTL ? FRIBIDI_TYPE_RTL : FRIBIDI_TYPE_LTR;
            FriBidiLevel*  firbidiEmbeddingLevels = arenaAllocArray(renderer->arena, FriBidiLevel, segmentLength);
            FriBidiLevel   embeddingResult = fribidi_get_par_embedding_levels_ex(
                bidiTypes,
                fribidiBracketTypes,
                segmentLength,
                &fribidiBaseDirection,
                firbidiEmbeddingLevels
            );

            assert(embeddingResult != 0);

            FriBidiChar* visualStr = arenaAllocArray(renderer->arena, FriBidiChar, segmentLength);
            SDL_memcpy(visualStr, segmentStart, segmentLength * sizeof(FriBidiChar));

            FriBidiLevel reorderResult = fribidi_reorder_line(
                0,
                bidiTypes,
                segmentLength,
                0,
                fribidiBaseDirection,
                firbidiEmbeddingLevels,
                visualStr,
                0
            );

            assert(reorderResult != 0);

            // NOTE(khvorov) Fribidi reverses arabic but not numbers - the oppposite of what we want.
            // So reverse it again here (so that the arabic is in the correct logical order but the numbers are reversed)
            if (hbDir == HB_DIRECTION_RTL) {
                for (i32 segIndex = 0; segIndex < segmentLength / 2; segIndex++) {
                    i32 otherIndex = segmentLength - 1 - segIndex;
                    u32 temp = visualStr[segIndex];
                    visualStr[segIndex] = visualStr[otherIndex];
                    visualStr[otherIndex] = temp;
                }
            }

            hb_buffer_clear_contents(renderer->fontManager.hbBuf);
            hb_buffer_add_utf32(renderer->fontManager.hbBuf, visualStr, segmentLength, 0, segmentLength);

            // NOTE(khvorov) This has to be done after contents
            hb_buffer_set_script(renderer->fontManager.hbBuf, hbScript);
            hb_buffer_set_direction(renderer->fontManager.hbBuf, hbDir);

            // NOTE(khvorov) We can also set the language here but it doesn't seem to make a difference to shaping
            // and I don't know if trying to detect the language of an arbitrary unicode string segment is worth it
            // hb_language_t hbLang = hb_language_from_string("en-US", -1);
            // hb_buffer_set_language(renderer->font.hbBuf, HB_LANGUAGE_INVALID);

            GetFontResult getFontResult =
                getFontForScriptAndUtf32Chars(&renderer->fontManager, curIcuScript, segmentStart, segmentLength);
            if (getFontResult.success) {
                Font* font = getFontResult.font;
                hb_shape(font->hbFont, renderer->fontManager.hbBuf, 0, 0);

                u32              hbGlyphCount = 0;
                hb_glyph_info_t* hbGlyphInfos = hb_buffer_get_glyph_infos(renderer->fontManager.hbBuf, &hbGlyphCount);
                hb_glyph_position_t* hbGlyphPositions =
                    hb_buffer_get_glyph_positions(renderer->fontManager.hbBuf, &hbGlyphCount);

                hb_position_t hbPosX = 0;
                hb_position_t hbPosY = 0;
                for (u32 glyphIndex = 0; glyphIndex < hbGlyphCount; glyphIndex++) {
                    hb_codepoint_t      glyphid = hbGlyphInfos[glyphIndex].codepoint;
                    hb_glyph_position_t pos = hbGlyphPositions[glyphIndex];

                    FT_Error loadGlyphResult = FT_Load_Glyph(font->ftFace, glyphid, FT_LOAD_DEFAULT);
                    if (loadGlyphResult == FT_Err_Ok) {
                        FT_Error renderGlyphResult = FT_Err_Ok;
                        if (font->ftFace->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
                            renderGlyphResult = FT_Render_Glyph(font->ftFace->glyph, FT_RENDER_MODE_NORMAL);
                        }

                        if (renderGlyphResult == FT_Err_Ok) {
                            FT_GlyphSlot ftGlyph = font->ftFace->glyph;
                            FT_Bitmap    bm = ftGlyph->bitmap;

                            Glyph glyph = {
                                .advanceX = ftGlyph->advance.x >> 6,
                                .width = bm.width,
                                .height = bm.rows,
                                .pitch = bm.pitch,
                                .offsetX = ftGlyph->bitmap_left,
                                .offsetY = font->fontHeight - ftGlyph->bitmap_top,
                            };

                            TempMemory tempMemGlyph = arenaBeginTemp(renderer->arena);
                            u32*       glyphPx = arenaAllocArray(renderer->arena, u32, bm.rows * bm.width);
                            for (i32 bmRow = 0; bmRow < (i32)bm.rows; bmRow++) {
                                for (i32 bmCol = 0; bmCol < (i32)bm.width; bmCol++) {
                                    i32 bmIndex = bmRow * bm.pitch + bmCol;
                                    i32 alpha = bm.buffer[bmIndex];
                                    u32 colorRGB = (color.r << 24) | (color.g << 16) | (color.b << 8) | (0);
                                    u32 fullColorRGBA = colorRGB | alpha;
                                    i32 bufIndex = bmRow * bm.width + bmCol;
                                    glyphPx[bufIndex] = fullColorRGBA;
                                }
                            }

                            int texPitch = bm.width * sizeof(*glyphPx);
                            int updateTexResult = SDL_UpdateTexture(renderer->sdlFontTexture, 0, glyphPx, texPitch);
                            if (updateTexResult == 0) {
                                SDL_Rect texRect = {.w = glyph.width, .h = glyph.height};
                                SDL_Rect destRect = {
                                    .x = curLeftX + hbPosX + glyph.offsetX,
                                    .y = curTopY + hbPosY + glyph.offsetY,
                                    .w = glyph.width,
                                    .h = glyph.height};
                                int copyResult = SDL_RenderCopy(
                                    renderer->sdlRenderer,
                                    renderer->sdlFontTexture,
                                    &texRect,
                                    &destRect
                                );
                                assert(copyResult == 0);
                            }

                            endTempMemory(tempMemGlyph);
                        }
                    }

                    hbPosX += pos.x_advance >> 6;
                    hbPosY += pos.y_advance >> 6;
                }

                curLeftX += hbPosX;
                curTopY += hbPosY;
            }

            curOffset = nextOffset;
        }

        endTempMemory(tempMem);
    }
}

function String
fmtMemUsage(Arena* arena, i32 memUsed) {
    char* sizes[] = {"B", "KB", "MB", "GB", "TB"};
    f32   sizeSmallEnough = (f32)memUsed;
    usize divisions = 0;
    while (sizeSmallEnough > 1024.0f && divisions < arrayLen(sizes) - 1) {
        sizeSmallEnough /= 1024.0f;
        divisions++;
    }
    String memsizeString = stringFmt(arena, "%.1f%s", sizeSmallEnough, sizes[divisions]);
    return memsizeString;
}

typedef struct MemRectText {
    String str;
    i32    xOff, yOff;
} MemRectText;

function i32
drawMemRect(Renderer* renderer, i32 topY, i32 memUsed, i32 totalMemoryUsed, i32 width, Color color, MemRectText text) {
    Rect2i memRect = {
        .x = 0,
        .y = topY,
        .w = width,
        .h = (i32)((f32)memUsed / (f32)totalMemoryUsed * (f32)renderer->width + 0.5f),
    };
    drawRect(renderer, memRect, color);
    drawRectOutline(renderer, memRect, (Color) {.a = 255}, 1);

    // NOTE(khvorov) Draw memory usage
    if (text.str.ptr) {
        TempMemory tempMem = arenaBeginTemp(renderer->arena);
        String     memUsageStr = fmtMemUsage(renderer->arena, memUsed);
        String     memsizeString = stringFmt(renderer->arena, "%s: %s", text.str.ptr, memUsageStr.ptr);
        drawTextline(renderer, memsizeString, memRect.w + text.xOff, topY + text.yOff, color);
        endTempMemory(tempMem);
    }

    i32 bottomY = memRect.y + memRect.h;
    return bottomY;
}

function i32
drawArenaUsage(Renderer* renderer, i32 size, i32 used, i32 topY, i32 totalMemoryUsed, i32 width, MemRectText text) {
    i32 newtopY =
        drawMemRect(renderer, topY, used, totalMemoryUsed, width, (Color) {.r = 100, .a = 255}, (MemRectText) {0});
    newtopY = drawMemRect(
        renderer,
        newtopY,
        size - used,
        totalMemoryUsed,
        width,
        (Color) {.g = 100, .a = 255},
        (MemRectText) {0}
    );

    TempMemory tempMem = arenaBeginTemp(renderer->arena);
    String     usageStr = stringFmt(
        renderer->arena,
        "%s: %s/%s",
        text.str.ptr,
        fmtMemUsage(renderer->arena, used).ptr,
        fmtMemUsage(renderer->arena, size).ptr
    );
    drawTextline(
        renderer,
        usageStr,
        width + text.xOff,
        topY + text.yOff,
        (Color) {.r = 200, .g = 200, .b = 200, .a = 255}
    );
    endTempMemory(tempMem);

    return newtopY;
}

//
// SECTION Main loop and events
//

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

int
main(int argc, char* argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    SDL_SetMemoryFunctions(sdlCustomMalloc, sdlCustomCalloc, sdlCustomRealloc, sdlCustomFree);
    int initResult = SDL_Init(SDL_INIT_VIDEO);
    if (initResult == 0) {
        Arena                virtualArena = createArenaFromVmem(3 * MEGABYTE);
        CreateRendererResult createRendererResult = createRenderer(&virtualArena);
        if (createRendererResult.success) {
            Renderer    renderer = createRendererResult.renderer;
            SDL_Window* sdlWindow = renderer.sdlWindow;

            String unicodeLines[] = {
                stringFromCstring("The Sun is the star at يبلغ قطرها حوالي 1,392,684 كيلومتر 約佔太陽系總質量的99.86"),
                stringFromCstring("銀河系の中ではありふれた массовое содержание водорода X ≈ 73"),
                stringFromCstring("현 태양의 나이는 약 45억 และมีมวลประมาณ 330,000 เท่าของโลก"),
                stringFromCstring("អង្កត់ផ្ចិតរបស់វាគឺប្រហែល 1,39 លានគីឡូម៉ែត្រពោលគឺ مقدار ۹۹٫۸۶٪ کل"),
                stringFromCstring("किलोमीटर या ९,२९,६०,००० मील है तथा e très nombreux astéroïdes et comètes"),
            };

            u64  prevFrameTicks = 0;
            u64  unicodeDrawTicks = 0;
            f32  ticksPerMS = (f32)SDL_GetPerformanceFrequency() / 1000.0f;
            bool running = true;
            while (running) {
                u64 frameStart = SDL_GetPerformanceCounter();
                assert(virtualArena.tempCount == 0);

                SDL_Event event = {0};
                assert(SDL_WaitEvent(&event) == 1);
                processEvent(sdlWindow, &event, &running);
                while (SDL_PollEvent(&event)) {
                    processEvent(sdlWindow, &event, &running);
                }

                assert(renderBegin(&renderer) == CompletionStatus_Sucess);

                u64 unicodeDrawStart = SDL_GetPerformanceCounter();
                i32 curTextTopY = 150;
                for (usize lineIndex = 0; lineIndex < arrayLen(unicodeLines); lineIndex++) {
                    String line = unicodeLines[lineIndex];
                    drawTextline(&renderer, line, 150, curTextTopY, (Color) {.r = 200, .g = 200, .b = 200, .a = 255});
                    i32 arbitraryLineHeight = 50;
                    curTextTopY += arbitraryLineHeight;
                }
                unicodeDrawTicks = SDL_GetPerformanceCounter() - unicodeDrawStart;

                // NOTE(khvorov) Visualize memory usage
                {
                    i32 totalMemoryUsed =
                        globalGPADataSDL.used + globalGPADataFT.used + globalGPADataHB.used + virtualArena.size;
                    i32 memRectHeight = 20;
                    i32 textXPad = 5;
                    i32 topY = drawMemRect(
                        &renderer,
                        0,
                        globalGPADataSDL.used,
                        totalMemoryUsed,
                        memRectHeight,
                        (Color) {.r = 100, .g = 100, .a = 255},
                        (MemRectText) {.str = stringFromCstring("SDL"), .xOff = textXPad}
                    );

                    topY = drawMemRect(
                        &renderer,
                        topY,
                        globalGPADataFT.used,
                        totalMemoryUsed,
                        memRectHeight,
                        (Color) {.r = 100, .b = 100, .a = 255},
                        (MemRectText) {.str = stringFromCstring("FT"), .xOff = textXPad, .yOff = -5}
                    );

                    topY = drawMemRect(
                        &renderer,
                        topY,
                        globalGPADataHB.used,
                        totalMemoryUsed,
                        memRectHeight,
                        (Color) {.g = 100, .b = 100, .a = 255},
                        (MemRectText) {.str = stringFromCstring("HB"), .xOff = textXPad, .yOff = 5}
                    );

                    topY = drawArenaUsage(
                        &renderer,
                        virtualArena.size,
                        virtualArena.used,
                        topY,
                        totalMemoryUsed,
                        memRectHeight,
                        (MemRectText) {.str = stringFromCstring("Arena"), .xOff = textXPad, .yOff = 15}
                    );
                }

                // NOTE(khvorov) Print timings
                {
                    TempMemory tempMem = arenaBeginTemp(renderer.arena);
                    f32        unicodeDrawMs = (f32)unicodeDrawTicks / ticksPerMS;
                    f32        frameMs = (f32)prevFrameTicks / ticksPerMS;
                    drawTextline(
                        &renderer,
                        stringFmt(renderer.arena, "unicode draw: %.1fms", unicodeDrawMs),
                        400,
                        600,
                        (Color) {.r = 200, .g = 200, .b = 200, .a = 255}
                    );
                    drawTextline(
                        &renderer,
                        stringFmt(renderer.arena, "prev frame: %.1fms", frameMs),
                        400,
                        630,
                        (Color) {.r = 200, .g = 200, .b = 200, .a = 255}
                    );
                    endTempMemory(tempMem);
                }

                renderEnd(&renderer);
                prevFrameTicks = SDL_GetPerformanceCounter() - frameStart;
            }
        }
    } else {
        // NOTE(khvorov) SDL log functions work even if it's not initialized
        const char* sdlError = SDL_GetError();
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to init SDL: %s\n", sdlError);
    }

    return 0;
}
