#include <stdio.h>

#include <ft2build.h>
#include <freetype/freetype.h>

int
main() {
    FT_Library ftLib;
    if (FT_Init_FreeType(&ftLib) == FT_Err_Ok) {
        printf("freetype loaded successfully\n");
    }
    return 0;
}