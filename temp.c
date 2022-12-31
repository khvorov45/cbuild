#include <Windows.h>
#include <tchar.h>

int CALLBACK EnumFontFamExProc(ENUMLOGFONTEX *lpelfe, NEWTEXTMETRICEX *lpntme, DWORD FontType, LPARAM lParam)
{
    // lpelfe->elfLogFont contains information about the enumerated font
    // Use this information to select a specific font if desired
    return 1;
}

int main()
{
    // Enumerate all available fonts
    LOGFONT lf = { 0 };
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfPitchAndFamily = 0;
    HDC hdc = GetDC(NULL);
    EnumFontFamiliesEx(hdc, &lf, (FONTENUMPROC)EnumFontFamExProc, 0, 0);
    ReleaseDC(NULL, hdc);

    return 0;
}
