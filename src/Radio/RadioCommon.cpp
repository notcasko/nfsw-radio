#include "RadioCommon.h"

// FMOD function pointers
t_FMOD_System_Create       pfn_FMOD_System_Create = NULL;
t_FMOD_System_Init         pfn_FMOD_System_Init = NULL;
t_FMOD_System_Close        pfn_FMOD_System_Close = NULL;
t_FMOD_System_Release      pfn_FMOD_System_Release = NULL;
t_FMOD_System_CreateStream pfn_FMOD_System_CreateStream = NULL;
t_FMOD_System_PlaySound    pfn_FMOD_System_PlaySound = NULL;
t_FMOD_System_Update       pfn_FMOD_System_Update = NULL;
t_FMOD_Channel_Stop        pfn_FMOD_Channel_Stop = NULL;
t_FMOD_Channel_SetPaused   pfn_FMOD_Channel_SetPaused = NULL;
t_FMOD_Channel_IsPlaying   pfn_FMOD_Channel_IsPlaying = NULL;
t_FMOD_Channel_SetVolume   pfn_FMOD_Channel_SetVolume = NULL;
t_FMOD_Sound_Release       pfn_FMOD_Sound_Release = NULL;

void* g_fmodSystem = NULL;
void* g_currentSound = NULL;
void* g_currentChannel = NULL;
HMODULE g_hFmod = NULL;

HINSTANCE g_hInstance = NULL;
ULONG_PTR g_gdiplusToken = 0;
HANDLE g_hRadioActiveEvent = NULL;
HANDLE g_hRadioModActiveEvent = NULL;

PlayOrder g_playOrder = ORDER_DEFAULT;
std::vector<Station> g_stations;
int g_currentStation = 0;
int g_currentTrack = 0;
bool g_isPlaying = false;
bool g_isStopped = false;
bool g_autoPaused = false;
float g_volume = 0.50f;
bool g_receivedUserInput = false;

std::string g_currentTrackTitle = "";
std::string g_currentArtist = "";

HHOOK g_hMouseHook = NULL;
DWORD g_mouseHookThreadId = 0;
HANDLE g_hMouseThread = NULL;

int g_cfgRadioKey = VK_TAB;
int g_cfgNextStationMod = VK_CAPITAL;
int g_cfgTogglePlaybackMod = 'Q';
int g_cfgPlaylistOrderMod = 'F';
int g_cfgSkinMod = 'G';
int g_cfgVolumeMod = 'X';

int g_popupX = -1;
int g_popupY = -1;
bool g_cfgShowInGameMusicPopUp = false;
std::string g_cfgMusicPath = "";

SkinConfig g_skin;
Gdiplus::Bitmap* g_pSkinBitmap = NULL;
Gdiplus::Bitmap* g_pCurrentCover = NULL;

HWND g_hPopupWnd = NULL;
CRITICAL_SECTION g_popupCs;
PlayerState g_popupPlayerState = STATE_PLAYING;
PlayOrder g_popupPlayOrder = ORDER_DEFAULT;
std::string g_popupStationName = "";
std::string g_popupTrackTitle = "";
std::string g_popupArtistName = "";
std::string g_popupStatus = "";
std::string g_popupOrder = "";
std::string g_popupVolume = "";
PopupState g_popupState = POPUP_IDLE;
DWORD g_popupHoldStart = 0;
DWORD g_popupFadeStart = 0;

HWND g_hInGamePopupWnd = NULL;
CRITICAL_SECTION g_inGamePopupCs;
PopupState g_inGamePopupState = POPUP_IDLE;
DWORD g_inGamePopupHoldStart = 0;
DWORD g_inGamePopupFadeStart = 0;
std::string g_inGamePopupTrackName = "";

DIBBuffer g_popupDIB;
DIBBuffer g_inGamePopupDIB;

const char* GetPlayOrderName(PlayOrder order)
{
    switch (order) {
    case ORDER_DEFAULT:         return "Default";
    case ORDER_REPEAT_PLAYLIST: return "Repeat Playlist";
    case ORDER_REPEAT_TRACK:    return "Repeat Track";
    case ORDER_SHUFFLE:         return "Shuffle";
    default:                    return "Default";
    }
}

std::string GetDirectoryOfFile(const std::string& path)
{
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return path.substr(0, lastSlash);
    }
    return ".";
}

std::string WideToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (len > 0) {
        std::string res(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &res[0], len, NULL, NULL);
        return res;
    }
    return "";
}

std::wstring StringToWide(const std::string& str)
{
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), (int)str.size(), NULL, 0);
    if (len > 0) {
        std::wstring wstr(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], len);
        return wstr;
    }
    // Try Japanese Shift-JIS (CP932) before fallback to system ANSI
    len = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, str.c_str(), (int)str.size(), NULL, 0);
    if (len > 0) {
        std::wstring wstr(len, 0);
        MultiByteToWideChar(932, 0, str.c_str(), (int)str.size(), &wstr[0], len);
        return wstr;
    }
    len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), NULL, 0);
    if (len > 0) {
        std::wstring wstr(len, 0);
        MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), &wstr[0], len);
        return wstr;
    }
    return std::wstring(str.begin(), str.end());
}

#if defined(_DEBUG) || defined(RADIOMOD_ENABLE_LOGGING)

void LogMsg(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    static std::wstring s_logFileW;
    static bool s_logInit = false;
    if (!s_logInit) {
        char modPath[MAX_PATH];
        GetModuleFileNameA(g_hInstance, modPath, MAX_PATH);
        std::string modDir = GetDirectoryOfFile(modPath);
        std::string modBase = GetFilenameWithoutExtension(modPath);
        if (modBase.empty()) modBase = "Radio";
        std::string logFile = modDir + "\\" + modBase + ".log";
        s_logFileW = StringToWide(logFile);
        s_logInit = true;
    }

    FILE* f = _wfopen(s_logFileW.c_str(), L"a");
    if (!f) {
        f = _wfopen(L"Radio.log", L"a");
        if (!f) {
            f = _wfopen(L"RadioMod.log", L"a");
        }
    }
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fclose(f);
    }
}

#endif

std::string GetFilenameWithoutExtension(const std::string& path)
{
    size_t lastSlash = path.find_last_of("\\/");
    std::string filename = (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);
    size_t lastDot = filename.find_last_of('.');
    if (lastDot != std::string::npos && lastDot > 0) {
        filename = filename.substr(0, lastDot);
    }
    return filename;
}

bool HasAudioExtension(const std::string& filename)
{
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos) return false;
    const char* ext = filename.c_str() + dot;

    static const char* const supported[] = {
        ".mp3", ".wav", ".ogg", ".flac", ".aac", ".m4a",
        ".wma", ".aiff", ".aif", ".opus", ".mid", ".midi",
        ".mod", ".s3m", ".xm", ".it", ".mp2", ".mp1", ".caf", ".ac3"
    };

    for (size_t i = 0; i < sizeof(supported) / sizeof(supported[0]); ++i) {
        if (_stricmp(ext, supported[i]) == 0) return true;
    }
    return false;
}

std::string TrimString(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

bool ContainsCjk(const std::wstring& text)
{
    for (wchar_t c : text) {
        if ((c >= 0x3000 && c <= 0x303F) || // CJK symbols and punctuation
            (c >= 0x3040 && c <= 0x309F) || // Hiragana
            (c >= 0x30A0 && c <= 0x30FF) || // Katakana
            (c >= 0x31F0 && c <= 0x31FF) || // Katakana Phonetic Extensions
            (c >= 0x3200 && c <= 0x32FF) || // Enclosed CJK Letters and Months
            (c >= 0x3400 && c <= 0x4DBF) || // CJK Unified Ideographs Extension A
            (c >= 0x4E00 && c <= 0x9FFF) || // CJK Unified Ideographs
            (c >= 0xF900 && c <= 0xFAFF) || // CJK Compatibility Ideographs
            (c >= 0xFF00 && c <= 0xFFEF) || // Halfwidth and Fullwidth Forms
            (c >= 0xAC00 && c <= 0xD7AF))   // Hangul Syllables
        {
            return true;
        }
    }
    return false;
}

bool IsKnownCjkFont(const std::wstring& fontName)
{
    std::wstring lower = fontName;
    for (wchar_t& c : lower) c = towlower(c);
    if (lower.find(L"gothic") != std::wstring::npos ||
        lower.find(L"meiryo") != std::wstring::npos ||
        lower.find(L"mincho") != std::wstring::npos ||
        lower.find(L"yahei") != std::wstring::npos ||
        lower.find(L"jhenghei") != std::wstring::npos ||
        lower.find(L"malgun") != std::wstring::npos ||
        lower.find(L"simsun") != std::wstring::npos ||
        lower.find(L"simhei") != std::wstring::npos ||
        lower.find(L"mingliu") != std::wstring::npos ||
        lower.find(L"yu ") != std::wstring::npos ||
        lower.find(L"arial unicode") != std::wstring::npos) {
        return true;
    }
    return false;
}

bool DoesFontSupportWideString(const std::wstring& fontName, const std::wstring& text)
{
    if (text.empty()) return true;

    bool hasNonAscii = false;
    for (wchar_t c : text) {
        if (c > 127) {
            hasNonAscii = true;
            break;
        }
    }
    if (!hasNonAscii) return true;

    if (ContainsCjk(text)) {
        if (!IsKnownCjkFont(fontName)) return false;
        Gdiplus::FontFamily fam(fontName.c_str());
        return fam.IsAvailable();
    }

    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc) return true;

    HFONT hFont = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        fontName.c_str()
    );
    if (!hFont) {
        DeleteDC(hdc);
        return false;
    }

    HGDIOBJ oldFont = SelectObject(hdc, hFont);

    std::wstring toTest;
    toTest.reserve(text.size());
    for (wchar_t c : text) {
        if (c > 32) toTest.push_back(c);
    }

    bool allSupported = true;
    if (!toTest.empty()) {
        std::vector<WORD> glyphs(toTest.size());
        DWORD res = GetGlyphIndicesW(hdc, toTest.c_str(), (int)toTest.size(), glyphs.data(), GGI_MARK_NONEXISTING_GLYPHS);
        if (res != GDI_ERROR) {
            for (WORD g : glyphs) {
                if (g == 0xFFFF) {
                    allSupported = false;
                    break;
                }
            }
        }
    }

    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
    DeleteDC(hdc);

    return allSupported;
}

std::wstring GetBestFontForText(const std::wstring& primaryFont, const std::wstring& text)
{
    if (text.empty()) return primaryFont;

    static CRITICAL_SECTION s_fontCacheCs;
    static bool s_fontCsInit = false;
    if (!s_fontCsInit) {
        InitializeCriticalSection(&s_fontCacheCs);
        s_fontCsInit = true;
    }

    EnterCriticalSection(&s_fontCacheCs);
    static std::map<std::pair<std::wstring, std::wstring>, std::wstring> s_fontCache;
    auto it = s_fontCache.find({primaryFont, text});
    if (it != s_fontCache.end()) {
        std::wstring res = it->second;
        LeaveCriticalSection(&s_fontCacheCs);
        return res;
    }
    LeaveCriticalSection(&s_fontCacheCs);

    std::wstring resolved = primaryFont;
    bool hasCjk = ContainsCjk(text);

    if (hasCjk) {
        if (IsKnownCjkFont(primaryFont)) {
            Gdiplus::FontFamily fam(primaryFont.c_str());
            if (fam.IsAvailable()) {
                resolved = primaryFont;
                goto FontResolved;
            }
        }

        static const wchar_t* cjkCandidates[] = {
            L"Yu Gothic UI",
            L"Meiryo UI",
            L"Meiryo",
            L"MS UI Gothic",
            L"MS Gothic",
            L"Yu Gothic",
            L"Microsoft YaHei UI",
            L"Microsoft YaHei",
            L"Microsoft JhengHei UI",
            L"Microsoft JhengHei",
            L"Malgun Gothic",
            L"Segoe UI Symbol",
            L"Arial Unicode MS"
        };

        for (const wchar_t* cand : cjkCandidates) {
            Gdiplus::FontFamily fam(cand);
            if (fam.IsAvailable()) {
                resolved = cand;
                goto FontResolved;
            }
        }
    }

    {
        Gdiplus::FontFamily fam(primaryFont.c_str());
        if (fam.IsAvailable()) {
            if (DoesFontSupportWideString(primaryFont, text)) {
                resolved = primaryFont;
                goto FontResolved;
            }
        }
    }

    static const wchar_t* generalCandidates[] = {
        L"Segoe UI",
        L"Segoe UI Symbol",
        L"Arial",
        L"Tahoma",
        L"Yu Gothic UI",
        L"Meiryo",
        L"MS Gothic"
    };

    for (const wchar_t* cand : generalCandidates) {
        Gdiplus::FontFamily fam(cand);
        if (fam.IsAvailable()) {
            if (DoesFontSupportWideString(cand, text)) {
                resolved = cand;
                goto FontResolved;
            }
        }
    }

    for (const wchar_t* cand : generalCandidates) {
        Gdiplus::FontFamily fam(cand);
        if (fam.IsAvailable()) {
            resolved = cand;
            goto FontResolved;
        }
    }

FontResolved:
    EnterCriticalSection(&s_fontCacheCs);
    if (s_fontCache.size() > 128) s_fontCache.clear();
    s_fontCache[{primaryFont, text}] = resolved;
    LeaveCriticalSection(&s_fontCacheCs);
    return resolved;
}

Gdiplus::Font* CreateSafeGdiplusFont(const Gdiplus::FontFamily* pFam, float size, bool bold)
{
    if (pFam && pFam->IsAvailable()) {
        Gdiplus::FontStyle style = (bold && pFam->IsStyleAvailable(Gdiplus::FontStyleBold)) ?
            Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular;
        Gdiplus::Font* f = new Gdiplus::Font(pFam, (Gdiplus::REAL)size, style, Gdiplus::UnitPixel);
        if (f && f->GetLastStatus() == Gdiplus::Ok) return f;
        delete f;

        if (pFam->IsStyleAvailable(Gdiplus::FontStyleRegular)) {
            f = new Gdiplus::Font(pFam, (Gdiplus::REAL)size, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            if (f && f->GetLastStatus() == Gdiplus::Ok) return f;
            delete f;
        }
    }

    const Gdiplus::FontFamily* pSans = Gdiplus::FontFamily::GenericSansSerif();
    if (pSans && pSans->IsAvailable()) {
        Gdiplus::Font* f = new Gdiplus::Font(pSans, (Gdiplus::REAL)size, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        if (f && f->GetLastStatus() == Gdiplus::Ok) return f;
        delete f;
    }

    return new Gdiplus::Font(L"Arial", (Gdiplus::REAL)size, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
}

std::string NormalizePath(std::string p)
{
    p = TrimString(p);
    while (p.size() >= 2 && ((p.front() == '"' && p.back() == '"') ||
                             (p.front() == '\'' && p.back() == '\''))) {
        p = TrimString(p.substr(1, p.size() - 2));
    }
    while (!p.empty() && (p.front() == '"' || p.front() == '\'')) p.erase(p.begin());
    while (!p.empty() && (p.back() == '"' || p.back() == '\'')) p.pop_back();
    p = TrimString(p);

    while (p.size() > 3 && (p.back() == '\\' || p.back() == '/')) {
        p.pop_back();
    }
    if (p.size() == 2 && p[1] == ':') {
        p += "\\";
    }
    return p;
}

Gdiplus::Bitmap* LoadBitmapFromMemory(const void* data, size_t size)
{
    if (!data || size == 0) return NULL;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return NULL;
    void* pLock = GlobalLock(hMem);
    if (!pLock) {
        GlobalFree(hMem);
        return NULL;
    }
    memcpy(pLock, data, size);
    GlobalUnlock(hMem);

    IStream* pStream = NULL;
    if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) != S_OK) {
        GlobalFree(hMem);
        return NULL;
    }

    Gdiplus::Bitmap* srcBmp = Gdiplus::Bitmap::FromStream(pStream);
    Gdiplus::Bitmap* finalBmp = NULL;

    if (srcBmp && srcBmp->GetLastStatus() == Gdiplus::Ok) {
        UINT w = srcBmp->GetWidth();
        UINT h = srcBmp->GetHeight();
        finalBmp = new Gdiplus::Bitmap(w, h, PixelFormat32bppPARGB);
        Gdiplus::Graphics g(finalBmp);
        g.DrawImage(srcBmp, 0, 0, w, h);
    }

    if (srcBmp) delete srcBmp;
    pStream->Release();
    return finalBmp;
}

Gdiplus::Bitmap* LoadBitmapFromFile(const std::string& path)
{
    std::wstring wpath = StringToWide(path);
    if (GetFileAttributesW(wpath.c_str()) == INVALID_FILE_ATTRIBUTES) return NULL;
    Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromFile(wpath.c_str());
    if (!src || src->GetLastStatus() != Gdiplus::Ok) {
        if (src) delete src;
        return NULL;
    }
    UINT w = src->GetWidth();
    UINT h = src->GetHeight();
    Gdiplus::Bitmap* finalBmp = new Gdiplus::Bitmap(w, h, PixelFormat32bppPARGB);
    Gdiplus::Graphics g(finalBmp);
    g.DrawImage(src, 0, 0, w, h);
    delete src;
    return finalBmp;
}

HWND FindGameWindow()
{
    DWORD currentPid = GetCurrentProcessId();

    HWND hGame = FindWindowA("GameFrame", NULL);
    if (hGame) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hGame, &pid);
        if (pid == currentPid) {
            return hGame;
        }
    }

    struct EnumData {
        DWORD pid;
        HWND hWnd;
    } data = { currentPid, NULL };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        EnumData* d = (EnumData*)lParam;
        DWORD p = 0;
        GetWindowThreadProcessId(hwnd, &p);
        if (p == d->pid) {
            char cls[64] = {0};
            GetClassNameA(hwnd, cls, sizeof(cls));
            if (_stricmp(cls, "GameFrame") == 0) {
                d->hWnd = hwnd;
                return FALSE;
            }
        }
        return TRUE;
    }, (LPARAM)&data);

    if (data.hWnd) return data.hWnd;

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        EnumData* d = (EnumData*)lParam;
        DWORD p = 0;
        GetWindowThreadProcessId(hwnd, &p);
        if (p == d->pid && IsWindowVisible(hwnd)) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            if ((rc.right - rc.left) > 300 && (rc.bottom - rc.top) > 200) {
                d->hWnd = hwnd;
                return FALSE;
            }
        }
        return TRUE;
    }, (LPARAM)&data);

    return data.hWnd;
}

void EnsurePopupParented(HWND hWnd)
{
    if (!hWnd) return;
    HWND hGame = FindGameWindow();
    if (!hGame) return;

    if (GetParent(hWnd) != hGame) {
        SetParent(hWnd, hGame);

        LONG_PTR style = GetWindowLongPtrA(hWnd, GWL_STYLE);
        style &= ~WS_POPUP;
        style |= WS_CHILD;
        SetWindowLongPtrA(hWnd, GWL_STYLE, style);

        LONG_PTR exStyle = GetWindowLongPtrA(hWnd, GWL_EXSTYLE);
        exStyle &= ~WS_EX_TOPMOST;
        SetWindowLongPtrA(hWnd, GWL_EXSTYLE, exStyle);

        SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}
